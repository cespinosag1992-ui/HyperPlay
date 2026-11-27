#pragma once

// IDirect3D9 de solo consulta para juegos de 32 bits (x86). Unreal Engine 3 crea un IDirect3D9 para
// leer la GPU (identificador, modos, caps) aunque luego renderice con Direct3D 11. CreateDevice
// devuelve D3DERR_NOTAVAILABLE: no hay dispositivo D3D9. Los juegos de 64 bits no pasan por aquí.

#include <cstdint>
#include <string>

namespace hyperplay::d3d9probe32 {

struct Display {
    uint32_t width = 1920;
    uint32_t height = 1080;
    uint32_t monitor = 0;  // HMONITOR que devuelve MonitorFromWindow
};

// Exportación de d3d9.dll para un invitado de 32 bits (nullptr si no está implementada).
void* resolve(const std::string& name, const Display& display);

}  // namespace hyperplay::d3d9probe32
