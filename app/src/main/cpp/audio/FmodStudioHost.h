#pragma once

// FMOD Studio sin FMOD: implementación nativa (AArch64) del subconjunto de la API de FMOD Studio
// 1.10 que usan los juegos .NET (Celeste), servida al juego en lugar del shim mudo de
// fmodstudio.dll. Lee los bancos del juego (FmodBank), decodifica sus sonidos Vorbis con Android
// (FsbVorbis) y los mezcla a 48 kHz estéreo hacia AAudio.

#include <cstdint>
#include <functional>
#include <string>

namespace hyperplay::fmodhost {

struct Hooks {
    /// Ruta de Windows del juego ("Content\FMOD\Desktop\ui.bank") -> ruta real en Android.
    std::function<std::string(const std::string&)> resolvePath;
    /// Bytes 0..15 del registro XMMn del hilo invitado que está llamando (floats Win64).
    std::function<const uint8_t*(int)> xmm;
    /// Carpeta del juego (donde está su fmod.dll con las cabeceras Vorbis).
    std::string gameDir;
};

void init(const Hooks& hooks);

/// Export de fmodstudio.dll por nombre (FMOD_Studio_*), o nullptr si no está implementado.
void* resolveExport(const char* name);

/// Pausa o reanuda la salida de audio (sesión en segundo plano, Quick Resume).
void setOutputPaused(bool paused);

/// Para todo y libera la salida (fin de la sesión de juego).
void shutdown();

}  // namespace hyperplay::fmodhost
