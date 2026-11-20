#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <sys/types.h>

// Perfilador de muestreo para medir dónde gasta CPU un juego (carga y frames).
//
// Se activa con `setprop debug.hyperplay.prof <segundos>`: un timer de tiempo de CPU del proceso
// (CLOCK_PROCESS_CPUTIME_ID, 1 ms) envía SIGPROF y el manejador solo guarda el PC del host en un
// anillo sin bloqueos. Cada bloque traducido registra su rango host -> RIP del huésped. Pasados
// los segundos pedidos, un hilo agrupa las muestras por bloque del huésped (y por símbolo nativo
// cuando el PC cae en libhyperplay/libc) y escribe el top en logcat con la etiqueta
// `HyperPlay.Prof`. Sin la propiedad no hay coste: `enabled()` es una lectura de un bool.
namespace hyperplay::prof {

// true si el perfilador está activo en este proceso (propiedad leída una vez).
bool enabled() noexcept;

// Arranca el timer y el hilo de volcado (idempotente). Llamar al empezar a ejecutar el juego;
// el reporte se escribe también en <savesDir>/profile.txt.
void startIfRequested(const std::string& savesDir);

// Hilo principal del juego, para `setprop debug.hyperplay.proftid main` (perfil de un solo hilo).
void setMainThread(pid_t tid) noexcept;

// Registra un bloque traducido: [hostStart, hostStart + hostSize) ejecuta el bloque guestRip.
void registerBlock(uintptr_t hostStart, size_t hostSize, uintptr_t guestRip);

}  // namespace hyperplay::prof
