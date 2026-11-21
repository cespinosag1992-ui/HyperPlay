#include "audio/FsbVorbis.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
using namespace hyperplay::fmodlite;

static std::vector<uint8_t> readAll(const char* p) {
    std::vector<uint8_t> v;
    FILE* f = std::fopen(p, "rb");
    if (!f) return v;
    std::fseek(f, 0, SEEK_END);
    v.resize(std::ftell(f));
    std::fseek(f, 0, SEEK_SET);
    std::fread(v.data(), 1, v.size(), f);
    std::fclose(f);
    return v;
}

int main(int argc, char** argv) {
    auto bank = readAll(argv[1]);
    auto dll = readAll(argv[2]);
    std::printf("bank=%zu dll=%zu\n", bank.size(), dll.size());
    size_t at = 0;
    for (size_t i = 0; i + 4 <= bank.size(); ++i) if (!std::memcmp(&bank[i], "FSB5", 4)) { at = i; break; }
    Fsb5 fsb;
    if (!parseFsb5(&bank[at], bank.size() - at, at, fsb)) { std::puts("parse FSB5 falló"); return 1; }
    std::printf("FSB5 @%zu codec=%u muestras=%zu\n", at, fsb.codec, fsb.samples.size());
    std::vector<uint32_t> crcs;
    for (auto& s : fsb.samples) crcs.push_back(s.vorbisCrc);
    VorbisSetupTable table;
    std::printf("setups encontradas: %zu\n", table.loadFromFmodDll(dll, crcs));
    int ok = 0;
    for (size_t i = 0; i < fsb.samples.size() && i < 8; ++i) {
        const auto& s = fsb.samples[i];
        std::vector<int16_t> pcm;
        std::string err;
        const bool r = decodeVorbisSample(bank.data(), s, table, pcm, &err);
        int peak = 0;
        for (int16_t v : pcm) peak = std::max(peak, std::abs(static_cast<int>(v)));
        std::printf("[%zu] '%s' %uch %uHz %u fotogramas -> %s pcm=%zu pico=%d %s\n", i, s.name.c_str(), s.channels, s.rate,
                    s.numSamples, r ? "OK" : "FALLO", pcm.size(), peak, err.c_str());
        if (r) ++ok;
        if (r && i == 0) {
            FILE* w = std::fopen("/data/local/tmp/s0.wav", "wb");
            const uint32_t data = static_cast<uint32_t>(pcm.size() * 2), rate = s.rate, ch = s.channels;
            const uint32_t hdr[] = {0x46464952, 36 + data, 0x45564157, 0x20746d66, 16, (ch << 16) | 1, rate, rate * ch * 2,
                                    (16u << 16) | (ch * 2), 0x61746164, data};
            std::fwrite(hdr, 1, sizeof(hdr), w);
            std::fwrite(pcm.data(), 2, pcm.size(), w);
            std::fclose(w);
        }
    }
    std::printf("decodificadas OK: %d\n", ok);
    return ok > 0 ? 0 : 1;
}
