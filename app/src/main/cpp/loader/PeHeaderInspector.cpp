#include "PeHeaderInspector.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <vector>

namespace hyperplay {
namespace {

[[maybe_unused]] constexpr uint16_t kPe32Magic = 0x10b;
[[maybe_unused]] constexpr uint16_t kPe32PlusMagic = 0x20b;
[[maybe_unused]] constexpr uint32_t kExportDirIndex = 0;
[[maybe_unused]] constexpr uint32_t kImportDirIndex = 1;
[[maybe_unused]] constexpr uint32_t kRelocationDirIndex = 5;
[[maybe_unused]] constexpr uint32_t kTlsDirIndex = 9;
[[maybe_unused]] constexpr uint32_t kDelayImportDirIndex = 13;
[[maybe_unused]] constexpr size_t kMaxSections = 96;
[[maybe_unused]] constexpr size_t kMaxDescriptors = 1024;
[[maybe_unused]] constexpr size_t kMaxDllName = 256;
[[maybe_unused]] constexpr size_t kMaxImportFunctions = 4096;
[[maybe_unused]] constexpr size_t kMaxTotalImportedFunctions = 65536;

// Tamaño de un descriptor según la tabla (IMAGE_IMPORT_DESCRIPTOR = 20 bytes,
// IMAGE_DELAYLOAD_DESCRIPTOR = 32 bytes).
[[maybe_unused]] constexpr uint64_t kImportDescriptorSize = 20;
[[maybe_unused]] constexpr uint64_t kDelayDescriptorSize = 32;

uint16_t rd16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
uint64_t rd64(const uint8_t* p) {
    return static_cast<uint64_t>(rd32(p)) | (static_cast<uint64_t>(rd32(p + 4)) << 32);
}

bool rvaToOffset(const std::vector<PeSection>& sections, uint32_t rva, uint64_t& out) {
    for (const auto& s : sections) {
        if (rva < s.virtualAddress) continue;
        const uint64_t delta = static_cast<uint64_t>(rva) - static_cast<uint64_t>(s.virtualAddress);
        // Solo los bytes presentes en el archivo tienen offset de archivo (BSS no).
        if (delta >= s.rawSize) continue;
        if (s.virtualSize != 0 && delta >= std::max<uint64_t>(s.virtualSize, s.rawSize)) continue;
        out = static_cast<uint64_t>(s.rawPointer) + delta;
        return true;
    }
    return false;
}

bool readCString(const PeReader& read, uint64_t fileSize, uint64_t offset, std::string& out) {
    out.clear();
    if (offset >= fileSize) return false;
    const size_t len = static_cast<size_t>(std::min<uint64_t>(kMaxDllName, fileSize - offset));
    char buf[kMaxDllName];
    if (!read(offset, buf, len)) return false;
    const size_t n = strnlen(buf, len);
    out.assign(buf, n);
    return n > 0;
}

std::string toLowerAscii(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

}  // namespace

bool PeInfo::hasImport(const std::string& dllName) const noexcept {
    const std::string target = toLowerAscii(dllName);
    for (const auto& mod : imports) {
        if (toLowerAscii(mod.name) == target) return true;
    }
    return false;
}

uint32_t PeInfo::findExportRva(const std::string& funcName) const {
    for (const auto& exp : exports) {
        if (exp.name == funcName) return exp.rva;
    }
    return 0;
}

uint32_t graphicsApiForDll(const std::string& dllName) {
    const std::string lower = toLowerAscii(dllName);
    if (lower == "d3d12.dll" || lower == "d3d12core.dll") return kApiD3D12;
    if (lower == "d3d11.dll") return kApiD3D11;
    if (lower == "d3d10.dll" || lower == "d3d10_1.dll" || lower == "d3d10core.dll") return kApiD3D10;
    if (lower == "d3d9.dll") return kApiD3D9;
    if (lower == "d3d8.dll") return kApiD3D8;
    if (lower == "ddraw.dll") return kApiDDraw;
    if (lower == "dxgi.dll") return kApiDxgi;
    if (lower == "vulkan-1.dll") return kApiVulkan;
    if (lower == "opengl32.dll") return kApiOpenGL;
    return 0;
}

PeInfo inspectPe(const PeReader& read, uint64_t fileSize) {
    PeInfo info;

    if (fileSize < 64) {
        info.status = PeStatus::kTruncated;
        return info;
    }

    uint8_t dosHeader[64];
    if (!read(0, dosHeader, sizeof(dosHeader))) {
        info.status = PeStatus::kIoError;
        return info;
    }

    if (dosHeader[0] != 'M' || dosHeader[1] != 'Z') {
        info.status = PeStatus::kNotMz;
        return info;
    }

    const uint32_t peOffset = rd32(dosHeader + 0x3C);
    if (peOffset == 0 || static_cast<uint64_t>(peOffset) + 4 + 20 > fileSize) {
        info.status = PeStatus::kTruncated;
        return info;
    }

    uint8_t peSig[4];
    if (!read(peOffset, peSig, sizeof(peSig))) {
        info.status = PeStatus::kIoError;
        return info;
    }
    if (peSig[0] != 'P' || peSig[1] != 'E' || peSig[2] != '\0' || peSig[3] != '\0') {
        info.status = PeStatus::kNotPe;
        return info;
    }

    uint8_t coff[20];
    const uint64_t coffOffset = static_cast<uint64_t>(peOffset) + 4;
    if (!read(coffOffset, coff, sizeof(coff))) {
        info.status = PeStatus::kIoError;
        return info;
    }

    info.machine = rd16(coff);
    const uint16_t numSections = rd16(coff + 2);
    const uint16_t optHeaderSize = rd16(coff + 16);
    info.characteristics = rd16(coff + 18);
    info.largeAddressAware = (info.characteristics & kLargeAddressAware) != 0;

    if (optHeaderSize < 2) {
        info.status = PeStatus::kBadOptionalHeader;
        return info;
    }

    const uint64_t optOffset = coffOffset + 20;
    if (optOffset + optHeaderSize > fileSize) {
        info.status = PeStatus::kTruncated;
        return info;
    }

    uint8_t optMagicBuf[2];
    if (!read(optOffset, optMagicBuf, 2)) {
        info.status = PeStatus::kIoError;
        return info;
    }
    const uint16_t optMagic = rd16(optMagicBuf);
    if (optMagic == kPe32PlusMagic) {
        info.is64Bit = true;
    } else if (optMagic == kPe32Magic) {
        info.is64Bit = false;
    } else {
        info.status = PeStatus::kBadOptionalHeader;
        return info;
    }

    const size_t minOptSize = info.is64Bit ? 112 : 96;
    if (optHeaderSize < minOptSize) {
        info.status = PeStatus::kBadOptionalHeader;
        return info;
    }

    std::vector<uint8_t> optBuf(optHeaderSize);
    if (!read(optOffset, optBuf.data(), optHeaderSize)) {
        info.status = PeStatus::kIoError;
        return info;
    }

    info.entryPointRva = rd32(optBuf.data() + 16);
    if (info.is64Bit) {
        info.imageBase = rd64(optBuf.data() + 24);
        info.imageSize = rd32(optBuf.data() + 56);
        info.headersSize = rd32(optBuf.data() + 60);
        info.subsystem = rd16(optBuf.data() + 68);
    } else {
        info.imageBase = rd32(optBuf.data() + 28);
        info.imageSize = rd32(optBuf.data() + 56);
        info.headersSize = rd32(optBuf.data() + 60);
        info.subsystem = rd16(optBuf.data() + 68);
    }

    const size_t dataDirOffset = info.is64Bit ? 112 : 96;
    uint32_t numRvaAndSizes = 0;
    if (optHeaderSize >= dataDirOffset) {
        numRvaAndSizes = rd32(optBuf.data() + (dataDirOffset - 4));
    }
    numRvaAndSizes = std::min<uint32_t>(numRvaAndSizes, 16);

    uint32_t importRva = 0;
    uint32_t delayImportRva = 0;
    if (numRvaAndSizes > kExportDirIndex && optHeaderSize >= dataDirOffset + (kExportDirIndex + 1) * 8) {
        info.exportRva = rd32(optBuf.data() + dataDirOffset + kExportDirIndex * 8);
        info.exportSize = rd32(optBuf.data() + dataDirOffset + kExportDirIndex * 8 + 4);
    }
    if (numRvaAndSizes > kImportDirIndex && optHeaderSize >= dataDirOffset + (kImportDirIndex + 1) * 8) {
        importRva = rd32(optBuf.data() + dataDirOffset + kImportDirIndex * 8);
    }
    if (numRvaAndSizes > kDelayImportDirIndex && optHeaderSize >= dataDirOffset + (kDelayImportDirIndex + 1) * 8) {
        delayImportRva = rd32(optBuf.data() + dataDirOffset + kDelayImportDirIndex * 8);
    }
    if (numRvaAndSizes > kRelocationDirIndex && optHeaderSize >= dataDirOffset + (kRelocationDirIndex + 1) * 8) {
        info.relocationRva = rd32(optBuf.data() + dataDirOffset + kRelocationDirIndex * 8);
        info.relocationSize = rd32(optBuf.data() + dataDirOffset + kRelocationDirIndex * 8 + 4);
    }
    if (numRvaAndSizes > kTlsDirIndex && optHeaderSize >= dataDirOffset + (kTlsDirIndex + 1) * 8) {
        info.tlsRva = rd32(optBuf.data() + dataDirOffset + kTlsDirIndex * 8);
        info.tlsSize = rd32(optBuf.data() + dataDirOffset + kTlsDirIndex * 8 + 4);
    }

    // Secciones
    const uint64_t secTableOffset = optOffset + optHeaderSize;
    const size_t sectionCount = std::min<size_t>(numSections, kMaxSections);
    if (secTableOffset + sectionCount * 40 > fileSize) {
        info.status = PeStatus::kTruncated;
        return info;
    }

    std::vector<uint8_t> secBuf(sectionCount * 40);
    if (sectionCount > 0 && !read(secTableOffset, secBuf.data(), secBuf.size())) {
        info.status = PeStatus::kIoError;
        return info;
    }

    info.sections.reserve(sectionCount);
    for (size_t i = 0; i < sectionCount; ++i) {
        const uint8_t* p = secBuf.data() + i * 40;
        PeSection s;
        char name[9] = {0};
        std::memcpy(name, p, 8);
        s.name = name;
        s.virtualSize = rd32(p + 8);
        s.virtualAddress = rd32(p + 12);
        s.rawSize = rd32(p + 16);
        s.rawPointer = rd32(p + 20);
        s.characteristics = rd32(p + 36);
        info.sections.push_back(s);
    }

    // Parse Imports
    size_t totalImportedFunctions = 0;
    auto parseImportsAt = [&](uint32_t dirRva, bool isDelay) {
        if (dirRva == 0) return;
        uint64_t descOffset = 0;
        if (!rvaToOffset(info.sections, dirRva, descOffset)) return;
        // Los descriptores de carga diferida tienen otro formato y otro tamaño (32 bytes).
        const uint64_t descSize = isDelay ? kDelayDescriptorSize : kImportDescriptorSize;

        for (size_t i = 0; i < kMaxDescriptors; ++i) {
            uint8_t desc[kDelayDescriptorSize] = {0};
            if (!read(descOffset + static_cast<uint64_t>(i) * descSize, desc, static_cast<size_t>(descSize))) break;

            uint32_t nameRva = 0;
            uint32_t firstThunk = 0;
            uint32_t originalFirstThunk = 0;
            bool vaBased = false;
            if (isDelay) {
                // IMAGE_DELAYLOAD_DESCRIPTOR: Attributes, DllNameRVA, ModuleHandleRVA,
                // ImportAddressTableRVA, ImportNameTableRVA, BoundIAT, UnloadIAT, TimeDateStamp.
                vaBased = (rd32(desc) & 1u) == 0;  // dlattrRva == 0 -> direcciones virtuales
                nameRva = rd32(desc + 4);
                firstThunk = rd32(desc + 12);
                originalFirstThunk = rd32(desc + 16);
            } else {
                originalFirstThunk = rd32(desc);
                nameRva = rd32(desc + 12);
                firstThunk = rd32(desc + 16);
            }

            if (nameRva == 0 && firstThunk == 0 && originalFirstThunk == 0) break;  // Fin de tabla

            if (vaBased) {
                // Solo posible en PE32: convertir VA -> RVA con la base de imagen.
                if (info.imageBase > 0xFFFFFFFFull) continue;
                const uint32_t base = static_cast<uint32_t>(info.imageBase);
                auto toRva = [base](uint32_t va) -> uint32_t { return va >= base ? va - base : 0; };
                nameRva = toRva(nameRva);
                firstThunk = toRva(firstThunk);
                originalFirstThunk = toRva(originalFirstThunk);
            }

            if (nameRva == 0 || firstThunk == 0) continue;

            uint64_t nameOffset = 0;
            if (!rvaToOffset(info.sections, nameRva, nameOffset)) continue;
            std::string dllName;
            if (!readCString(read, fileSize, nameOffset, dllName)) continue;

            info.graphicsApis |= graphicsApiForDll(dllName);

            ImportedModule mod;
            mod.name = dllName;
            mod.firstThunkRva = firstThunk;
            mod.originalFirstThunkRva = originalFirstThunk;

            const uint32_t thunkTableRva = (originalFirstThunk != 0) ? originalFirstThunk : firstThunk;
            uint64_t thunkOffset = 0;
            if (thunkTableRva != 0 && rvaToOffset(info.sections, thunkTableRva, thunkOffset)) {
                const size_t ptrSize = info.is64Bit ? 8 : 4;
                for (size_t f = 0; f < kMaxImportFunctions; ++f) {
                    if (totalImportedFunctions >= kMaxTotalImportedFunctions) break;
                    uint8_t entry[8] = {0};
                    if (!read(thunkOffset + f * ptrSize, entry, ptrSize)) break;
                    const uint64_t val = info.is64Bit ? rd64(entry) : rd32(entry);
                    if (val == 0) break;

                    const uint64_t ordinalMask = info.is64Bit ? (1ULL << 63) : (1ULL << 31);
                    ImportedFunction fn;
                    const uint64_t iatRva = static_cast<uint64_t>(firstThunk) + f * ptrSize;
                    if (iatRva > 0xFFFFFFFFull) break;  // la IAT no cabe en un RVA
                    fn.iatRva = static_cast<uint32_t>(iatRva);

                    if ((val & ordinalMask) != 0) {
                        fn.ordinal = static_cast<uint16_t>(val & 0xFFFF);
                    } else {
                        const uint32_t hintNameRva = static_cast<uint32_t>(val & 0x7FFFFFFF);
                        uint64_t hintOffset = 0;
                        if (rvaToOffset(info.sections, hintNameRva, hintOffset)) {
                            uint8_t hintBuf[2] = {0};
                            if (read(hintOffset, hintBuf, 2)) {
                                fn.ordinal = rd16(hintBuf);
                                readCString(read, fileSize, hintOffset + 2, fn.name);
                            }
                        }
                    }
                    mod.functions.push_back(fn);
                    ++totalImportedFunctions;
                }
            }
            info.imports.push_back(mod);
        }
    };

    // Parse Exports (IMAGE_DIRECTORY_ENTRY_EXPORT, índice 0)
    if (info.exportRva != 0 && info.exportSize >= 40) {
        uint64_t expOffset = 0;
        if (rvaToOffset(info.sections, info.exportRva, expOffset)) {
            uint8_t expBuf[40] = {0};
            if (read(expOffset, expBuf, 40)) {
                const uint32_t baseOrdinal = rd32(expBuf + 16);
                const uint32_t numFunctions = rd32(expBuf + 20);
                const uint32_t numNames = rd32(expBuf + 24);
                const uint32_t addrFunctionsRva = rd32(expBuf + 28);
                const uint32_t addrNamesRva = rd32(expBuf + 32);
                const uint32_t addrNameOrdinalsRva = rd32(expBuf + 36);

                uint64_t funcTableOffset = 0;
                uint64_t nameTableOffset = 0;
                uint64_t ordTableOffset = 0;
                if (rvaToOffset(info.sections, addrFunctionsRva, funcTableOffset) &&
                    rvaToOffset(info.sections, addrNamesRva, nameTableOffset) &&
                    rvaToOffset(info.sections, addrNameOrdinalsRva, ordTableOffset)) {
                    const size_t parseCount = std::min<size_t>(numNames, 4096u);
                    info.exports.reserve(parseCount);
                    for (size_t i = 0; i < parseCount; ++i) {
                        uint8_t nameRvaBuf[4] = {0};
                        uint8_t ordBuf[2] = {0};
                        if (!read(nameTableOffset + i * 4, nameRvaBuf, 4) ||
                            !read(ordTableOffset + i * 2, ordBuf, 2)) {
                            break;
                        }
                        const uint32_t nameRva = rd32(nameRvaBuf);
                        const uint16_t ordIndex = rd16(ordBuf);
                        if (ordIndex >= numFunctions) continue;

                        uint8_t fnRvaBuf[4] = {0};
                        if (!read(funcTableOffset + ordIndex * 4, fnRvaBuf, 4)) {
                            break;
                        }
                        const uint32_t fnRva = rd32(fnRvaBuf);

                        uint64_t nameStrOffset = 0;
                        std::string fnName;
                        if (rvaToOffset(info.sections, nameRva, nameStrOffset)) {
                            readCString(read, fileSize, nameStrOffset, fnName);
                        }
                        if (!fnName.empty() && fnRva != 0) {
                            info.exports.push_back(ExportedFunction{fnName, static_cast<uint16_t>(baseOrdinal + ordIndex), fnRva});
                        }
                    }
                }
            }
        }
    }

    parseImportsAt(importRva, false);
    parseImportsAt(delayImportRva, true);

    info.status = PeStatus::kOk;
    return info;
}

PeInfo inspectPeBuffer(const uint8_t* data, size_t size) {
    if (data == nullptr || size == 0) {
        PeInfo info;
        info.status = PeStatus::kIoError;
        return info;
    }
    auto reader = [data, size](uint64_t offset, void* dst, size_t len) -> bool {
        if (offset + len > size) return false;
        std::memcpy(dst, data + offset, len);
        return true;
    };
    return inspectPe(reader, size);
}

PeInfo inspectPeFile(const std::string& path) {
    PeInfo info;
    const int fd = open(path.c_str(), O_RDONLY);
    if (fd < 0) {
        info.status = PeStatus::kIoError;
        return info;
    }
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size <= 0) {
        close(fd);
        info.status = PeStatus::kIoError;
        return info;
    }
    const uint64_t fileSize = static_cast<uint64_t>(st.st_size);
    auto reader = [fd](uint64_t offset, void* dst, size_t len) -> bool {
        return pread(fd, dst, len, static_cast<off_t>(offset)) == static_cast<ssize_t>(len);
    };
    info = inspectPe(reader, fileSize);
    close(fd);
    return info;
}

}  // namespace hyperplay
