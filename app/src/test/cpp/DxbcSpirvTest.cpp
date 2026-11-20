// Prueba del traductor DXBC -> SPIR-V sobre corpus reales de shaders volcados por HyperPlay.
//
// Uso (en el dispositivo): dxbc_spirv_test <dir_entrada> [<dir_entrada>...] <dir_salida>
// Traduce cada *.dxbc a <dir_salida>/<nombre>.spv y resume éxitos/fallos. La validación
// estructural se hace después en el PC con spirv-val (--target-env vulkan1.1).

#include <dirent.h>

#include <cstdio>
#include <map>
#include <string>
#include <vector>

#include "gfx/DxbcToSpirv.h"
#include "loader/DxbcShader.h"

int main(int argc, char** argv) {
    if (argc < 3) {
        std::printf("uso: %s <dir_dxbc>... <dir_spv>\n", argv[0]);
        return 2;
    }
    const std::string outDir = argv[argc - 1];
    int total = 0, ok = 0, parseFail = 0;
    std::map<std::string, int> errors;
    for (int a = 1; a < argc - 1; ++a) {
        DIR* dir = opendir(argv[a]);
        if (dir == nullptr) continue;
        while (dirent* de = readdir(dir)) {
            const std::string name = de->d_name;
            if (name.size() < 6 || name.substr(name.size() - 5) != ".dxbc") continue;
            const std::string path = std::string(argv[a]) + "/" + name;
            FILE* f = std::fopen(path.c_str(), "rb");
            if (f == nullptr) continue;
            std::vector<uint8_t> bc;
            uint8_t buf[4096];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) bc.insert(bc.end(), buf, buf + n);
            std::fclose(f);
            ++total;
            const hyperplay::dxbc::Program p = hyperplay::dxbc::parse(bc.data(), bc.size());
            if (!p.ok()) {
                ++parseFail;
                continue;
            }
            const hyperplay::dxbc2spv::Result r = hyperplay::dxbc2spv::translate(p);
            if (!r.ok()) {
                std::string key = r.error;
                const size_t paren = key.find(" (instr");
                if (paren != std::string::npos) key = key.substr(0, paren);
                ++errors[key];
                if (errors[key] <= 2) std::printf("FALLO %s: %s\n", name.c_str(), r.error.c_str());
                continue;
            }
            ++ok;
            const std::string tag = std::string(argv[a]).find("Cuphead") != std::string::npos ? "cup_" : "";
            const std::string out = outDir + "/" + tag + name.substr(0, name.size() - 5) + ".spv";
            if (FILE* o = std::fopen(out.c_str(), "wb")) {
                std::fwrite(r.spirv.data(), 4, r.spirv.size(), o);
                std::fclose(o);
            }
        }
        closedir(dir);
    }
    std::printf("DXBC->SPIR-V: %d shaders, %d traducidos, %d no parseados, %d con error\n", total, ok, parseFail,
                total - ok - parseFail);
    for (const auto& [e, c] : errors) std::printf("  %4d x %s\n", c, e.c_str());
    return 0;
}
