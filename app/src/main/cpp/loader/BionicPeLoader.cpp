#include "BionicPeLoader.h"
#include "ManagedHost.h"
#include "NativeDllInterceptor.h"
#include "cpu/OryonEmitterBackend.h"

#include <android/log.h>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/system_properties.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstring>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

#define LOG_TAG "HyperPlay.PeLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace {

constexpr uint32_t kScnMemExecute = 0x20000000;
constexpr uint32_t kScnMemWrite   = 0x80000000;

// Base relocations (IMAGE_REL_BASED_*).
constexpr uint16_t kRelAbsolute  = 0;
constexpr uint16_t kRelHigh      = 1;
constexpr uint16_t kRelLow       = 2;
constexpr uint16_t kRelHighLow   = 3;
constexpr uint16_t kRelHighAdj   = 4;
constexpr uint16_t kRelDir64     = 10;

// Límites defensivos: un PE real nunca llega aquí; evita reservar gigabytes por una cabecera corrupta.
constexpr uint64_t kMaxImageSize32 = 0xC0000000ull;  // 3 GiB
constexpr uint64_t kMaxImageSize64 = 8ull * 1024 * 1024 * 1024;

size_t pageSize() {
    const long ps = sysconf(_SC_PAGESIZE);
    return ps > 0 ? static_cast<size_t>(ps) : 4096;
}

size_t alignUp(size_t value, size_t alignment) {
    if (alignment == 0) return value;
    return (value + alignment - 1) & ~(alignment - 1);
}

std::string getParentDir(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    if (slash == std::string::npos) return ".";
    return path.substr(0, slash);
}

// Copia sin depender de accesos no alineados (AArch64 permite EL0 no alineado, pero el compilador
// puede asumir alineación al desreferenciar un uint32_t*: memcpy es la forma correcta en C++).
uint16_t loadU16(const uint8_t* p) {
    uint16_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
uint32_t loadU32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
uint64_t loadU64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof(v));
    return v;
}
void storeU16(uint8_t* p, uint16_t v) { std::memcpy(p, &v, sizeof(v)); }
void storeU32(uint8_t* p, uint32_t v) { std::memcpy(p, &v, sizeof(v)); }
void storeU64(uint8_t* p, uint64_t v) { std::memcpy(p, &v, sizeof(v)); }

bool readExact(int fd, void* dst, size_t len, off_t offset) {
    auto* out = static_cast<uint8_t*>(dst);
    size_t done = 0;
    while (done < len) {
        const ssize_t n = pread(fd, out + done, len - done, offset + static_cast<off_t>(done));
        if (n < 0) {
            if (errno == EINTR) continue;
            return false;
        }
        if (n == 0) return false;  // archivo truncado
        done += static_cast<size_t>(n);
    }
    return true;
}

int protFor(uint32_t characteristics) {
    int prot = PROT_READ;
    if ((characteristics & kScnMemWrite) != 0) prot |= PROT_WRITE;
    if ((characteristics & kScnMemExecute) != 0) prot |= PROT_EXEC;
    return prot;
}

}  // namespace

LoadedPeImage::~LoadedPeImage() {
    std::unordered_set<void*> unmappedBases;
    for (const auto& entry : localDlls) {
        if (entry.second.base != nullptr && entry.second.size > 0) {
            if (unmappedBases.insert(entry.second.base).second) {
                munmap(entry.second.base, entry.second.size);
            }
        }
    }
    localDlls.clear();
    if (!borrowsArena && baseAddress != nullptr && totalMappedSize > 0) {
        munmap(baseAddress, totalMappedSize);
    }
    baseAddress = nullptr;
    entryPoint = nullptr;
    totalMappedSize = 0;
}

BionicPeLoader::BionicPeLoader() = default;
BionicPeLoader::~BionicPeLoader() = default;

void* BionicPeLoader::allocateArena(size_t size, bool is32Bit, uint64_t preferredBase) {
    const size_t ps = pageSize();
    if (size == 0) return nullptr;
    const size_t allocSize = alignUp(size, ps);
    if (allocSize < size) return nullptr;  // desbordamiento al alinear

    void* ptr = MAP_FAILED;
    const int prot = PROT_READ | PROT_WRITE;

    if (is32Bit) {
        // Un proceso x86 emulado solo puede direccionar 32 bits: la arena debe quedar por debajo
        // de 4 GiB. Con MAP_FIXED_NOREPLACE el kernel usa exactamente la dirección pedida o falla
        // (sin pisar nada); antes, con una simple sugerencia ocupada devolvía una dirección alta,
        // se descartaba y, tras 7 intentos, la DLL acababa por encima de 4 GiB (DmC: PhysX).
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
        auto tryAt = [&](uint64_t addr) -> void* {
            if (addr == 0 || addr + allocSize > 0x100000000ull) return MAP_FAILED;
            void* p = mmap(reinterpret_cast<void*>(static_cast<uintptr_t>(addr)), allocSize, prot,
                           MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
            if (p != MAP_FAILED && reinterpret_cast<uintptr_t>(p) != addr) {
                munmap(p, allocSize);  // kernel antiguo que ignora la bandera
                return MAP_FAILED;
            }
            return p;
        };
        if (preferredBase > 0) ptr = tryAt(preferredBase);
        // Huecos libres en pasos de 16 MiB por debajo de 4 GiB (LARGE_ADDRESS_AWARE).
        for (uint64_t trial = 0x10000000; ptr == MAP_FAILED && trial < 0x100000000ull; trial += 0x1000000) {
            ptr = tryAt(trial);
        }
        if (ptr == MAP_FAILED) {
            LOGE("Sin hueco de %zu bytes por debajo de 4 GiB para el proceso de 32 bits", allocSize);
            return nullptr;
        }
    } else {
        void* hint = preferredBase != 0 ? reinterpret_cast<void*>(static_cast<uintptr_t>(preferredBase)) : nullptr;
        ptr = mmap(hint, allocSize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }

    if (ptr == MAP_FAILED) {
        ptr = mmap(nullptr, allocSize, prot, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    }
    if (ptr == MAP_FAILED) {
        LOGE("mmap(%zu) falló: %s", allocSize, std::strerror(errno));
        return nullptr;
    }
    return ptr;
}

bool BionicPeLoader::mapSections(int fd, const PeInfo& info, LoadedPeImage& image) {
    auto* base = static_cast<uint8_t*>(image.baseAddress);
    const size_t mapped = image.totalMappedSize;
    if (base == nullptr || mapped == 0) return false;

    // Cabeceras (necesarias para GetModuleHandle/PEB y para introspección del propio juego).
    const size_t headersSize = std::min<size_t>(info.headersSize, mapped);
    if (headersSize > 0 && !readExact(fd, base, headersSize, 0)) {
        LOGE("No se pudieron leer las cabeceras del PE (%zu bytes)", headersSize);
        return false;
    }

    // Se copia todo con PROT_READ|PROT_WRITE y se aplican las protecciones al final: las
    // reubicaciones escriben en secciones que luego pueden quedar de solo lectura.
    for (const auto& sec : info.sections) {
        const uint64_t start = sec.virtualAddress;
        if (start >= mapped) {
            LOGW("Sección %s (RVA 0x%x) fuera de la imagen mapeada (%zu bytes); se omite",
                 sec.name.c_str(), sec.virtualAddress, mapped);
            continue;
        }
        const size_t sectionSpan = static_cast<size_t>(
            std::min<uint64_t>({static_cast<uint64_t>(std::max(sec.virtualSize, sec.rawSize)),
                                static_cast<uint64_t>(mapped) - start, mapped}));

        uint8_t* dest = base + start;
        size_t copied = 0;
        if (sec.rawSize > 0 && sec.rawPointer > 0 && sectionSpan > 0) {
            copied = std::min<size_t>(sec.rawSize, sectionSpan);
            if (!readExact(fd, dest, copied, static_cast<off_t>(sec.rawPointer))) {
                // Un PE truncado no se puede ejecutar con ceros en el código: fallar en vez de
                // arrancar una imagen corrupta que fallará más tarde sin explicación.
                LOGE("Sección %s truncada: no se pudieron copiar %zu bytes desde 0x%x",
                     sec.name.c_str(), copied, sec.rawPointer);
                return false;
            }
        }
        // BSS (y cola no inicializada): mmap ya entrega ceros, pero la sección puede solaparse
        // con otra copia previa; se limpia explícitamente.
        if (sectionSpan > copied) {
            std::memset(dest + copied, 0, sectionSpan - copied);
        }

        LoadedSection ls;
        ls.name = sec.name;
        ls.virtualAddress = dest;
        ls.size = sectionSpan;
        ls.characteristics = sec.characteristics;
        image.sections.push_back(ls);
    }
    return true;
}

bool BionicPeLoader::protectSections(const PeInfo& info, const LoadedPeImage& image) {
    auto* base = static_cast<uint8_t*>(image.baseAddress);
    if (base == nullptr) return false;
    const size_t ps = pageSize();
    const uintptr_t baseAddr = reinterpret_cast<uintptr_t>(base);
    const uintptr_t endAddr = baseAddr + image.totalMappedSize;

    // Cabeceras: nunca ejecutables.
    const size_t headersSize = std::min<size_t>(info.headersSize, image.totalMappedSize);
    if (headersSize > 0) {
        const size_t len = std::min<size_t>(alignUp(headersSize, ps), image.totalMappedSize);
        if (mprotect(base, len, PROT_READ) != 0) {
            LOGW("mprotect(cabeceras, R) falló: %s", std::strerror(errno));
        }
    }

    for (const auto& ls : image.sections) {
        const uintptr_t start = reinterpret_cast<uintptr_t>(ls.virtualAddress);
        if (start < baseAddr || start >= endAddr) continue;
        const uintptr_t rawEnd = start + ls.size;
        const uintptr_t end = std::min<uintptr_t>(rawEnd, endAddr);
        if (end <= start) continue;
        uintptr_t pageStart = start & ~(static_cast<uintptr_t>(ps) - 1);
        // No proteger de nuevo las cabeceras con los permisos de la primera sección.
        if (pageStart < baseAddr + headersSize) {
            pageStart = ((baseAddr + headersSize) + ps - 1) & ~(static_cast<uintptr_t>(ps) - 1);
        }
        if (pageStart >= end) continue;
        const size_t len = alignUp(static_cast<size_t>(end - pageStart), ps);
        const int prot = protFor(ls.characteristics);
        if (mprotect(reinterpret_cast<void*>(pageStart), len, prot) != 0) {
            LOGW("mprotect(%s, 0x%x) falló: %s", ls.name.c_str(), static_cast<unsigned>(prot),
                 std::strerror(errno));
        }
    }
    return true;
}

bool BionicPeLoader::applyRelocations(LoadedPeImage& image) {
    const int64_t delta = image.relocationDelta;
    if (delta == 0) return true;  // cargada en su base preferida

    const PeInfo& info = image.peInfo;
    auto* base = static_cast<uint8_t*>(image.baseAddress);
    const size_t mapped = image.totalMappedSize;
    if (base == nullptr || mapped == 0) return false;

    uint64_t relocRva = info.relocationRva;
    uint64_t relocSize = info.relocationSize;
    if (relocSize == 0) {
        // Sin directorio de reubicaciones utilizable: buscar la sección .reloc.
        for (const auto& sec : info.sections) {
            if (sec.name == ".reloc" && sec.virtualSize > 0) {
                relocRva = sec.virtualAddress;
                relocSize = sec.virtualSize;
                break;
            }
        }
    }
    // Sin reubicaciones y sin IMAGE_FILE_RELOCS_STRIPPED (0x0001): no hay nada que corregir y,
    // como en Windows, se puede cargar en cualquier base (p. ej. los shims x64 de HyperPlay, todo
    // relativo a RIP, que chocan con la base 0x180000000 de mono-2.0-sgen.dll).
    if (relocSize == 0 && (info.characteristics & 0x0001u) == 0) return true;
    if (relocSize == 0 || relocRva >= mapped) {
        LOGE("PE reubicable sin tabla .reloc válida (base 0x%llx, delta %lld): no se puede cargar",
             static_cast<unsigned long long>(info.imageBase), static_cast<long long>(delta));
        return false;
    }
    relocSize = std::min<uint64_t>(relocSize, static_cast<uint64_t>(mapped) - relocRva);

    const uint8_t* data = base + relocRva;
    size_t offset = 0;
    size_t applied = 0;
    size_t skipped = 0;

    while (offset + 8 <= relocSize) {
        const uint32_t pageRva = loadU32(data + offset);
        const uint32_t blockSize = loadU32(data + offset + 4);
        if (blockSize < 8 || (blockSize & 1u) != 0 || offset + blockSize > relocSize) break;

        const size_t entryCount = (blockSize - 8) / 2;
        for (size_t i = 0; i < entryCount; ++i) {
            const uint16_t entry = loadU16(data + offset + 8 + i * 2);
            const uint16_t type = static_cast<uint16_t>(entry >> 12);
            // Suma en 64 bits: pageRva + 0xFFF puede desbordar uint32_t y apuntar a otra zona.
            const uint64_t targetRva = static_cast<uint64_t>(pageRva) + (entry & 0x0FFFu);
            if (type == kRelAbsolute) continue;

            switch (type) {
                case kRelHighLow: {
                    if (targetRva + 4 > mapped) { ++skipped; break; }
                    uint8_t* p = base + targetRva;
                    const uint32_t value = static_cast<uint32_t>(
                        static_cast<int64_t>(loadU32(p)) + delta);
                    storeU32(p, value);
                    ++applied;
                    break;
                }
                case kRelDir64: {
                    if (targetRva + 8 > mapped) { ++skipped; break; }
                    uint8_t* p = base + targetRva;
                    const uint64_t value = static_cast<uint64_t>(
                        static_cast<int64_t>(loadU64(p)) + delta);
                    storeU64(p, value);
                    ++applied;
                    break;
                }
                case kRelHigh: {
                    if (targetRva + 2 > mapped) { ++skipped; break; }
                    uint8_t* p = base + targetRva;
                    const uint16_t value = static_cast<uint16_t>(
                        static_cast<int64_t>(loadU16(p)) + ((delta >> 16) & 0xFFFF));
                    storeU16(p, value);
                    ++applied;
                    break;
                }
                case kRelLow: {
                    if (targetRva + 2 > mapped) { ++skipped; break; }
                    uint8_t* p = base + targetRva;
                    storeU16(p, static_cast<uint16_t>(static_cast<int64_t>(loadU16(p)) + delta));
                    ++applied;
                    break;
                }
                case kRelHighAdj: {
                    // Consume el siguiente entry de 16 bits como addend alto.
                    if (i + 1 >= entryCount) { ++skipped; break; }
                    const int16_t extra = static_cast<int16_t>(loadU16(data + offset + 8 + (i + 1) * 2));
                    ++i;
                    if (targetRva + 2 > mapped) { ++skipped; break; }
                    uint8_t* p = base + targetRva;
                    int64_t value = static_cast<int64_t>(loadU16(p)) << 16;
                    value += delta + (static_cast<int64_t>(extra) << 16);
                    // IMAGE_REL_BASED_HIGHADJ redondea al alza antes de truncar a 16 bits.
                    storeU16(p, static_cast<uint16_t>((value + 0x8000) >> 16));
                    ++applied;
                    break;
                }
                default:
                    ++skipped;
                    break;
            }
        }
        offset += blockSize;
    }

    if (skipped > 0) {
        LOGW("Reubicaciones aplicadas: %zu, omitidas (tipo no soportado o fuera de imagen): %zu", applied, skipped);
    }
    return true;
}

void BionicPeLoader::setupSaveRedirection(LoadedPeImage& image) {
    // Juego .NET: el ejecutable es mono.exe (en el runtime) pero D:\ es la carpeta del juego.
    image.gameDir = managed::active() ? managed::g_gameDir : getParentDir(image.filePath);
    image.savesDir = image.gameDir + "/_HyperPlaySaves";
    mkdir(image.savesDir.c_str(), 0755);
    mkdir((image.savesDir + "/AppData").c_str(), 0755);
    mkdir((image.savesDir + "/LocalLow").c_str(), 0755);
    mkdir((image.savesDir + "/Documents").c_str(), 0755);
    mkdir((image.savesDir + "/Documents/My Games").c_str(), 0755);
    mkdir((image.savesDir + "/hypercache").c_str(), 0755);
    // Caché JIT persistente: la 2.ª ejecución reutiliza las traducciones de la 1.ª.
    JitDiskCache::setDirectory(image.savesDir + "/hypercache");
    LOGI("Directorio de guardado portable: %s", image.savesDir.c_str());
}

std::unique_ptr<LoadedPeImage> BionicPeLoader::loadExecutable(const std::string& exePath) {
    const int fd = open(exePath.c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) {
        LOGE("No se pudo abrir el ejecutable %s: %s", exePath.c_str(), std::strerror(errno));
        return nullptr;
    }

    PeInfo info = inspectPeFile(exePath);
    if (!info.ok()) {
        LOGE("Cabecera PE inválida en %s (estado %d)", exePath.c_str(), static_cast<int>(info.status));
        close(fd);
        return nullptr;
    }
    if ((info.characteristics & kFileDll) != 0) {
        LOGE("El archivo es una DLL, no un ejecutable: %s", exePath.c_str());
        close(fd);
        return nullptr;
    }
    const bool machineOk = (info.machine == kMachineI386 && !info.is64Bit) ||
                           (info.machine == kMachineAmd64 && info.is64Bit);
    if (!machineOk) {
        LOGE("Arquitectura no soportada (machine 0x%04x, PE32+ %d): %s", info.machine, info.is64Bit ? 1 : 0,
             exePath.c_str());
        close(fd);
        return nullptr;
    }

    const uint64_t limit = info.is64Bit ? kMaxImageSize64 : kMaxImageSize32;
    if (info.imageSize == 0 || info.imageSize > limit || info.headersSize > info.imageSize) {
        LOGE("Tamaño de imagen inválido (SizeOfImage %u, SizeOfHeaders %u): %s", info.imageSize, info.headersSize,
             exePath.c_str());
        close(fd);
        return nullptr;
    }
    const size_t ps = pageSize();
    (void)ps;
    constexpr uint64_t kWinAllocGranularity = 65536;
    constexpr uint64_t kGuardHeadroom = 1048576; // 1MB de margen relleno de ceros para escaneos vectoriales
    const uint64_t alignedSize = ((static_cast<uint64_t>(info.imageSize) + kWinAllocGranularity - 1) &
                                  ~static_cast<uint64_t>(kWinAllocGranularity - 1)) + kGuardHeadroom;
    if (alignedSize == 0 || alignedSize > limit) {
        close(fd);
        return nullptr;
    }

    auto image = std::make_unique<LoadedPeImage>();
    image->filePath = exePath;
    image->peInfo = info;
    image->is32Bit = !info.is64Bit;
    image->originalImageBase = info.imageBase;
    image->totalMappedSize = static_cast<size_t>(alignedSize);

    image->baseAddress = allocateArena(image->totalMappedSize, image->is32Bit, info.imageBase);
    if (image->baseAddress == nullptr) {
        LOGE("Sin memoria para la arena de %zu bytes", image->totalMappedSize);
        close(fd);
        return nullptr;
    }

    image->relocationDelta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(image->baseAddress)) -
                             static_cast<int64_t>(info.imageBase);

    if (!mapSections(fd, info, *image) || !applyRelocations(*image) || !protectSections(info, *image)) {
        close(fd);
        return nullptr;
    }

    if (info.entryPointRva >= image->totalMappedSize) {
        LOGE("AddressOfEntryPoint 0x%x fuera de la imagen (%zu bytes)", info.entryPointRva,
             image->totalMappedSize);
        close(fd);
        return nullptr;
    }
    setupSaveRedirection(*image);
    image->entryPoint = static_cast<uint8_t*>(image->baseAddress) + info.entryPointRva;
    close(fd);

    LOGI("PE cargado en %p (RVA base 0x%llx, %zu bytes, %s, entrada %p)", image->baseAddress,
         static_cast<unsigned long long>(info.imageBase), image->totalMappedSize,
         image->is32Bit ? "PE32" : "PE32+", image->entryPoint);

    OryonEmitterBackend::addStaticImage(reinterpret_cast<uintptr_t>(image->baseAddress), image->totalMappedSize);
    if (auto* b = OryonEmitterBackend::activeBackend(); b != nullptr) {
        const std::string name = exePath.find_last_of("/\\") != std::string::npos
                                     ? exePath.substr(exePath.find_last_of("/\\") + 1)
                                     : exePath;
        b->registerModule(name, reinterpret_cast<uintptr_t>(image->baseAddress), image->totalMappedSize);
    }
    return image;
}

void* BionicPeLoader::loadLocalDll(LoadedPeImage& image, const std::string& dllName) {
    if (dllName.empty()) return nullptr;

    // Normalizar separadores '\' -> '/'
    std::string norm;
    norm.reserve(dllName.size());
    for (char c : dllName) {
        if (c == '\\') norm.push_back('/');
        else if (c != '\0' && c != '\"') norm.push_back(c);
    }

    // Seguridad: no permitir escapes de directorio '..'
    if (norm == "." || norm == ".." || norm.find("/../") != std::string::npos ||
        norm.rfind("../", 0) == 0 || (norm.size() >= 3 && norm.substr(norm.size() - 3) == "/..")) {
        LOGW("DLL local con path traversal no permitido: %s", dllName.c_str());
        return nullptr;
    }

    const size_t slash = norm.find_last_of('/');
    const std::string baseName = (slash != std::string::npos) ? norm.substr(slash + 1) : norm;
    std::string lowerBase = baseName;
    for (char& c : lowerBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

    // Si ya está cargada bajo cualquiera de sus variantes:
    if (auto it = image.localDlls.find(dllName); it != image.localDlls.end()) return it->second.base;
    if (auto it = image.localDlls.find(norm); it != image.localDlls.end()) return it->second.base;
    if (auto it = image.localDlls.find(baseName); it != image.localDlls.end()) return it->second.base;
    if (auto it = image.localDlls.find(lowerBase); it != image.localDlls.end()) return it->second.base;

    // Construcción de rutas candidatas para localizar el binario en disco
    std::vector<std::string> candidatePaths;
    // 0. Juego .NET: primero las DLL x64 del runtime (FNA y shims de Steam/FMOD), que sustituyen a
    //    las de 32 bits que trae el juego (CSteamworks.dll, fmod.dll...).
    if (const std::string user = managed::userOverride(lowerBase); !user.empty()) candidatePaths.push_back(user);
    if (managed::active()) candidatePaths.push_back(managed::g_runtimeDir + "/fna/" + baseName);

    // 1. Si norm especifica una ruta virtual o con letra de unidad ("D:/..."), resolver con resolveVirtualPath
    if (norm.size() >= 2 && (norm[1] == ':' || norm[0] == '/')) {
        candidatePaths.push_back(resolveVirtualPath(image, norm));
    }
    // 2. Ruta relativa exacta respecto a gameDir (p.ej. "MonoBleedingEdge/EmbedRuntime/mono-2.0-bdwgc.dll")
    if (slash != std::string::npos) {
        candidatePaths.push_back(image.gameDir + "/" + norm);
    }
    // 3. En la raíz de gameDir con baseName
    candidatePaths.push_back(image.gameDir + "/" + baseName);
    // 3b. Junto al .exe principal (Mono: mono.exe y mono-2.0-sgen.dll en el runtime).
    if (const std::string exeDir = getParentDir(image.filePath); exeDir != image.gameDir) {
        candidatePaths.push_back(exeDir + "/" + baseName);
    }

    // 4. Subcarpetas estándar de motores Windows conocidos (Unity, Unreal Engine, plugins)
    candidatePaths.push_back(image.gameDir + "/MonoBleedingEdge/EmbedRuntime/" + baseName);
    candidatePaths.push_back(image.gameDir + "/Mono/EmbedRuntime/" + baseName);
    candidatePaths.push_back(image.gameDir + "/Binaries/Win64/" + baseName);
    candidatePaths.push_back(image.gameDir + "/Binaries/" + baseName);
    candidatePaths.push_back(image.gameDir + "/Plugins/x86_64/" + baseName);
    candidatePaths.push_back(image.gameDir + "/Plugins/" + baseName);

    // Si el ejecutable tiene su carpeta <stem>_Data (p.ej. hollow_knight_Data)
    const size_t exeSlash = image.filePath.find_last_of("/\\");
    const std::string exeFile = (exeSlash != std::string::npos) ? image.filePath.substr(exeSlash + 1) : image.filePath;
    const size_t dotPos = exeFile.find_last_of('.');
    const std::string stem = (dotPos != std::string::npos) ? exeFile.substr(0, dotPos) : exeFile;
    if (!stem.empty()) {
        candidatePaths.push_back(image.gameDir + "/" + stem + "_Data/Plugins/x86_64/" + baseName);
        candidatePaths.push_back(image.gameDir + "/" + stem + "_Data/Plugins/" + baseName);
        candidatePaths.push_back(image.gameDir + "/" + stem + "_Data/Managed/" + baseName);
    }

    std::string resolvedPath;
    int fd = -1;
    for (const auto& path : candidatePaths) {
        fd = open(path.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd >= 0) {
            resolvedPath = path;
            break;
        }
    }

    // 5. Búsqueda acotada (profundidad <= 3) en gameDir con un índice nombre->ruta construido una
    //    sola vez. Antes se recorría el árbol en cada fallo: Mono sondea cada ensamblado como DLL
    //    nativa ("mscorlib.dll.dll", "aot-cache/amd64/...") y cada sondeo costaba ~200 ms sobre el
    //    FUSE de /sdcard (~40 s del arranque de Hollow Knight).
    if (fd < 0) {
        static std::mutex s_indexMutex;
        static std::unordered_map<std::string, std::unordered_map<std::string, std::string>> s_index;
        std::string found;
        {
            std::lock_guard<std::mutex> lock(s_indexMutex);
            auto [it, fresh] = s_index.try_emplace(image.gameDir);
            auto& index = it->second;
            if (fresh) {
                auto scan = [&](auto& self, const std::string& dir, int depth) -> void {
                    if (depth > 3) return;
                    DIR* d = opendir(dir.c_str());
                    if (!d) return;
                    struct dirent* ent;
                    while ((ent = readdir(d)) != nullptr) {
                        if (ent->d_name[0] == '.') continue;
                        const std::string entryName = ent->d_name;
                        const std::string subPath = dir + "/" + entryName;
                        if (ent->d_type == DT_DIR) {
                            if (entryName != "_HyperPlaySaves") self(self, subPath, depth + 1);
                        } else if (ent->d_type == DT_REG || ent->d_type == DT_UNKNOWN) {
                            std::string entryLower = entryName;
                            for (char& c : entryLower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                            index.try_emplace(entryLower, subPath);  // primero encontrado, como antes
                        }
                    }
                    closedir(d);
                };
                scan(scan, image.gameDir, 1);
                LOGI("Índice de archivos del juego: %zu entradas", index.size());
            }
            if (auto f = index.find(lowerBase); f != index.end()) found = f->second;
        }
        if (!found.empty()) {
            fd = open(found.c_str(), O_RDONLY | O_CLOEXEC);
            if (fd >= 0) resolvedPath = found;
        }
    }

    if (fd < 0) {
        LOGW("No se pudo localizar DLL local en disco: %s (base %s)", dllName.c_str(), baseName.c_str());
        return nullptr;
    }

    LOGI("Cargando DLL local '%s' desde: %s", dllName.c_str(), resolvedPath.c_str());

    PeInfo info = inspectPeFile(resolvedPath);
    if (!info.ok() || info.imageSize == 0 || info.headersSize > info.imageSize ||
        (info.machine != kMachineI386 && info.machine != kMachineAmd64)) {
        LOGW("DLL local inválida: %s", resolvedPath.c_str());
        close(fd);
        return nullptr;
    }
    // Como Windows (ERROR_BAD_EXE_FORMAT): una DLL de 32 bits no se carga en un proceso de 64 (y al
    // revés). Antes se mapeaba igual y su código x86 se ejecutaba como x86-64 (basura). Celeste en
    // Mono x64 trae fmod.dll de 32 bits: LoadLibrary falla limpio y el juego lo ve.
    if (info.is64Bit == image.is32Bit) {
        LOGW("DLL local de %d bits en un proceso de %d bits, no se carga: %s", info.is64Bit ? 64 : 32,
             image.is32Bit ? 32 : 64, resolvedPath.c_str());
        close(fd);
        return nullptr;
    }
#if defined(__ANDROID__)
    // Experimento (setprop debug.hyperplay.nomanagedload 1): los ensamblados .NET no se cargan
    // como DLL nativa; CoreCLR los mapea él mismo (CreateFileMapping SEC_IMAGE).
    if (info.clrRva != 0) {
        char v[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.hyperplay.nomanagedload", v) > 0 && v[0] == '1') {
            LOGI("Ensamblado .NET no cargado como DLL nativa (experimento): %s", resolvedPath.c_str());
            close(fd);
            return nullptr;
        }
    }
#endif

    const size_t ps = pageSize();
    (void)ps;
    constexpr uint64_t kWinAllocGranularity = 65536;
    constexpr uint64_t kGuardHeadroom = 1048576; // 1MB de margen relleno de ceros para escaneos vectoriales
    const uint64_t alignedSize = ((static_cast<uint64_t>(info.imageSize) + kWinAllocGranularity - 1) &
                                  ~static_cast<uint64_t>(kWinAllocGranularity - 1)) + kGuardHeadroom;
    if (alignedSize == 0 || alignedSize > kMaxImageSize64) {
        close(fd);
        return nullptr;
    }
    void* base = allocateArena(static_cast<size_t>(alignedSize), image.is32Bit, info.imageBase);
    if (base == nullptr) {
        close(fd);
        return nullptr;
    }

    LoadedPeImage dllModule;
    dllModule.is32Bit = !info.is64Bit;  // la IAT de 32 bits tiene ranuras de 4 bytes
    dllModule.borrowsArena = true;  // La arena la libera `image.localDlls`.
    dllModule.baseAddress = base;
    dllModule.totalMappedSize = static_cast<size_t>(alignedSize);
    dllModule.peInfo = info;
    dllModule.relocationDelta = static_cast<int64_t>(reinterpret_cast<uintptr_t>(base)) -
                                static_cast<int64_t>(info.imageBase);

    const bool ok = mapSections(fd, info, dllModule) && applyRelocations(dllModule) &&
                    protectSections(info, dllModule);
    close(fd);
    if (!ok) {
        munmap(base, static_cast<size_t>(alignedSize));
        return nullptr;
    }

    // Intercepta la IAT de la DLL local (con dynamic stubs para secundarios)
    NativeDllInterceptor::hijackLocalDllIat(dllModule);

    if (lowerBase == "unityplayer.dll" && dllModule.peInfo.is64Bit) {
        NativeDllInterceptor::registerUnityMemoryHooks(reinterpret_cast<uintptr_t>(base));
    }

    // Registra el rango en el motor JIT activo
    OryonEmitterBackend::addStaticImage(reinterpret_cast<uintptr_t>(base), static_cast<size_t>(alignedSize));
    if (auto* b = OryonEmitterBackend::activeBackend(); b != nullptr) {
        b->registerModule(baseName, reinterpret_cast<uintptr_t>(base), static_cast<size_t>(alignedSize));
        b->addCodeRange(reinterpret_cast<uintptr_t>(base), static_cast<size_t>(alignedSize));
    }

    const LocalDllRegion reg{base, static_cast<size_t>(alignedSize), info, resolvedPath};
    image.localDlls[dllName] = reg;
    image.localDlls[norm] = reg;
    image.localDlls[baseName] = reg;
    image.localDlls[lowerBase] = reg;
    std::string lowerNorm = norm;
    for (char& c : lowerNorm) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    image.localDlls[lowerNorm] = reg;
    // También por el nombre real del archivo (fmod.dll servido desde fmod64.dll): si otra DLL lo
    // importa por ese nombre, se reutiliza en vez de cargar una segunda copia.
    {
        std::string fileBase = resolvedPath.substr(resolvedPath.find_last_of('/') + 1);
        for (char& c : fileBase) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        image.localDlls.try_emplace(fileBase, reg);
    }
    image.initOrder.push_back(lowerBase);

    LOGI("DLL local cargada: %s (%zu exports) en %p (tamaño 0x%zx)",
         baseName.c_str(), info.exports.size(), base, static_cast<size_t>(alignedSize));

    // (Eliminado: parche de `_pioinfo` en un offset fijo 0x73efe0 de mono. Compensaba la CRT sin
    // inicializar por el TLS roto de las DLL dinámicas; con otra versión de Mono escribía fuera de
    // la imagen — en Cuphead, mono.dll mide 0x3e0000.)

    // TLS propio antes de DllMain (las DLL del arranque las cubre setupImageTls).
    OryonEmitterBackend::registerDynamicModuleTls(base, info, baseName);

    // Si la DLL tiene EntryPoint (DllMain) y aún no ha sido inicializada, ejecutarla
    if (info.entryPointRva != 0) {
        bool shouldInit = false;
        {
            static std::mutex s_dllInitMutex;
            static std::unordered_set<void*> s_initedDlls;
            std::lock_guard<std::mutex> lock(s_dllInitMutex);
            shouldInit = s_initedDlls.insert(base).second;
        }
        if (shouldInit) {
            void* dllEntry = static_cast<uint8_t*>(base) + info.entryPointRva;
            if (auto* b = OryonEmitterBackend::activeBackend(); b != nullptr) {
                LOGI("Llamando a DllMain anidado para %s en %p (RVA 0x%x)",
                     baseName.c_str(), dllEntry, info.entryPointRva);
                const int64_t ret = b->callDllEntryNested(dllEntry, base);
                LOGI("DllMain anidado de %s finalizo: %ld", baseName.c_str(), static_cast<long>(ret));
            }
        }
    }

    return base;
}

void BionicPeLoader::unload(std::unique_ptr<LoadedPeImage> image) {
    image.reset();  // LoadedPeImage::~LoadedPeImage desmapea la imagen y sus DLLs
}

std::string BionicPeLoader::resolveVirtualPath(const LoadedPeImage& image, const std::string& winPath) {
    std::string norm;
    norm.reserve(winPath.size());
    for (char c : winPath) {
        if (c == '\\') norm.push_back('/');
        else if (c != '\0' && c != '\"') norm.push_back(c);
    }
    // Rutas largas de Win32 ("\\?\D:\Saves", "\??\D:\..."; Mono las usa al crear carpetas) y de
    // dispositivo ("\\.\D:\..."): el prefijo no forma parte de la ruta.
    for (const char* prefix : {"//?/", "/?\?/", "//./"}) {
        if (norm.rfind(prefix, 0) == 0) {
            norm.erase(0, 4);
            break;
        }
    }

    // C:\HyperPlay\Mono\... -> runtime Mono (juegos .NET).
    if (std::string mounted = managed::resolveMount(norm); !mounted.empty()) return mounted;

    // Cualquier prefijo de unidad "D:", "C:", "d:/", etc. se quita para obtener la ruta relativa al directorio del juego
    std::string relative;
    if (norm.size() >= 2 && norm[1] == ':') {
        size_t offset = 2;
        while (offset < norm.size() && norm[offset] == '/') {
            ++offset;
        }
        relative = norm.substr(offset);
    } else {
        relative = norm;
    }

    // Sin componentes "..": un ejecutable no puede escapar de su carpeta (ni leer datos privados).
    std::string safe;
    size_t start = 0;
    while (start <= relative.size()) {
        const size_t slash = relative.find('/', start);
        const std::string part = relative.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (!part.empty() && part != "." && part != "..") {
            if (!safe.empty()) safe.push_back('/');
            safe += part;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    if (safe.empty()) safe = ".";

    // Perfil de usuario (C:\users\..., C:\ProgramData\...) y carpetas de guardado: se aíslan en
    // <Game>/_HyperPlaySaves conservando el árbol COMPLETO, de modo que padre e hijo se resuelven
    // de forma coherente (CreateDirectory/GetFileAttributes recorren la ruta componente a componente).
    std::string lower = safe;
    for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const bool profileTree = lower == "users" || lower.rfind("users/", 0) == 0 ||
                             lower == "programdata" || lower.rfind("programdata/", 0) == 0;
    const bool saveLike = profileTree ||
                          safe.find("AppData") != std::string::npos ||
                          safe.find("LocalLow") != std::string::npos ||
                          safe.find("Saved Games") != std::string::npos ||
                          safe.find("My Games") != std::string::npos;
    if (saveLike && !image.savesDir.empty()) {
        return image.savesDir + "/" + safe;
    }
    return image.gameDir + "/" + safe;
}

}  // namespace hyperplay
