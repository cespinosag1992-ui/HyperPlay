#include "TurnipDriverInspector.h"

#include <dlfcn.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>

namespace hyperplay {
namespace {

constexpr uint8_t kElfClass64 = 2;
constexpr uint8_t kElfDataLsb = 1;
constexpr uint16_t kEtDyn = 3;

constexpr uint32_t kPtLoad = 1;
constexpr uint32_t kPtDynamic = 2;
constexpr uint32_t kPtNote = 4;

constexpr uint32_t kShtStrtab = 3;
constexpr uint32_t kShtDynamic = 6;
constexpr uint32_t kShtDynsym = 11;

constexpr uint64_t kDtNull = 0;
constexpr uint64_t kDtNeeded = 1;
constexpr uint64_t kDtHash = 4;
constexpr uint64_t kDtStrtab = 5;
constexpr uint64_t kDtSymtab = 6;
constexpr uint64_t kDtStrsz = 10;
constexpr uint64_t kDtSyment = 11;
constexpr uint64_t kDtGnuHash = 0x6ffffef5;

constexpr uint16_t kShnUndef = 0;
constexpr uint8_t kStbGlobal = 1;
constexpr uint8_t kStbWeak = 2;
constexpr uint8_t kSttFunc = 2;
constexpr uint8_t kSttGnuIfunc = 10;
constexpr uint8_t kStvHidden = 2;
constexpr uint8_t kStvInternal = 1;

constexpr size_t kEhdrSize = 64;
constexpr size_t kPhdrSize = 56;
constexpr size_t kShdrSize = 64;
constexpr size_t kSymSize = 24;
constexpr size_t kDynSize = 16;

// Límites defensivos: un Turnip real está muy por debajo de todos.
constexpr size_t kMaxPhdrs = 256;
constexpr size_t kMaxShdrs = 4096;
constexpr size_t kMaxDynEntries = 4096;
constexpr size_t kMaxNeeded = 256;
constexpr uint64_t kMaxSymbols = 1u << 20;
constexpr uint64_t kMaxStrtab = 16u << 20;
constexpr uint64_t kMaxGnuBuckets = 1u << 20;
constexpr uint64_t kMaxBloomWords = 1u << 20;
constexpr uint64_t kMaxNoteBytes = 64u << 10;
constexpr size_t kSymChunk = 256;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) {
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

// [off, off+len) cabe dentro del archivo, sin desbordar.
bool inBounds(uint64_t off, uint64_t len, uint64_t size) { return off <= size && len <= size - off; }

struct Load {
    uint64_t offset;
    uint64_t vaddr;
    uint64_t filesz;
};

struct Range {
    uint64_t offset = 0;
    uint64_t size = 0;
    bool present = false;
};

struct Shdr {
    uint32_t type;
    uint64_t offset;
    uint64_t size;
    uint32_t link;
    uint64_t entsize;
};

// Traduce una dirección virtual a offset de archivo usando los PT_LOAD.
bool vaddrToOffset(const std::vector<Load>& loads, uint64_t vaddr, uint64_t& out) {
    for (const auto& l : loads) {
        if (vaddr >= l.vaddr && vaddr - l.vaddr < l.filesz) {
            const uint64_t delta = vaddr - l.vaddr;
            if (l.offset > std::numeric_limits<uint64_t>::max() - delta) return false;
            out = l.offset + delta;
            return true;
        }
    }
    return false;
}

// Cadena terminada en NUL dentro de la tabla; vacía si el offset es inválido.
std::string strAt(const std::vector<char>& table, uint64_t off) {
    if (off >= table.size()) return {};
    const char* start = table.data() + off;
    const size_t avail = table.size() - static_cast<size_t>(off);
    const void* nul = std::memchr(start, '\0', avail);
    if (nul == nullptr) return {};
    return std::string(start, static_cast<const char*>(nul) - start);
}

// Busca una nota ELF con owner "Android" (la añade el NDK, p. ej. .note.android.ident).
bool hasAndroidNote(const ElfReader& read, uint64_t fileSize, uint64_t offset, uint64_t size,
                    uint64_t align) {
    if (size == 0 || size > kMaxNoteBytes || !inBounds(offset, size, fileSize)) return false;
    std::vector<uint8_t> buf(static_cast<size_t>(size));
    if (!read(offset, buf.data(), buf.size())) return false;
    const uint64_t a = align == 8 ? 8 : 4;
    auto pad = [a](uint64_t v) { return (v + a - 1) & ~(a - 1); };
    uint64_t pos = 0;
    while (pos + 12 <= buf.size()) {
        const uint64_t namesz = rd32(buf.data() + pos);
        const uint64_t descsz = rd32(buf.data() + pos + 4);
        const uint64_t nameAt = pos + 12;
        if (namesz > buf.size() - nameAt) return false;
        if (namesz == 8 && std::memcmp(buf.data() + nameAt, "Android\0", 8) == 0) return true;
        const uint64_t descAt = nameAt + pad(namesz);
        if (descAt > buf.size() || descsz > buf.size() - descAt) return false;
        pos = descAt + pad(descsz);
    }
    return false;
}

// Número de símbolos de .dynsym a partir de DT_GNU_HASH (no hay campo explícito).
bool gnuHashSymbolCount(const ElfReader& read, uint64_t fileSize, uint64_t off, uint64_t& count) {
    uint8_t hdr[16];
    if (!inBounds(off, sizeof(hdr), fileSize) || !read(off, hdr, sizeof(hdr))) return false;
    const uint64_t nbuckets = rd32(hdr);
    const uint64_t symoffset = rd32(hdr + 4);
    const uint64_t bloomWords = rd32(hdr + 8);
    if (nbuckets == 0 || nbuckets > kMaxGnuBuckets || bloomWords > kMaxBloomWords) return false;

    const uint64_t bucketsOff = off + sizeof(hdr) + bloomWords * 8;
    if (bucketsOff < off || !inBounds(bucketsOff, nbuckets * 4, fileSize)) return false;
    std::vector<uint8_t> buckets(static_cast<size_t>(nbuckets * 4));
    if (!read(bucketsOff, buckets.data(), buckets.size())) return false;

    uint64_t maxSym = 0;
    for (size_t i = 0; i < nbuckets; ++i) maxSym = std::max<uint64_t>(maxSym, rd32(&buckets[i * 4]));
    if (maxSym < symoffset) {
        count = symoffset;
        return true;
    }

    // La última cadena termina en la entrada con el bit 0 activo.
    const uint64_t chainsOff = bucketsOff + nbuckets * 4;
    for (uint64_t idx = maxSym; idx - symoffset < kMaxSymbols; ++idx) {
        const uint64_t at = chainsOff + (idx - symoffset) * 4;
        uint8_t v[4];
        if (!inBounds(at, 4, fileSize) || !read(at, v, 4)) return false;
        if (rd32(v) & 1u) {
            count = idx + 1;
            return true;
        }
    }
    return false;
}

}  // namespace

DriverInspection inspectDriverElfWith(const ElfReader& read, uint64_t fileSize) {
    DriverInspection r;

    uint8_t eh[kEhdrSize];
    if (fileSize < kEhdrSize || !read(0, eh, sizeof(eh))) {
        r.error = "archivo demasiado corto para una cabecera ELF64";
        return r;
    }
    if (std::memcmp(eh, "\x7f" "ELF", 4) != 0) {
        r.error = "no es un ELF (firma incorrecta)";
        return r;
    }
    if (eh[4] != kElfClass64 || eh[5] != kElfDataLsb) {
        r.error = "no es ELF64 little-endian";
        return r;
    }
    const uint16_t type = rd16(eh + 16);
    const uint16_t machine = rd16(eh + 18);
    r.aarch64 = machine == kElfMachineAarch64;
    if (type != kEtDyn) {
        r.error = "no es una biblioteca compartida (ET_DYN)";
        return r;
    }
    r.validElf = true;

    const uint64_t phoff = rd64(eh + 32);
    const uint64_t shoff = rd64(eh + 40);
    const uint16_t phentsize = rd16(eh + 54);
    const uint16_t phnum = rd16(eh + 56);
    const uint16_t shentsize = rd16(eh + 58);
    const uint16_t shnum = rd16(eh + 60);

    // ---- Program headers: PT_LOAD (traducción de direcciones), PT_DYNAMIC, PT_NOTE ----
    std::vector<Load> loads;
    Range dynSeg;
    bool androidNote = false;
    if (phoff != 0 && phentsize >= kPhdrSize) {
        const size_t n = std::min<size_t>(phnum, kMaxPhdrs);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t at = phoff + i * phentsize;
            uint8_t ph[kPhdrSize];
            if (at < phoff || !inBounds(at, kPhdrSize, fileSize) || !read(at, ph, sizeof(ph))) break;
            const uint32_t ptype = rd32(ph);
            const uint64_t poff = rd64(ph + 8);
            const uint64_t pvaddr = rd64(ph + 16);
            const uint64_t pfilesz = rd64(ph + 32);
            const uint64_t palign = rd64(ph + 48);
            if (ptype == kPtLoad) {
                loads.push_back({poff, pvaddr, pfilesz});
            } else if (ptype == kPtDynamic && !dynSeg.present) {
                dynSeg = {poff, pfilesz, true};
            } else if (ptype == kPtNote && !androidNote) {
                androidNote = hasAndroidNote(read, fileSize, poff, pfilesz, palign);
            }
        }
    }

    // ---- Section headers (preferidos): .dynsym + su .dynstr, .dynamic ----
    std::vector<Shdr> sections;
    if (shoff != 0 && shnum != 0 && shentsize >= kShdrSize) {
        const size_t n = std::min<size_t>(shnum, kMaxShdrs);
        for (size_t i = 0; i < n; ++i) {
            const uint64_t at = shoff + i * shentsize;
            uint8_t sh[kShdrSize];
            if (at < shoff || !inBounds(at, kShdrSize, fileSize) || !read(at, sh, sizeof(sh))) {
                sections.clear();  // Tabla truncada: no se usa a medias.
                break;
            }
            sections.push_back({rd32(sh + 4), rd64(sh + 24), rd64(sh + 32), rd32(sh + 40), rd64(sh + 56)});
        }
    }

    Range symSec;
    uint64_t symEnt = kSymSize;
    Range strSec;
    Range dynSec;
    for (const auto& s : sections) {
        if (s.type == kShtDynsym && !symSec.present) {
            symSec = {s.offset, s.size, true};
            if (s.entsize != 0) symEnt = s.entsize;
            if (s.link < sections.size() && sections[s.link].type == kShtStrtab) {
                strSec = {sections[s.link].offset, sections[s.link].size, true};
            }
        } else if (s.type == kShtDynamic && !dynSec.present) {
            dynSec = {s.offset, s.size, true};
        }
    }

    // ---- Tabla dinámica ----
    const Range dyn = dynSec.present ? dynSec : dynSeg;
    std::vector<uint64_t> neededOffsets;
    uint64_t dtStrtab = 0, dtStrsz = 0, dtSymtab = 0, dtSyment = 0, dtHash = 0, dtGnuHash = 0;
    if (dyn.present) {
        const uint64_t count = std::min<uint64_t>(dyn.size / kDynSize, kMaxDynEntries);
        for (uint64_t i = 0; i < count; ++i) {
            const uint64_t at = dyn.offset + i * kDynSize;
            uint8_t d[kDynSize];
            if (at < dyn.offset || !inBounds(at, kDynSize, fileSize) || !read(at, d, sizeof(d))) break;
            const uint64_t tag = rd64(d);
            const uint64_t val = rd64(d + 8);
            if (tag == kDtNull) break;
            switch (tag) {
                case kDtNeeded:
                    if (neededOffsets.size() < kMaxNeeded) neededOffsets.push_back(val);
                    break;
                case kDtStrtab: dtStrtab = val; break;
                case kDtStrsz: dtStrsz = val; break;
                case kDtSymtab: dtSymtab = val; break;
                case kDtSyment: dtSyment = val; break;
                case kDtHash: dtHash = val; break;
                case kDtGnuHash: dtGnuHash = val; break;
                default: break;
            }
        }
    }

    // ---- Tabla de cadenas dinámica ----
    Range str = strSec;
    if (!str.present && dtStrtab != 0) {
        uint64_t off = 0;
        if (vaddrToOffset(loads, dtStrtab, off)) str = {off, dtStrsz, true};
    }
    std::vector<char> strtab;
    if (str.present) {
        const uint64_t size = std::min<uint64_t>(str.size, kMaxStrtab);
        if (size > 0 && inBounds(str.offset, size, fileSize)) {
            strtab.resize(static_cast<size_t>(size));
            if (!read(str.offset, strtab.data(), strtab.size())) strtab.clear();
        }
    }
    if (strtab.empty()) {
        r.error = "sin tabla de cadenas dinámica (.dynstr)";
        return r;
    }

    // ---- DT_NEEDED y tipo de libc ----
    bool glibc = false;
    for (uint64_t off : neededOffsets) {
        std::string name = strAt(strtab, off);
        if (name.empty()) continue;
        if (name == "libc.so") r.bionic = true;
        if (name == "libc.so.6" || name.rfind("ld-linux", 0) == 0) glibc = true;
        r.needed.push_back(std::move(name));
    }
    if (glibc) {
        r.bionic = false;
    } else if (androidNote) {
        r.bionic = true;
    }

    // ---- Tabla de símbolos dinámica ----
    uint64_t symOff = 0;
    uint64_t symCount = 0;
    if (symSec.present) {
        symOff = symSec.offset;
        symCount = symEnt >= kSymSize ? symSec.size / symEnt : 0;
    } else if (dtSymtab != 0 && vaddrToOffset(loads, dtSymtab, symOff)) {
        if (dtSyment != 0) symEnt = dtSyment;
        uint64_t hashOff = 0;
        if (dtHash != 0 && vaddrToOffset(loads, dtHash, hashOff)) {
            uint8_t h[8];
            if (inBounds(hashOff, sizeof(h), fileSize) && read(hashOff, h, sizeof(h))) symCount = rd32(h + 4);
        }
        if (symCount == 0 && dtGnuHash != 0 && vaddrToOffset(loads, dtGnuHash, hashOff)) {
            if (!gnuHashSymbolCount(read, fileSize, hashOff, symCount)) symCount = 0;
        }
    }
    if (symEnt < kSymSize || symEnt > 4096) {
        r.error = "tamaño de entrada de símbolo inválido";
        return r;
    }
    symCount = std::min<uint64_t>(symCount, kMaxSymbols);
    if (symOff > fileSize) symCount = 0;
    symCount = std::min<uint64_t>(symCount, (fileSize - std::min(symOff, fileSize)) / symEnt);
    if (symCount == 0) {
        r.error = "sin tabla de símbolos dinámica (.dynsym)";
        return r;
    }

    std::vector<uint8_t> chunk(static_cast<size_t>(kSymChunk * symEnt));
    for (uint64_t base = 0; base < symCount && !(r.hasIcdEntry && r.hasNegotiate); base += kSymChunk) {
        const uint64_t n = std::min<uint64_t>(kSymChunk, symCount - base);
        const uint64_t at = symOff + base * symEnt;
        if (!read(at, chunk.data(), static_cast<size_t>(n * symEnt))) break;
        for (uint64_t i = 0; i < n; ++i) {
            const uint8_t* s = chunk.data() + i * symEnt;
            const uint8_t info = s[4];
            const uint8_t vis = s[5] & 0x3;
            const uint16_t shndx = rd16(s + 6);
            const uint8_t bind = info >> 4;
            const uint8_t stype = info & 0xf;
            if (shndx == kShnUndef) continue;
            if (bind != kStbGlobal && bind != kStbWeak) continue;
            if (stype != kSttFunc && stype != kSttGnuIfunc) continue;
            if (vis == kStvHidden || vis == kStvInternal) continue;
            const std::string name = strAt(strtab, rd32(s));
            if (name == kIcdEntrySymbol) r.hasIcdEntry = true;
            if (name == kIcdNegotiateSymbol) r.hasNegotiate = true;
        }
    }
    return r;
}

DriverInspection inspectDriverElfBuffer(const uint8_t* data, size_t size) {
    auto reader = [data, size](uint64_t offset, void* dst, size_t len) {
        if (offset > size || len > size - offset) return false;
        std::memcpy(dst, data + offset, len);
        return true;
    };
    return inspectDriverElfWith(reader, size);
}

DriverInspection inspectDriverElf(const std::string& path) {
    DriverInspection r;
    const int fd = ::open(path.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        r.error = "no se puede abrir: " + std::string(std::strerror(errno));
        return r;
    }
    struct stat st {};
    if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        ::close(fd);
        r.error = "no es un archivo regular";
        return r;
    }
    const uint64_t fileSize = static_cast<uint64_t>(st.st_size);
    auto reader = [fd, fileSize](uint64_t offset, void* dst, size_t len) {
        if (offset > fileSize || len > fileSize - offset) return false;
        size_t done = 0;
        auto* out = static_cast<uint8_t*>(dst);
        while (done < len) {
            const ssize_t n = ::pread(fd, out + done, len - done, static_cast<off_t>(offset + done));
            if (n <= 0) return false;
            done += static_cast<size_t>(n);
        }
        return true;
    };
    r = inspectDriverElfWith(reader, fileSize);
    ::close(fd);
    return r;
}

bool dlopenCheck(const std::string& path, std::string& error) {
    error.clear();
    dlerror();  // Limpia errores previos.
    void* handle = ::dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* e = dlerror();
        error = e != nullptr ? e : "dlopen falló sin mensaje";
        return false;
    }
    void* sym = ::dlsym(handle, kIcdEntrySymbol);
    if (sym == nullptr) {
        const char* e = dlerror();
        error = e != nullptr ? e : std::string("falta ") + kIcdEntrySymbol;
        ::dlclose(handle);
        return false;
    }
    ::dlclose(handle);
    return true;
}

namespace {

// Lee un archivo pequeño de sysfs y recorta espacios y saltos de línea.
std::string readSmallFile(const char* path) {
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0) return {};
    char buf[128];
    const ssize_t n = ::read(fd, buf, sizeof(buf));
    ::close(fd);
    if (n <= 0) return {};
    std::string s(buf, static_cast<size_t>(n));
    const auto notSpace = [](char c) { return c != ' ' && c != '\n' && c != '\r' && c != '\t' && c != '\0'; };
    const auto first = std::find_if(s.begin(), s.end(), notSpace);
    const auto last = std::find_if(s.rbegin(), s.rend(), notSpace).base();
    return first < last ? std::string(first, last) : std::string();
}

}  // namespace

std::string adrenoModel() {
    std::string model = readSmallFile("/sys/class/kgsl/kgsl-3d0/gpu_model");
    if (!model.empty()) return model;
    const std::string chip = readSmallFile("/sys/class/kgsl/kgsl-3d0/gpu_chipid");
    return chip.empty() ? std::string() : "chipid:" + chip;
}

bool kgslAccessible() { return ::access("/dev/kgsl-3d0", R_OK | W_OK) == 0; }

}  // namespace hyperplay
