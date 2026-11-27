// Corpus del traductor D3D9 (SM3) -> SPIR-V: traduce cada <vs|ps>_*.bin de un directorio y escribe el .spv
// junto a él (para validarlo con spirv-val en el PC). Los shaders del corpus salen de los juegos del usuario
// (setprop debug.hyperplay.d3d9dump 1) y no se publican.
//
// Uso (en el dispositivo o en cualquier host): d3d9_spirv_test <dir_entrada> <dir_salida>
#include <dirent.h>

#include <cstdio>
#include <string>
#include <vector>

#include "gfx/D3D9ShaderToSpirv.h"

int main(int argc, char** argv) {
    if (argc < 3) return 2;
    DIR* d = opendir(argv[1]);
    if (d == nullptr) return 2;
    int ok = 0, bad = 0;
    while (dirent* e = readdir(d)) {
        const std::string name = e->d_name;
        if (name.size() < 5 || name.substr(name.size() - 4) != ".bin") continue;
        FILE* f = std::fopen((std::string(argv[1]) + "/" + name).c_str(), "rb");
        if (f == nullptr) continue;
        std::vector<uint32_t> tok;
        uint32_t w = 0;
        while (std::fread(&w, 4, 1, f) == 1) tok.push_back(w);
        std::fclose(f);
        std::vector<uint32_t> spv;
        hyperplay::d3d9sh::ShaderInfo info;
        std::string err;
        if (!hyperplay::d3d9sh::translate(tok.data(), tok.size(), spv, info, err)) {
            std::printf("FALLO %s: %s\n", name.c_str(), err.c_str());
            ++bad;
            continue;
        }
        ++ok;
        FILE* o = std::fopen((std::string(argv[2]) + "/" + name.substr(0, name.size() - 4) + ".spv").c_str(), "wb");
        if (o != nullptr) {
            std::fwrite(spv.data(), 4, spv.size(), o);
            std::fclose(o);
        }
    }
    closedir(d);
    std::printf("D3D9SpirvTest: %d traducidos, %d fallos\n", ok, bad);
    return bad == 0 ? 0 : 1;
}
