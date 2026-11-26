#pragma once

// Memoria por debajo de 4 GB para invitados de 32 bits (todos sus punteros son de 4 bytes: pila, TEB,
// TLS, montón Win32...). Android/arm64 no tiene MAP_32BIT: se buscan huecos libres con
// MAP_FIXED_NOREPLACE de arriba abajo entre 1.5 y 3.75 GB. Las imágenes PE del invitado se cargan más
// abajo y el alias de funciones nativas está en 0x7F000000 (los huecos ocupados se saltan solos).

#include <sys/mman.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace hyperplay::lowmem {

// Activo mientras se ejecuta un invitado de 32 bits (lo fija el cargador).
inline std::atomic<bool> g_enabled{false};

inline bool enabled() noexcept { return g_enabled.load(std::memory_order_relaxed); }

// Como mmap(nullptr, size, prot, MAP_PRIVATE|MAP_ANONYMOUS[|extra]); MAP_FAILED si no hay hueco.
inline void* mmapBelow4G(size_t size, int prot = PROT_READ | PROT_WRITE, int extraFlags = 0) {
    static std::mutex s_m;
    static uintptr_t s_next = 0xEF000000u;
    const size_t len = (size + 0xFFFF) & ~size_t{0xFFFF};
    if (len == 0 || len > 0x80000000u) return MAP_FAILED;
    std::lock_guard<std::mutex> lk(s_m);
    for (int pass = 0; pass < 2; ++pass) {
        for (uintptr_t top = s_next; top >= 0x60000000u + len; top -= 0x10000) {
            void* p = mmap(reinterpret_cast<void*>(top - len), len, prot,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE | extraFlags, -1, 0);
            if (p == MAP_FAILED) continue;
            s_next = reinterpret_cast<uintptr_t>(p);
            return p;
        }
        s_next = 0xEF000000u;  // segunda vuelta: huecos liberados
    }
    return MAP_FAILED;
}

// mmap anónimo normal, o por debajo de 4 GB si hay un invitado de 32 bits.
inline void* mmapGuest(size_t size, int prot = PROT_READ | PROT_WRITE, int extraFlags = 0) {
    if (enabled()) return mmapBelow4G(size, prot, extraFlags);
    return mmap(nullptr, size, prot, MAP_PRIVATE | MAP_ANONYMOUS | extraFlags, -1, 0);
}

}  // namespace hyperplay::lowmem
