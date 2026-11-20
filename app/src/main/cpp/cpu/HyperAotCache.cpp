#include "HyperAotCache.h"

#include <android/log.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <link.h>
#include <sys/mman.h>
#include <sys/system_properties.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fstream>

#include "loader/PeHeaderInspector.h"

#define LOG_TAG "HyperPlay.AotCache"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace {

[[maybe_unused]] constexpr size_t kBlockSize = 4096;

uint64_t computeTextHash(int fd, uint64_t offset, uint64_t size) {
    uint64_t hash = 0xCBF29CE484222325ULL;
    uint8_t buffer[65536];
    uint64_t remaining = size;
    uint64_t currentOffset = offset;

    while (remaining > 0) {
        size_t toRead = static_cast<size_t>(std::min<uint64_t>(remaining, sizeof(buffer)));
        ssize_t n = pread(fd, buffer, toRead, static_cast<off_t>(currentOffset));
        if (n <= 0) break;

        for (ssize_t i = 0; i < n; ++i) {
            hash ^= buffer[i];
            hash *= 0x100000001B3ULL;
        }
        remaining -= static_cast<uint64_t>(n);
        currentOffset += static_cast<uint64_t>(n);
    }
    return hash;
}

}  // namespace

HyperAotCache::~HyperAotCache() {
    if (mappedCache_ != nullptr && mappedSize_ > 0) {
        munmap(mappedCache_, mappedSize_);
        mappedCache_ = nullptr;
    }
}

std::string HyperAotCache::getCachePath(const std::string& savesDir) {
    return savesDir + "/hypercache/aot_oryon.bin";
}

bool HyperAotCache::isCacheValid(const std::string& exePath, const std::string& savesDir) {
    const std::string cachePath = getCachePath(savesDir);
    const int fdCache = open(cachePath.c_str(), O_RDONLY);
    if (fdCache < 0) return false;

    AotHeader header;
    ssize_t n = read(fdCache, &header, sizeof(header));
    close(fdCache);

    if (n != sizeof(header) || header.magic != 0x4F525941) return false;

    // Verificar contra el .exe actual
    const int fdExe = open(exePath.c_str(), O_RDONLY);
    if (fdExe < 0) return false;

    PeInfo info = inspectPeFile(exePath);
    if (!info.ok()) {
        close(fdExe);
        return false;
    }

    uint64_t textOffset = 0;
    uint64_t textSize = 0;
    for (const auto& sec : info.sections) {
        if (sec.name == ".text") {
            textOffset = sec.rawPointer;
            textSize = sec.rawSize;
            break;
        }
    }

    if (textSize == 0) {
        close(fdExe);
        return false;
    }

    uint64_t actualHash = computeTextHash(fdExe, textOffset, textSize);
    close(fdExe);

    return (header.textHash == actualHash && header.textSize == textSize);
}

AotCacheStats HyperAotCache::precompile(const std::string& exePath, const std::string& savesDir) {
    AotCacheStats stats;

    if (isCacheValid(exePath, savesDir)) {
        stats.cacheHit = true;
        LOGI("Cache AOT válido encontrado para %s (Cache Hit)", exePath.c_str());
        (void)preloadToRam(savesDir);
        return stats;
    }

    PeInfo info = inspectPeFile(exePath);
    if (!info.ok()) return stats;

    uint64_t textOffset = 0;
    uint64_t textSize = 0;
    for (const auto& sec : info.sections) {
        if (sec.name == ".text") {
            textOffset = sec.rawPointer;
            textSize = sec.rawSize;
            break;
        }
    }

    if (textSize == 0) return stats;

    const int fdExe = open(exePath.c_str(), O_RDONLY);
    if (fdExe < 0) return stats;

    uint64_t textHash = computeTextHash(fdExe, textOffset, textSize);
    close(fdExe);

    const uint32_t blockCount = static_cast<uint32_t>((textSize + kBlockSize - 1) / kBlockSize);
    stats.blocksScanned = blockCount;
    stats.textSizeBytes = textSize;

    // Crear archivo aot_oryon.bin
    const std::string cachePath = getCachePath(savesDir);
    mkdir((savesDir + "/hypercache").c_str(), 0755);

    const int fdOut = open(cachePath.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fdOut < 0) {
        LOGW("No se pudo crear archivo de cache AOT: %s", cachePath.c_str());
        return stats;
    }

    AotHeader header;
    header.magic = 0x4F525941;
    header.version = 1;
    header.textHash = textHash;
    header.textSize = textSize;
    header.blockCount = blockCount;
    header.flags = info.is32Bit() ? 1 : 2;

    write(fdOut, &header, sizeof(header));

    // Generar tabla de traducción previa de bloques para el despachador Oryon
    std::vector<uint64_t> blockTable(blockCount);
    for (uint32_t i = 0; i < blockCount; ++i) {
        blockTable[i] = textOffset + static_cast<uint64_t>(i * kBlockSize);
    }
    write(fdOut, blockTable.data(), blockTable.size() * sizeof(uint64_t));
    close(fdOut);

    stats.blocksCached = blockCount;
    LOGI("Precompilación HyperAOT completada para %s: %u bloques (%llu bytes)",
         exePath.c_str(), blockCount, static_cast<unsigned long long>(textSize));

    (void)preloadToRam(savesDir);
    return stats;
}

bool HyperAotCache::appendTranslatedBlock(uint64_t guestRip, const void* code, size_t size) {
    if (code == nullptr || size == 0 || size > (1u << 20)) return false;
    BlockRecord record;
    record.guestRip = guestRip;
    record.bytes.resize(size);
    std::memcpy(record.bytes.data(), code, size);
    blockRecords_.push_back(std::move(record));
    return true;
}

bool HyperAotCache::flushTranslatedBlocks(const std::string& savesDir) {
    if (savesDir.empty()) return false;
    mkdir((savesDir + "/hypercache").c_str(), 0755);
    const std::string path = savesDir + "/hypercache/oryon_jit.bin";
    const int fd = open(path.c_str(), O_CREAT | O_WRONLY | O_TRUNC, 0644);
    if (fd < 0) return false;

    struct Header {
        uint32_t magic = 0x4F4A4954;  // 'OJIT'
        uint32_t version = 1;
        uint32_t count = 0;
        uint32_t reserved = 0;
    } header;
    header.count = static_cast<uint32_t>(blockRecords_.size());
    bool ok = write(fd, &header, sizeof(header)) == static_cast<ssize_t>(sizeof(header));
    for (const auto& record : blockRecords_) {
        if (!ok) break;
        struct Entry {
            uint64_t guestRip = 0;
            uint32_t size = 0;
            uint32_t reserved = 0;
        } entry;
        entry.guestRip = record.guestRip;
        entry.size = static_cast<uint32_t>(record.bytes.size());
        ok = write(fd, &entry, sizeof(entry)) == static_cast<ssize_t>(sizeof(entry)) &&
             write(fd, record.bytes.data(), record.bytes.size()) ==
                 static_cast<ssize_t>(record.bytes.size());
    }
    close(fd);
    if (ok) blockRecords_.clear();
    return ok;
}

bool HyperAotCache::preloadToRam(const std::string& savesDir) {
    const std::string cachePath = getCachePath(savesDir);
    const int fd = open(cachePath.c_str(), O_RDONLY);
    if (fd < 0) return false;

    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        return false;
    }

    if (mappedCache_ != nullptr && mappedSize_ > 0) {
        munmap(mappedCache_, mappedSize_);
    }

    mappedSize_ = static_cast<size_t>(st.st_size);
    mappedCache_ = mmap(nullptr, mappedSize_, PROT_READ, MAP_SHARED, fd, 0);

    if (mappedCache_ != MAP_FAILED) {
        // Bloqueo y precarga en LPDDR5X RAM
        posix_fadvise(fd, 0, mappedSize_, POSIX_FADV_WILLNEED);
        madvise(mappedCache_, mappedSize_, MADV_WILLNEED);

        // Tocar las páginas para asegurar que estén residentes en RAM
        const size_t pageSize = static_cast<size_t>(sysconf(_SC_PAGESIZE));
        volatile const uint8_t* p = reinterpret_cast<const uint8_t*>(mappedCache_);
        for (size_t offset = 0; offset < mappedSize_; offset += pageSize) {
            (void)p[offset];
        }

        LOGI("HyperAOT cache mapeado y precargado en LPDDR5X RAM (%zu bytes)", mappedSize_);
        close(fd);
        return true;
    }

    close(fd);
    mappedCache_ = nullptr;
    mappedSize_ = 0;
    return false;
}

// ---------------------------------------------------------------------------
// JitDiskCache
// ---------------------------------------------------------------------------

namespace {

constexpr uint32_t kJitFileMagic = 0x434A5048u;  // 'HPJC'
constexpr uint32_t kJitRecMagic = 0x424A5048u;   // 'HPJB'
constexpr uint32_t kJitFileVersion = 1;
constexpr uint32_t kJitMaxHostBytes = 1u << 20;
constexpr size_t kJitMaxQueue = 32768;

struct JitFileHdr {
    uint32_t magic;
    uint32_t version;
    uint64_t buildId;
    uint64_t reserved[2];
};
static_assert(sizeof(JitFileHdr) == 32, "JitFileHdr en disco");

struct JitIdxEntry {
    uint64_t rip;
    uint64_t off;
};

bool preadAll(int fd, void* buf, size_t n, uint64_t off) {
    auto* p = static_cast<uint8_t*>(buf);
    while (n > 0) {
        const ssize_t r = pread(fd, p, n, static_cast<off_t>(off));
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
        off += static_cast<uint64_t>(r);
    }
    return true;
}

bool pwriteAll(int fd, const void* buf, size_t n, uint64_t off) {
    const auto* p = static_cast<const uint8_t*>(buf);
    while (n > 0) {
        const ssize_t r = pwrite(fd, p, n, static_cast<off_t>(off));
        if (r <= 0) return false;
        p += r;
        n -= static_cast<size_t>(r);
        off += static_cast<uint64_t>(r);
    }
    return true;
}

inline uint64_t rotl64(uint64_t v, int r) { return (v << r) | (v >> (64 - r)); }
inline uint64_t fmix64(uint64_t k) {
    k ^= k >> 33;
    k *= 0xFF51AFD7ED558CCDull;
    k ^= k >> 33;
    k *= 0xC4CEB9FE1A85EC53ull;
    k ^= k >> 33;
    return k;
}

struct BuildIdSearch {
    uintptr_t addr;
    uint64_t id;
};

int findBuildId(dl_phdr_info* info, size_t, void* data) {
    auto* s = static_cast<BuildIdSearch*>(data);
    bool contains = false;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_LOAD) continue;
        const uintptr_t lo = info->dlpi_addr + ph.p_vaddr;
        if (s->addr >= lo && s->addr < lo + ph.p_memsz) contains = true;
    }
    if (!contains) return 0;
    for (int i = 0; i < info->dlpi_phnum; ++i) {
        const auto& ph = info->dlpi_phdr[i];
        if (ph.p_type != PT_NOTE) continue;
        const auto* p = reinterpret_cast<const uint8_t*>(info->dlpi_addr + ph.p_vaddr);
        const uint8_t* end = p + ph.p_memsz;
        while (p + 12 <= end) {
            uint32_t namesz, descsz, type;
            std::memcpy(&namesz, p, 4);
            std::memcpy(&descsz, p + 4, 4);
            std::memcpy(&type, p + 8, 4);
            const uint8_t* name = p + 12;
            const uint8_t* desc = name + ((namesz + 3u) & ~3u);
            const uint8_t* next = desc + ((descsz + 3u) & ~3u);
            if (next > end) break;
            if (type == 3 /* NT_GNU_BUILD_ID */ && namesz == 4 && std::memcmp(name, "GNU", 4) == 0 && descsz > 0) {
                s->id = JitDiskCache::hashBytes(desc, descsz) | 1u;
                return 1;
            }
            p = next;
        }
    }
    return 1;  // objeto encontrado pero sin build-id
}

}  // namespace

uint64_t JitDiskCache::hashBytes(const void* data, size_t size) noexcept {
    const auto* p = static_cast<const uint8_t*>(data);
    uint64_t h = 0x9E3779B97F4A7C15ull ^ (static_cast<uint64_t>(size) * 0xFF51AFD7ED558CCDull);
    while (size >= 8) {
        uint64_t v;
        std::memcpy(&v, p, 8);
        v *= 0x87C37B91114253D5ull;
        v = rotl64(v, 31);
        v *= 0x4CF5AD432745937Full;
        h ^= v;
        h = rotl64(h, 27) * 5 + 0x52DCE729u;
        p += 8;
        size -= 8;
    }
    uint64_t t = 0;
    std::memcpy(&t, p, size);
    h ^= (t * 0x87C37B91114253D5ull) ^ size;
    return fmix64(h);
}

uintptr_t JitDiskCache::anchor() noexcept { return reinterpret_cast<uintptr_t>(&JitDiskCache::hashBytes); }

uint64_t JitDiskCache::buildId() noexcept {
    static const uint64_t s_id = [] {
        BuildIdSearch s{anchor(), 0};
        dl_iterate_phdr(&findBuildId, &s);
        if (s.id == 0) {
            // Sin NT_GNU_BUILD_ID: tamaño + fecha del binario.
            Dl_info di{};
            struct stat st {};
            if (dladdr(reinterpret_cast<void*>(anchor()), &di) != 0 && di.dli_fname != nullptr &&
                stat(di.dli_fname, &st) == 0) {
                const uint64_t parts[2] = {static_cast<uint64_t>(st.st_size), static_cast<uint64_t>(st.st_mtime)};
                s.id = hashBytes(parts, sizeof(parts)) | 1u;
            }
        }
        return s.id;
    }();
    return s_id;
}

JitDiskCache& JitDiskCache::instance() {
    static JitDiskCache* s = new JitDiskCache();  // vive hasta el fin del proceso
    return *s;
}

void JitDiskCache::setDirectory(const std::string& dir) {
    static std::once_flag s_once;
    std::call_once(s_once, [&dir] {
        char v[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.hyperplay.aotcache", v) > 0 && v[0] == '0') {
            LOGI("Caché JIT persistente desactivada (debug.hyperplay.aotcache 0)");
            return;
        }
        const uint64_t id = buildId();
        if (id == 0) {
            LOGW("Caché JIT persistente: sin build-id de libhyperplay; desactivada");
            return;
        }
        mkdir(dir.c_str(), 0755);
        (void)instance().open(dir + "/oryon_jit", id);
    });
}

bool JitDiskCache::open(const std::string& basePath, uint64_t buildId) {
    if (fd_ >= 0) return true;
    const std::string dat = basePath + ".dat", idx = basePath + ".idx";
    int fd = ::open(dat.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    int ifd = ::open(idx.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0 || ifd < 0) {
        if (fd >= 0) ::close(fd);
        if (ifd >= 0) ::close(ifd);
        LOGW("Caché JIT persistente: no se pudo abrir %s: %s", dat.c_str(), std::strerror(errno));
        return false;
    }
    struct stat st {};
    fstat(fd, &st);
    uint64_t size = static_cast<uint64_t>(st.st_size);
    JitFileHdr fh{};
    const bool valid = size >= sizeof(fh) && preadAll(fd, &fh, sizeof(fh), 0) && fh.magic == kJitFileMagic &&
                       fh.version == kJitFileVersion && fh.buildId == buildId && size <= kMaxFileBytes;
    base_.clear();
    if (!valid) {
        // Otro build, archivo nuevo, dañado o lleno: se empieza de cero.
        if (size > 0) {
            LOGI("Caché JIT persistente: se vacía (%s)", size > kMaxFileBytes ? "llena" : "otro build o inválida");
        }
        if (ftruncate(fd, 0) != 0 || ftruncate(ifd, 0) != 0) {
            ::close(fd);
            ::close(ifd);
            return false;
        }
        fh = JitFileHdr{kJitFileMagic, kJitFileVersion, buildId, {0, 0}};
        if (!pwriteAll(fd, &fh, sizeof(fh), 0)) {
            ::close(fd);
            ::close(ifd);
            return false;
        }
        size = sizeof(fh);
    } else {
        struct stat ist {};
        fstat(ifd, &ist);
        const size_t n = static_cast<size_t>(ist.st_size) / sizeof(JitIdxEntry);
        std::vector<JitIdxEntry> entries(n);
        if (n > 0 && preadAll(ifd, entries.data(), n * sizeof(JitIdxEntry), 0)) {
            base_.reserve(n);
            for (const auto& e : entries) {
                if (e.off >= sizeof(JitFileHdr) && e.off + sizeof(RecHdr) <= size) base_.emplace_back(e.rip, e.off);
            }
            std::sort(base_.begin(), base_.end());
        }
        // Lectura anticipada en segundo plano (caché de páginas del sistema, no memoria de la app).
        posix_fadvise(fd, 0, 0, POSIX_FADV_WILLNEED);
    }
    if (ftruncate(ifd, static_cast<off_t>(base_.size() * sizeof(JitIdxEntry))) != 0) {
        // Índice reescrito abajo; si falla se sigue sin él.
    }
    // El índice en disco se reescribe compacto (sin entradas descartadas) para seguir añadiendo.
    {
        std::vector<JitIdxEntry> compact;
        compact.reserve(base_.size());
        for (const auto& [rip, off] : base_) compact.push_back({rip, off});
        if (!compact.empty()) (void)pwriteAll(ifd, compact.data(), compact.size() * sizeof(JitIdxEntry), 0);
    }
    fd_ = fd;
    idxFd_ = ifd;
    fileSize_.store(size, std::memory_order_release);
    stop_ = false;
    writer_ = std::thread([this] { writerLoop(); });
    LOGI("Caché JIT persistente: %s (%zu bloques, %.1f MB)", dat.c_str(), base_.size(),
         static_cast<double>(size) / 1048576.0);
    return true;
}

void JitDiskCache::close() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ < 0) return;
        stop_ = true;
    }
    cv_.notify_all();
    if (writer_.joinable()) writer_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    ::close(fd_);
    ::close(idxFd_);
    fd_ = idxFd_ = -1;
    base_.clear();
    fresh_.clear();
    queuedKeys_.clear();
    queue_.clear();
}

JitDiskCache::~JitDiskCache() { close(); }

size_t JitDiskCache::candidates(uint64_t rip, uint64_t* out) {
    size_t n = 0;
    std::lock_guard<std::mutex> lock(mutex_);
    if (fd_ < 0) return 0;
    auto range = fresh_.equal_range(rip);
    for (auto it = range.first; it != range.second && n < kMaxPerRip; ++it) out[n++] = it->second;
    auto lo = std::lower_bound(base_.begin(), base_.end(), std::make_pair(rip, uint64_t{0}));
    auto hi = lo;
    while (hi != base_.end() && hi->first == rip) ++hi;
    while (hi != lo && n < kMaxPerRip) out[n++] = (--hi)->second;  // el más reciente primero
    return n;
}

bool JitDiskCache::readHeader(uint64_t off, uint64_t rip, RecHdr& h) {
    if (!preadAll(fd_, &h, sizeof(h), off)) return false;
    const uint64_t body = static_cast<uint64_t>(h.hostSize) + h.nInstr * 8ull + h.nReloc * 16ull + h.nDep * 16ull;
    const bool ok = h.magic == kJitRecMagic && h.guestRip == rip && h.bodySize == body &&
                    (h.hostSize & 3u) == 0 && h.hostSize > 0 && h.hostSize <= kJitMaxHostBytes &&
                    off + sizeof(h) + body <= fileSize_.load(std::memory_order_acquire);
    if (!ok) rejected_.fetch_add(1, std::memory_order_relaxed);
    return ok;
}

bool JitDiskCache::readBody(uint64_t off, const RecHdr& h, JitRecord& out) {
    std::vector<uint8_t> buf(h.bodySize);
    if (!preadAll(fd_, buf.data(), buf.size(), off + sizeof(RecHdr)) || hashBytes(buf.data(), buf.size()) != h.bodyHash) {
        rejected_.fetch_add(1, std::memory_order_relaxed);
        return false;
    }
    out.guestRip = h.guestRip;
    out.guestHash = h.guestHash;
    out.guestSize = h.guestSize;
    out.cfg = h.cfg;
    out.endsBlock = h.endsBlock != 0;
    const uint8_t* p = buf.data();
    out.host.assign(p, p + h.hostSize);
    p += h.hostSize;
    out.instrMap.resize(h.nInstr);
    for (auto& e : out.instrMap) {
        std::memcpy(&e.first, p, 4);
        std::memcpy(&e.second, p + 4, 4);
        p += 8;
    }
    out.relocs.resize(h.nReloc);
    if (h.nReloc) std::memcpy(out.relocs.data(), p, h.nReloc * 16u);
    p += h.nReloc * 16u;
    out.deps.resize(h.nDep);
    if (h.nDep) std::memcpy(out.deps.data(), p, h.nDep * 16u);
    return true;
}

void JitDiskCache::append(JitRecord&& rec) {
    if (rec.host.empty() || rec.host.size() > kJitMaxHostBytes || rec.instrMap.size() > 0xFFFF ||
        rec.relocs.size() > 0xFFFF || rec.deps.size() > 0xFFFF) {
        return;
    }
    const uint64_t parts[3] = {rec.guestRip, rec.guestHash, rec.cfg};
    const uint64_t key = hashBytes(parts, sizeof(parts));
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (fd_ < 0 || stop_ || queue_.size() >= kJitMaxQueue) return;
        if (fileSize_.load(std::memory_order_relaxed) >= kMaxFileBytes) return;  // llena: se vacía al abrir
        if (!queuedKeys_.insert(key).second) return;  // otro hilo ya guardó esta traducción
        queue_.push_back(std::move(rec));
    }
    cv_.notify_one();
}

void JitDiskCache::flush() {
    std::unique_lock<std::mutex> lock(mutex_);
    drained_.wait(lock, [this] { return fd_ < 0 || (queue_.empty() && !writing_); });
}

void JitDiskCache::writerLoop() {
    std::vector<uint8_t> buf;
    std::vector<JitIdxEntry> idx;
    for (;;) {
        std::deque<JitRecord> batch;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            cv_.wait(lock, [this] { return stop_ || !queue_.empty(); });
            if (queue_.empty() && stop_) {
                drained_.notify_all();
                return;
            }
            batch.swap(queue_);
            writing_ = true;
        }
        buf.clear();
        idx.clear();
        uint64_t off = fileSize_.load(std::memory_order_acquire);
        const uint64_t start = off;
        for (const JitRecord& r : batch) {
            RecHdr h{};
            h.magic = kJitRecMagic;
            h.guestRip = r.guestRip;
            h.guestHash = r.guestHash;
            h.guestSize = r.guestSize;
            h.cfg = r.cfg;
            h.hostSize = static_cast<uint32_t>(r.host.size());
            h.nInstr = static_cast<uint16_t>(r.instrMap.size());
            h.nReloc = static_cast<uint16_t>(r.relocs.size());
            h.nDep = static_cast<uint16_t>(r.deps.size());
            h.endsBlock = r.endsBlock ? 1 : 0;
            const size_t hdrPos = buf.size();
            buf.resize(hdrPos + sizeof(h));
            const size_t bodyPos = buf.size();
            buf.insert(buf.end(), r.host.begin(), r.host.end());
            for (const auto& e : r.instrMap) {
                uint8_t w[8];
                std::memcpy(w, &e.first, 4);
                std::memcpy(w + 4, &e.second, 4);
                buf.insert(buf.end(), w, w + 8);
            }
            const auto* rp = reinterpret_cast<const uint8_t*>(r.relocs.data());
            buf.insert(buf.end(), rp, rp + r.relocs.size() * sizeof(JitReloc));
            const auto* dp = reinterpret_cast<const uint8_t*>(r.deps.data());
            buf.insert(buf.end(), dp, dp + r.deps.size() * sizeof(JitDep));
            h.bodySize = static_cast<uint32_t>(buf.size() - bodyPos);
            h.bodyHash = hashBytes(buf.data() + bodyPos, h.bodySize);
            std::memcpy(buf.data() + hdrPos, &h, sizeof(h));
            idx.push_back({r.guestRip, off});
            off += sizeof(h) + h.bodySize;
        }
        // Datos primero, índice después: un índice nunca apunta a datos sin escribir.
        bool ok = pwriteAll(fd_, buf.data(), buf.size(), start);
        if (ok) {
            struct stat ist {};
            fstat(idxFd_, &ist);
            ok = pwriteAll(idxFd_, idx.data(), idx.size() * sizeof(JitIdxEntry), static_cast<uint64_t>(ist.st_size));
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (ok) {
                fileSize_.store(off, std::memory_order_release);
                for (const auto& e : idx) fresh_.emplace(e.rip, e.off);
                written_.fetch_add(batch.size(), std::memory_order_relaxed);
            }
            writing_ = false;
        }
        drained_.notify_all();
    }
}

}  // namespace hyperplay
