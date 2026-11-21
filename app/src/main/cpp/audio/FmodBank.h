#pragma once

// Lector de bancos de FMOD Studio 1.10 (Celeste) sin código de Firelight. Formato en
// tools/managed-runtime/FMOD_BANK_FORMAT.md, comprobado con el FMOD real del juego en un PC.
//
// Del banco solo se usa lo necesario para reproducir: nombres (STDT), eventos (EVTB → línea de
// tiempo), líneas de tiempo (TLNB: instrumentos con inicio y duración), regiones de bucle (TRNB),
// instrumentos (onda WAIB, multi MUIT/PLST, dispersor SPIT, evento anidado EVIB), ondas (WAV →
// sonido del FSB5) y el FSB5 del chunk SND. Las referencias a instrumentos se encuentran buscando
// sus GUID dentro del registro (más robusto que depender del layout exacto de cada versión).

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "FsbVorbis.h"

namespace hyperplay::fmodlite {

struct Guid {
    uint8_t b[16] = {};
    bool operator==(const Guid& o) const noexcept { return std::memcmp(b, o.b, 16) == 0; }
    bool isZero() const noexcept {
        for (uint8_t v : b) if (v != 0) return false;
        return true;
    }
    static Guid at(const uint8_t* p) {
        Guid g;
        std::memcpy(g.b, p, 16);
        return g;
    }
};
struct GuidHash {
    size_t operator()(const Guid& g) const noexcept {
        uint64_t a, c;
        std::memcpy(&a, g.b, 8);
        std::memcpy(&c, g.b + 8, 8);
        return static_cast<size_t>(a * 0x9E3779B97F4A7C15ull ^ c);
    }
};

struct TimelineItem {
    Guid instrument;
    uint32_t start = 0;   // fotogramas a 48 kHz
    uint32_t length = 0;
};

/// Salto incondicional de la línea de tiempo: al llegar a `at` vuelve a `to` (fin de una región de
/// bucle -> su inicio). TRNB de 60 bytes con inicio == fin; los de 86 bytes llevan condiciones
/// (parámetros) y aún no se aplican.
struct TimelineJump {
    uint32_t at = 0, to = 0;
};

struct EventDef {
    Guid id;
    Guid timeline;
    std::string path;
    bool snapshot = false;
};

struct TimelineDef {
    std::vector<TimelineItem> items;
    std::vector<TimelineJump> jumps;
    uint32_t length = 0;  // fin del último instrumento
};

enum class InstKind : uint8_t { Wave, Multi, Scatter, Event };

struct InstrumentDef {
    InstKind kind = InstKind::Wave;
    Guid target;                   // onda (Wave) o evento (Event)
    std::vector<Guid> children;    // Multi / Scatter
    int32_t loopCount = 0;         // INST+24: 0 = una vez, -1 = bucle infinito, n = repeticiones
};

struct WaveDef {
    int bank = -1;       // índice en Bank::all()
    uint32_t subsound = 0;
};

/// Un .bank cargado (el archivo queda mapeado en memoria mientras vive).
class Bank {
public:
    ~Bank();
    bool load(const std::string& hostPath, int index, std::string* error = nullptr);
    [[nodiscard]] const uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] size_t size() const noexcept { return size_; }
    [[nodiscard]] const Fsb5& fsb() const noexcept { return fsb_; }
    [[nodiscard]] bool hasFsb() const noexcept { return !fsb_.samples.empty(); }
    [[nodiscard]] const std::string& path() const noexcept { return path_; }
    [[nodiscard]] const Guid& id() const noexcept { return id_; }

    // Lo que aportó este banco al modelo (para GetEventCount/GetEventList).
    std::vector<Guid> events;

private:
    friend class Model;
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    bool mapped_ = false;
    std::vector<uint8_t> owned_;
    Fsb5 fsb_;
    std::string path_;
    Guid id_;
};

/// Modelo combinado de todos los bancos cargados.
class Model {
public:
    /// Añade al modelo lo que contiene `bank` (ya cargado). Devuelve false si no es un banco.
    bool ingest(Bank& bank, int bankIndex);

    std::unordered_map<Guid, EventDef, GuidHash> events;
    std::unordered_map<Guid, TimelineDef, GuidHash> timelines;
    std::unordered_map<Guid, InstrumentDef, GuidHash> instruments;
    std::unordered_map<Guid, WaveDef, GuidHash> waves;
    std::unordered_map<std::string, Guid> idByPath;      // "event:/ui/game/pause", "bus:/...", "vca:/..."
    std::unordered_map<Guid, std::string, GuidHash> pathById;

private:
    void readStrings(const uint8_t* p, size_t size);
};

std::string guidToString(const Guid& g);
bool parseGuid(const char* text, Guid& out);

}  // namespace hyperplay::fmodlite
