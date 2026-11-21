#include "FmodBank.h"

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cstdio>
#include <functional>

namespace hyperplay::fmodlite {
namespace {

template <typename T>
T rd(const uint8_t* p) {
    T v;
    std::memcpy(&v, p, sizeof(T));
    return v;
}

bool tag(const uint8_t* p, const char* t) { return std::memcmp(p, t, 4) == 0; }

// Recorre los chunks RIFF: fn(parentListType, chunkId, body, size). Las LIST no se notifican
// (se entra en ellas); `parent` es el tipo de la LIST que contiene el chunk.
void walk(const uint8_t* b, size_t off, size_t end, const char* parent,
          const std::function<void(const char*, const uint8_t*, const uint8_t*, size_t)>& fn) {
    while (off + 8 <= end) {
        const uint8_t* c = b + off;
        const uint32_t size = rd<uint32_t>(c + 4);
        const size_t body = off + 8;
        if (body + size > end) return;
        if (tag(c, "RIFF") || tag(c, "LIST")) {
            char type[5] = {};
            std::memcpy(type, b + body, 4);
            walk(b, body + 4, body + size, type, fn);
        } else {
            fn(parent, c, b + body, size);
        }
        off = body + size + (size & 1u);
    }
}

}  // namespace

std::string guidToString(const Guid& g) {
    char s[40];
    std::snprintf(s, sizeof(s), "%08x-%04x-%04x-%02x%02x-%02x%02x%02x%02x%02x%02x", rd<uint32_t>(g.b),
                  rd<uint16_t>(g.b + 4), rd<uint16_t>(g.b + 6), g.b[8], g.b[9], g.b[10], g.b[11], g.b[12], g.b[13],
                  g.b[14], g.b[15]);
    return s;
}

bool parseGuid(const char* text, Guid& out) {
    if (text == nullptr) return false;
    unsigned d1 = 0, d2 = 0, d3 = 0, b[8] = {};
    const char* t = text[0] == '{' ? text + 1 : text;
    if (std::sscanf(t, "%8x-%4x-%4x-%2x%2x-%2x%2x%2x%2x%2x%2x", &d1, &d2, &d3, &b[0], &b[1], &b[2], &b[3], &b[4], &b[5],
                    &b[6], &b[7]) != 11) {
        return false;
    }
    const uint32_t v1 = d1;
    const uint16_t v2 = static_cast<uint16_t>(d2), v3 = static_cast<uint16_t>(d3);
    std::memcpy(out.b, &v1, 4);
    std::memcpy(out.b + 4, &v2, 2);
    std::memcpy(out.b + 6, &v3, 2);
    for (int i = 0; i < 8; ++i) out.b[8 + i] = static_cast<uint8_t>(b[i]);
    return true;
}

Bank::~Bank() {
    if (mapped_ && data_ != nullptr) munmap(data_, size_);
}

bool Bank::load(const std::string& hostPath, int index, std::string* error) {
    (void)index;
    path_ = hostPath;
    const int fd = ::open(hostPath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        if (error) *error = "no se pudo abrir el banco";
        return false;
    }
    struct stat st{};
    if (fstat(fd, &st) != 0 || st.st_size < 64) {
        ::close(fd);
        if (error) *error = "banco vacío";
        return false;
    }
    size_ = static_cast<size_t>(st.st_size);
    void* m = mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd, 0);
    if (m != MAP_FAILED) {
        data_ = static_cast<uint8_t*>(m);
        mapped_ = true;
    } else {
        owned_.resize(size_);
        size_t got = 0;
        while (got < size_) {
            const ssize_t n = ::pread(fd, owned_.data() + got, size_ - got, static_cast<off_t>(got));
            if (n <= 0) break;
            got += static_cast<size_t>(n);
        }
        if (got != size_) {
            ::close(fd);
            if (error) *error = "lectura incompleta del banco";
            return false;
        }
        data_ = owned_.data();
    }
    ::close(fd);
    if (!tag(data_, "RIFF") || !tag(data_ + 8, "FEV ")) {
        if (error) *error = "no es un banco FEV";
        return false;
    }
    // FSB5 dentro del chunk SND (puede llevar relleno antes de la firma).
    walk(data_, 12, size_, "FEV ", [&](const char*, const uint8_t* c, const uint8_t* body, size_t size) {
        if (tag(c, "SND ")) {
            for (size_t i = 0; i + 4 <= size && i < 64; ++i) {
                if (tag(body + i, "FSB5")) {
                    parseFsb5(body + i, size - i, static_cast<size_t>((body + i) - data_), fsb_);
                    break;
                }
            }
        } else if (tag(c, "BNKI") && size >= 16) {
            id_ = Guid::at(body);
        }
    });
    return true;
}

float Model::evalCurve(const Guid& ctrl, float x) const {
    auto it = curves.find(ctrl);
    if (it == curves.end() || it->second.empty()) return 0.f;
    const auto& p = it->second;
    if (x <= p.front().x) return p.front().y;
    if (x >= p.back().x) return p.back().y;
    for (size_t i = 1; i < p.size(); ++i) {
        if (x <= p[i].x) {
            const float span = p[i].x - p[i - 1].x;
            const float t = span > 0 ? (x - p[i - 1].x) / span : 1.f;
            return p[i - 1].y + (p[i].y - p[i - 1].y) * t;  // tramo lineal (la forma se ignora)
        }
    }
    return p.back().y;
}

void Model::readStrings(const uint8_t* d, size_t size) {
    if (size < 8) return;
    // u16 en +4 = palabras de 4 bytes del área de nodos (nodos de 8 bytes: n/2).
    const uint16_t words = rd<uint16_t>(d + 4);
    const uint32_t n = words / 2u;
    const size_t gstart = 8 + static_cast<size_t>(words) * 4;
    if (gstart > size) return;
    auto node = [&](uint32_t i, uint32_t& w1, uint32_t& w2) {
        w1 = rd<uint32_t>(d + 8 + i * 8);
        w2 = rd<uint32_t>(d + 8 + i * 8 + 4);
    };
    // 1ª pasada: cuántas hojas (= GUIDs) hay, para saber dónde empiezan las etiquetas.
    uint32_t maxLeaf = 0;
    bool anyLeaf = false;
    std::function<void(uint32_t, int)> count = [&](uint32_t idx, int depth) {
        if (idx >= n || depth > 512) return;
        uint32_t w1, w2;
        node(idx, w1, w2);
        const uint32_t ch = w1 >> 24, first = w2 & 0xFFFFFF, cnt = w2 >> 24;
        if (idx != 0 && (ch == 0 || cnt == 0)) {
            maxLeaf = std::max(maxLeaf, first);
            anyLeaf = true;
            return;
        }
        for (uint32_t k = 0; k < cnt; ++k) count(first + k, depth + 1);
    };
    count(0, 0);
    if (!anyLeaf) return;
    // Tras los GUID: u16 (tamaño del bloque) y las etiquetas.
    const size_t labels = gstart + static_cast<size_t>(maxLeaf + 1) * 16 + 2;
    if (labels > size) return;
    auto label = [&](uint32_t val) -> std::string {
        const size_t o = labels + val;
        if (val == 0xFFFFFF || o >= size) return {};
        const void* z = std::memchr(d + o, 0, size - o);
        const size_t len = z ? static_cast<size_t>(static_cast<const uint8_t*>(z) - (d + o)) : size - o;
        return std::string(reinterpret_cast<const char*>(d + o), len);
    };
    std::function<void(uint32_t, const std::string&, int)> rec = [&](uint32_t idx, const std::string& s, int depth) {
        if (idx >= n || depth > 512) return;
        uint32_t w1, w2;
        node(idx, w1, w2);
        const uint32_t val = w1 & 0xFFFFFF, ch = w1 >> 24, first = w2 & 0xFFFFFF, cnt = w2 >> 24;
        auto leaf = [&](const std::string& path) {
            if (first > maxLeaf) return;
            const Guid g = Guid::at(d + gstart + static_cast<size_t>(first) * 16);
            idByPath[path] = g;
            pathById[g] = path;
        };
        if (idx != 0 && ch == 0) {
            leaf(s);
            return;
        }
        const std::string s2 = idx != 0 ? s + label(val) : s;
        if (idx != 0 && cnt == 0) {
            leaf(s2);
            return;
        }
        for (uint32_t k = 0; k < cnt; ++k) rec(first + k, s2, depth + 1);
    };
    rec(0, std::string(), 0);
}

bool Model::ingest(Bank& bank, int bankIndex) {
    const uint8_t* b = bank.data();
    const size_t size = bank.size();
    std::vector<std::pair<Guid, std::pair<const uint8_t*, size_t>>> rawTimelines, rawMulti, rawScatter, rawTransitions;
    Guid lastMulti, lastScatter, lastTimeline, lastInst;
    // Dueño de los PRPS siguientes (instrumento) y parámetro/evento del último PMLB.
    Guid propOwner, pmlParam, pmlEvent, lastBus;
    bool haveOwner = false, havePml = false, haveBus = false;
    std::vector<std::pair<Guid, SheetItem>> sheetItems;  // (evento, instrumento sobre parámetro)
    static Guid kVolumeProps[3];
    static bool kPropsInit = false;
    if (!kPropsInit) {
        // Propiedades de volumen (dB) con automatización: instrumento, pista/evento y otra.
        parseGuid("7400f0ce-d180-44f9-94a2-dbe1fe7dbaf4", kVolumeProps[0]);
        parseGuid("d65ba128-a6f5-41d9-b6a1-a7c402f329ac", kVolumeProps[1]);
        parseGuid("c021a555-2747-4628-a01b-b732941315e2", kVolumeProps[2]);
        kPropsInit = true;
    }
    // Lista FMOD: u16 (n << 1 | 1), u16 tamaño del elemento, elementos.
    auto list = [](const uint8_t* p, size_t avail, size_t& count, size_t& elem) -> bool {
        if (avail < 4) return false;
        count = rd<uint16_t>(p) >> 1;
        elem = rd<uint16_t>(p + 2);
        return elem > 0 && 4 + count * elem <= avail;
    };
    bool haveTimeline = false;
    std::unordered_map<Guid, std::vector<std::pair<const uint8_t*, size_t>>, GuidHash> transitions;

    walk(b, 12, size, "FEV ", [&](const char* parent, const uint8_t* c, const uint8_t* body, size_t sz) {
        const bool instHeader = tag(c, "WAIB") || tag(c, "MUIB") || tag(c, "SPIB") || tag(c, "EVIB");
        if (instHeader && sz >= 16) {
            propOwner = Guid::at(body);
            haveOwner = true;
        } else if (c[3] == 'B') {
            haveOwner = false;  // cabecera de otro objeto: sus PRPS no son de un instrumento
        }
        if (tag(c, "PROP") && haveOwner && sz >= 28 && std::strcmp(parent, "PRPS") == 0) {
            const Guid prop = Guid::at(body + 8);
            bool volume = false;
            for (const Guid& v : kVolumeProps) volume = volume || v == prop;
            size_t n = 0, elem = 0;
            if (volume && list(body + 24, sz - 24, n, elem) && elem >= 16) {
                auto& ctrls = instruments[propOwner].volumeCtrls;
                for (size_t k = 0; k < n; ++k) ctrls.push_back(Guid::at(body + 28 + k * elem));
            }
            return;
        }
        if ((tag(c, "GBSB") || tag(c, "MBSB") || tag(c, "IBSB") || tag(c, "RBSB")) && sz >= 34) {
            lastBus = Guid::at(body);
            buses[lastBus].parent = Guid::at(body + 18);
            haveBus = true;
            return;
        }
        if (tag(c, "BUS ") && haveBus && sz >= 9) {
            // Dos listas (vacía = 2 bytes), u16 y el fader en dB.
            size_t p = 5;
            for (int k = 0; k < 2 && p + 2 <= sz; ++k) {
                const size_t n = rd<uint16_t>(body + p) >> 1;
                p += n == 0 ? 2 : 4 + n * (p + 4 <= sz ? rd<uint16_t>(body + p + 2) : 0);
            }
            p += 2;
            if (p + 4 <= sz) {
                const float db = rd<float>(body + p);
                if (db >= -100.f && db <= 30.f) buses[lastBus].faderDb = db;
            }
            haveBus = false;
            return;
        }
        if (tag(c, "SNAB") && sz >= 24) {
            auto& entries = snapshots[Guid::at(body)];
            entries.clear();
            size_t n = 0, elem = 0;
            if (list(body + 20, sz - 20, n, elem) && elem >= 28) {
                for (size_t k = 0; k < n; ++k) {
                    const uint8_t* e = body + 24 + k * elem;
                    SnapEntry s;
                    s.bus = Guid::at(e + 4);
                    s.type = rd<uint32_t>(e + 20);
                    s.value = rd<float>(e + 24);
                    entries.push_back(s);
                }
            }
            return;
        }
        if (tag(c, "PRMB") && sz >= 23) {
            const size_t n = static_cast<size_t>(body[21]) | (static_cast<size_t>(body[22]) << 8);
            if (23 + n + 12 <= sz) {
                ParamDef p;
                p.name.assign(reinterpret_cast<const char*>(body + 23), n);
                while (!p.name.empty() && p.name.back() == '\0') p.name.pop_back();
                p.minimum = rd<float>(body + 23 + n);
                p.maximum = rd<float>(body + 27 + n);
                p.defaultValue = rd<float>(body + 31 + n);
                params[Guid::at(body)] = p;
            }
            return;
        }
        if (tag(c, "CURV") && sz >= 36) {
            size_t n = 0, elem = 0;
            if (list(body + 32, sz - 32, n, elem) && elem >= 8) {
                auto& pts = curves[Guid::at(body)];
                pts.clear();
                for (size_t k = 0; k < n; ++k) {
                    CurvePoint pt;
                    pt.x = rd<float>(body + 36 + k * elem);
                    pt.y = rd<float>(body + 40 + k * elem);
                    pts.push_back(pt);
                }
            }
            return;
        }
        if (tag(c, "PMLB") && sz >= 50) {
            pmlParam = Guid::at(body + 16);
            pmlEvent = Guid::at(body + 32);
            havePml = true;
            size_t n = 0, elem = 0;
            if (list(body + 48, sz - 48, n, elem) && elem >= 24) {
                for (size_t k = 0; k < n; ++k) {
                    const uint8_t* e = body + 52 + k * elem;
                    SheetItem it;
                    it.param = pmlParam;
                    it.instrument = Guid::at(e);
                    it.start = rd<float>(e + 16);
                    it.length = rd<float>(e + 20);
                    sheetItems.push_back({pmlEvent, it});
                }
            }
            return;
        }
        if (tag(c, "CTRO") && havePml && std::strcmp(parent, "PMLO") == 0) {
            size_t n = 0, elem = 0;
            if (list(body, sz, n, elem) && elem >= 16) {
                for (size_t k = 0; k < n; ++k) ctrlParam[Guid::at(body + 4 + k * elem)] = pmlParam;
            }
            return;
        }
        if (tag(c, "STDT")) {
            readStrings(body, sz);
        } else if (tag(c, "EVTB") && sz >= 48) {
            EventDef e;
            e.id = Guid::at(body);
            e.timeline = Guid::at(body + 32);
            e.snapshotRef = Guid::at(body + 16);
            if (sz >= 64) e.inputBus = Guid::at(body + 48);
            events[e.id] = e;
            bank.events.push_back(e.id);
        } else if (tag(c, "TLNB") && sz >= 32) {
            lastTimeline = Guid::at(body);
            haveTimeline = true;
            rawTimelines.push_back({lastTimeline, {body, sz}});
        } else if (tag(c, "TRNB") && haveTimeline && std::strcmp(parent, "TRAN") == 0) {
            transitions[lastTimeline].push_back({body, sz});
        } else if (tag(c, "WAIB") && sz >= 32) {
            InstrumentDef d;
            d.kind = InstKind::Wave;
            d.target = Guid::at(body + 16);
            lastInst = Guid::at(body);
            instruments[lastInst] = d;
        } else if (tag(c, "EVIB") && sz >= 32) {
            InstrumentDef d;
            d.kind = InstKind::Event;
            d.target = Guid::at(body + 16);
            lastInst = Guid::at(body);
            instruments[lastInst] = d;
        } else if (tag(c, "MUIB") && sz >= 16) {
            lastMulti = Guid::at(body);
            lastInst = lastMulti;
            instruments[lastMulti].kind = InstKind::Multi;
        } else if (tag(c, "SPIB") && sz >= 16) {
            lastScatter = Guid::at(body);
            lastInst = lastScatter;
            instruments[lastScatter].kind = InstKind::Scatter;
        } else if (tag(c, "INST") && sz >= 28 && instruments.count(lastInst) != 0) {
            // Bloque común del instrumento: +0 línea de tiempo dueña, +24 número de bucles.
            instruments[lastInst].loopCount = rd<int32_t>(body + 24);
        } else if (tag(c, "PLST")) {
            if (std::strcmp(parent, "MUIT") == 0) rawMulti.push_back({lastMulti, {body, sz}});
            else if (std::strcmp(parent, "SPIT") == 0) rawScatter.push_back({lastScatter, {body, sz}});
        } else if (tag(c, "WAV ") && sz >= 24) {
            WaveDef w;
            w.bank = bankIndex;
            w.subsound = rd<uint16_t>(body + 22);
            waves[Guid::at(body)] = w;
        }
    });

    for (auto& [id, e] : events) {
        if (!e.snapshotRef.isZero() && snapshots.count(e.snapshotRef) != 0) e.snapshot = true;
    }
    for (auto& [ev, it] : sheetItems) {
        auto e = events.find(ev);
        if (e != events.end() && instruments.count(it.instrument) != 0) e->second.sheet.push_back(it);
    }

    // Referencias por GUID: se buscan las de instrumentos conocidos dentro de cada registro.
    auto findRefs = [&](const uint8_t* p, size_t sz, size_t from, const std::function<void(size_t, const Guid&)>& fn) {
        for (size_t o = from; o + 16 <= sz; ++o) {
            const Guid g = Guid::at(p + o);
            if (instruments.count(g) != 0) {
                fn(o, g);
                o += 15;
            }
        }
    };
    for (auto& [id, raw] : rawTimelines) {
        TimelineDef& t = timelines[id];
        findRefs(raw.first, raw.second, 32, [&](size_t o, const Guid& g) {
            if (o + 24 > raw.second) return;
            TimelineItem it;
            it.instrument = g;
            it.start = rd<uint32_t>(raw.first + o + 16);
            it.length = rd<uint32_t>(raw.first + o + 20);
            t.items.push_back(it);
            t.length = std::max(t.length, it.start + it.length);
        });
        // Saltos: TRNB = id, región destino (+16), inicio (+32), fin (+36). Los de 60 bytes con
        // inicio == fin son el final de una región de bucle: vuelven al inicio de la región, cuya
        // posición está en la TLNB tras su GUID (u32). Sin encontrarla, al principio.
        // Los de 86 bytes llevan en +40 una lista de condiciones {parámetro, mín, máx}: con
        // inicio == fin saltan al llegar solo si se cumple; con inicio < fin son una región de
        // transición: mientras el playhead esté dentro, saltan en cuanto se cumple.
        for (auto& [p, sz] : transitions[id]) {
            if (sz != 60 && sz != 86) continue;
            const uint32_t s = rd<uint32_t>(p + 32), e = rd<uint32_t>(p + 36);
            TimelineJump j;
            j.at = s;
            if (sz == 86) {
                size_t n = 0, elem = 0;
                if (!list(p + 40, sz - 40, n, elem) || n == 0 || elem < 24) continue;
                j.hasCond = true;
                j.condParam = Guid::at(p + 44);
                j.condMin = rd<float>(p + 60);
                j.condMax = rd<float>(p + 64);
                if (e > s) j.regionEnd = e;
            } else if (s != e || s == 0) {
                continue;
            }
            const Guid region = Guid::at(p + 16);
            bool found = false;
            for (size_t o = 32; o + 20 <= raw.second; ++o) {
                if (std::memcmp(raw.first + o, region.b, 16) == 0) {
                    j.to = rd<uint32_t>(raw.first + o + 16);
                    found = true;
                    break;
                }
            }
            if (!found && j.hasCond) continue;  // destino fuera de esta línea de tiempo
            if (j.regionEnd > j.at && j.to >= j.at && j.to < j.regionEnd) continue;  // saltaría en bucle
            if (j.hasCond ? j.to != j.at : j.to < j.at) t.jumps.push_back(j);
        }
    }
    for (auto& [id, raw] : rawMulti) {
        auto& kids = instruments[id].children;
        findRefs(raw.first, raw.second, 0, [&](size_t, const Guid& g) { kids.push_back(g); });
    }
    for (auto& [id, raw] : rawScatter) {
        auto& kids = instruments[id].children;
        findRefs(raw.first, raw.second, 0, [&](size_t, const Guid& g) { kids.push_back(g); });
    }
    for (auto& [id, e] : events) {
        if (e.path.empty()) {
            auto it = pathById.find(id);
            if (it != pathById.end()) e.path = it->second;
        }
    }
    return true;
}

}  // namespace hyperplay::fmodlite
