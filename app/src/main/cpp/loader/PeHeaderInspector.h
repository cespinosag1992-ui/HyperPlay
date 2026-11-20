#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace hyperplay {

// Valores del campo Machine del COFF header.
inline constexpr uint16_t kMachineI386 = 0x014c;
inline constexpr uint16_t kMachineAmd64 = 0x8664;
inline constexpr uint16_t kMachineArm64 = 0xaa64;

// Characteristics del COFF header.
inline constexpr uint16_t kLargeAddressAware = 0x0020;
inline constexpr uint16_t kFileExecutableImage = 0x0002;
inline constexpr uint16_t kFileDll = 0x2000;

// Subsystem
inline constexpr uint16_t kSubsystemWindowsGui = 2;
inline constexpr uint16_t kSubsystemWindowsCui = 3;

// Bits de APIs gráficas detectadas en la tabla de importaciones.
enum GraphicsApi : uint32_t {
    kApiDDraw   = 1u << 0,
    kApiD3D8    = 1u << 1,
    kApiD3D9    = 1u << 2,
    kApiD3D10   = 1u << 3,
    kApiD3D11   = 1u << 4,
    kApiD3D12   = 1u << 5,
    kApiDxgi    = 1u << 6,
    kApiVulkan  = 1u << 7,
    kApiOpenGL  = 1u << 8,
};

enum class PeStatus : int32_t {
    kOk = 0,
    kIoError = 1,
    kNotMz = 2,
    kNotPe = 3,
    kTruncated = 4,
    kBadOptionalHeader = 5,
};

struct ExportedFunction {
    std::string name;
    uint16_t ordinal = 0;
    uint32_t rva = 0;
};

struct ImportedFunction {
    std::string name;
    uint16_t ordinal = 0;
    uint32_t iatRva = 0;
};

struct ImportedModule {
    std::string name;
    uint32_t firstThunkRva = 0;
    uint32_t originalFirstThunkRva = 0;
    std::vector<ImportedFunction> functions;
};

struct PeSection {
    std::string name;
    uint32_t virtualAddress = 0;
    uint32_t virtualSize = 0;
    uint32_t rawPointer = 0;
    uint32_t rawSize = 0;
    uint32_t characteristics = 0;
};

struct PeInfo {
    PeStatus status = PeStatus::kIoError;
    uint16_t machine = 0;
    uint16_t characteristics = 0;
    uint16_t subsystem = 0;
    bool is64Bit = false;        // Optional header PE32+ (magic 0x20b).
    bool largeAddressAware = false;
    uint32_t entryPointRva = 0;
    uint64_t imageBase = 0;
    uint32_t imageSize = 0;
    uint32_t headersSize = 0;
    uint32_t exportRva = 0;      // Data directory 0 (exports).
    uint32_t exportSize = 0;
    uint32_t relocationRva = 0;  // Data directory 5 (base relocations).
    uint32_t relocationSize = 0;
    uint32_t tlsRva = 0;         // Data directory 9 (TLS directory).
    uint32_t tlsSize = 0;
    uint32_t graphicsApis = 0;   // Máscara de GraphicsApi.

    std::vector<PeSection> sections;
    std::vector<ImportedModule> imports;
    std::vector<ExportedFunction> exports;

    [[nodiscard]] bool ok() const noexcept { return status == PeStatus::kOk; }
    [[nodiscard]] bool is32Bit() const noexcept { return machine == kMachineI386; }
    [[nodiscard]] bool hasImport(const std::string& dllName) const noexcept;
    [[nodiscard]] uint32_t findExportRva(const std::string& funcName) const;
};

// Lector arbitrario seguro de bytes.
using PeReader = std::function<bool(uint64_t offset, void* dst, size_t len)>;

PeInfo inspectPe(const PeReader& read, uint64_t fileSize);
PeInfo inspectPeBuffer(const uint8_t* data, size_t size);
PeInfo inspectPeFile(const std::string& path);

uint32_t graphicsApiForDll(const std::string& dllName);

}  // namespace hyperplay
