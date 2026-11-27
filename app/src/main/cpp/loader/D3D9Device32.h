#pragma once

// IDirect3DDevice9 para juegos de 32 bits (x86): objetos COM en memoria baja con vtables de 32 bits
// (stdcall, `this` incluido, aridad exacta de cada método de d3d9.h).
//
// Etapa 1 (este archivo): el dispositivo y todos sus recursos existen y se comportan como en Windows
// para la CPU (Lock/Unlock con memoria real, Get* devuelve lo fijado con Set*, consultas completas),
// pero todavía no se dibuja: Present mantiene el ritmo de 60 Hz. El renderizado Vulkan se apoya
// después en el estado que guarda este dispositivo.

#include <cstdint>

namespace hyperplay::d3d9dev32 {

struct CreateParams {
    uint32_t direct3d = 0;      // IDirect3D9 (proxy de 32 bits) que creó el dispositivo
    uint32_t adapter = 0;
    uint32_t deviceType = 1;
    uint32_t focusWindow = 0;
    uint32_t behaviorFlags = 0;
    uint32_t presentParams = 0;  // D3DPRESENT_PARAMETERS* del invitado
    uint32_t screenWidth = 1920;
    uint32_t screenHeight = 1080;
};

// Crea el dispositivo. Devuelve el puntero de 32 bits (0 si falla).
uint32_t createDevice(const CreateParams& params);

}  // namespace hyperplay::d3d9dev32
