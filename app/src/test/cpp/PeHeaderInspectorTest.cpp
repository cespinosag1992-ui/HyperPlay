// Test de host para PeHeaderInspector (no lo compila Gradle).
// Uso (Linux/WSL):
//   g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I app/src/main/cpp
//       app/src/test/cpp/PeHeaderInspectorTest.cpp app/src/main/cpp/core/PeHeaderInspector.cpp
//       -o /tmp/pe_test && /tmp/pe_test [ruta_exe_32bit ruta_exe_64bit]
#include <cstdio>
#include <cstring>
#include <vector>

#include "loader/PeHeaderInspector.h"

using namespace hyperplay;

static int failures = 0;
#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            std::fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void put16(std::vector<uint8_t>& b, size_t at, uint16_t v) {
    b[at] = v & 0xff;
    b[at + 1] = v >> 8;
}
static void put32(std::vector<uint8_t>& b, size_t at, uint32_t v) {
    for (int i = 0; i < 4; ++i) b[at + i] = (v >> (8 * i)) & 0xff;
}

// PE mínimo con una sección (.idata) en RVA 0x1000 / archivo 0x200.
static std::vector<uint8_t> makePe(bool pe32plus, uint16_t machine, uint16_t characteristics,
                                   const char* importDll, const char* delayDll) {
    std::vector<uint8_t> b(0x400, 0);
    b[0] = 'M';
    b[1] = 'Z';
    const uint32_t lfanew = 0x80;
    put32(b, 0x3C, lfanew);
    std::memcpy(&b[lfanew], "PE\0\0", 4);

    const size_t coff = lfanew + 4;
    const uint16_t optSize = pe32plus ? 240 : 224;
    put16(b, coff + 0, machine);
    put16(b, coff + 2, 1);
    put16(b, coff + 16, optSize);
    put16(b, coff + 18, characteristics);

    const size_t opt = coff + 20;
    put16(b, opt, pe32plus ? 0x20b : 0x10b);
    put16(b, opt + 68, 2);  // GUI
    const size_t rvaCount = pe32plus ? 108 : 92;
    put32(b, opt + rvaCount, 16);
    const size_t dirs = opt + rvaCount + 4;
    if (importDll) put32(b, dirs + 1 * 8, 0x1000);
    if (delayDll) put32(b, dirs + 13 * 8, 0x1040);

    const size_t sec = opt + optSize;
    put32(b, sec + 8, 0x200);    // VirtualSize
    put32(b, sec + 12, 0x1000);  // VirtualAddress
    put32(b, sec + 16, 0x200);   // SizeOfRawData
    put32(b, sec + 20, 0x200);   // PointerToRawData

    if (importDll) {
        put32(b, 0x200 + 12, 0x1100);  // Name RVA -> archivo 0x300
        std::strcpy(reinterpret_cast<char*>(&b[0x300]), importDll);
    }
    if (delayDll) {
        put32(b, 0x240 + 0, 1);       // Attributes (RVA based)
        put32(b, 0x240 + 4, 0x1180);  // DllNameRVA -> archivo 0x380
        std::strcpy(reinterpret_cast<char*>(&b[0x380]), delayDll);
    }
    return b;
}

int main(int argc, char** argv) {
    {  // 32-bit, LAA, importa D3D9 (mayúsculas).
        auto pe = makePe(false, kMachineI386, kLargeAddressAware | 0x0102, "D3D9.dll", nullptr);
        auto info = inspectPeBuffer(pe.data(), pe.size());
        CHECK(info.ok());
        CHECK(info.machine == kMachineI386);
        CHECK(!info.is64Bit);
        CHECK(info.largeAddressAware);
        CHECK(info.subsystem == 2);
        CHECK(info.graphicsApis == kApiD3D9);
    }
    {  // 64-bit, sin LAA, d3d11 importado + d3d12 diferido.
        auto pe = makePe(true, kMachineAmd64, 0x0002, "d3d11.dll", "d3d12.dll");
        auto info = inspectPeBuffer(pe.data(), pe.size());
        CHECK(info.ok());
        CHECK(info.machine == kMachineAmd64);
        CHECK(info.is64Bit);
        CHECK(!info.largeAddressAware);
        CHECK(info.graphicsApis == (kApiD3D11 | kApiD3D12));
    }
    {  // No es MZ.
        std::vector<uint8_t> junk(0x200, 'A');
        CHECK(inspectPeBuffer(junk.data(), junk.size()).status == PeStatus::kNotMz);
    }
    {  // e_lfanew fuera del archivo.
        auto pe = makePe(false, kMachineI386, 0, nullptr, nullptr);
        put32(pe, 0x3C, 0xFFFFFFF0u);
        CHECK(inspectPeBuffer(pe.data(), pe.size()).status == PeStatus::kTruncated);
    }
    {  // Firma PE rota.
        auto pe = makePe(false, kMachineI386, 0, nullptr, nullptr);
        pe[0x80] = 'X';
        CHECK(inspectPeBuffer(pe.data(), pe.size()).status == PeStatus::kNotPe);
    }
    {  // Name RVA apuntando fuera de toda sección: no rompe, solo no detecta APIs.
        auto pe = makePe(false, kMachineI386, 0, "d3d9.dll", nullptr);
        put32(pe, 0x200 + 12, 0x7FFFFFF0u);
        auto info = inspectPeBuffer(pe.data(), pe.size());
        CHECK(info.ok());
        CHECK(info.graphicsApis == 0);
    }
    {  // Archivo truncado en cada longitud posible: nunca debe leer fuera.
        auto pe = makePe(true, kMachineAmd64, 0, "dxgi.dll", "vulkan-1.dll");
        for (size_t n = 0; n < pe.size(); ++n) (void)inspectPeBuffer(pe.data(), n);
    }
    CHECK(inspectPeFile("/no/existe.exe").status == PeStatus::kIoError);

    if (argc == 3) {  // Ejecutables reales: argv[1] 32-bit, argv[2] 64-bit.
        auto x86 = inspectPeFile(argv[1]);
        auto x64 = inspectPeFile(argv[2]);
        std::printf("x86: status=%d machine=0x%04x laa=%d apis=0x%x\n", (int)x86.status, x86.machine,
                    x86.largeAddressAware, x86.graphicsApis);
        std::printf("x64: status=%d machine=0x%04x laa=%d apis=0x%x\n", (int)x64.status, x64.machine,
                    x64.largeAddressAware, x64.graphicsApis);
        CHECK(x86.ok() && x86.machine == kMachineI386 && !x86.is64Bit);
        CHECK(x64.ok() && x64.machine == kMachineAmd64 && x64.is64Bit);
    }

    std::printf(failures == 0 ? "OK\n" : "%d fallos\n", failures);
    return failures == 0 ? 0 : 1;
}
