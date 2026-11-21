#pragma once

// Vulkan directo para juegos que traen su propio renderizador Vulkan (FNA3D/SDL GPU en Celeste).
//
// El juego (x86-64 traducido) llama a vulkan-1.dll. Las estructuras de Vulkan tienen el mismo
// layout en Win64 (LLP64) y en Android arm64 (LP64): solo usa tipos de tamaño fijo, punteros y
// size_t. Así que casi todas las funciones se pasan TAL CUAL al Vulkan del teléfono: el puntero
// nativo se registra como trampolín y el despachador de thunks mueve los argumentos Win64 a
// AAPCS64. Solo se envuelve lo que depende de Windows:
//   - VK_KHR_win32_surface <-> VK_KHR_android_surface (extensiones, vkCreateInstance, superficie)
//   - vkGet*ProcAddr (para registrar cada puntero que se entrega)
//   - vkQueuePresentKHR (cuenta frames para el HUD y la pantalla de carga)
//   - depuración (VK_EXT_debug_utils/report se ocultan: sus callbacks serían código invitado)

#include <cstdint>

namespace hyperplay::vkpass {

using RegisterFn = void (*)(void* fn, const char* name);

/// `reg` registra un puntero de función nativo como trampolín (NativeDllInterceptor).
void init(RegisterFn reg);

/// Fotogramas que el juego ha presentado por Vulkan (para quitar la pantalla de carga).
uint64_t presentCount();

/// GetProcAddress(vulkan-1.dll, name). nullptr si no existe.
void* guestProcAddr(const char* name);

}  // namespace hyperplay::vkpass
