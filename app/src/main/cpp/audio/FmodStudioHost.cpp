#include "FmodStudioHost.h"

#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <list>
#include <memory>
#include <mutex>
#include <random>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "DirectAAudioEngine.h"
#include "FmodBank.h"
#include "FsbVorbis.h"

#define LOG_TAG "HyperPlay.FmodHost"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::fmodhost {
namespace {

using namespace hyperplay::fmodlite;
using u64 = uint64_t;

// FMOD_RESULT (FMOD 1.10).
constexpr u64 kOk = 0;
constexpr u64 kErrInvalidHandle = 30;
constexpr u64 kErrInvalidParam = 31;
constexpr u64 kErrEventNotFound = 74;
constexpr u64 kErrFileNotFound = 18;

// FMOD_STUDIO_PLAYBACK_STATE.
enum : int { kPlaying = 0, kSustaining = 1, kStopped = 2, kStarting = 3, kStopping = 4 };

constexpr int kRate = 48000;
constexpr int kBlock = 480;                       // 10 ms
constexpr uint32_t kStreamFrames = 48000 * 12;    // más largo que esto: se decodifica por bloques
constexpr size_t kCacheBudget = 160u << 20;       // PCM de muestras cortas en memoria

Hooks g_hooks;
std::recursive_mutex g_mu;  // estado de la API y del mezclador

float argF(int slot) {
    if (!g_hooks.xmm) return 0.f;
    const uint8_t* x = g_hooks.xmm(slot);
    if (x == nullptr) return 0.f;
    float f;
    std::memcpy(&f, x, 4);
    return f;
}

u64 putString(const std::string& s, char* buf, int size, int* retrieved) {
    if (retrieved) *retrieved = static_cast<int>(s.size() + 1);
    if (buf != nullptr && size > 0) {
        const size_t n = std::min(s.size(), static_cast<size_t>(size - 1));
        std::memcpy(buf, s.data(), n);
        buf[n] = 0;
    }
    return kOk;
}

// ------------------------------------------------------------------ PCM: caché y streaming

struct Pcm {
    std::vector<int16_t> data;
    uint32_t channels = 1, rate = 48000, frames = 0;
    std::atomic<bool> ready{false};
    std::atomic<bool> failed{false};
};

struct Stream {
    // Lo rellena el hilo decodificador; lo consume el mezclador.
    std::mutex mu;
    std::deque<int16_t> buf;
    uint32_t channels = 1, rate = 48000;
    uint64_t skip = 0;          // fotogramas a descartar tras (re)arrancar
    bool restartRequested = true;
    bool eof = false;
    bool closed = false;
    int bank = -1;
    uint32_t subsound = 0;
    VorbisStream dec;
    bool opened = false;
};

struct Engine {
    std::vector<std::unique_ptr<Bank>> banks;
    Model model;
    VorbisSetupTable setups;
    std::vector<uint8_t> fmodDll;
    bool fmodDllTried = false;

    // Caché de muestras cortas (bank<<32 | subsound).
    std::unordered_map<u64, std::shared_ptr<Pcm>> cache;
    std::list<u64> lru;
    size_t cacheBytes = 0;

    // Trabajo del decodificador.
    std::mutex jobMu;
    std::condition_variable jobCv;
    std::deque<std::pair<std::shared_ptr<Pcm>, std::pair<int, uint32_t>>> jobKeys;
    std::vector<std::weak_ptr<Stream>> streams;
    std::thread decoder;
    std::atomic<bool> quit{false};

    // Salida.
    std::thread mixer;
    std::atomic<bool> outputPaused{false};
    bool running = false;
};
Engine* g_engine = nullptr;

const FsbSample* sampleOf(int bank, uint32_t sub) {
    if (g_engine == nullptr || bank < 0 || bank >= static_cast<int>(g_engine->banks.size())) return nullptr;
    const auto& fsb = g_engine->banks[static_cast<size_t>(bank)]->fsb();
    return sub < fsb.samples.size() ? &fsb.samples[sub] : nullptr;
}

void ensureSetups() {
    Engine& e = *g_engine;
    if (!e.fmodDllTried) {
        e.fmodDllTried = true;
        for (const char* name : {"/fmod.dll", "/fmod64.dll"}) {
            const std::string p = g_hooks.gameDir + name;
            const int fd = ::open(p.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd < 0) continue;
            // Lectura directa al vector (sin búferes grandes en la pila del hilo del juego).
            std::vector<uint8_t> b;
            const off_t len = ::lseek(fd, 0, SEEK_END);
            if (len > 0) {
                b.resize(static_cast<size_t>(len));
                size_t got = 0;
                while (got < b.size()) {
                    const ssize_t n = ::pread(fd, b.data() + got, b.size() - got, static_cast<off_t>(got));
                    if (n <= 0) break;
                    got += static_cast<size_t>(n);
                }
                b.resize(got);
            }
            ::close(fd);
            if (b.size() > 0x1000) {
                e.fmodDll.swap(b);
                LOGI("Cabeceras Vorbis: %s (%zu bytes)", p.c_str(), e.fmodDll.size());
                break;
            }
        }
        if (e.fmodDll.empty()) LOGW("Sin fmod.dll en %s: no se pueden decodificar los sonidos", g_hooks.gameDir.c_str());
    }
    if (e.fmodDll.empty()) return;
    std::vector<uint32_t> crcs;
    std::unordered_set<uint32_t> seen;
    for (auto& b : e.banks) {
        for (const auto& s : b->fsb().samples) {
            if (s.vorbisCrc != 0 && seen.insert(s.vorbisCrc).second && e.setups.find(s.vorbisCrc) == nullptr) {
                crcs.push_back(s.vorbisCrc);
            }
        }
    }
    if (!crcs.empty()) {
        const size_t before = e.setups.size();
        e.setups.loadFromFmodDll(e.fmodDll, crcs);
        LOGI("Cabeceras Vorbis: %zu nuevas de %zu pedidas", e.setups.size() - before, crcs.size());
    }
}

/// PCM completo de una muestra corta (se decodifica en segundo plano la primera vez).
std::shared_ptr<Pcm> getPcm(int bank, uint32_t sub) {
    Engine& e = *g_engine;
    const u64 key = (static_cast<u64>(static_cast<uint32_t>(bank)) << 32) | sub;
    auto it = e.cache.find(key);
    if (it != e.cache.end()) {
        e.lru.remove(key);
        e.lru.push_front(key);
        return it->second;
    }
    auto pcm = std::make_shared<Pcm>();
    e.cache[key] = pcm;
    e.lru.push_front(key);
    {
        std::lock_guard<std::mutex> lock(e.jobMu);
        e.jobKeys.push_back({pcm, {bank, sub}});
    }
    e.jobCv.notify_one();
    return pcm;
}

void trimCache() {
    Engine& e = *g_engine;
    while (e.cacheBytes > kCacheBudget && !e.lru.empty()) {
        const u64 key = e.lru.back();
        e.lru.pop_back();
        auto it = e.cache.find(key);
        if (it == e.cache.end()) continue;
        if (it->second.use_count() > 1 || !it->second->ready) {  // en uso o pendiente: se queda
            e.lru.push_front(key);
            break;
        }
        e.cacheBytes -= it->second->data.size() * sizeof(int16_t);
        e.cache.erase(it);
    }
}

void decoderLoop() {
    Engine& e = *g_engine;
    while (!e.quit) {
        std::pair<std::shared_ptr<Pcm>, std::pair<int, uint32_t>> job;
        bool haveJob = false;
        {
            std::unique_lock<std::mutex> lock(e.jobMu);
            e.jobCv.wait_for(lock, std::chrono::milliseconds(20), [&] { return e.quit || !e.jobKeys.empty(); });
            if (!e.jobKeys.empty()) {
                job = e.jobKeys.front();
                e.jobKeys.pop_front();
                haveJob = true;
            }
        }
        if (haveJob) {
            FsbSample sample;
            const uint8_t* bankData = nullptr;
            {
                std::lock_guard<std::recursive_mutex> lock(g_mu);
                if (const FsbSample* s = sampleOf(job.second.first, job.second.second)) {
                    sample = *s;
                    bankData = e.banks[static_cast<size_t>(job.second.first)]->data();
                }
            }
            const FsbSample* s = bankData != nullptr ? &sample : nullptr;
            std::string err;
            std::vector<int16_t> pcm;
            const bool ok = s != nullptr && decodeVorbisSample(bankData, *s, e.setups, pcm, &err);
            if (ok) {
                job.first->channels = s->channels;
                job.first->rate = s->rate;
                job.first->frames = static_cast<uint32_t>(pcm.size() / s->channels);
                job.first->data.swap(pcm);
                job.first->ready = true;
                std::lock_guard<std::recursive_mutex> lock(g_mu);
                e.cacheBytes += job.first->data.size() * sizeof(int16_t);
                trimCache();
            } else {
                job.first->failed = true;
                static std::atomic<int> logged{0};
                if (logged.fetch_add(1) < 10) {
                    LOGW("No se pudo decodificar el sonido %u del banco %d: %s", job.second.second, job.second.first,
                         err.c_str());
                }
            }
        }
        // Streams: mantener ~1,5 s decodificados por delante.
        std::vector<std::shared_ptr<Stream>> live;
        {
            std::lock_guard<std::mutex> lock(e.jobMu);
            for (auto it = e.streams.begin(); it != e.streams.end();) {
                if (auto s = it->lock()) {
                    live.push_back(s);
                    ++it;
                } else {
                    it = e.streams.erase(it);
                }
            }
        }
        for (auto& st : live) {
            bool restart, eof;
            size_t have;
            {
                std::lock_guard<std::mutex> lock(st->mu);
                if (st->closed) continue;
                restart = st->restartRequested;
                st->restartRequested = false;
                eof = st->eof;
                have = st->buf.size() / std::max<uint32_t>(1, st->channels);
                if (restart) {
                    st->buf.clear();
                    st->eof = false;
                    have = 0;
                }
            }
            if (restart) {
                FsbSample sample;
                const uint8_t* bankData = nullptr;
                {
                    std::lock_guard<std::recursive_mutex> lock(g_mu);
                    if (const FsbSample* s = sampleOf(st->bank, st->subsound)) {
                        sample = *s;
                        bankData = e.banks[static_cast<size_t>(st->bank)]->data();
                    }
                }
                if (bankData == nullptr) continue;
                std::string err;
                st->opened = st->opened ? st->dec.restart() : st->dec.open(bankData, sample, e.setups, &err);
                if (!st->opened) {
                    LOGW("Stream del sonido %u (banco %d) no se pudo abrir: %s", st->subsound, st->bank, err.c_str());
                    std::lock_guard<std::mutex> lock(st->mu);
                    st->eof = true;
                    continue;
                }
                eof = false;
            } else if (eof || have > st->rate * 3 / 2) {
                continue;
            }
            if (!st->opened) continue;
            std::vector<int16_t> chunk;
            st->dec.read(chunk, st->rate / 4);
            std::lock_guard<std::mutex> lock(st->mu);
            size_t from = 0;
            const size_t ch = std::max<uint32_t>(1, st->channels);
            while (st->skip > 0 && from + ch <= chunk.size()) {
                from += ch;
                --st->skip;
            }
            st->buf.insert(st->buf.end(), chunk.begin() + static_cast<long>(from), chunk.end());
            if (st->dec.finished()) st->eof = true;
        }
    }
}

// ------------------------------------------------------------------ objetos de la API

struct Desc;
struct Instance;

struct Voice {
    std::shared_ptr<Pcm> pcm;        // muestra corta
    std::shared_ptr<Stream> stream;  // o streaming
    double pos = 0;                  // fotograma de la muestra (si pcm)
    double frac = 0;                 // fase para streaming
    int16_t last[2] = {0, 0}, prev[2] = {0, 0};
    int item = -1;                   // índice en la línea de tiempo (>= 0) o -1 - k en la hoja de parámetro
    uint32_t endAt = 0;              // playhead en el que termina (fin del instrumento)
    int loops = 0;                   // bucles que quedan (-1 = infinito)
    uint32_t loopStart = 0;          // fotograma de la muestra al que vuelve
    const InstrumentDef* inst[3] = {nullptr, nullptr, nullptr};  // cadena de instrumentos (volumen automatizado)
    float gainNow = -1.f;            // ganancia automatizada actual (suavizada)
    bool done = false;
};

struct Desc {
    Guid id;
    const EventDef* def = nullptr;
    const TimelineDef* tl = nullptr;
    std::string path;
    void* userData = nullptr;
    int category = 0;  // 0 sfx, 1 música, 2 interfaz
    std::vector<Guid> chain;  // buses del evento (entrada -> ... -> bus:/)
    bool chainBuilt = false;
};

struct Instance {
    Desc* desc = nullptr;
    int state = kStopped;
    bool started = false;
    bool paused = false;
    bool released = false;
    bool stopping = false;
    float volume = 1.f, pitch = 1.f;
    float fade = 1.f, fadeStep = 0.f;
    float busNow = -1.f;  // ganancia de buses/snapshots suavizada
    int64_t playhead = 0;
    std::vector<bool> active;           // instrumento activo en la vuelta actual
    std::vector<bool> sheetActive;      // instrumentos de la hoja de parámetro sonando
    std::vector<Voice> voices;
    std::vector<std::unique_ptr<Instance>> children;  // eventos anidados
    std::unordered_map<std::string, float> params;
    void* userData = nullptr;
    float pos[3] = {0, 0, 0};
    bool has3d = false;
};

struct BusObj {
    std::string path;
    Guid id;
    float volume = 1.f;
    bool paused = false, mute = false;
};

struct BankObj {
    int index = -1;
    void* userData = nullptr;
};

struct ParamObj {
    Instance* inst = nullptr;
    std::string name;
};

struct Studio {
    bool initialized = false;
    std::unordered_map<Guid, std::unique_ptr<Desc>, GuidHash> descs;
    std::unordered_set<Instance*> instances;
    std::unordered_map<std::string, std::unique_ptr<BusObj>> buses;   // bus:/... y vca:/...
    std::vector<std::unique_ptr<BankObj>> bankObjs;
    std::unordered_map<std::string, std::unique_ptr<ParamObj>> paramObjs;
    float listener[3] = {0, 0, 0};
    void* userData = nullptr;
    std::mt19937 rng{12345};
};
Studio* g_studio = nullptr;
int g_studioToken = 0;  // el "puntero" del sistema que ve el juego
int g_lowLevelToken = 0;

bool validInstance(void* p) { return p != nullptr && g_studio && g_studio->instances.count(static_cast<Instance*>(p)) != 0; }
bool validDesc(void* p) {
    if (p == nullptr || g_studio == nullptr) return false;
    for (auto& [k, v] : g_studio->descs) if (v.get() == p) return true;
    return false;
}

int categoryFor(const std::string& path) {
    if (path.rfind("event:/music", 0) == 0) return 1;
    if (path.rfind("event:/ui", 0) == 0) return 2;
    return 0;
}

/// La ruta llega con el banco de nombres, que puede cargarse después que el del evento.
void refreshPath(Desc& d) {
    if (!d.path.empty() || d.def == nullptr) return;
    auto it = g_engine->model.pathById.find(d.id);
    if (it == g_engine->model.pathById.end()) return;
    d.path = it->second;
    d.category = categoryFor(d.path);
}

Desc* descFor(const Guid& id) {
    auto it = g_studio->descs.find(id);
    if (it != g_studio->descs.end()) {
        refreshPath(*it->second);
        return it->second.get();
    }
    auto ev = g_engine->model.events.find(id);
    if (ev == g_engine->model.events.end()) return nullptr;
    auto d = std::make_unique<Desc>();
    d->id = id;
    d->def = &ev->second;
    auto tl = g_engine->model.timelines.find(ev->second.timeline);
    d->tl = tl != g_engine->model.timelines.end() ? &tl->second : nullptr;
    d->path = ev->second.path;
    d->category = categoryFor(d->path);
    Desc* raw = d.get();
    g_studio->descs[id] = std::move(d);
    return raw;
}

/// Ganancia de los buses/VCA que el juego haya tocado para la categoría del evento.
/// Cadena de buses del evento: su bus de entrada y los padres hasta bus:/.
const std::vector<Guid>& busChain(Desc& d) {
    if (!d.chainBuilt) {
        d.chainBuilt = true;
        if (d.def != nullptr) {
            Guid b = d.def->inputBus;
            for (int i = 0; i < 32 && !b.isZero(); ++i) {
                auto it = g_engine->model.buses.find(b);
                if (it == g_engine->model.buses.end()) break;
                d.chain.push_back(b);
                b = it->second.parent;
            }
        }
    }
    return d.chain;
}

float busGainHeuristic(int category, bool& paused);

/// Ganancia de los buses del evento: faders del banco + snapshots activas (dB) y lo que el juego
/// haya puesto con Bus/VCA SetVolume, SetPaused o SetMute.
float busGain(Desc& d, const std::unordered_map<Guid, float, GuidHash>& snapDb, bool& paused) {
    const auto& chain = busChain(d);
    if (chain.empty()) return busGainHeuristic(d.category, paused);
    paused = false;
    float db = 0.f, g = 1.f;
    for (const Guid& b : chain) {
        db += g_engine->model.buses.at(b).faderDb;
        auto s = snapDb.find(b);
        if (s != snapDb.end()) db += s->second;
    }
    for (auto& [path, bo] : g_studio->buses) {
        bool applies = false;
        if (path.rfind("vca:/", 0) == 0) {  // VCA: por categoría
            const bool music = path.find("music") != std::string::npos;
            applies = path == "vca:/" || (d.category == 1 ? music : !music);
        } else {
            applies = std::find(chain.begin(), chain.end(), bo->id) != chain.end();
        }
        if (!applies) continue;
        g *= bo->mute ? 0.f : bo->volume;
        paused = paused || bo->paused;
    }
    if (db <= -79.f) return 0.f;
    return g * std::pow(10.f, db / 20.f);
}

float busGainHeuristic(int category, bool& paused) {
    float g = 1.f;
    paused = false;
    for (auto& [path, b] : g_studio->buses) {
        const bool master = path == "bus:/" || path == "vca:/";
        const bool music = path.find("music") != std::string::npos;
        const bool ui = path.find("ui") != std::string::npos;
        const bool sfx = path.find("sfx") != std::string::npos || path.find("gameplay") != std::string::npos;
        bool applies = master;
        if (!applies) {
            if (category == 1) applies = music;
            else if (category == 2) applies = ui || (sfx && !music && path.find("gameplay") == std::string::npos);
            else applies = sfx && !ui && !music;
        }
        if (!applies) continue;
        g *= b->mute ? 0.f : b->volume;
        paused = paused || b->paused;
    }
    return g;
}

// ------------------------------------------------------------------ mezcla

struct VoiceStart {
    int item = -1;
    uint32_t endAt = UINT32_MAX;
    int loops = 0;
    const InstrumentDef* chain[3] = {nullptr, nullptr, nullptr};
    int chainLen = 0;
};

void startVoiceFor(Instance& in, VoiceStart vs, const Guid& inst, uint32_t offsetFrames, int depth);

/// Arranca un instrumento de la línea de tiempo (índice `itemIdx`).
void startItem(Instance& in, int itemIdx, const Guid& inst, uint32_t offset) {
    VoiceStart vs;
    vs.item = itemIdx;
    const TimelineItem& it = in.desc->tl->items[static_cast<size_t>(itemIdx)];
    vs.endAt = it.start + it.length;
    startVoiceFor(in, vs, inst, offset, 0);
}

void startWave(Instance& in, const VoiceStart& vs, const Guid& waveId, uint32_t offset) {
    auto w = g_engine->model.waves.find(waveId);
    if (w == g_engine->model.waves.end()) return;
    const FsbSample* s = sampleOf(w->second.bank, w->second.subsound);
    if (s == nullptr) return;
    const int loops = vs.loops;
    Voice v;
    v.item = vs.item;
    v.endAt = vs.endAt;
    v.loops = loops;
    for (int i = 0; i < vs.chainLen && i < 3; ++i) v.inst[i] = vs.chain[i];
    v.loopStart = s->hasLoop && s->loopStart < s->numSamples ? s->loopStart : 0;
    double srcOffset = static_cast<double>(offset) * s->rate / kRate;
    if (loops != 0 && s->numSamples > 0 && srcOffset >= s->numSamples) {  // entra a mitad de un bucle
        srcOffset = v.loopStart + std::fmod(srcOffset - v.loopStart, static_cast<double>(s->numSamples - v.loopStart));
    }
    if (s->numSamples > kStreamFrames) {
        auto st = std::make_shared<Stream>();
        st->bank = w->second.bank;
        st->subsound = w->second.subsound;
        st->channels = s->channels;
        st->rate = s->rate;
        st->skip = static_cast<uint64_t>(srcOffset);
        {
            std::lock_guard<std::mutex> lock(g_engine->jobMu);
            g_engine->streams.push_back(st);
        }
        g_engine->jobCv.notify_one();
        v.stream = st;
    } else {
        v.pcm = getPcm(w->second.bank, w->second.subsound);
        v.pos = srcOffset;
    }
    in.voices.push_back(std::move(v));
}

void startVoiceFor(Instance& in, VoiceStart vs, const Guid& instId, uint32_t offset, int depth) {
    if (depth > 6) return;
    auto it = g_engine->model.instruments.find(instId);
    if (it == g_engine->model.instruments.end()) return;
    const InstrumentDef& d = it->second;
    if (d.loopCount != 0) vs.loops = d.loopCount;  // el bucle lo marca el instrumento o su contenedor
    if (!d.volumeCtrls.empty() && vs.chainLen < 3) vs.chain[vs.chainLen++] = &d;
    switch (d.kind) {
        case InstKind::Wave:
            startWave(in, vs, d.target, offset);
            break;
        case InstKind::Multi:
        case InstKind::Scatter:
            if (!d.children.empty()) {
                std::uniform_int_distribution<size_t> pick(0, d.children.size() - 1);
                startVoiceFor(in, vs, d.children[pick(g_studio->rng)], offset, depth + 1);
            }
            break;
        case InstKind::Event: {
            Desc* cd = descFor(d.target);
            if (cd == nullptr || (cd->def && cd->def->snapshot)) break;
            if (cd->tl == nullptr && (cd->def == nullptr || cd->def->sheet.empty())) break;
            auto child = std::make_unique<Instance>();
            child->desc = cd;
            child->started = true;
            child->state = kPlaying;
            child->playhead = offset;
            if (cd->tl) child->active.assign(cd->tl->items.size(), false);
            in.children.push_back(std::move(child));
            break;
        }
    }
}

/// Valor actual de un parámetro en la instancia: el que puso el juego o el del banco.
float paramValue(const Instance& in, const Guid& param) {
    auto p = g_engine->model.params.find(param);
    if (p == g_engine->model.params.end()) return 0.f;
    const ParamDef& d = p->second;
    auto v = in.params.find(d.name);
    const float x = v != in.params.end() ? v->second : d.defaultValue;
    return std::max(d.minimum, std::min(d.maximum, x));
}

/// Ganancia lineal de la automatización de volumen de una voz (suma de dB de sus controladores).
float voiceGain(const Instance& in, const Voice& v) {
    float db = 0.f;
    bool any = false;
    for (const InstrumentDef* d : v.inst) {
        if (d == nullptr) continue;
        for (const Guid& c : d->volumeCtrls) {
            auto p = g_engine->model.ctrlParam.find(c);
            if (p == g_engine->model.ctrlParam.end()) continue;
            db += g_engine->model.evalCurve(c, paramValue(in, p->second));
            any = true;
        }
    }
    if (!any) return 1.f;
    if (db <= -41.9f) return 0.f;  // el fondo de las curvas de FMOD Studio es silencio
    return std::pow(10.f, db / 20.f);
}

/// Mezcla `frames` fotogramas de la instancia en `out` (estéreo float). Devuelve false al acabar.
bool mixInstance(Instance& in, float* out, int frames, float gain, float panL = 1.f, float panR = 1.f) {
    static const TimelineDef kEmpty;
    const TimelineDef* tl = in.desc->tl ? in.desc->tl : &kEmpty;
    const std::vector<SheetItem>* sheet = in.desc->def ? &in.desc->def->sheet : nullptr;
    if (in.desc->tl == nullptr && (sheet == nullptr || sheet->empty())) return false;
    if (in.active.size() != tl->items.size()) in.active.assign(tl->items.size(), false);

    // Posición 3D (Celeste: relativa a la cámara, unidades de píxel, oyente en z = -345):
    // paneo por el ángulo (0 dB en el centro) y atenuación suave fuera de la pantalla.
    float gl = panL, gr = panR;
    if (in.has3d && g_studio != nullptr) {
        const float dx = in.pos[0] - g_studio->listener[0];
        const float dy = in.pos[1] - g_studio->listener[1];
        const float dz = in.pos[2] - g_studio->listener[2];
        const float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist > 1e-3f) {
            const float pan = std::max(-1.f, std::min(1.f, dx / dist));  // seno del ángulo horizontal
            constexpr float kMinDist = 420.f;                             // dentro: volumen completo
            const float att = dist <= kMinDist ? 1.f : std::max(0.f, kMinDist / dist);
            gl *= att * std::min(1.f, 1.f - pan * 0.8f);
            gr *= att * std::min(1.f, 1.f + pan * 0.8f);
        }
    }

    // Hoja de parámetro: cada instrumento suena mientras el valor esté en su rango.
    if (sheet != nullptr && !sheet->empty()) {
        if (in.sheetActive.size() != sheet->size()) in.sheetActive.assign(sheet->size(), false);
        for (size_t k = 0; k < sheet->size(); ++k) {
            const SheetItem& si = (*sheet)[k];
            const float x = paramValue(in, si.param);
            const bool inRange = !in.stopping && x >= si.start && x <= si.start + si.length;
            const int tag = -1 - static_cast<int>(k);
            if (inRange && !in.sheetActive[k]) {
                in.sheetActive[k] = true;
                VoiceStart vs;
                vs.item = tag;  // el bucle (si lo hay) lo marca el instrumento; suena una vez al entrar
                startVoiceFor(in, vs, si.instrument, 0, 0);
            } else if (!inRange && in.sheetActive[k]) {
                in.sheetActive[k] = false;
                for (auto& v : in.voices) if (v.item == tag) v.done = true;
            }
        }
    }

    // Salto (fin de una región de bucle) que cae en este bloque, mientras no se esté parando.
    const TimelineJump* jump = nullptr;
    if (!in.stopping) {
        for (const auto& j : tl->jumps) {
            if (j.hasCond) {
                const float x = paramValue(in, j.condParam);
                if (x < j.condMin || x > j.condMax) continue;
            }
            const bool hit = j.regionEnd > j.at
                                 ? in.playhead >= j.at && in.playhead < j.regionEnd  // región: ya
                                 : in.playhead <= j.at && in.playhead + frames >= j.at;  // >=: acaba justo ahí
            if (hit) {
                jump = &j;
                break;
            }
        }
    }
    // Instrumentos que empiezan en este bloque.
    const int64_t blockEnd = in.playhead + frames;
    for (size_t i = 0; i < tl->items.size(); ++i) {
        const TimelineItem& it = tl->items[i];
        const bool inside = blockEnd > it.start && in.playhead < static_cast<int64_t>(it.start) + it.length;
        if (inside && !in.active[i]) {
            in.active[i] = true;
            const uint32_t off = in.playhead > it.start ? static_cast<uint32_t>(in.playhead - it.start) : 0;
            startItem(in, static_cast<int>(i), it.instrument, off);
        }
    }

    const float vol = gain * in.volume;
    for (auto& v : in.voices) {
        if (v.done) continue;
        // Volumen automatizado, suavizado (~50 ms) para que los cambios de parámetro no chasqueen.
        const float target = voiceGain(in, v);
        v.gainNow = v.gainNow < 0.f ? target : v.gainNow + (target - v.gainNow) * 0.2f;
        const float vol = gain * in.volume * v.gainNow;
        const int64_t localEnd = static_cast<int64_t>(v.endAt) - in.playhead;  // fotogramas que le quedan
        int n = static_cast<int>(std::min<int64_t>(frames, std::max<int64_t>(0, localEnd)));
        if (n <= 0) {
            v.done = true;
            continue;
        }
        if (v.pcm) {
            Pcm& p = *v.pcm;
            if (p.failed) {
                v.done = true;
                continue;
            }
            if (!p.ready) continue;  // aún decodificando: silencio (la línea de tiempo sigue)
            const double step = static_cast<double>(p.rate) / kRate * in.pitch;
            const uint32_t ch = p.channels;
            for (int f = 0; f < n; ++f) {
                size_t i0 = static_cast<size_t>(v.pos);
                if (i0 + 1 >= p.frames) {
                    if (v.loops == 0 || p.frames <= v.loopStart + 1) {
                        v.done = true;
                        break;
                    }
                    if (v.loops > 0) --v.loops;
                    v.pos = v.loopStart + (v.pos - static_cast<double>(p.frames - 1));
                    i0 = static_cast<size_t>(v.pos);
                    if (i0 + 1 >= p.frames) i0 = v.loopStart;
                }
                const float t = static_cast<float>(v.pos - static_cast<double>(i0));
                float l, r;
                if (ch == 1) {
                    l = r = (p.data[i0] + (p.data[i0 + 1] - p.data[i0]) * t) * (1.f / 32768.f);
                } else {
                    const int16_t* a = &p.data[i0 * ch];
                    const int16_t* b = &p.data[(i0 + 1) * ch];
                    l = (a[0] + (b[0] - a[0]) * t) * (1.f / 32768.f);
                    r = (a[1] + (b[1] - a[1]) * t) * (1.f / 32768.f);
                }
                out[2 * f] += l * vol * in.fade * gl;
                out[2 * f + 1] += r * vol * in.fade * gr;
                v.pos += step;
            }
        } else if (v.stream) {
            Stream& s = *v.stream;
            std::lock_guard<std::mutex> lock(s.mu);
            const uint32_t ch = std::max<uint32_t>(1, s.channels);
            const double step = static_cast<double>(s.rate) / kRate * in.pitch;
            for (int f = 0; f < n; ++f) {
                v.frac += step;
                while (v.frac >= 1.0) {
                    if (s.buf.size() < ch) break;
                    v.prev[0] = v.last[0];
                    v.prev[1] = v.last[1];
                    v.last[0] = s.buf.front();
                    s.buf.pop_front();
                    v.last[1] = ch > 1 ? s.buf.front() : v.last[0];
                    if (ch > 1) s.buf.pop_front();
                    for (uint32_t c = 2; c < ch; ++c) s.buf.pop_front();
                    v.frac -= 1.0;
                }
                const float t = static_cast<float>(std::min(1.0, v.frac));
                const float l = (v.prev[0] + (v.last[0] - v.prev[0]) * t) * (1.f / 32768.f);
                const float r = (v.prev[1] + (v.last[1] - v.prev[1]) * t) * (1.f / 32768.f);
                out[2 * f] += l * vol * in.fade * gl;
                out[2 * f + 1] += r * vol * in.fade * gr;
            }
            if (s.eof && s.buf.empty()) {
                if (v.loops != 0) {  // bucle: el decodificador vuelve a empezar
                    if (v.loops > 0) --v.loops;
                    s.eof = false;
                    s.skip = v.loopStart;
                    s.restartRequested = true;
                } else {
                    v.done = true;
                }
            }
        }
    }

    // Eventos anidados.
    for (auto& c : in.children) {
        if (c->state == kStopped) continue;
        c->stopping = in.stopping;
        c->fade = in.fade;
        if (!mixInstance(*c, out, frames, vol, gl, gr)) c->state = kStopped;
    }

    // Fundido de salida.
    if (in.fadeStep > 0.f) {
        in.fade -= in.fadeStep * frames;
        if (in.fade <= 0.f) {
            in.fade = 0.f;
            return false;
        }
    }

    in.playhead += frames;
    // Salto: vuelta al inicio de la región; los instrumentos que la cubren vuelven a sonar.
    if (jump != nullptr && in.playhead >= jump->at) {
        if (in.desc->category == 1) {
            LOGI("%s %s: %u -> %u", jump->hasCond ? "Transición" : "Bucle", in.desc->path.c_str(), jump->at, jump->to);
        }
        in.playhead = jump->regionEnd > jump->at ? static_cast<int64_t>(jump->to)
                                                 : jump->to + (in.playhead - jump->at);
        for (auto& v : in.voices) {
            if (v.item < 0) continue;  // hoja de parámetro: no depende de la línea de tiempo
            const TimelineItem& it = tl->items[static_cast<size_t>(v.item)];
            if (it.start + it.length > jump->to) v.done = true;  // se reinicia
        }
        for (size_t i = 0; i < tl->items.size(); ++i) {
            const TimelineItem& it = tl->items[i];
            if (it.start + it.length > jump->to) in.active[i] = false;
        }
        for (auto& c : in.children) c->state = kStopped;
    }
    in.voices.erase(std::remove_if(in.voices.begin(), in.voices.end(), [](const Voice& v) { return v.done; }),
                    in.voices.end());
    in.children.erase(std::remove_if(in.children.begin(), in.children.end(),
                                     [](const std::unique_ptr<Instance>& c) { return c->state == kStopped; }),
                      in.children.end());

    // Fin: pasado el último instrumento y sin voces vivas (ni bucle que lo retenga).
    // Pendiente: algún instrumento aún no ha sonado y la línea de tiempo no ha pasado su final
    // (con "< inicio" un instrumento que empieza justo en el borde del bloque nunca sonaba).
    bool pending = false;
    for (size_t i = 0; i < tl->items.size() && !pending; ++i) {
        const TimelineItem& it = tl->items[i];
        pending = !in.active[i] && in.playhead < static_cast<int64_t>(it.start) + it.length;
    }
    // Un evento con hoja de parámetro sigue vivo (en silencio) hasta que lo paren: el juego puede
    // mover el parámetro dentro del rango más tarde.
    // Una instancia ya liberada (Play + release de un efecto) termina al quedarse sin voces.
    const bool sheetHold = sheet != nullptr && !sheet->empty() && !in.stopping && !in.released;
    return !in.voices.empty() || !in.children.empty() || pending || jump != nullptr || sheetHold;
}

/// Snapshots colocadas en la línea de tiempo de un evento (instrumento-evento que es snapshot):
/// se aplican mientras el playhead esté dentro de su región. También en eventos anidados.
void collectTimelineSnapshots(const Instance& in, std::unordered_map<Guid, float, GuidHash>& snapDb, int depth) {
    if (depth > 4 || in.desc->tl == nullptr || in.stopping) return;
    for (const TimelineItem& it : in.desc->tl->items) {
        if (in.playhead < it.start || in.playhead >= static_cast<int64_t>(it.start) + it.length) continue;
        auto idef = g_engine->model.instruments.find(it.instrument);
        if (idef == g_engine->model.instruments.end() || idef->second.kind != InstKind::Event) continue;
        auto ev = g_engine->model.events.find(idef->second.target);
        if (ev == g_engine->model.events.end() || !ev->second.snapshot) continue;
        auto s = g_engine->model.snapshots.find(ev->second.snapshotRef);
        if (s == g_engine->model.snapshots.end()) continue;
        for (const SnapEntry& e : s->second) {
            if (e.type == 0) snapDb[e.bus] += e.value;
        }
    }
    for (const auto& c : in.children) collectTimelineSnapshots(*c, snapDb, depth + 1);
}

void mixerLoop() {
    Engine& e = *g_engine;
    DirectAAudioEngine out;
    bool started = false;
    std::vector<float> mix(kBlock * 2);
    std::vector<int16_t> pcm(kBlock * 2);
    while (!e.quit) {
        if (e.outputPaused) {
            if (started) {
                out.stop();
                started = false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }
        if (!started) {
            started = out.start(kRate, 2);
            if (!started) {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
        }
        std::fill(mix.begin(), mix.end(), 0.f);
        {
            std::lock_guard<std::recursive_mutex> lock(g_mu);
            if (g_studio != nullptr) {
                std::vector<Instance*> finished;
                // Snapshots activas: dB que suman a sus buses (volumen, tipo 0).
                std::unordered_map<Guid, float, GuidHash> snapDb;
                for (Instance* in : g_studio->instances) {
                    const EventDef* def = in->desc->def;
                    if (def == nullptr || !def->snapshot || !in->started || in->state == kStopped) continue;
                    if (in->state == kStarting) in->state = kPlaying;
                    if (in->stopping) {  // una snapshot parada deja de aplicarse
                        in->state = kStopped;
                        if (in->released) finished.push_back(in);
                        continue;
                    }
                    auto s = g_engine->model.snapshots.find(def->snapshotRef);
                    if (s == g_engine->model.snapshots.end()) continue;
                    for (const SnapEntry& e : s->second) {
                        if (e.type == 0) snapDb[e.bus] += e.value;
                    }
                }
                for (Instance* in : g_studio->instances) {
                    if (in->started && !in->paused && in->state != kStopped) collectTimelineSnapshots(*in, snapDb, 0);
                }
                static size_t s_lastSnap = 0;
                if (snapDb.size() != s_lastSnap) {
                    s_lastSnap = snapDb.size();
                    LOGI("Snapshots activas: %zu buses afectados", s_lastSnap);
                }
                for (Instance* in : g_studio->instances) {
                    if (!in->started || in->paused || in->state == kStopped) continue;
                    if (in->desc->def != nullptr && in->desc->def->snapshot) continue;
                    bool busPaused = false;
                    const float target = busGain(*in->desc, snapDb, busPaused);
                    if (busPaused) continue;
                    // Cambios de bus/snapshot suavizados (~50 ms).
                    in->busNow = in->busNow < 0.f ? target : in->busNow + (target - in->busNow) * 0.2f;
                    const float g = in->busNow;
                    if (in->state == kStarting) in->state = kPlaying;
                    if (!mixInstance(*in, mix.data(), kBlock, g)) {
                        if (in->desc->category == 1) {
                            LOGI("Fin %s (playhead %lld)", in->desc->path.c_str(), static_cast<long long>(in->playhead));
                        }
                        in->state = kStopped;
                        in->voices.clear();
                        in->children.clear();
                        if (in->released) finished.push_back(in);
                    }
                }
                for (Instance* in : finished) {
                    g_studio->instances.erase(in);
                    delete in;
                }
            }
        }
        static float s_peak = 0.f;
        static int s_blocks = 0;
        for (int i = 0; i < kBlock * 2; ++i) {
            // Limitador suave: lineal hasta 0,8 y curva hacia 1,0 (sin el chasquido de recortar).
            float v = mix[static_cast<size_t>(i)] * 0.8f;  // margen: aún no se aplican los volúmenes de los buses
            const float a = std::fabs(v);
            if (a > 0.8f) v = std::copysign(0.8f + 0.2f * std::tanh((a - 0.8f) / 0.2f), v);
            s_peak = std::max(s_peak, std::fabs(v));
            pcm[static_cast<size_t>(i)] = static_cast<int16_t>(v * 32767.f);
        }
        if (++s_blocks >= 3000) {  // cada 30 s: estado del mezclador
            size_t live = 0, voices = 0;
            std::string names;
            {
                std::lock_guard<std::recursive_mutex> lock(g_mu);
                if (g_studio) {
                    for (Instance* in : g_studio->instances) {
                        if (in->state == kStopped || !in->started) continue;
                        ++live;
                        voices += in->voices.size();
                        if (names.size() < 400) {
                            names += in->desc->path;
                            if (in->desc->category == 1) {  // música: ganancia de cada capa
                                names += "[";
                                for (const auto& v : in->voices) {
                                    char g[12];
                                    std::snprintf(g, sizeof(g), "%.2f ", v.gainNow);
                                    names += g;
                                }
                                names += "]";
                            }
                            names += " ";
                        }
                    }
                }
            }
            LOGI("Mezclador: %zu eventos sonando, %zu voces, pico %.2f | %s", live, voices, s_peak, names.c_str());
            s_peak = 0.f;
            s_blocks = 0;
        }
        // AAudio acepta por tandas (espera 10 ms como mucho): se escribe el bloque entero; así el
        // mezclador va al ritmo real de la salida y no se pierde audio.
        int done = 0;
        for (int tries = 0; done < kBlock && tries < 50 && !e.quit && !e.outputPaused; ++tries) {
            const int32_t w = out.writePcm(pcm.data() + done * 2, kBlock - done);
            if (w < 0) {
                out.stop();
                started = false;
                break;
            }
            done += w;
        }
    }
    if (started) out.stop();
}

void ensureEngine() {
    if (g_engine != nullptr) return;
    g_engine = new Engine();
    g_engine->decoder = std::thread(decoderLoop);
    g_engine->mixer = std::thread(mixerLoop);
    LOGI("FMOD Studio nativo de HyperPlay iniciado (48 kHz estéreo)");
}

// ------------------------------------------------------------------ API: sistema

#define H(x) reinterpret_cast<void*>(x)

u64 System_Create(void** out, u64) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    ensureEngine();
    if (g_studio == nullptr) g_studio = new Studio();
    if (out) *out = &g_studioToken;
    return kOk;
}
u64 System_Initialize(void*, u64, u64, u64, void*) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (g_studio) g_studio->initialized = true;
    return kOk;
}
u64 System_Release(void*) { return kOk; }
u64 System_Update(void*) { return kOk; }
u64 System_Ok(void*) { return kOk; }
u64 System_IsValid(void* s) { return s == &g_studioToken ? 1 : 0; }
u64 System_GetLowLevelSystem(void*, void** out) {
    if (out) *out = &g_lowLevelToken;
    return kOk;
}

u64 loadBank(const std::string& hostPath, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    ensureEngine();
    if (g_studio == nullptr) g_studio = new Studio();
    Engine& e = *g_engine;
    for (auto& bo : g_studio->bankObjs) {
        if (e.banks[static_cast<size_t>(bo->index)]->path() == hostPath) {
            if (out) *out = bo.get();
            return kOk;
        }
    }
    auto bank = std::make_unique<Bank>();
    const int index = static_cast<int>(e.banks.size());
    std::string err;
    if (!bank->load(hostPath, index, &err)) {
        LOGW("LoadBankFile(%s): %s", hostPath.c_str(), err.c_str());
        return kErrFileNotFound;
    }
    e.model.ingest(*bank, index);
    LOGI("Banco %s: %zu eventos, %zu sonidos (nombres en total: %zu)", hostPath.c_str(), bank->events.size(),
         bank->fsb().samples.size(), e.model.idByPath.size());
    e.banks.push_back(std::move(bank));
    ensureSetups();
    auto bo = std::make_unique<BankObj>();
    bo->index = index;
    if (out) *out = bo.get();
    g_studio->bankObjs.push_back(std::move(bo));
    return kOk;
}

u64 System_LoadBankFile(void*, const char* file, u64, void** out) {
    if (file == nullptr) return kErrInvalidParam;
    const std::string host = g_hooks.resolvePath ? g_hooks.resolvePath(file) : std::string(file);
    return loadBank(host, out);
}

u64 System_GetEvent(void*, const char* path, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (out) *out = nullptr;
    if (path == nullptr || g_engine == nullptr) return kErrInvalidParam;
    Guid id;
    if (path[0] == '{') {
        if (!parseGuid(path, id)) return kErrInvalidParam;
    } else {
        auto it = g_engine->model.idByPath.find(path);
        if (it == g_engine->model.idByPath.end()) return kErrEventNotFound;
        id = it->second;
    }
    Desc* d = descFor(id);
    static int s_logged = 0;
    if (s_logged < 40) {
        ++s_logged;
        LOGI("GetEvent(%s) -> %s (%zu instrumentos)", path, d ? "ok" : "NO ENCONTRADO",
             d && d->tl ? d->tl->items.size() : static_cast<size_t>(0));
    }
    if (d == nullptr) return kErrEventNotFound;
    if (out) *out = d;
    return kOk;
}

u64 System_GetEventByID(void*, const Guid* id, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (out) *out = nullptr;
    if (id == nullptr || g_engine == nullptr) return kErrInvalidParam;
    Desc* d = descFor(*id);
    if (d == nullptr) return kErrEventNotFound;
    if (out) *out = d;
    return kOk;
}

BusObj* busFor(const std::string& path) {
    auto& b = g_studio->buses[path];
    if (!b) {
        b = std::make_unique<BusObj>();
        b->path = path;
        auto it = g_engine->model.idByPath.find(path);
        if (it != g_engine->model.idByPath.end()) b->id = it->second;
    }
    return b.get();
}

u64 System_GetBus(void*, const char* path, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (path == nullptr || g_studio == nullptr || g_engine == nullptr) return kErrInvalidParam;
    if (out) *out = busFor(path);
    return kOk;
}

u64 System_LookupID(void*, const char* path, Guid* out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (path == nullptr || g_engine == nullptr) return kErrInvalidParam;
    auto it = g_engine->model.idByPath.find(path);
    if (it == g_engine->model.idByPath.end()) return kErrEventNotFound;
    if (out) *out = it->second;
    return kOk;
}

u64 System_LookupPath(void*, const Guid* id, char* buf, int size, int* retrieved) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (id == nullptr || g_engine == nullptr) return kErrInvalidParam;
    auto it = g_engine->model.pathById.find(*id);
    if (it == g_engine->model.pathById.end()) return kErrEventNotFound;
    return putString(it->second, buf, size, retrieved);
}

u64 System_SetListenerAttributes(void*, u64, const float* attr) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (attr && g_studio) std::memcpy(g_studio->listener, attr, sizeof(g_studio->listener));
    return kOk;
}
u64 System_GetListenerAttributes(void*, u64, float* attr) {
    if (attr) std::memset(attr, 0, 48);
    return kOk;
}
u64 System_GetNumListeners(void*, int* n) {
    if (n) *n = 1;
    return kOk;
}
u64 System_SetUserData(void*, void* ud) {
    if (g_studio) g_studio->userData = ud;
    return kOk;
}
u64 System_GetUserData(void*, void** ud) {
    if (ud) *ud = g_studio ? g_studio->userData : nullptr;
    return kOk;
}
u64 System_GetBankCount(void*, int* n) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (n) *n = g_studio ? static_cast<int>(g_studio->bankObjs.size()) : 0;
    return kOk;
}
u64 System_GetBufferUsage(void*, void* usage) {
    if (usage) std::memset(usage, 0, 32);
    return kOk;
}
u64 System_GetCPUUsage(void*, void* usage) {
    if (usage) std::memset(usage, 0, 20);
    return kOk;
}
u64 ParseID(const char* s, Guid* out) {
    if (s == nullptr || out == nullptr || !parseGuid(s, *out)) return kErrInvalidParam;
    return kOk;
}

// ------------------------------------------------------------------ API: bancos

u64 Bank_GetLoadingState(void*, int* st) {
    if (st) *st = 3;  // LOADED
    return kOk;
}
u64 Bank_Ok(void*) { return kOk; }
u64 Bank_IsValid(void* b) { return b != nullptr ? 1 : 0; }
u64 Bank_GetEventCount(void* b, int* n) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (n) *n = 0;
    auto* bo = static_cast<BankObj*>(b);
    if (bo == nullptr || g_engine == nullptr) return kErrInvalidHandle;
    if (n) *n = static_cast<int>(g_engine->banks[static_cast<size_t>(bo->index)]->events.size());
    return kOk;
}
u64 Bank_GetEventList(void* b, void** arr, int cap, int* count) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto* bo = static_cast<BankObj*>(b);
    if (bo == nullptr || g_engine == nullptr) return kErrInvalidHandle;
    const auto& ev = g_engine->banks[static_cast<size_t>(bo->index)]->events;
    int n = 0;
    for (const Guid& g : ev) {
        if (n >= cap) break;
        if (Desc* d = descFor(g)) arr[n++] = d;
    }
    if (count) *count = n;
    return kOk;
}
u64 Bank_GetPath(void* b, char* buf, int size, int* retrieved) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto* bo = static_cast<BankObj*>(b);
    if (bo == nullptr || g_engine == nullptr) return kErrInvalidHandle;
    auto it = g_engine->model.pathById.find(g_engine->banks[static_cast<size_t>(bo->index)]->id());
    return putString(it != g_engine->model.pathById.end() ? it->second : std::string(), buf, size, retrieved);
}
u64 Bank_SetUserData(void* b, void* ud) {
    if (b) static_cast<BankObj*>(b)->userData = ud;
    return kOk;
}
u64 Bank_GetUserData(void* b, void** ud) {
    if (ud) *ud = b ? static_cast<BankObj*>(b)->userData : nullptr;
    return kOk;
}

// ------------------------------------------------------------------ API: descripciones

u64 Desc_CreateInstance(void* d, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (out) *out = nullptr;
    if (!validDesc(d)) return kErrInvalidHandle;
    auto* in = new Instance();
    in->desc = static_cast<Desc*>(d);
    g_studio->instances.insert(in);
    if (out) *out = in;
    return kOk;
}
u64 Desc_GetID(void* d, Guid* id) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validDesc(d)) return kErrInvalidHandle;
    if (id) *id = static_cast<Desc*>(d)->id;
    return kOk;
}
u64 Desc_GetPath(void* d, char* buf, int size, int* retrieved) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validDesc(d)) return kErrInvalidHandle;
    auto* desc = static_cast<Desc*>(d);
    refreshPath(*desc);
    return putString(desc->path, buf, size, retrieved);
}
u64 Desc_GetLength(void* d, int* ms) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validDesc(d)) return kErrInvalidHandle;
    const TimelineDef* tl = static_cast<Desc*>(d)->tl;
    if (ms) *ms = tl ? static_cast<int>(tl->length / (kRate / 1000)) : 0;
    return kOk;
}
u64 Desc_Is3D(void* d, int* b) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b) *b = validDesc(d) && static_cast<Desc*>(d)->category == 0 ? 1 : 0;
    return kOk;
}
u64 Desc_IsOneshot(void* d, int* b) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b) *b = validDesc(d) && static_cast<Desc*>(d)->tl && static_cast<Desc*>(d)->tl->jumps.empty() ? 1 : 0;
    return kOk;
}
u64 Desc_False(void*, int* b) {
    if (b) *b = 0;
    return kOk;
}
u64 Desc_IsSnapshot(void* d, int* b) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b) *b = validDesc(d) && static_cast<Desc*>(d)->def && static_cast<Desc*>(d)->def->snapshot ? 1 : 0;
    return kOk;
}
u64 Desc_IsValid(void* d) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    return validDesc(d) ? 1 : 0;
}
u64 Desc_Ok(void*) { return kOk; }
u64 Desc_GetSampleLoadingState(void*, int* st) {
    if (st) *st = 3;
    return kOk;
}
u64 Desc_GetInstanceCount(void* d, int* n) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    int c = 0;
    if (g_studio) for (Instance* in : g_studio->instances) if (in->desc == d && !in->released) ++c;
    if (n) *n = c;
    return kOk;
}
u64 Desc_GetParameterCount(void*, int* n) {
    if (n) *n = 0;
    return kOk;
}
u64 Desc_GetDistance(void*, float* f) {
    if (f) *f = 0.f;
    return kOk;
}
u64 Desc_ReleaseAllInstances(void* d) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (g_studio) {
        for (Instance* in : g_studio->instances) {
            if (in->desc != d) continue;
            in->released = true;
            in->stopping = true;
            in->state = kStopped;
        }
    }
    return kOk;
}
u64 Desc_SetUserData(void* d, void* ud) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validDesc(d)) return kErrInvalidHandle;
    static_cast<Desc*>(d)->userData = ud;
    return kOk;
}
u64 Desc_GetUserData(void* d, void** ud) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (ud) *ud = validDesc(d) ? static_cast<Desc*>(d)->userData : nullptr;
    return kOk;
}
u64 Desc_SetCallback(void*, void*, u64) { return kOk; }

// ------------------------------------------------------------------ API: instancias

u64 Inst_Start(void* p) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    auto* in = static_cast<Instance*>(p);
    in->voices.clear();
    in->children.clear();
    in->active.clear();
    in->playhead = 0;
    in->fade = 1.f;
    in->fadeStep = 0.f;
    in->stopping = false;
    in->started = true;
    in->state = kStarting;
    static int s_logged = 0;
    if (s_logged < 300) {
        ++s_logged;
        LOGI("Start %s", in->desc->path.c_str());
    }
    // Precalentar: las muestras cortas del principio empiezan a decodificarse ya.
    if (in->desc->tl) {
        for (const auto& it : in->desc->tl->items) {
            if (it.start > kRate / 2) continue;
            auto idef = g_engine->model.instruments.find(it.instrument);
            if (idef == g_engine->model.instruments.end() || idef->second.kind != InstKind::Wave) continue;
            auto w = g_engine->model.waves.find(idef->second.target);
            if (w == g_engine->model.waves.end()) continue;
            const FsbSample* s = sampleOf(w->second.bank, w->second.subsound);
            if (s && s->numSamples <= kStreamFrames) getPcm(w->second.bank, w->second.subsound);
        }
    }
    return kOk;
}
u64 Inst_Stop(void* p, u64 mode) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    auto* in = static_cast<Instance*>(p);
    if (in->state == kStopped) return kOk;
    if (in->desc->category == 1) LOGI("Stop %s (modo %u)", in->desc->path.c_str(), static_cast<unsigned>(mode));
    in->stopping = true;
    if (mode == 1) {  // IMMEDIATE
        in->state = kStopped;
        in->voices.clear();
        in->children.clear();
        if (in->released) {
            g_studio->instances.erase(in);
            delete in;
        }
    } else {  // ALLOWFADEOUT: fundido corto (música más largo)
        const float secs = in->desc->category == 1 ? 1.0f : 0.15f;
        in->fadeStep = 1.f / (secs * kRate);
        in->state = kStopping;
    }
    return kOk;
}
u64 Inst_Release(void* p) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    auto* in = static_cast<Instance*>(p);
    in->released = true;
    if (in->state == kStopped) {
        g_studio->instances.erase(in);
        delete in;
    }
    return kOk;
}
u64 Inst_GetPlaybackState(void* p, int* st) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (st) *st = kStopped;
    if (!validInstance(p)) return kErrInvalidHandle;
    if (st) *st = static_cast<Instance*>(p)->state;
    return kOk;
}
u64 Inst_SetPaused(void* p, u64 paused) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    static_cast<Instance*>(p)->paused = (paused & 0xFFFFFFFFu) != 0;
    return kOk;
}
u64 Inst_GetPaused(void* p, int* b) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (b) *b = static_cast<Instance*>(p)->paused ? 1 : 0;
    return kOk;
}
u64 Inst_SetVolume(void* p) {
    const float v = argF(1);
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    static_cast<Instance*>(p)->volume = std::max(0.f, v);
    return kOk;
}
u64 Inst_GetVolume(void* p, float* v, float* fin) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (v) *v = static_cast<Instance*>(p)->volume;
    if (fin) *fin = static_cast<Instance*>(p)->volume;
    return kOk;
}
u64 Inst_SetPitch(void* p) {
    const float v = argF(1);
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    static_cast<Instance*>(p)->pitch = std::max(0.05f, std::min(8.f, v));
    return kOk;
}
u64 Inst_GetPitch(void* p, float* v, float* fin) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (v) *v = static_cast<Instance*>(p)->pitch;
    if (fin) *fin = static_cast<Instance*>(p)->pitch;
    return kOk;
}
u64 Inst_Set3DAttributes(void* p, const float* attr) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    auto* in = static_cast<Instance*>(p);
    if (attr) {
        std::memcpy(in->pos, attr, sizeof(in->pos));
        in->has3d = true;
        static int s_logged = 0;
        if (s_logged < 25) {
            ++s_logged;
            LOGI("3D %s pos=(%.2f, %.2f, %.2f) oyente=(%.2f, %.2f, %.2f)", in->desc->path.c_str(), attr[0], attr[1],
                 attr[2], g_studio->listener[0], g_studio->listener[1], g_studio->listener[2]);
        }
    }
    return kOk;
}
u64 Inst_Get3DAttributes(void* p, float* attr) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (attr) {
        std::memset(attr, 0, 48);
        std::memcpy(attr, static_cast<Instance*>(p)->pos, sizeof(float) * 3);
    }
    return kOk;
}
u64 Inst_SetParameterValue(void* p, const char* name) {
    const float v = argF(2);
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p) || name == nullptr) return kErrInvalidHandle;
    static_cast<Instance*>(p)->params[name] = v;
    return kOk;
}
u64 Inst_GetParameterValue(void* p, const char* name, float* v, float* fin) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p) || name == nullptr) return kErrInvalidHandle;
    auto& m = static_cast<Instance*>(p)->params;
    auto it = m.find(name);
    const float val = it != m.end() ? it->second : 0.f;
    if (v) *v = val;
    if (fin) *fin = val;
    return kOk;
}
u64 Inst_SetParameterValueByIndex(void* p, u64) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    return validInstance(p) ? kOk : kErrInvalidHandle;
}
u64 Inst_GetParameter(void* p, const char* name, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p) || name == nullptr) return kErrInvalidHandle;
    const std::string key = std::to_string(reinterpret_cast<uintptr_t>(p)) + "|" + name;
    auto& po = g_studio->paramObjs[key];
    if (!po) {
        po = std::make_unique<ParamObj>();
        po->inst = static_cast<Instance*>(p);
        po->name = name;
    }
    if (out) *out = po.get();
    return kOk;
}
u64 Param_SetValue(void* p) {
    const float v = argF(1);
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto* po = static_cast<ParamObj*>(p);
    if (po == nullptr || !validInstance(po->inst)) return kErrInvalidHandle;
    po->inst->params[po->name] = v;
    return kOk;
}
u64 Param_GetValue(void* p, float* v) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto* po = static_cast<ParamObj*>(p);
    if (po == nullptr || !validInstance(po->inst)) return kErrInvalidHandle;
    auto it = po->inst->params.find(po->name);
    if (v) *v = it != po->inst->params.end() ? it->second : 0.f;
    return kOk;
}
u64 Param_IsValid(void* p) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    auto* po = static_cast<ParamObj*>(p);
    return po != nullptr && validInstance(po->inst) ? 1 : 0;
}
u64 Inst_GetDescription(void* p, void** out) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (out) *out = static_cast<Instance*>(p)->desc;
    return kOk;
}
u64 Inst_IsValid(void* p) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    return validInstance(p) ? 1 : 0;
}
u64 Inst_SetTimelinePosition(void* p, u64 ms) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    auto* in = static_cast<Instance*>(p);
    in->playhead = static_cast<int64_t>(static_cast<int32_t>(ms)) * (kRate / 1000);
    in->voices.clear();
    in->children.clear();
    in->active.clear();
    return kOk;
}
u64 Inst_GetTimelinePosition(void* p, int* ms) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (ms) *ms = static_cast<int>(static_cast<Instance*>(p)->playhead / (kRate / 1000));
    return kOk;
}
u64 Inst_SetUserData(void* p, void* ud) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    static_cast<Instance*>(p)->userData = ud;
    return kOk;
}
u64 Inst_GetUserData(void* p, void** ud) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (!validInstance(p)) return kErrInvalidHandle;
    if (ud) *ud = static_cast<Instance*>(p)->userData;
    return kOk;
}
u64 Inst_Ok(void* p) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    return validInstance(p) ? kOk : kErrInvalidHandle;
}
u64 Inst_IsVirtual(void*, int* b) {
    if (b) *b = 0;
    return kOk;
}

// ------------------------------------------------------------------ API: buses y VCA

u64 Bus_SetVolume(void* b) {
    const float v = argF(1);
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    static_cast<BusObj*>(b)->volume = std::max(0.f, v);
    return kOk;
}
u64 Bus_GetVolume(void* b, float* v, float* fin) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    if (v) *v = static_cast<BusObj*>(b)->volume;
    if (fin) *fin = static_cast<BusObj*>(b)->volume;
    return kOk;
}
u64 Bus_SetPaused(void* b, u64 v) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    static_cast<BusObj*>(b)->paused = (v & 0xFFFFFFFFu) != 0;
    return kOk;
}
u64 Bus_GetPaused(void* b, int* v) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    if (v) *v = static_cast<BusObj*>(b)->paused ? 1 : 0;
    return kOk;
}
u64 Bus_SetMute(void* b, u64 v) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    static_cast<BusObj*>(b)->mute = (v & 0xFFFFFFFFu) != 0;
    return kOk;
}
u64 Bus_GetMute(void* b, int* v) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    if (v) *v = static_cast<BusObj*>(b)->mute ? 1 : 0;
    return kOk;
}
u64 Bus_StopAllEvents(void* b, u64 mode) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr || g_studio == nullptr) return kErrInvalidHandle;
    const std::string& path = static_cast<BusObj*>(b)->path;
    std::vector<Instance*> all(g_studio->instances.begin(), g_studio->instances.end());
    for (Instance* in : all) {
        bool unused;
        (void)unused;
        const int cat = in->desc->category;
        const bool match = path == "bus:/" || (cat == 1 && path.find("music") != std::string::npos) ||
                           (cat == 2 && path.find("ui") != std::string::npos) ||
                           (cat == 0 && (path.find("sfx") != std::string::npos || path.find("gameplay") != std::string::npos));
        if (match) Inst_Stop(in, mode);
    }
    return kOk;
}
u64 Bus_GetID(void* b, Guid* id) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    if (id) *id = static_cast<BusObj*>(b)->id;
    return kOk;
}
u64 Bus_GetPath(void* b, char* buf, int size, int* retrieved) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (b == nullptr) return kErrInvalidHandle;
    return putString(static_cast<BusObj*>(b)->path, buf, size, retrieved);
}
u64 Bus_IsValid(void* b) { return b != nullptr ? 1 : 0; }
u64 Bus_Ok(void*) { return kOk; }

// ------------------------------------------------------------------ tabla de exports

struct Export {
    const char* name;
    void* fn;
};
const Export kExports[] = {
    {"FMOD_Studio_System_Create", H(System_Create)},
    {"FMOD_Studio_System_Initialize", H(System_Initialize)},
    {"FMOD_Studio_System_Release", H(System_Release)},
    {"FMOD_Studio_System_Update", H(System_Update)},
    {"FMOD_Studio_System_FlushCommands", H(System_Ok)},
    {"FMOD_Studio_System_FlushSampleLoading", H(System_Ok)},
    {"FMOD_Studio_System_UnloadAll", H(System_Ok)},
    {"FMOD_Studio_System_IsValid", H(System_IsValid)},
    {"FMOD_Studio_System_GetLowLevelSystem", H(System_GetLowLevelSystem)},
    {"FMOD_Studio_System_LoadBankFile", H(System_LoadBankFile)},
    {"FMOD_Studio_System_GetEvent", H(System_GetEvent)},
    {"FMOD_Studio_System_GetEventByID", H(System_GetEventByID)},
    {"FMOD_Studio_System_GetBus", H(System_GetBus)},
    {"FMOD_Studio_System_GetVCA", H(System_GetBus)},
    {"FMOD_Studio_System_LookupID", H(System_LookupID)},
    {"FMOD_Studio_System_LookupPath", H(System_LookupPath)},
    {"FMOD_Studio_System_SetListenerAttributes", H(System_SetListenerAttributes)},
    {"FMOD_Studio_System_GetListenerAttributes", H(System_GetListenerAttributes)},
    {"FMOD_Studio_System_GetNumListeners", H(System_GetNumListeners)},
    {"FMOD_Studio_System_SetNumListeners", H(System_Ok)},
    {"FMOD_Studio_System_SetUserData", H(System_SetUserData)},
    {"FMOD_Studio_System_GetUserData", H(System_GetUserData)},
    {"FMOD_Studio_System_SetCallback", H(System_Ok)},
    {"FMOD_Studio_System_GetBankCount", H(System_GetBankCount)},
    {"FMOD_Studio_System_GetBufferUsage", H(System_GetBufferUsage)},
    {"FMOD_Studio_System_ResetBufferUsage", H(System_Ok)},
    {"FMOD_Studio_System_GetCPUUsage", H(System_GetCPUUsage)},
    {"FMOD_Studio_ParseID", H(ParseID)},
    {"FMOD_Studio_Bank_GetLoadingState", H(Bank_GetLoadingState)},
    {"FMOD_Studio_Bank_GetSampleLoadingState", H(Bank_GetLoadingState)},
    {"FMOD_Studio_Bank_LoadSampleData", H(Bank_Ok)},
    {"FMOD_Studio_Bank_UnloadSampleData", H(Bank_Ok)},
    {"FMOD_Studio_Bank_Unload", H(Bank_Ok)},
    {"FMOD_Studio_Bank_IsValid", H(Bank_IsValid)},
    {"FMOD_Studio_Bank_GetEventCount", H(Bank_GetEventCount)},
    {"FMOD_Studio_Bank_GetEventList", H(Bank_GetEventList)},
    {"FMOD_Studio_Bank_GetPath", H(Bank_GetPath)},
    {"FMOD_Studio_Bank_SetUserData", H(Bank_SetUserData)},
    {"FMOD_Studio_Bank_GetUserData", H(Bank_GetUserData)},
    {"FMOD_Studio_EventDescription_CreateInstance", H(Desc_CreateInstance)},
    {"FMOD_Studio_EventDescription_GetID", H(Desc_GetID)},
    {"FMOD_Studio_EventDescription_GetPath", H(Desc_GetPath)},
    {"FMOD_Studio_EventDescription_GetLength", H(Desc_GetLength)},
    {"FMOD_Studio_EventDescription_Is3D", H(Desc_Is3D)},
    {"FMOD_Studio_EventDescription_IsOneshot", H(Desc_IsOneshot)},
    {"FMOD_Studio_EventDescription_IsSnapshot", H(Desc_IsSnapshot)},
    {"FMOD_Studio_EventDescription_IsStream", H(Desc_False)},
    {"FMOD_Studio_EventDescription_HasCue", H(Desc_False)},
    {"FMOD_Studio_EventDescription_IsValid", H(Desc_IsValid)},
    {"FMOD_Studio_EventDescription_LoadSampleData", H(Desc_Ok)},
    {"FMOD_Studio_EventDescription_UnloadSampleData", H(Desc_Ok)},
    {"FMOD_Studio_EventDescription_GetSampleLoadingState", H(Desc_GetSampleLoadingState)},
    {"FMOD_Studio_EventDescription_GetInstanceCount", H(Desc_GetInstanceCount)},
    {"FMOD_Studio_EventDescription_GetParameterCount", H(Desc_GetParameterCount)},
    {"FMOD_Studio_EventDescription_GetMaximumDistance", H(Desc_GetDistance)},
    {"FMOD_Studio_EventDescription_GetMinimumDistance", H(Desc_GetDistance)},
    {"FMOD_Studio_EventDescription_ReleaseAllInstances", H(Desc_ReleaseAllInstances)},
    {"FMOD_Studio_EventDescription_SetUserData", H(Desc_SetUserData)},
    {"FMOD_Studio_EventDescription_GetUserData", H(Desc_GetUserData)},
    {"FMOD_Studio_EventDescription_SetCallback", H(Desc_SetCallback)},
    {"FMOD_Studio_EventInstance_Start", H(Inst_Start)},
    {"FMOD_Studio_EventInstance_Stop", H(Inst_Stop)},
    {"FMOD_Studio_EventInstance_Release", H(Inst_Release)},
    {"FMOD_Studio_EventInstance_GetPlaybackState", H(Inst_GetPlaybackState)},
    {"FMOD_Studio_EventInstance_SetPaused", H(Inst_SetPaused)},
    {"FMOD_Studio_EventInstance_GetPaused", H(Inst_GetPaused)},
    {"FMOD_Studio_EventInstance_SetVolume", H(Inst_SetVolume)},
    {"FMOD_Studio_EventInstance_GetVolume", H(Inst_GetVolume)},
    {"FMOD_Studio_EventInstance_SetPitch", H(Inst_SetPitch)},
    {"FMOD_Studio_EventInstance_GetPitch", H(Inst_GetPitch)},
    {"FMOD_Studio_EventInstance_Set3DAttributes", H(Inst_Set3DAttributes)},
    {"FMOD_Studio_EventInstance_Get3DAttributes", H(Inst_Get3DAttributes)},
    {"FMOD_Studio_EventInstance_SetParameterValue", H(Inst_SetParameterValue)},
    {"FMOD_Studio_EventInstance_GetParameterValue", H(Inst_GetParameterValue)},
    {"FMOD_Studio_EventInstance_SetParameterValueByIndex", H(Inst_SetParameterValueByIndex)},
    {"FMOD_Studio_EventInstance_GetParameter", H(Inst_GetParameter)},
    {"FMOD_Studio_EventInstance_GetDescription", H(Inst_GetDescription)},
    {"FMOD_Studio_EventInstance_IsValid", H(Inst_IsValid)},
    {"FMOD_Studio_EventInstance_IsVirtual", H(Inst_IsVirtual)},
    {"FMOD_Studio_EventInstance_SetTimelinePosition", H(Inst_SetTimelinePosition)},
    {"FMOD_Studio_EventInstance_GetTimelinePosition", H(Inst_GetTimelinePosition)},
    {"FMOD_Studio_EventInstance_SetUserData", H(Inst_SetUserData)},
    {"FMOD_Studio_EventInstance_GetUserData", H(Inst_GetUserData)},
    {"FMOD_Studio_EventInstance_SetCallback", H(Inst_Ok)},
    {"FMOD_Studio_EventInstance_TriggerCue", H(Inst_Ok)},
    {"FMOD_Studio_EventInstance_SetListenerMask", H(Inst_Ok)},
    {"FMOD_Studio_EventInstance_SetReverbLevel", H(Inst_Ok)},
    {"FMOD_Studio_EventInstance_SetProperty", H(Inst_Ok)},
    {"FMOD_Studio_ParameterInstance_SetValue", H(Param_SetValue)},
    {"FMOD_Studio_ParameterInstance_GetValue", H(Param_GetValue)},
    {"FMOD_Studio_ParameterInstance_IsValid", H(Param_IsValid)},
    {"FMOD_Studio_Bus_SetVolume", H(Bus_SetVolume)},
    {"FMOD_Studio_Bus_GetVolume", H(Bus_GetVolume)},
    {"FMOD_Studio_Bus_SetPaused", H(Bus_SetPaused)},
    {"FMOD_Studio_Bus_GetPaused", H(Bus_GetPaused)},
    {"FMOD_Studio_Bus_SetMute", H(Bus_SetMute)},
    {"FMOD_Studio_Bus_GetMute", H(Bus_GetMute)},
    {"FMOD_Studio_Bus_StopAllEvents", H(Bus_StopAllEvents)},
    {"FMOD_Studio_Bus_GetID", H(Bus_GetID)},
    {"FMOD_Studio_Bus_GetPath", H(Bus_GetPath)},
    {"FMOD_Studio_Bus_IsValid", H(Bus_IsValid)},
    {"FMOD_Studio_Bus_LockChannelGroup", H(Bus_Ok)},
    {"FMOD_Studio_Bus_UnlockChannelGroup", H(Bus_Ok)},
    {"FMOD_Studio_VCA_SetVolume", H(Bus_SetVolume)},
    {"FMOD_Studio_VCA_GetVolume", H(Bus_GetVolume)},
    {"FMOD_Studio_VCA_GetID", H(Bus_GetID)},
    {"FMOD_Studio_VCA_GetPath", H(Bus_GetPath)},
    {"FMOD_Studio_VCA_IsValid", H(Bus_IsValid)},
};

}  // namespace

void init(const Hooks& hooks) {
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    g_hooks = hooks;
}

void* resolveExport(const char* name) {
    if (name == nullptr) return nullptr;
    for (const Export& e : kExports) {
        if (std::strcmp(e.name, name) == 0) return e.fn;
    }
    return nullptr;
}

void setOutputPaused(bool paused) {
    if (g_engine) g_engine->outputPaused = paused;
}

void shutdown() {
    Engine* e = g_engine;
    if (e == nullptr) return;
    // Los hilos usan g_engine: primero se paran (sin el candado, que también toman ellos).
    e->quit = true;
    e->jobCv.notify_all();
    if (e->mixer.joinable()) e->mixer.join();
    if (e->decoder.joinable()) e->decoder.join();
    std::lock_guard<std::recursive_mutex> lock(g_mu);
    if (g_studio) {
        for (Instance* in : g_studio->instances) delete in;
        delete g_studio;
        g_studio = nullptr;
    }
    g_engine = nullptr;
    delete e;
}

}  // namespace hyperplay::fmodhost
