#pragma once

// WinSafeHeap: montón Win32 (HeapAlloc/LocalAlloc/hooks de Unity) aislado de Scudo/Bionic.
//
// Antes cada asignación era un mmap propio de >= 4 KB: Mono/Unity hacen millones de
// asignaciones de 16-64 bytes, lo que disparaba la RSS (x64..x256) hasta que Android mataba el
// proceso sin señal. Ahora:
//   - Bloques <= 64 KB: clases de tamaño servidas desde arenas de 64 MB (bump + listas libres).
//   - Bloques  > 64 KB: mmap dedicado (se devuelve al sistema al liberar).
// Cada bloque lleva una cabecera de 16 bytes (alineación de usuario de 16 bytes, como Windows x64).
// La pertenencia se comprueba por rango de arena / mapa de bloques grandes: nunca se lee memoria
// de un puntero ajeno. Header-only para que la suite nativa valide el mismo código.

#include <sys/mman.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace hyperplay::winheap {

inline constexpr size_t kHeaderSize = 16;
inline constexpr uint32_t kMagicLive = 0x484E4957u;  // "WINH"
inline constexpr uint32_t kMagicFree = 0x45455246u;  // "FREE"
inline constexpr uint32_t kLargeClass = 0xFFFFFFFFu;
inline constexpr size_t kArenaSize = 64u * 1024u * 1024u;
inline constexpr std::array<uint32_t, 24> kClassSizes = {
    16, 32, 48, 64, 96, 128, 192, 256, 384, 512, 768, 1024,
    1536, 2048, 3072, 4096, 6144, 8192, 12288, 16384, 24576, 32768, 49152, 65536};

struct Header {
    uint32_t magic;
    uint32_t sizeClass;  // índice en kClassSizes o kLargeClass
    uint64_t userSize;
};
static_assert(sizeof(Header) == kHeaderSize, "cabecera de 16 bytes");

class Heap {
public:
    void* alloc(size_t bytes) {
        const size_t userSize = bytes == 0 ? 1 : bytes;
        const uint32_t cls = classFor(userSize);
        std::lock_guard<std::mutex> lock(mutex_);
        uint8_t* block = nullptr;
        size_t usable = 0;
        if (cls == kLargeClass) {
            const size_t total = (kHeaderSize + userSize + 4095) & ~size_t{4095};
            void* p = ::mmap(nullptr, total, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
            if (p == MAP_FAILED) return nullptr;
            block = static_cast<uint8_t*>(p);
            large_[reinterpret_cast<uintptr_t>(block)] = total;
            usable = total - kHeaderSize;  // mmap ya viene a cero
        } else {
            usable = kClassSizes[cls];
            auto& head = free_[cls];
            if (head != nullptr) {
                block = head;
                head = *reinterpret_cast<uint8_t**>(block + kHeaderSize);
            } else {
                block = bump(kHeaderSize + usable);
                if (block == nullptr) return nullptr;
            }
            // HEAP_ZERO_MEMORY implícito: el comportamiento anterior (mmap nuevo) siempre
            // entregaba ceros y hay código huésped que depende de ello.
            std::memset(block + kHeaderSize, 0, usable);
        }
        auto* h = reinterpret_cast<Header*>(block);
        h->magic = kMagicLive;
        h->sizeClass = cls;
        h->userSize = userSize;
        ++live_;
        return block + kHeaderSize;
    }

    // true si el puntero era nuestro (vivo) y se liberó.
    bool free(void* ptr) {
        if (ptr == nullptr) return true;
        std::lock_guard<std::mutex> lock(mutex_);
        Header* h = headerLocked(ptr);
        if (h == nullptr || h->magic != kMagicLive) return false;
        auto* block = reinterpret_cast<uint8_t*>(h);
        --live_;
        if (h->sizeClass == kLargeClass) {
            auto it = large_.find(reinterpret_cast<uintptr_t>(block));
            const size_t total = it->second;
            large_.erase(it);
            ::munmap(block, total);
            return true;
        }
        h->magic = kMagicFree;
        *reinterpret_cast<uint8_t**>(block + kHeaderSize) = free_[h->sizeClass];
        free_[h->sizeClass] = block;
        return true;
    }

    // Tamaño pedido por el huésped; 0 si el puntero no es un bloque vivo nuestro.
    size_t size(const void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Header* h = headerLocked(ptr);
        return (h != nullptr && h->magic == kMagicLive) ? static_cast<size_t>(h->userSize) : 0;
    }

    bool owns(const void* ptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        const Header* h = headerLocked(ptr);
        return h != nullptr && h->magic == kMagicLive;
    }

    size_t liveBlocks() {
        std::lock_guard<std::mutex> lock(mutex_);
        return live_;
    }

    size_t arenaCount() {
        std::lock_guard<std::mutex> lock(mutex_);
        return arenas_.size();
    }

    static uint32_t classFor(size_t userSize) noexcept {
        for (uint32_t i = 0; i < kClassSizes.size(); ++i) {
            if (userSize <= kClassSizes[i]) return i;
        }
        return kLargeClass;
    }

private:
    struct Arena {
        uintptr_t begin;
        uintptr_t end;
    };

    static uintptr_t untag(const void* p) noexcept {
        return reinterpret_cast<uintptr_t>(p) & 0x00FFFFFFFFFFFFFFull;
    }

    // Cabecera del bloque o nullptr si el puntero no está en memoria del montón.
    Header* headerLocked(const void* ptr) const {
        if (ptr == nullptr) return nullptr;
        const uintptr_t user = untag(ptr);
        if (user < kHeaderSize || (user & 0xF) != 0) return nullptr;
        const uintptr_t hdr = user - kHeaderSize;
        if (large_.count(hdr) != 0) return reinterpret_cast<Header*>(hdr);
        for (const Arena& a : arenas_) {
            if (hdr >= a.begin && hdr + kHeaderSize <= a.end) {
                // Solo es válido si coincide con el inicio de un bloque emitido: la magia
                // está en el inicio de cada bloque y la memoria de la arena siempre está mapeada.
                return reinterpret_cast<Header*>(hdr);
            }
        }
        return nullptr;
    }

    uint8_t* bump(size_t blockSize) {
        if (arenas_.empty() || cursor_ + blockSize > arenas_.back().end) {
            void* p = ::mmap(nullptr, kArenaSize, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_NORESERVE, -1, 0);
            if (p == MAP_FAILED) return nullptr;
            const auto begin = reinterpret_cast<uintptr_t>(p);
            arenas_.push_back({begin, begin + kArenaSize});
            cursor_ = begin;
        }
        auto* block = reinterpret_cast<uint8_t*>(cursor_);
        cursor_ += blockSize;  // blockSize es múltiplo de 16: la alineación se conserva
        return block;
    }

    std::mutex mutex_;
    std::vector<Arena> arenas_;
    uintptr_t cursor_ = 0;
    std::array<uint8_t*, kClassSizes.size()> free_{};
    std::unordered_map<uintptr_t, size_t> large_;
    size_t live_ = 0;
};

inline Heap& processHeap() {
    static Heap s_heap;
    return s_heap;
}

}  // namespace hyperplay::winheap
