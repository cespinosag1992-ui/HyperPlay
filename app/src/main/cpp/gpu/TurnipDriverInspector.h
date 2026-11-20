#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hyperplay {

// e_machine de ARM64 en la cabecera ELF.
inline constexpr uint16_t kElfMachineAarch64 = 183;

// Puntos de entrada que el cargador Vulkan busca en un ICD.
inline constexpr const char* kIcdEntrySymbol = "vk_icdGetInstanceProcAddr";
inline constexpr const char* kIcdNegotiateSymbol = "vk_icdNegotiateLoaderICDInterfaceVersion";

struct DriverInspection {
    bool validElf = false;      // ELF64 little-endian de tipo ET_DYN.
    bool aarch64 = false;       // e_machine == EM_AARCH64.
    bool bionic = false;        // DT_NEEDED con libc.so (sin versión) o nota "Android".
    bool hasIcdEntry = false;   // Exporta vk_icdGetInstanceProcAddr.
    bool hasNegotiate = false;  // Exporta vk_icdNegotiateLoaderICDInterfaceVersion.
    std::vector<std::string> needed;
    std::string error;          // Vacío si el análisis terminó sin problemas.
};

// Lee `len` bytes en `offset`; devuelve false si no hay tantos bytes.
using ElfReader = std::function<bool(uint64_t offset, void* dst, size_t len)>;

// Analiza un ELF a partir de un lector aleatorio. Ningún campo del archivo es de
// fiar: cada offset y tamaño se valida contra `fileSize` antes de leer.
DriverInspection inspectDriverElfWith(const ElfReader& read, uint64_t fileSize);

// Atajos: buffer en memoria o ruta en disco.
DriverInspection inspectDriverElfBuffer(const uint8_t* data, size_t size);
DriverInspection inspectDriverElf(const std::string& path);

// Solo tiene sentido con builds bionic: dlopen + dlsym(vk_icdGetInstanceProcAddr).
// Devuelve false y rellena `error` con dlerror() si algo falla.
bool dlopenCheck(const std::string& path, std::string& error);

// Modelo de GPU según KGSL (p. ej. "Adreno830v2"); "chipid:<valor>" como respaldo;
// vacío si sysfs no es legible.
std::string adrenoModel();

// true si el proceso puede abrir /dev/kgsl-3d0 en lectura y escritura.
bool kgslAccessible();

}  // namespace hyperplay
