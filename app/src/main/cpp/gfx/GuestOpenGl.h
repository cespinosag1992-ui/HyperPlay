#pragma once

// opengl32.dll de HyperPlay: WGL + OpenGL de escritorio sobre EGL/GLES 3.2 del teléfono.
//
// Juegos que dibujan con OpenGL (MonoGame DesktopGL vía SDL2: Stardew Valley). Las funciones
// GL que solo llevan enteros y punteros se entregan tal cual (puntero de libGLESv3, la ABI
// coincide registro a registro); las que llevan float/double (en XMM en Win64) o que difieren
// entre GL de escritorio y GLES llevan un envoltorio. El contexto WGL es un contexto EGL; la
// ventana del juego es el ANativeWindow de la sesión (el mismo que usa Vulkan, cedido).
//
// Además, el GDI mínimo que SDL necesita para su vídeo Win32 (formatos de píxel, DC, mapas de
// bits de cursor) y SwapBuffers.

#include <cstdint>

namespace hyperplay::guestgl {

using RegisterFn = void (*)(void* fn, const char* name);
/// Registro XMMn del hilo invitado en curso (16 bytes) o nullptr.
using XmmFn = const uint8_t* (*)(int index);

void init(RegisterFn reg, XmmFn xmm);

/// Exportación de opengl32.dll (wgl* y gl*), también lo que devuelve wglGetProcAddress.
void* glProcAddr(const char* name);

/// gdi32.dll: ChoosePixelFormat, SetPixelFormat, DescribePixelFormat, GetPixelFormat,
/// SwapBuffers, CreateDC*, DeleteDC, CreateCompatibleDC, CreateDIBSection, CreateBitmap,
/// DeleteObject, SelectObject, GetObjectW. nullptr si no es de las suyas.
void* gdiProcAddr(const char* name);

/// user32.dll: CreateIconIndirect / CreateCursor / DestroyCursor (handles de cursor ficticios).
void* user32ProcAddr(const char* name);

/// Fotogramas presentados con SwapBuffers (pantalla de carga, HUD).
uint64_t presentCount();

}  // namespace hyperplay::guestgl
