#pragma once

// DirectInput 8 mínimo para juegos de 32 bits (teclado/ratón sin datos; mandos por XInput).

#include <string>

namespace hyperplay::dinput32 {

// Exportación de dinput8.dll para un invitado de 32 bits (nullptr si no está).
void* resolve(const std::string& name);

}  // namespace hyperplay::dinput32
