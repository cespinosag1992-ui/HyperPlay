#pragma once

#include <string>

namespace hyperplay {

/// Android 10+ impone W^X sobre el almacenamiento compartido: un .so situado en /sdcard,
/// /storage/emulated o /mnt no se puede mapear con dlopen. Este componente copia la librería a
/// un directorio privado de la app (0700) con permisos 0500 antes de abrirla.
namespace BionicLibraryStager {

/// Directorio privado de staging. Prioridad: HYPERPLAY_STAGING_DIR, derivado de `referencePath`,
/// derivado de /proc/self/cmdline. Cadena vacía si no se puede resolver.
[[nodiscard]] std::string stagingDir(const std::string& referencePath = {});

/// Indica si la ruta requiere staging (almacenamiento compartido o no ejecutable).
[[nodiscard]] bool needsStaging(const std::string& path) noexcept;

/// Copia `srcPath` al directorio de staging y devuelve la ruta resultante (0500).
/// Devuelve cadena vacía si la ruta ya es privada o si la copia falla.
[[nodiscard]] std::string stageLibrary(const std::string& srcPath, const std::string& referencePath = {});

/// dlopen con staging automático. `stagedPath` recibe la ruta realmente abierta.
[[nodiscard]] void* dlopenStaged(const std::string& path, int flags, std::string* stagedPath = nullptr);

/// Registra el último dlerror() con contexto y ruta.
void logDlError(const char* context, const std::string& path);

}  // namespace BionicLibraryStager

}  // namespace hyperplay
