#pragma once

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace hyperplay {

struct AotHeader {
    uint32_t magic = 0x4F525941; // 'ARYO' (Oryon AOT)
    uint32_t version = 1;
    uint64_t textHash = 0;
    uint64_t textSize = 0;
    uint32_t blockCount = 0;
    uint32_t flags = 0;
};

struct AotCacheStats {
    uint32_t blocksScanned = 0;
    uint32_t blocksCached = 0;
    uint64_t textSizeBytes = 0;
    bool cacheHit = false;
};

class HyperAotCache {
public:
    HyperAotCache() = default;
    ~HyperAotCache();

    HyperAotCache(const HyperAotCache&) = delete;
    HyperAotCache& operator=(const HyperAotCache&) = delete;

    /// Escanea el ejecutable y DLLs de la carpeta portable, precompilando bloques
    /// aot_oryon.bin en <gameDir>/_HyperPlaySaves/hypercache/
    [[nodiscard]] AotCacheStats precompile(const std::string& exePath, const std::string& savesDir);

    /// Precarga el archivo aot_oryon.bin en LPDDR5X RAM con posix_fadvise(POSIX_FADV_WILLNEED) y madvise.
    [[nodiscard]] bool preloadToRam(const std::string& savesDir);

    [[nodiscard]] static std::string getCachePath(const std::string& savesDir);

    [[nodiscard]] bool isCacheValid(const std::string& exePath, const std::string& savesDir);

    /// Añade a la caché en memoria un bloque nativo emitido por OryonEmitterBackend.
    /// Los bloques contienen direcciones absolutas: la caché en disco es diagnóstica/AOT y no
    /// se reutiliza como código ejecutable sin reubicar.
    [[nodiscard]] bool appendTranslatedBlock(uint64_t guestRip, const void* code, size_t size);

    /// Escribe <savesDir>/hypercache/oryon_jit.bin con los bloques acumulados.
    [[nodiscard]] bool flushTranslatedBlocks(const std::string& savesDir);

    [[nodiscard]] size_t pendingTranslatedBlocks() const noexcept { return blockRecords_.size(); }

private:
    struct BlockRecord {
        uint64_t guestRip = 0;
        std::vector<uint8_t> bytes;
    };
    std::vector<BlockRecord> blockRecords_;

    void* mappedCache_ = nullptr;
    size_t mappedSize_ = 0;
};

// ---------------------------------------------------------------------------
// Caché persistente de traducciones JIT (HyperAOT reubicable).
//
// La 1.ª ejecución traduce y guarda cada bloque en <Juego>/_HyperPlaySaves/hypercache/; las
// siguientes lo copian a la arena en vez de retraducirlo. En disco, no en RAM: solo se mantiene
// un índice compacto (RIP -> posición); el código se lee con pread al pedirlo.
//
// Validez de un registro:
//  - archivo: mismo build de libhyperplay (build-id ELF); si no, se vacía.
//  - bloque: misma configuración del emisor (TSO, encadenado, ...) y los bytes x86 actuales en
//    la RIP tienen el mismo hash (código modificado o en otra dirección -> se retraduce).
//  - reubicaciones: direcciones de funciones de libhyperplay se guardan relativas a un ancla y
//    se reescriben al cargar; las llamadas nativas rápidas leídas de la IAT se revalidan.
//  - suma de control de todo el registro: un registro corrupto o truncado se descarta.
// ---------------------------------------------------------------------------

struct JitReloc {
    uint32_t word = 0;    // índice de palabra (MOVZ + 3 MOVK) desde el inicio del bloque
    uint8_t reg = 0;      // registro destino del MOV
    uint8_t kind = 0;     // kJitRelocLib / kJitRelocFastNative / kJitRelocFastNativeBlocking
    uint16_t pad = 0;
    int64_t delta = 0;    // dirección - ancla de libhyperplay
};
static_assert(sizeof(JitReloc) == 16, "JitReloc en disco");

struct JitDep {
    uint64_t slot = 0;    // dirección invitada de la ranura IAT leída al traducir
    int64_t delta = 0;    // valor esperado (función nativa) - ancla
};
static_assert(sizeof(JitDep) == 16, "JitDep en disco");

inline constexpr uint8_t kJitRelocLib = 0;
inline constexpr uint8_t kJitRelocFastNative = 1;
inline constexpr uint8_t kJitRelocFastNativeBlocking = 2;

struct JitRecord {
    uint64_t guestRip = 0;
    uint64_t guestHash = 0;
    uint32_t guestSize = 0;
    uint32_t cfg = 0;
    bool endsBlock = false;
    std::vector<uint8_t> host;
    std::vector<std::pair<uint32_t, uint32_t>> instrMap;
    std::vector<JitReloc> relocs;
    std::vector<JitDep> deps;
};

class JitDiskCache {
public:
    static JitDiskCache& instance();

    /// Carpeta del juego (<saves>/hypercache): abre la caché la primera vez (idempotente).
    /// `debug.hyperplay.aotcache 0` la desactiva.
    static void setDirectory(const std::string& dir);

    /// Abre (o crea) `basePath`.dat/.idx para el build `buildId`. Público para las pruebas.
    bool open(const std::string& basePath, uint64_t buildId);
    void close();
    [[nodiscard]] bool enabled() const noexcept { return fd_ >= 0; }

    /// Busca un registro de `rip` aceptado por `guestMatches(guestHash, guestSize)` y con la
    /// configuración `cfg`. Lee del disco solo el registro elegido.
    template <typename F>
    bool lookup(uint64_t rip, uint32_t cfg, F&& guestMatches, JitRecord& out) {
        uint64_t offs[kMaxPerRip];
        const size_t n = candidates(rip, offs);
        for (size_t i = 0; i < n; ++i) {
            RecHdr h{};
            if (!readHeader(offs[i], rip, h) || h.cfg != cfg) continue;
            if (!guestMatches(h.guestHash, h.guestSize)) continue;
            if (readBody(offs[i], h, out)) {
                hits_.fetch_add(1, std::memory_order_relaxed);
                return true;
            }
        }
        misses_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }

    /// Encola el registro; un hilo lo escribe al final del archivo (no bloquea al JIT).
    void append(JitRecord&& rec);
    /// Espera a que se escriba todo lo encolado (pruebas / cierre).
    void flush();

    [[nodiscard]] uint64_t hits() const noexcept { return hits_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t misses() const noexcept { return misses_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t written() const noexcept { return written_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t rejected() const noexcept { return rejected_.load(std::memory_order_relaxed); }

    static uint64_t hashBytes(const void* data, size_t size) noexcept;
    /// Ancla de reubicación: una función de libhyperplay (las deltas son constantes por build).
    static uintptr_t anchor() noexcept;
    /// Identificador del build de libhyperplay (NT_GNU_BUILD_ID); 0 si no se pudo leer.
    static uint64_t buildId() noexcept;

    static constexpr uint64_t kMaxFileBytes = 512ull << 20;
    static constexpr size_t kMaxPerRip = 4;

    JitDiskCache() = default;
    ~JitDiskCache();
    JitDiskCache(const JitDiskCache&) = delete;
    JitDiskCache& operator=(const JitDiskCache&) = delete;

private:
    struct RecHdr {
        uint32_t magic;
        uint32_t bodySize;
        uint64_t guestRip;
        uint64_t guestHash;
        uint64_t bodyHash;
        uint32_t guestSize;
        uint32_t cfg;
        uint32_t hostSize;
        uint16_t nInstr;
        uint16_t nReloc;
        uint16_t nDep;
        uint8_t endsBlock;
        uint8_t pad0;
        uint32_t pad1;
    };
    static_assert(sizeof(RecHdr) == 56, "RecHdr en disco");

    size_t candidates(uint64_t rip, uint64_t* out);
    bool readHeader(uint64_t off, uint64_t rip, RecHdr& h);
    bool readBody(uint64_t off, const RecHdr& h, JitRecord& out);
    void writerLoop();

    int fd_ = -1;
    int idxFd_ = -1;
    std::atomic<uint64_t> fileSize_{0};
    std::vector<std::pair<uint64_t, uint64_t>> base_;   // (rip, offset) ordenado: archivo previo
    std::unordered_multimap<uint64_t, uint64_t> fresh_; // escritos en esta ejecución
    std::unordered_set<uint64_t> queuedKeys_;           // (rip, hash, cfg) ya encolados
    std::mutex mutex_;
    std::condition_variable cv_;
    std::condition_variable drained_;
    std::deque<JitRecord> queue_;
    std::thread writer_;
    bool stop_ = false;
    bool writing_ = false;
    std::atomic<uint64_t> hits_{0}, misses_{0}, written_{0}, rejected_{0};
};

}  // namespace hyperplay
