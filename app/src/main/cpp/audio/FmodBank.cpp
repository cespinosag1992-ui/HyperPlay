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
    bool haveTimeline = false;
    std::unordered_map<Guid, std::vector<std::pair<const uint8_t*, size_t>>, GuidHash> transitions;

    walk(b, 12, size, "FEV ", [&](const char* parent, const uint8_t* c, const uint8_t* body, size_t sz) {
        if (tag(c, "STDT")) {
            readStrings(body, sz);
        } else if (tag(c, "EVTB") && sz >= 48) {
            EventDef e;
            e.id = Guid::at(body);
            e.timeline = Guid::at(body + 32);
            events[e.id] = e;
            bank.events.push_back(e.id);
        } else if (tag(c, "SNAB") && sz >= 16) {
            EventDef e;
            e.id = Guid::at(body);
            e.snapshot = true;
            events.emplace(e.id, e);
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
        // Regiones de bucle: TRNB = id, región, inicio, fin, ... (inicio < fin).
        for (auto& [p, sz] : transitions[id]) {
            if (sz < 40) continue;
            LoopRegion r;
            r.start = rd<uint32_t>(p + 32);
            r.end = rd<uint32_t>(p + 36);
            if (r.end > r.start) t.loops.push_back(r);
            else if (r.end == r.start && r.start > 0) t.sustains.push_back(r.start);
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
