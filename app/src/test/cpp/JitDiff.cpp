// Prueba diferencial del JIT: ejecuta con OryonEmitterBackend los mismos bytes x86 que
// tools/jitdiff_gen.py ejecutó en una CPU x86-64 real y compara el estado final (XMM0-3, RAX, RDX,
// R8, RCX y los flags CF/PF/ZF/SF/OF definidos) bit a bit. Agrupa los fallos por instrucción.
// El arnés (prólogo/epílogo) viene en el propio archivo de vectores.
//
// Uso (en el dispositivo): jit_diff jitdiff.bin [filtro]
#include "cpu/OryonEmitterBackend.h"

#include <sys/mman.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "loader/NativeDllInterceptor.h"

using hyperplay::EmitterConfig;
using hyperplay::OryonEmitterBackend;

namespace hyperplay {
void NativeDllInterceptor::setGuestTeb(void*, bool) noexcept {}
bool NativeDllInterceptor::isRegisteredTrampoline(uintptr_t) noexcept { return false; }
int32_t NativeDllInterceptor::dispatchGuestException(OryonEmitterBackend&, uint64_t) { return 0; }
bool NativeDllInterceptor::prepareGuestException32(CpuContext&, uint32_t, uint32_t, uint32_t) { return false; }
}  // namespace hyperplay

namespace {

constexpr size_t kState = 128;
constexpr int kF32 = 0, kF64 = 1;
constexpr uint8_t kApprox = 0x10;

struct Record {
    std::string name;
    std::vector<uint8_t> code;
    uint8_t kind = 0, flagMask = 0;
    uint8_t in[kState]{}, out[kState]{};
};

bool isNan32(uint32_t v) { return (v & 0x7F800000u) == 0x7F800000u && (v & 0x7FFFFFu) != 0; }
bool isNan64(uint64_t v) { return (v & 0x7FF0000000000000ull) == 0x7FF0000000000000ull && (v & 0xFFFFFFFFFFFFFull) != 0; }

// 0 = igual, 1 = solo carga útil de NaN, 2 = aproximación dentro de tolerancia, 3 = fallo real.
int compareXmm(const uint8_t* want, const uint8_t* got, int kind, bool approx) {
    int worst = 0;
    if (kind == kF64) {
        for (int i = 0; i < 8; ++i) {
            uint64_t a, b;
            std::memcpy(&a, want + i * 8, 8);
            std::memcpy(&b, got + i * 8, 8);
            if (a == b) continue;
            worst = std::max(worst, (isNan64(a) && isNan64(b)) ? 1 : 3);
        }
        return worst;
    }
    for (int i = 0; i < 16; ++i) {
        uint32_t a, b;
        std::memcpy(&a, want + i * 4, 4);
        std::memcpy(&b, got + i * 4, 4);
        if (a == b) continue;
        if (kind == kF32 && isNan32(a) && isNan32(b)) { worst = std::max(worst, 1); continue; }
        if (approx) {
            float fa, fb;
            std::memcpy(&fa, &a, 4);
            std::memcpy(&fb, &b, 4);
            if (std::isfinite(fa) && std::isfinite(fb) && std::fabs(fa - fb) <= std::fabs(fa) * (1.0f / 2048.0f)) {
                worst = std::max(worst, 2);
                continue;
            }
        }
        worst = 3;
    }
    return worst;
}

void hex(const char* label, const uint8_t* p, size_t n) {
    std::printf("    %s:", label);
    for (size_t i = 0; i < n; ++i) std::printf("%s%02x", (i % 4 == 0) ? " " : "", p[i]);
    std::printf("\n");
}

bool readBytes(FILE* f, void* p, size_t n) { return std::fread(p, 1, n, f) == n; }

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("uso: %s vectores.bin [filtro]\n", argv[0]);
        return 2;
    }
    const char* filter = argc > 2 ? argv[2] : nullptr;
    FILE* f = std::fopen(argv[1], "rb");
    if (f == nullptr) return 2;
    char magic[4];
    uint8_t pl = 0, el = 0;
    uint32_t count = 0;
    std::vector<uint8_t> prologue, epilogue;
    if (!readBytes(f, magic, 4) || std::memcmp(magic, "JDF2", 4) != 0 || !readBytes(f, &pl, 1)) return 2;
    prologue.resize(pl);
    if (!readBytes(f, prologue.data(), pl) || !readBytes(f, &el, 1)) return 2;
    epilogue.resize(el);
    if (!readBytes(f, epilogue.data(), el) || !readBytes(f, &count, 4)) return 2;
    std::vector<Record> recs;
    recs.reserve(count);
    for (uint32_t i = 0; i < count; ++i) {
        uint8_t nl = 0, cl = 0;
        if (!readBytes(f, &nl, 1) || !readBytes(f, &cl, 1)) return 2;
        Record r;
        r.name.resize(nl);
        r.code.resize(cl);
        if (!readBytes(f, r.name.data(), nl) || !readBytes(f, r.code.data(), cl) || !readBytes(f, &r.kind, 1) ||
            !readBytes(f, &r.flagMask, 1) || !readBytes(f, r.in, kState) || !readBytes(f, r.out, kState)) {
            return 2;
        }
        recs.push_back(std::move(r));
    }
    std::fclose(f);

    OryonEmitterBackend b;
    EmitterConfig c;
    c.is32Bit = false;
    c.stackSize = 1u << 20;
    c.codeCacheSize = 64u << 20;
    c.persistBlocks = false;
    if (!b.configure(c)) return 2;

    struct Stat {
        int total = 0, ok = 0, nanOnly = 0, approx = 0, fail = 0, flagFail = 0, gprFail = 0, untranslated = 0;
        bool shown = false;
    };
    std::map<std::string, Stat> stats;
    std::map<std::string, uint8_t*> codeFor;
    alignas(64) static uint8_t state[kState + 64];

    for (const Record& r : recs) {
        if (filter != nullptr && r.name.find(filter) == std::string::npos) continue;
        uint8_t*& entry = codeFor[r.name];
        if (entry == nullptr) {
            void* p = mmap(nullptr, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) return 2;
            auto* m = static_cast<uint8_t*>(p);
            size_t n = 0;
            std::memcpy(m + n, prologue.data(), prologue.size()); n += prologue.size();
            std::memcpy(m + n, r.code.data(), r.code.size()); n += r.code.size();
            std::memcpy(m + n, epilogue.data(), epilogue.size());
            b.addExecutableRange(reinterpret_cast<uintptr_t>(m), 4096);
            entry = m;
        }
        std::memcpy(state, r.in, kState);
        auto* stack = static_cast<uint8_t*>(b.virtualStack());
        std::memset(stack + c.stackSize - 256, 0, 256);
        b.context().gpr[4] = reinterpret_cast<uintptr_t>(stack + c.stackSize - 64);
        b.context().gpr[1] = reinterpret_cast<uintptr_t>(state);
        const int64_t rc = b.executeEntry(entry);
        Stat& s = stats[r.name];
        ++s.total;
        if (rc == -2) ++s.untranslated;
        const int x = compareXmm(r.out, state, r.kind & 0x0F, (r.kind & kApprox) != 0);
        bool flagsBad = false;
        for (int i = 0; i < 5; ++i) {
            if ((r.flagMask >> i) & 1u) flagsBad |= r.out[80 + i] != state[80 + i];
        }
        const bool gprBad = std::memcmp(r.out + 64, state + 64, 16) != 0 || std::memcmp(r.out + 88, state + 88, 16) != 0;
        if (x == 3 || flagsBad || gprBad) {
            if (x == 3) ++s.fail;
            if (flagsBad) ++s.flagFail;
            if (gprBad) ++s.gprFail;
            if (!s.shown) {
                s.shown = true;
                std::printf("FALLO %s%s%s\n", r.name.c_str(), rc == -2 ? " (no traducido: " : "",
                            rc == -2 ? (b.lastError() + ")").c_str() : "");
                hex("entrada rax/rdx", r.in + 64, 16);
                hex("entrada r8/rcx ", r.in + 88, 16);
                std::printf("    CF entrada=%u\n", r.in[104]);
                for (int k = 0; k < 4; ++k) {
                    if (std::memcmp(r.out + k * 16, state + k * 16, 16) == 0) continue;
                    char l1[24], l2[24], l3[24];
                    std::snprintf(l1, sizeof(l1), "entrada xmm%d", k);
                    std::snprintf(l2, sizeof(l2), "x86     xmm%d", k);
                    std::snprintf(l3, sizeof(l3), "JIT     xmm%d", k);
                    hex(l1, r.in + k * 16, 16);
                    hex(l2, r.out + k * 16, 16);
                    hex(l3, state + k * 16, 16);
                }
                hex("x86 rax/rdx    ", r.out + 64, 16);
                hex("JIT rax/rdx    ", state + 64, 16);
                hex("x86 r8/rcx     ", r.out + 88, 16);
                hex("JIT r8/rcx     ", state + 88, 16);
                std::printf("    rflags interno del JIT=0x%llx\n", static_cast<unsigned long long>(b.context().rflags));
                std::printf("    flags CF PF ZF SF OF (máscara %02x)  x86=%u%u%u%u%u  JIT=%u%u%u%u%u\n", r.flagMask,
                            r.out[80], r.out[81], r.out[82], r.out[83], r.out[84], state[80], state[81], state[82],
                            state[83], state[84]);
            }
        } else if (x == 1) {
            ++s.nanOnly;
        } else if (x == 2) {
            ++s.approx;
        } else {
            ++s.ok;
        }
    }
    int bad = 0, untranslated = 0;
    std::printf("\n=== Resumen (instrucciones con fallos) ===\n");
    for (const auto& [name, s] : stats) {
        if (s.fail + s.flagFail + s.gprFail == 0) continue;
        if (s.untranslated == s.total) {
            ++untranslated;
            std::printf("%-26s NO SOPORTADA\n", name.c_str());
            continue;
        }
        ++bad;
        std::printf("%-26s casos=%d xmm=%d flags=%d gpr=%d (nan=%d aprox=%d)\n", name.c_str(), s.total, s.fail,
                    s.flagFail, s.gprFail, s.nanOnly, s.approx);
    }
    std::printf("instrucciones=%zu con_fallos=%d no_soportadas=%d\n", stats.size(), bad, untranslated);
    return bad == 0 ? 0 : 1;
}
