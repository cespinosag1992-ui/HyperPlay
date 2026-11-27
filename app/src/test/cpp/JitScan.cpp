// Escáner de cobertura del JIT: carga un PE32+ (sin ejecutarlo), recorre cada función de .pdata
// bloque a bloque y traduce con OryonEmitterBackend. Agrupa los errores ("opcode XX no
// soportado", etc.) para arreglar todos los opcodes que faltan de una vez, antes de que un hilo
// del juego muera en mitad de la partida.
//
// Uso (en el dispositivo): jit_scan <dll|exe> [maxFunciones]
#include "cpu/OryonEmitterBackend.h"

#include <sys/mman.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "loader/NativeDllInterceptor.h"

using hyperplay::EmitterConfig;
using hyperplay::OryonEmitterBackend;

// El escáner solo traduce: el interceptor real no se enlaza.
namespace hyperplay {
void NativeDllInterceptor::setGuestTeb(void*, bool) noexcept {}
bool NativeDllInterceptor::isRegisteredTrampoline(uintptr_t) noexcept { return false; }
int32_t NativeDllInterceptor::dispatchGuestException(OryonEmitterBackend&, uint64_t) { return 0; }
bool NativeDllInterceptor::prepareGuestException32(CpuContext&, uint32_t, uint32_t, uint32_t) { return false; }
}  // namespace hyperplay

namespace {

template <typename T>
T rd(const std::vector<uint8_t>& d, size_t off) {
    T v{};
    if (off + sizeof(T) <= d.size()) std::memcpy(&v, d.data() + off, sizeof(T));
    return v;
}

bool configure(OryonEmitterBackend& b, uintptr_t base, size_t size) {
    EmitterConfig c;
    c.is32Bit = false;
    c.stackSize = 1u << 20;
    c.codeCacheSize = 256u << 20;
    c.persistBlocks = false;
    if (!b.configure(c)) return false;
    b.addExecutableRange(base, size);
    OryonEmitterBackend::addStaticImage(base, size);  // como el cargador real (flags entre bloques)
    return true;
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::printf("uso: %s <pe> [maxFunciones]\n", argv[0]);
        return 2;
    }
    FILE* f = std::fopen(argv[1], "rb");
    if (!f) {
        std::printf("no se puede abrir %s\n", argv[1]);
        return 2;
    }
    std::vector<uint8_t> d;
    {
        std::fseek(f, 0, SEEK_END);
        d.resize(static_cast<size_t>(std::ftell(f)));
        std::fseek(f, 0, SEEK_SET);
        if (std::fread(d.data(), 1, d.size(), f) != d.size()) return 2;
        std::fclose(f);
    }
    const uint32_t pe = rd<uint32_t>(d, 0x3C);
    if (rd<uint16_t>(d, pe + 4) != 0x8664) {
        std::printf("solo PE32+ x64\n");
        return 2;
    }
    const uint16_t nsec = rd<uint16_t>(d, pe + 6);
    const uint16_t optSize = rd<uint16_t>(d, pe + 20);
    const size_t opt = pe + 24;
    const uint64_t imageBase = rd<uint64_t>(d, opt + 24);
    const uint32_t imageSize = rd<uint32_t>(d, opt + 56);
    const uint32_t pdataRva = rd<uint32_t>(d, opt + 112 + 3 * 8);
    const uint32_t pdataSize = rd<uint32_t>(d, opt + 112 + 3 * 8 + 4);

    void* map = mmap(reinterpret_cast<void*>(imageBase), imageSize, PROT_READ | PROT_WRITE,
                     MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (map != reinterpret_cast<void*>(imageBase)) {
        std::printf("no se pudo mapear en 0x%llx\n", static_cast<unsigned long long>(imageBase));
        return 2;
    }
    auto* img = static_cast<uint8_t*>(map);
    std::memcpy(img, d.data(), std::min<size_t>(d.size(), 0x1000));
    for (uint16_t i = 0; i < nsec; ++i) {
        const size_t s = opt + optSize + 40u * i;
        const uint32_t vsize = rd<uint32_t>(d, s + 8), va = rd<uint32_t>(d, s + 12);
        const uint32_t rawSize = rd<uint32_t>(d, s + 16), raw = rd<uint32_t>(d, s + 20);
        const size_t n = std::min<size_t>(std::min(vsize ? vsize : rawSize, rawSize), d.size() - raw);
        if (va + n <= imageSize) std::memcpy(img + va, d.data() + raw, n);
    }

    const size_t nfunc = pdataSize / 12;
    OryonEmitterBackend b;
    if (!configure(b, imageBase, imageSize)) return 2;
    // jit_scan <pe> dump <rva hex>: traduce un bloque y lo vuelca como ".inst 0x..." (ensamblable
    // en el PC para desensamblarlo) con el tamaño x86 consumido.
    if (argc > 3 && std::strcmp(argv[2], "dump") == 0) {
        const uintptr_t rip = imageBase + std::strtoull(argv[3], nullptr, 16);
        hyperplay::TranslatedBlock* tb = b.translateBlock(rip);
        if (tb == nullptr) {
            std::printf("error: %s\n", b.lastError().c_str());
            return 1;
        }
        std::printf("// rip=0x%llx x86=%zu B host=%zu B instrs_x86=%zu\n", static_cast<unsigned long long>(rip),
                    tb->guestSize, tb->hostSize, tb->instrMap.size());
        size_t mi = 0;
        for (size_t off = 0; off + 4 <= tb->hostSize; off += 4) {
            while (mi < tb->instrMap.size() && tb->instrMap[mi].first == off) {
                std::printf("// --- x86 +0x%x\n", tb->instrMap[mi].second);
                ++mi;
            }
            uint32_t w;
            std::memcpy(&w, static_cast<const uint8_t*>(tb->hostCode) + off, 4);
            std::printf(".inst 0x%08x\n", w);
        }
        return 0;
    }
    const size_t maxFunc = argc > 2 ? std::strtoul(argv[2], nullptr, 10) : nfunc;

    struct Err {
        size_t count = 0;
        uint64_t rip = 0;
    };
    std::map<std::string, Err> errors;
    size_t blocks = 0, funcsOk = 0, funcsBad = 0, hostBytes = 0, guestBytes = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (size_t i = 0; i < nfunc && i < maxFunc; ++i) {
        const uint32_t begin = *reinterpret_cast<uint32_t*>(img + pdataRva + 12 * i);
        const uint32_t end = *reinterpret_cast<uint32_t*>(img + pdataRva + 12 * i + 4);
        if (i % 3000 == 2999 && !configure(b, imageBase, imageSize)) return 2;  // arena llena
        uintptr_t rip = imageBase + begin;
        bool bad = false;
        while (rip < imageBase + end) {
            hyperplay::TranslatedBlock* tb = b.translateBlock(rip);
            if (tb == nullptr) {
                std::string e = b.lastError();
                const size_t at = e.find(" en RIP=");
                if (at != std::string::npos) e.resize(at);
                Err& x = errors[e];
                if (x.count++ == 0) x.rip = rip;
                bad = true;
                break;
            }
            ++blocks;
            hostBytes += tb->hostSize;
            guestBytes += tb->guestSize;
            if (tb->guestSize == 0) break;
            rip += tb->guestSize;
        }
        bad ? ++funcsBad : ++funcsOk;
    }
    const double us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - t0).count();
    std::printf("funciones=%zu ok=%zu con_error=%zu bloques=%zu | %.1f us/bloque, host %.0f B/bloque, x86 %.0f B/bloque\n",
                funcsOk + funcsBad, funcsOk, funcsBad, blocks, us / std::max<size_t>(1, blocks),
                static_cast<double>(hostBytes) / std::max<size_t>(1, blocks),
                static_cast<double>(guestBytes) / std::max<size_t>(1, blocks));
    std::vector<std::pair<size_t, std::string>> sorted;
    for (const auto& [k, v] : errors) sorted.emplace_back(v.count, k);
    std::sort(sorted.rbegin(), sorted.rend());
    for (const auto& [n, k] : sorted) {
        std::printf("%6zu  %s  (p.ej. bloque 0x%llx)\n", n, k.c_str(),
                    static_cast<unsigned long long>(errors[k].rip));
    }
    return 0;
}
