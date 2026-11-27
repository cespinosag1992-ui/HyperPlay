#pragma once

// Steam sin cliente para juegos de 32 bits (x86). Los juegos de 64 bits usan NativeSteamStub.

#include <string>

namespace hyperplay::steamemu32 {

// Exportación de steam_api.dll para un invitado de 32 bits (nullptr si no está). saveDir es la
// carpeta de guardado del juego (la nube de Steam va a <saveDir>/steam_cloud).
void* resolve(const std::string& name, const std::string& saveDir);

}  // namespace hyperplay::steamemu32
