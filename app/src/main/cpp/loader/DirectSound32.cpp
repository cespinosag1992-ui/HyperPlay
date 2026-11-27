// DirectSound 8 para invitados de 32 bits sobre AAudio. Ver DirectSound32.h.
//
// Objetos COM en memoria baja con vtables de 32 bits: cada método es una función nativa con la
// aridad exacta (stdcall, `this` incluido). Los búferes de sonido viven en memoria baja: Lock devuelve
// punteros directos. Cada búfer secundario en reproducción tiene un hilo que lee un periodo (10 ms)
// desde el cursor de reproducción, lo convierte a PCM16 estéreo y lo escribe (bloqueando) en AAudio,
// que marca el ritmo. Al pasar por las posiciones de IDirectSoundNotify se señalan sus eventos.

#include "DirectSound32.h"

#include <android/log.h>
#include <sys/mman.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_set>
#include <vector>

#include "../audio/DirectAAudioEngine.h"
#include "FramePacing.h"
#include "LowMem.h"
#include "NativeDllInterceptor.h"
#include "Win32Arity.h"

#define LOG_TAG "HyperPlay.DSound32"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace w32alias {
uintptr_t lowFor(void* host, const std::string& name);  // NativeDllInterceptor.cpp
}

namespace dsound32 {
namespace {

using u32 = uint32_t;

constexpr u32 kOk = 0;
constexpr u32 kNoInterface = 0x80004002u;
constexpr u32 kPointer = 0x80004003u;
constexpr u32 kInvalidParam = 0x80070057u;
constexpr u32 kOutOfMemory = 0x8007000Eu;
constexpr u32 kDsUnsupported = 0x80004001u;  // E_NOTIMPL
constexpr u32 kDsbcapsPrimary = 0x1;

template <typename T = uint8_t>
T* G(u32 a) { return reinterpret_cast<T*>(static_cast<uintptr_t>(a)); }
u32 L(const void* p) { return static_cast<u32>(reinterpret_cast<uintptr_t>(p)); }
void put32(u32 addr, u32 v) { if (addr != 0) std::memcpy(G<void>(addr), &v, 4); }
u32 rd32(u32 addr) { u32 v = 0; if (addr != 0) std::memcpy(&v, G<void>(addr), 4); return v; }
uint32_t guidData1(u32 addr) { return rd32(addr); }

void* lowAlloc(size_t n) {
    static std::mutex m;
    static uint8_t* cur = nullptr;
    static size_t left = 0;
    std::lock_guard<std::mutex> lk(m);
    n = (n + 15) & ~size_t{15};
    if (n > 0x8000) {
        void* p = lowmem::mmapBelow4G((n + 0xFFF) & ~size_t{0xFFF});
        return p == MAP_FAILED ? nullptr : p;
    }
    if (cur == nullptr || left < n) {
        void* p = lowmem::mmapBelow4G(0x10000);
        if (p == MAP_FAILED) return nullptr;
        cur = static_cast<uint8_t*>(p);
        left = 0x10000;
    }
    uint8_t* r = cur;
    cur += n;
    left -= n;
    return r;
}

struct Format {
    u32 rate = 48000;
    u32 channels = 2;
    u32 bits = 16;
    u32 blockAlign = 4;
    bool isFloat = false;
};

bool readFormat(u32 addr, Format& f) {
    if (addr == 0) return false;
    const uint8_t* p = G(addr);
    uint16_t tag = 0, ch = 0, align = 0, bits = 0, cb = 0;
    u32 rate = 0;
    std::memcpy(&tag, p, 2);
    std::memcpy(&ch, p + 2, 2);
    std::memcpy(&rate, p + 4, 4);
    std::memcpy(&align, p + 12, 2);
    std::memcpy(&bits, p + 14, 2);
    std::memcpy(&cb, p + 16, 2);
    bool isFloat = tag == 3;
    if (tag == 0xFFFE && cb >= 22) {
        u32 sub = 0;
        std::memcpy(&sub, p + 24, 4);
        isFloat = sub == 3;
        if (sub != 1 && sub != 3) return false;
    } else if (tag != 1 && tag != 3) {
        return false;
    }
    if (ch == 0 || ch > 8 || rate < 4000 || rate > 192000) return false;
    if (isFloat ? bits != 32 : (bits != 8 && bits != 16 && bits != 24 && bits != 32)) return false;
    f.rate = rate;
    f.channels = ch;
    f.bits = bits;
    f.blockAlign = align != 0 ? align : ch * (bits / 8);
    f.isFloat = isFloat;
    return true;
}

float sampleAt(const Format& f, const uint8_t* frame, u32 ch) {
    const uint8_t* s = frame + ch * (f.bits / 8);
    if (f.isFloat) { float v; std::memcpy(&v, s, 4); return v; }
    switch (f.bits) {
        case 8: return (static_cast<float>(*s) - 128.0f) / 128.0f;
        case 16: { int16_t v; std::memcpy(&v, s, 2); return static_cast<float>(v) / 32768.0f; }
        case 24: {
            const int32_t v = (static_cast<int32_t>(s[0]) << 8 | static_cast<int32_t>(s[1]) << 16 |
                               static_cast<int32_t>(s[2]) << 24) >> 8;
            return static_cast<float>(v) / 8388608.0f;
        }
        default: { int32_t v; std::memcpy(&v, s, 4); return static_cast<float>(v) / 2147483648.0f; }
    }
}

int16_t to16(float v) {
    v *= 32767.0f;
    if (v > 32767.0f) v = 32767.0f;
    if (v < -32768.0f) v = -32768.0f;
    return static_cast<int16_t>(v);
}

struct Notify {
    u32 offset;
    u32 event;
};

struct Buffer;

// Cabeceras de los objetos COM: la primera palabra es la vtable de 32 bits.
struct ComHead {
    u32 vtbl;
    u32 kind;  // 1 = IDirectSound8, 2 = búfer, 3 = IDirectSoundNotify
    Buffer* owner;
};

struct Buffer {
    ComHead com;     // IDirectSoundBuffer8
    ComHead notify;  // IDirectSoundNotify (mismo búfer)
    std::atomic<u32> refs{1};
    bool primary = false;
    Format fmt;
    uint8_t* data = nullptr;  // memoria baja
    u32 bytes = 0;
    std::mutex mtx;
    std::atomic<u32> playPos{0};  // en bytes
    std::atomic<bool> playing{false};
    std::atomic<bool> looping{false};
    std::atomic<bool> quit{false};
    std::atomic<float> gain{1.0f};
    std::vector<Notify> notifies;
    std::thread pump;
};

struct Device {
    ComHead com;
    std::atomic<u32> refs{1};
};

std::mutex g_buffersMutex;
std::unordered_set<Buffer*> g_buffers;
u32 g_vtDevice = 0, g_vtBuffer = 0, g_vtNotify = 0;

Buffer* B(u32 self) {
    auto* h = G<ComHead>(self);
    return h != nullptr ? h->owner : nullptr;
}

void signalPassed(Buffer* b, u32 from, u32 to) {
    // Eventos cuyo offset está en (from, to] avanzando circularmente.
    std::vector<u32> events;
    {
        std::lock_guard<std::mutex> lk(b->mtx);
        for (const Notify& n : b->notifies) {
            if (n.offset == 0xFFFFFFFFu) continue;
            const bool hit = from <= to ? (n.offset >= from && n.offset < to) : (n.offset >= from || n.offset < to);
            if (hit) events.push_back(n.event);
        }
    }
    for (u32 e : events) NativeDllInterceptor::win_SetEvent(reinterpret_cast<void*>(static_cast<uintptr_t>(e)));
}

void signalStop(Buffer* b) {
    std::vector<u32> events;
    {
        std::lock_guard<std::mutex> lk(b->mtx);
        for (const Notify& n : b->notifies) if (n.offset == 0xFFFFFFFFu) events.push_back(n.event);
    }
    for (u32 e : events) NativeDllInterceptor::win_SetEvent(reinterpret_cast<void*>(static_cast<uintptr_t>(e)));
}

void pumpLoop(Buffer* b) {
    DirectAAudioEngine engine;
    bool haveEngine = engine.start(static_cast<int32_t>(b->fmt.rate), 2, 40);
    const u32 periodFrames = std::max<u32>(1, b->fmt.rate / 100);  // 10 ms
    const u32 periodBytes = periodFrames * b->fmt.blockAlign;
    std::vector<int16_t> out(static_cast<size_t>(periodFrames) * 2);
    LOGI("búfer %u bytes, %u Hz %u canales %s%u, AAudio=%d", b->bytes, b->fmt.rate, b->fmt.channels,
         b->fmt.isFloat ? "float" : "pcm", b->fmt.bits, haveEngine ? 1 : 0);
    auto next = std::chrono::steady_clock::now();
    bool pausedStop = false;
    while (!b->quit.load(std::memory_order_acquire)) {
        if (pacing::paused()) {
            if (haveEngine) { engine.stop(); haveEngine = false; pausedStop = true; }
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            next = std::chrono::steady_clock::now();
            continue;
        }
        if (pausedStop) { pausedStop = false; haveEngine = engine.start(static_cast<int32_t>(b->fmt.rate), 2, 40); }
        if (!b->playing.load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            next = std::chrono::steady_clock::now();
            continue;
        }
        const u32 from = b->playPos.load(std::memory_order_acquire);
        const float g = b->gain.load(std::memory_order_relaxed);
        u32 pos = from;
        bool ended = false;
        for (u32 i = 0; i < periodFrames; ++i) {
            if (pos + b->fmt.blockAlign > b->bytes) {
                if (!b->looping.load()) { ended = true; }
                pos = 0;
            }
            if (ended) { out[i * 2] = out[i * 2 + 1] = 0; continue; }
            const uint8_t* frame = b->data + pos;
            const float l = sampleAt(b->fmt, frame, 0) * g;
            const float r = (b->fmt.channels >= 2 ? sampleAt(b->fmt, frame, 1) : sampleAt(b->fmt, frame, 0)) * g;
            out[i * 2] = to16(l);
            out[i * 2 + 1] = to16(r);
            pos += b->fmt.blockAlign;
        }
        (void)periodBytes;
        bool wrote = false;
        if (haveEngine && engine.isRunning()) {
            u32 done = 0;
            for (int tries = 0; tries < 30 && done < periodFrames && !b->quit.load(); ++tries) {
                const int32_t n = engine.writePcm(out.data() + done * 2, static_cast<int32_t>(periodFrames - done));
                if (n > 0) done += static_cast<u32>(n);
            }
            wrote = done > 0;
        }
        if (!wrote) {
            next += std::chrono::microseconds(10000);
            std::this_thread::sleep_until(next);
        }
        b->playPos.store(pos, std::memory_order_release);
        signalPassed(b, from, pos);
        if (ended) {
            b->playing.store(false);
            b->playPos.store(0);
            signalStop(b);
        }
    }
    engine.stop();
}

void startPump(Buffer* b) {
    if (!b->pump.joinable()) b->pump = std::thread(pumpLoop, b);
}

// ----------------------------------------------------------------------------
// IDirectSoundBuffer8 / IDirectSoundNotify
// ----------------------------------------------------------------------------
u32 Buf_QI(u32 t, u32 riid, u32 ppv) {
    Buffer* b = B(t);
    const u32 iid = guidData1(riid);
    u32 out = 0;
    // IUnknown, IDirectSoundBuffer, IDirectSoundBuffer8
    if (iid == 0x00000000u || iid == 0x279AFA85u || iid == 0x6825A449u) out = L(&b->com);
    else if (iid == 0xB0210783u) out = L(&b->notify);  // IDirectSoundNotify
    put32(ppv, out);
    if (out == 0) return kNoInterface;
    b->refs.fetch_add(1);
    return kOk;
}
u32 Buf_AddRef(u32 t) { return B(t)->refs.fetch_add(1) + 1; }
u32 Buf_Release(u32 t) {
    Buffer* b = B(t);
    const u32 prev = b->refs.fetch_sub(1);
    if (prev == 1) {
        b->playing.store(false);
        b->quit.store(true);
        // La memoria se conserva (punteros que el juego aún pueda tener); el hilo termina solo.
        if (b->pump.joinable()) b->pump.detach();
    }
    return prev - 1;
}
u32 Buf_GetCaps(u32 t, u32 caps) {
    Buffer* b = B(t);
    if (caps == 0) return kPointer;
    put32(caps + 4, b->primary ? kDsbcapsPrimary : 0x00010000u /* DSBCAPS_GETCURRENTPOSITION2 */);
    put32(caps + 8, b->bytes);
    put32(caps + 12, 0);
    put32(caps + 16, 0);
    return kOk;
}
u32 Buf_GetCurrentPosition(u32 t, u32 pPlay, u32 pWrite) {
    Buffer* b = B(t);
    const u32 play = b->playPos.load(std::memory_order_acquire);
    put32(pPlay, play);
    if (pWrite != 0) {
        // Cursor de escritura: un periodo (10 ms) por delante, como el hardware real.
        const u32 ahead = std::max<u32>(1, b->fmt.rate / 100) * b->fmt.blockAlign;
        put32(pWrite, b->bytes != 0 ? (play + ahead) % b->bytes : 0);
    }
    return kOk;
}
u32 Buf_GetFormat(u32 t, u32 wfx, u32 size, u32 written) {
    Buffer* b = B(t);
    uint8_t f[18] = {};
    const uint16_t tag = b->fmt.isFloat ? 3 : 1, ch = static_cast<uint16_t>(b->fmt.channels),
                   align = static_cast<uint16_t>(b->fmt.blockAlign), bits = static_cast<uint16_t>(b->fmt.bits);
    const u32 rate = b->fmt.rate, avg = b->fmt.rate * b->fmt.blockAlign;
    std::memcpy(f, &tag, 2); std::memcpy(f + 2, &ch, 2); std::memcpy(f + 4, &rate, 4);
    std::memcpy(f + 8, &avg, 4); std::memcpy(f + 12, &align, 2); std::memcpy(f + 14, &bits, 2);
    if (wfx != 0) std::memcpy(G<void>(wfx), f, std::min<u32>(size, 18));
    put32(written, 18);
    return kOk;
}
u32 Buf_GetVolume(u32 t, u32 p) {
    const float g = B(t)->gain.load();
    const int32_t v = g <= 0.0001f ? -10000 : static_cast<int32_t>(2000.0f * std::log10(g));
    put32(p, static_cast<u32>(v));
    return kOk;
}
u32 Buf_GetPan(u32, u32 p) { put32(p, 0); return kOk; }
u32 Buf_GetFrequency(u32 t, u32 p) { put32(p, B(t)->fmt.rate); return kOk; }
u32 Buf_GetStatus(u32 t, u32 p) {
    Buffer* b = B(t);
    u32 s = 0;
    if (b->playing.load()) s |= 0x1 | (b->looping.load() ? 0x4u : 0u);  // DSBSTATUS_PLAYING | LOOPING
    put32(p, s);
    return kOk;
}
u32 Buf_Initialize(u32, u32, u32) { return 0x88780082u; }  // DSERR_ALREADYINITIALIZED
u32 Buf_Lock(u32 t, u32 offset, u32 bytes, u32 pp1, u32 pb1, u32 pp2, u32 pb2, u32 flags) {
    Buffer* b = B(t);
    if (b->primary || b->data == nullptr || b->bytes == 0) return kInvalidParam;
    if (flags & 0x2) bytes = b->bytes;                                    // DSBLOCK_ENTIREBUFFER
    if (flags & 0x1) offset = (b->playPos.load() + std::max<u32>(1, b->fmt.rate / 100) * b->fmt.blockAlign) % b->bytes;  // FROMWRITECURSOR
    if (offset >= b->bytes || bytes > b->bytes) return kInvalidParam;
    const u32 first = std::min(bytes, b->bytes - offset);
    put32(pp1, L(b->data + offset));
    put32(pb1, first);
    put32(pp2, bytes > first ? L(b->data) : 0);
    put32(pb2, bytes > first ? bytes - first : 0);
    return kOk;
}
u32 Buf_Play(u32 t, u32, u32, u32 flags) {
    Buffer* b = B(t);
    b->looping.store((flags & 0x1) != 0);  // DSBPLAY_LOOPING
    if (b->primary) { b->playing.store(true); return kOk; }
    b->playing.store(true, std::memory_order_release);
    startPump(b);
    return kOk;
}
u32 Buf_SetCurrentPosition(u32 t, u32 pos) {
    Buffer* b = B(t);
    if (b->bytes != 0) b->playPos.store((pos / std::max<u32>(1, b->fmt.blockAlign)) * b->fmt.blockAlign % b->bytes);
    return kOk;
}
u32 Buf_SetFormat(u32 t, u32 wfx) {
    Buffer* b = B(t);
    Format f;
    if (!readFormat(wfx, f)) return kInvalidParam;
    if (b->primary) b->fmt = f;  // el primario solo informa del formato de salida
    return kOk;
}
u32 Buf_SetVolume(u32 t, u32 v) {
    const int32_t db100 = static_cast<int32_t>(v);
    B(t)->gain.store(db100 <= -10000 ? 0.0f : std::pow(10.0f, static_cast<float>(db100) / 2000.0f));
    return kOk;
}
u32 Buf_SetPan(u32, u32) { return kOk; }
u32 Buf_SetFrequency(u32, u32) { return kOk; }
u32 Buf_Stop(u32 t) {
    Buffer* b = B(t);
    const bool was = b->playing.exchange(false);
    if (was && !b->primary) signalStop(b);
    return kOk;
}
u32 Buf_Unlock(u32, u32, u32, u32, u32) { return kOk; }
u32 Buf_Restore(u32) { return kOk; }
u32 Buf_SetFX(u32, u32 count, u32, u32) { return count == 0 ? kOk : kDsUnsupported; }
u32 Buf_AcquireResources(u32, u32, u32, u32) { return kOk; }
u32 Buf_GetObjectInPath(u32, u32, u32, u32, u32 pp) { put32(pp, 0); return 0x88780096u; }  // DSERR_OBJECTNOTFOUND

u32 Not_QI(u32 t, u32 riid, u32 ppv) { return Buf_QI(t, riid, ppv); }
u32 Not_SetNotificationPositions(u32 t, u32 count, u32 arr) {
    Buffer* b = B(t);
    if (b->playing.load()) return 0x88780096u;  // DSERR_INVALIDCALL (simplificado)
    std::lock_guard<std::mutex> lk(b->mtx);
    b->notifies.clear();
    for (u32 i = 0; i < count; ++i) b->notifies.push_back({rd32(arr + 8 * i), rd32(arr + 8 * i + 4)});
    LOGI("SetNotificationPositions: %u posiciones", count);
    return kOk;
}

// ----------------------------------------------------------------------------
// IDirectSound8
// ----------------------------------------------------------------------------
Device* D(u32 self) { return G<Device>(self); }
u32 Dev_QI(u32 t, u32 riid, u32 ppv) {
    const u32 iid = guidData1(riid);
    // IUnknown, IDirectSound, IDirectSound8
    if (iid == 0x00000000u || iid == 0x279AFA83u || iid == 0xC50A7E93u) {
        D(t)->refs.fetch_add(1);
        put32(ppv, t);
        return kOk;
    }
    put32(ppv, 0);
    return kNoInterface;
}
u32 Dev_AddRef(u32 t) { return D(t)->refs.fetch_add(1) + 1; }
u32 Dev_Release(u32 t) { const u32 p = D(t)->refs.fetch_sub(1); return p - 1; }

u32 Dev_CreateSoundBuffer(u32, u32 desc, u32 ppBuf, u32) {
    if (desc == 0 || ppBuf == 0) return kPointer;
    const u32 flags = rd32(desc + 4), bytes = rd32(desc + 8), wfx = rd32(desc + 16);
    auto* mem = lowAlloc(sizeof(Buffer));
    if (mem == nullptr) return kOutOfMemory;
    auto* b = new (mem) Buffer();
    b->com = {g_vtBuffer, 2, b};
    b->notify = {g_vtNotify, 3, b};
    b->primary = (flags & kDsbcapsPrimary) != 0;
    if (!b->primary) {
        if (!readFormat(wfx, b->fmt) || bytes < 4 || bytes > 0x4000000u) {
            LOGW("CreateSoundBuffer: formato o tamaño no válido (%u bytes)", bytes);
            put32(ppBuf, 0);
            return kInvalidParam;
        }
        b->bytes = bytes - bytes % b->fmt.blockAlign;
        b->data = static_cast<uint8_t*>(lowAlloc(b->bytes));
        if (b->data == nullptr) { put32(ppBuf, 0); return kOutOfMemory; }
        std::memset(b->data, b->fmt.bits == 8 ? 0x80 : 0, b->bytes);
    }
    {
        std::lock_guard<std::mutex> lk(g_buffersMutex);
        g_buffers.insert(b);
    }
    LOGI("CreateSoundBuffer(flags=0x%x, %u bytes, %s) -> 0x%x", flags, b->bytes, b->primary ? "primario" : "secundario",
         L(&b->com));
    put32(ppBuf, L(&b->com));
    return kOk;
}
u32 Dev_GetCaps(u32, u32 caps) {
    if (caps == 0) return kPointer;
    // DSCAPS mide 96 bytes; no se confía en dwSize (puede venir sin inicializar).
    std::memset(G<void>(caps + 4), 0, 92);
    put32(caps + 4, 0xF1Fu);  // PRIMARY/SECONDARY MONO|STEREO|8BIT|16BIT, CONTINUOUSRATE
    put32(caps + 8, 100);     // dwMinSecondarySampleRate
    put32(caps + 12, 200000); // dwMaxSecondarySampleRate
    put32(caps + 16, 1);      // dwPrimaryBuffers
    return kOk;
}
u32 Dev_DuplicateSoundBuffer(u32, u32, u32 pp) { put32(pp, 0); return kDsUnsupported; }
u32 Dev_SetCooperativeLevel(u32, u32, u32) { return kOk; }
u32 Dev_Compact(u32) { return kOk; }
u32 Dev_GetSpeakerConfig(u32, u32 p) { put32(p, 0x00140004u); return kOk; }  // DSSPEAKER_STEREO, geometría ancha
u32 Dev_SetSpeakerConfig(u32, u32) { return kOk; }
u32 Dev_Initialize(u32, u32) { return kOk; }
u32 Dev_VerifyCertification(u32, u32 p) { put32(p, 0); return kOk; }

u32 makeVtable(const std::vector<void*>& m, const char* name) {
    auto* vt = static_cast<u32*>(lowAlloc(4 * m.size()));
    if (vt == nullptr) return 0;
    for (size_t i = 0; i < m.size(); ++i)
        vt[i] = static_cast<u32>(w32alias::lowFor(m[i], std::string("dsound.dll!") + name + "#" + std::to_string(i)));
    return L(vt);
}

void ensureVtables() {
    static std::once_flag once;
    std::call_once(once, [] {
        g_vtDevice = makeVtable({
            w32arity::hpFn(&Dev_QI), w32arity::hpFn(&Dev_AddRef), w32arity::hpFn(&Dev_Release),
            w32arity::hpFn(&Dev_CreateSoundBuffer), w32arity::hpFn(&Dev_GetCaps), w32arity::hpFn(&Dev_DuplicateSoundBuffer),
            w32arity::hpFn(&Dev_SetCooperativeLevel), w32arity::hpFn(&Dev_Compact), w32arity::hpFn(&Dev_GetSpeakerConfig),
            w32arity::hpFn(&Dev_SetSpeakerConfig), w32arity::hpFn(&Dev_Initialize), w32arity::hpFn(&Dev_VerifyCertification),
        }, "IDirectSound8");
        g_vtBuffer = makeVtable({
            w32arity::hpFn(&Buf_QI), w32arity::hpFn(&Buf_AddRef), w32arity::hpFn(&Buf_Release),
            w32arity::hpFn(&Buf_GetCaps), w32arity::hpFn(&Buf_GetCurrentPosition), w32arity::hpFn(&Buf_GetFormat),
            w32arity::hpFn(&Buf_GetVolume), w32arity::hpFn(&Buf_GetPan), w32arity::hpFn(&Buf_GetFrequency),
            w32arity::hpFn(&Buf_GetStatus), w32arity::hpFn(&Buf_Initialize), w32arity::hpFn(&Buf_Lock),
            w32arity::hpFn(&Buf_Play), w32arity::hpFn(&Buf_SetCurrentPosition), w32arity::hpFn(&Buf_SetFormat),
            w32arity::hpFn(&Buf_SetVolume), w32arity::hpFn(&Buf_SetPan), w32arity::hpFn(&Buf_SetFrequency),
            w32arity::hpFn(&Buf_Stop), w32arity::hpFn(&Buf_Unlock), w32arity::hpFn(&Buf_Restore),
            w32arity::hpFn(&Buf_SetFX), w32arity::hpFn(&Buf_AcquireResources), w32arity::hpFn(&Buf_GetObjectInPath),
        }, "IDirectSoundBuffer8");
        g_vtNotify = makeVtable({
            w32arity::hpFn(&Not_QI), w32arity::hpFn(&Buf_AddRef), w32arity::hpFn(&Buf_Release),
            w32arity::hpFn(&Not_SetNotificationPositions),
        }, "IDirectSoundNotify");
    });
}

// DirectSoundCreate8(LPCGUID, LPDIRECTSOUND8*, LPUNKNOWN) (stdcall)
u32 DirectSoundCreate8(u32, u32 pp, u32) {
    const u32 d = create();
    put32(pp, d);
    return d != 0 ? kOk : kOutOfMemory;
}

}  // namespace

uint32_t create() {
    ensureVtables();
    auto* mem = lowAlloc(sizeof(Device));
    if (mem == nullptr) return 0;
    auto* d = new (mem) Device();
    d->com = {g_vtDevice, 1, nullptr};
    LOGI("IDirectSound8 de 32 bits -> 0x%x (AAudio)", L(d));
    return L(d);
}

void* resolve(const std::string& name) {
    if (name == "DirectSoundCreate8" || name == "DirectSoundCreate") return w32arity::hpFn(&DirectSoundCreate8);
    return nullptr;
}

void stopAll() {
    std::lock_guard<std::mutex> lk(g_buffersMutex);
    for (Buffer* b : g_buffers) {
        b->playing.store(false);
        b->quit.store(true);
    }
}

}  // namespace dsound32
}  // namespace hyperplay
