#pragma once

#include <sys/mman.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "PeHeaderInspector.h"

namespace hyperplay {

struct LoadedSection {
    std::string name;
    void* virtualAddress = nullptr;
    size_t size = 0;
    uint32_t characteristics = 0;
};

/// Región mmap de una DLL local. Se guarda el tamaño para poder desmapearla entera.
struct LocalDllRegion {
    void* base = nullptr;
    size_t size = 0;
    PeInfo peInfo;
    std::string hostPath;  // archivo del que se cargó (GetModuleFileName)
};

struct LoadedPeImage {
    std::string filePath;
    std::string gameDir;
    std::string savesDir;
    void* baseAddress = nullptr;
    size_t totalMappedSize = 0;
    void* entryPoint = nullptr;
    bool is32Bit = false;
    uint64_t originalImageBase = 0;
    int64_t relocationDelta = 0;

    PeInfo peInfo;
    std::vector<LoadedSection> sections;
    std::unordered_map<std::string, LocalDllRegion> localDlls;
    /// Orden en que terminó de cargarse cada DLL local (sus dependencias antes que ella): es el
    /// orden de los DllMain, como en Windows (el CRT antes que quien lo usa).
    std::vector<std::string> initOrder;

    /// true para imágenes temporales que solo usan la arena de otra imagen (no la liberan).
    bool borrowsArena = false;

    LoadedPeImage() = default;
    ~LoadedPeImage();

    LoadedPeImage(const LoadedPeImage&) = delete;
    LoadedPeImage& operator=(const LoadedPeImage&) = delete;

    [[nodiscard]] bool isValid() const noexcept { return baseAddress != nullptr; }
};

class BionicPeLoader {
public:
    BionicPeLoader();
    ~BionicPeLoader();

    BionicPeLoader(const BionicPeLoader&) = delete;
    BionicPeLoader& operator=(const BionicPeLoader&) = delete;

    /// Carga el ejecutable portable y mapea sus secciones en la memoria virtual de Android.
    [[nodiscard]] std::unique_ptr<LoadedPeImage> loadExecutable(const std::string& exePath);

    /// Carga una DLL dependiente local ubicada en la misma carpeta portable del .exe.
    [[nodiscard]] void* loadLocalDll(LoadedPeImage& image, const std::string& dllName);

    /// Libera los mappings del módulo (el destructor de LoadedPeImage hace el trabajo).
    void unload(std::unique_ptr<LoadedPeImage> image);

    /// Resuelve rutas de disco virtual de Windows (D:\ -> carpeta del juego, guardados -> _HyperPlaySaves).
    [[nodiscard]] static std::string resolveVirtualPath(const LoadedPeImage& image, const std::string& winPath);
    // true si la última resolveVirtualPath de este hilo salió de la carpeta del ejecutable con
    // "..\" (instalación de Unreal). Esas rutas son de solo lectura + creación: nada de borrar,
    // mover ni reemplazar (un juego borró así Config y Localization de su instalación).
    [[nodiscard]] static bool lastPathEscapedGameDir() noexcept;

private:
    void* allocateArena(size_t size, bool is32Bit, uint64_t preferredBase);
    bool mapSections(int fd, const PeInfo& info, LoadedPeImage& image);
    bool protectSections(const PeInfo& info, const LoadedPeImage& image);
    bool applyRelocations(LoadedPeImage& image);
    void setupSaveRedirection(LoadedPeImage& image);
};

}  // namespace hyperplay
