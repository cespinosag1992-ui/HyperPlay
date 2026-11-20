// Test de host para TurnipDriverInspector (no lo compila Gradle).
// Uso (Linux/WSL):
//   g++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I app/src/main/cpp
//       app/src/test/cpp/TurnipDriverInspectorTest.cpp app/src/main/cpp/gpu/TurnipDriverInspector.cpp
//       -ldl -o /tmp/turnip_test && /tmp/turnip_test [ruta.so esperado]...
// `esperado` son 5 dígitos 0/1: validElf aarch64 bionic hasIcdEntry hasNegotiate.
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "gpu/TurnipDriverInspector.h"

using namespace hyperplay;

static int failures = 0;
#define CHECK(cond)                                                               \
    do {                                                                          \
        if (!(cond)) {                                                            \
            std::fprintf(stderr, "FALLO %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++failures;                                                           \
        }                                                                         \
    } while (0)

static std::string flags(const DriverInspection& r) {
    std::string s;
    for (bool b : {r.validElf, r.aarch64, r.bionic, r.hasIcdEntry, r.hasNegotiate}) s += b ? '1' : '0';
    return s;
}

static std::vector<uint8_t> loadFile(const char* path) {
    std::ifstream in(path, std::ios::binary);
    return std::vector<uint8_t>(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

static void testSynthetic() {
    CHECK(!inspectDriverElfBuffer(nullptr, 0).validElf);

    std::vector<uint8_t> b(64, 0);
    CHECK(!inspectDriverElfBuffer(b.data(), b.size()).validElf);  // Sin firma.

    std::memcpy(b.data(), "\x7f" "ELF", 4);
    b[4] = 1;  // ELFCLASS32
    b[5] = 1;
    CHECK(!inspectDriverElfBuffer(b.data(), b.size()).validElf);

    b[4] = 2;
    b[16] = 2;  // ET_EXEC
    b[18] = 183;
    DriverInspection r = inspectDriverElfBuffer(b.data(), b.size());
    CHECK(!r.validElf);
    CHECK(r.aarch64);
    CHECK(!r.error.empty());

    b[16] = 3;  // ET_DYN sin tablas: válido pero sin símbolos.
    r = inspectDriverElfBuffer(b.data(), b.size());
    CHECK(r.validElf);
    CHECK(!r.hasIcdEntry);
    CHECK(!r.error.empty());

    // Offsets basura: no debe leer fuera del buffer (ASan lo detectaría).
    for (size_t at = 32; at < 64; ++at) b[at] = 0xff;
    r = inspectDriverElfBuffer(b.data(), b.size());
    CHECK(!r.hasIcdEntry);
    CHECK(inspectDriverElf("/ruta/inexistente.so").error.size() > 0);
}

static void testRealFile(const char* path, const char* expected) {
    const std::vector<uint8_t> data = loadFile(path);
    CHECK(!data.empty());
    const DriverInspection fromFile = inspectDriverElf(path);
    const DriverInspection fromBuf = inspectDriverElfBuffer(data.data(), data.size());
    std::printf("%s -> %s needed=[", path, flags(fromFile).c_str());
    for (const auto& n : fromFile.needed) std::printf(" %s", n.c_str());
    std::printf(" ] error='%s'\n", fromFile.error.c_str());
    CHECK(flags(fromFile) == flags(fromBuf));
    if (expected != nullptr) CHECK(flags(fromFile) == expected);

    // Sin section headers: debe llegar al mismo resultado vía PT_DYNAMIC + hash.
    if (data.size() >= 64) {
        std::vector<uint8_t> stripped(data);
        std::memset(&stripped[40], 0, 8);  // e_shoff
        std::memset(&stripped[60], 0, 2);  // e_shnum
        const DriverInspection s = inspectDriverElfBuffer(stripped.data(), stripped.size());
        std::printf("  sin secciones -> %s error='%s'\n", flags(s).c_str(), s.error.c_str());
        CHECK(flags(s) == flags(fromFile));
        CHECK(s.needed == fromFile.needed);
    }

    // Truncado en cada longitud (paso adaptativo para archivos grandes).
    const size_t step = data.size() > 200000 ? 61 : 1;
    for (size_t len = 0; len < data.size(); len += step) {
        const DriverInspection t = inspectDriverElfBuffer(data.data(), len);
        if (t.hasIcdEntry) CHECK(fromFile.hasIcdEntry);
    }
    // Bytes corrompidos uno a uno en la cabecera y tablas de programa.
    std::vector<uint8_t> mutated(data);
    for (size_t i = 0; i < std::min<size_t>(mutated.size(), 1024); ++i) {
        const uint8_t old = mutated[i];
        mutated[i] = static_cast<uint8_t>(old ^ 0xa5);
        (void)inspectDriverElfBuffer(mutated.data(), mutated.size());
        mutated[i] = old;
    }

    if (fromFile.bionic == false && fromFile.validElf) {
        std::string err;
        const bool ok = dlopenCheck(path, err);
        std::printf("  dlopenCheck -> %s %s\n", ok ? "OK" : "FALLO", err.c_str());
    }
}

int main(int argc, char** argv) {
    testSynthetic();
    for (int i = 1; i < argc; ++i) {
        const char* expected = (i + 1 < argc && std::strlen(argv[i + 1]) == 5 &&
                                std::strspn(argv[i + 1], "01") == 5)
                                   ? argv[i + 1]
                                   : nullptr;
        testRealFile(argv[i], expected);
        if (expected != nullptr) ++i;
    }
    std::printf("adrenoModel='%s' kgsl=%d\n", adrenoModel().c_str(), kgslAccessible() ? 1 : 0);
    if (failures == 0) std::printf("OK\n");
    return failures == 0 ? 0 : 1;
}
