#pragma once

// Decodificador de sonidos FSB5 (bancos de FMOD Studio) con Vorbis, sin FMOD.
//
// FMOD guarda Vorbis "pelado": cada muestra lleva solo los paquetes de audio y el CRC32 de su
// cabecera "setup" (los codebooks), que FMOD trae compilada en fmod.dll. HyperPlay la lee del
// fmod.dll del propio juego (tabla {puntero, tamaño, crc32}), reconstruye la cabecera de
// identificación y decodifica con el Vorbis de Android (AMediaCodec, "audio/vorbis").
//
// Formato FSB5 (versión 1), según la documentación pública de la comunidad (vgmstream):
//   cabecera 0x3C: "FSB5", version, numSamples, sampleHeadersSize, nameTableSize, dataSize, mode, ...
//   por muestra un uint64: bit0 = hay trozos extra, bits1-4 = frecuencia (tabla), bits5-6 =
//   canales (1/2/6/8), bits7-33 = offset de datos >> 5, bits34-63 = número de muestras PCM.
//   trozos extra: uint32 (bit0 = sigue otro, bits1-24 = tamaño, bits25-31 = tipo):
//   1 = canales, 2 = frecuencia, 3 = bucle (inicio, fin), 11 = Vorbis (crc32 + tabla de búsqueda).
//   Datos Vorbis: secuencia de [uint16 tamaño][paquete].

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace hyperplay::fmodlite {

struct FsbSample {
    uint32_t channels = 1;
    uint32_t rate = 44100;
    uint32_t numSamples = 0;   // fotogramas PCM
    uint32_t loopStart = 0, loopEnd = 0;
    bool hasLoop = false;
    uint32_t vorbisCrc = 0;
    size_t dataOffset = 0;     // absoluto dentro del buffer del banco
    size_t dataSize = 0;
    std::string name;
};

struct Fsb5 {
    std::vector<FsbSample> samples;
    uint32_t codec = 0;        // 15 = Vorbis
};

/// Parsea el FSB5 que empieza en `fsb` (dentro de un banco de `size` bytes desde ahí).
bool parseFsb5(const uint8_t* fsb, size_t size, size_t baseOffset, Fsb5& out);

/// Cabeceras setup de Vorbis por CRC32, sacadas de un fmod.dll de Windows (32 o 64 bits).
class VorbisSetupTable {
public:
    /// Busca en el fmod.dll las cabeceras de los CRC pedidos. @return cuántas encontró.
    size_t loadFromFmodDll(const std::vector<uint8_t>& dll, const std::vector<uint32_t>& crcs);
    const std::vector<uint8_t>* find(uint32_t crc) const;
    size_t size() const { return headers_.size(); }

private:
    std::unordered_map<uint32_t, std::vector<uint8_t>> headers_;
};

/// Decodifica una muestra a PCM int16 entrelazado. `bank` = buffer del banco entero.
bool decodeVorbisSample(const uint8_t* bank, const FsbSample& s, const VorbisSetupTable& setups,
                        std::vector<int16_t>& pcm, std::string* error = nullptr);

uint32_t crc32(const uint8_t* data, size_t size);

}  // namespace hyperplay::fmodlite
