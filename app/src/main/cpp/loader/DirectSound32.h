#pragma once

// DirectSound 8 para juegos de 32 bits (x86) sobre AAudio. Wwise (DmC) lo usa cuando no hay XAudio2:
// un búfer secundario circular que el juego rellena con Lock/Unlock según GetCurrentPosition y,
// opcionalmente, eventos de IDirectSoundNotify. Los juegos de 64 bits no pasan por aquí.

#include <cstdint>
#include <string>

namespace hyperplay::dsound32 {

// CoCreateInstance(CLSID_DirectSound8 / CLSID_DirectSound): IDirectSound8 en memoria baja (0 si falla).
uint32_t create();

// Exportación de dsound.dll (DirectSoundCreate8, DirectSoundCreate...) o nullptr.
void* resolve(const std::string& name);

// Detiene todos los búferes (cierre del juego).
void stopAll();

}  // namespace hyperplay::dsound32
