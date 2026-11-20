#pragma once

#include <sys/types.h>

#include <cstdint>

// Ritmo de frames y modo eficiente: limitador preciso (clock_nanosleep absoluto), tope de FPS
// ajustable en caliente desde el Quick Menu, informe por frame a ADPF y anclaje del hilo
// principal del juego (Prime o relajado por la protección térmica).
namespace hyperplay::pacing {

/// Tope pedido por la app: >0 = FPS, 0 = sin límite, <0 = por defecto (la mitad del refresco).
/// `debug.hyperplay.fpscap` (si existe) tiene prioridad para pruebas A/B.
void setFpsCap(int fps) noexcept;

/// Tope en vigor (0 = sin límite).
[[nodiscard]] int effectiveCap() noexcept;

/// Hilo principal del juego (el despachador). Se ancla a Prime salvo protección térmica o
/// `setprop debug.hyperplay.primepin 0`.
void setMainThread(pid_t tid) noexcept;
[[nodiscard]] pid_t mainThread() noexcept;

/// Protección térmica: true = relaja el anclaje a Prime (todos los núcleos). Reversible.
void setThermalRelax(bool relax) noexcept;
[[nodiscard]] bool thermalRelaxed() noexcept;

/// Llamado en cada Present del juego, antes de enviar el frame: espera al instante del frame y
/// informa a ADPF del trabajo real del frame. Con el juego en pausa (Quick Resume / segundo
/// plano) bloquea aquí hasta reanudar: el hilo de render se queda quieto y el principal le espera.
void paceFrame() noexcept;

/// Pausa / reanuda el juego (Quick Resume, app en segundo plano, pantalla apagada).
void setPaused(bool paused) noexcept;
[[nodiscard]] bool paused() noexcept;

}  // namespace hyperplay::pacing
