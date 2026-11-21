#pragma once

#include <vulkan/vulkan.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <map>
#include <mutex>
#include <thread>

// HyperFG: generación de fotogramas 2x por flujo óptico en Vulkan Compute.
//
// Opera en el espacio del backbuffer del juego (antes del pase de presentación, así la rotación
// pre-transform y el escalado/SGSR no cambian). Por cada frame real N:
//   hilo de render: copia el backbuffer al historial (A/B en GPU), pirámide de luminancia, flujo
//                   coarse-to-fine e interpolación t = 0.5 -> presenta interp(N-1, N) ahora;
//   hilo HyperFG:   a t + medio intervalo (clock_nanosleep absoluto) presenta el frame real N.
// Todos los recursos se crean una vez (al activar o si cambia el tamaño) y se liberan al
// desactivar. Sin asignaciones por frame ni vkDeviceWaitIdle: barreras y fences por frame.
// Apagado (por defecto) no hace nada: la ruta de presentación es exactamente la de siempre.
#include "DirectVulkanBridge.h"

namespace hyperplay::gpu {

/// Dispositivo y swapchain donde trabaja HyperFG: el de HyperPlay (DirectVulkanBridge, juegos
/// D3D) o el del juego (GuestSwapchain, juegos con Vulkan propio).
class FrameGenHost {
public:
    virtual ~FrameGenHost() = default;
    virtual bool gpuContext(DirectVulkanBridge::GpuContext& out) = 0;
    /// Si devuelve true, el llamador DEBE llamar a endPresent.
    virtual bool beginPresent(DirectVulkanBridge::SwapTarget& out) = 0;
    virtual bool endPresent(const DirectVulkanBridge::SwapTarget& target, VkCommandBuffer cmd, VkFence fence,
                            bool countFrame) = 0;
};

class HyperFrameGen {
public:
    /// HyperFG del renderizador D3D de HyperPlay.
    static HyperFrameGen& shared();
    /// HyperFG propio para otro dispositivo (el del juego).
    explicit HyperFrameGen(FrameGenHost* host) : host_(host) {}

    /// Imagen del último frame intermedio (R8G8B8A8_UNORM, SHADER_READ_ONLY_OPTIMAL tras record()).
    [[nodiscard]] VkImage interpImage() noexcept;

    /// Interruptor del Quick Menu; `debug.hyperplay.fg` (0/1) tiene prioridad (A/B).
    static void setEnabled(bool on) noexcept;
    [[nodiscard]] static bool enabled() noexcept;

    /// Hilo de render, dentro del Present y antes de adquirir el swapchain. `src` (el backbuffer)
    /// debe estar en SHADER_READ_ONLY_OPTIMAL y queda igual. `gameFps` = tope del juego.
    /// Devuelve la vista del frame intermedio (SHADER_READ_ONLY) para presentar ahora, o
    /// VK_NULL_HANDLE si este ciclo no se genera (primer frame, frame lento > 22 ms, sin tope,
    /// recursos no disponibles): entonces se presenta `src` como siempre.
    VkImageView record(VkCommandBuffer cmd, VkImage src, VkFormat format, uint32_t width, uint32_t height,
                       int gameFps);

    /// Hilo de render, tras enviar el Present del frame intermedio: programa el frame real a
    /// `atNs` (CLOCK_MONOTONIC) en el hilo HyperFG.
    void scheduleReal(int64_t atNs);

    /// Hilo de render, en cada Present con HyperFG apagado: libera los recursos si quedaban.
    void releaseIfIdle();

    // Repetición de frames (sin generación): con el juego a la mitad del refresco (60 en un
    // panel de 120 Hz) cada frame se presenta dos veces, el mismo, a medio intervalo. La imagen
    // es idéntica (cada frame dura 2 vsyncs igual que antes), pero el sistema ve 120
    // presentaciones/s: el control de frecuencia de nubia (RedMagic) deja de limitar la CPU, que
    // con 60 presentaciones/s bajaba a 1,5 GHz y provocaba frames de 25 ms (57-58 FPS).
    // `setprop debug.hyperplay.repeat 0` lo desactiva.
    static constexpr int kRepeatMaxFps = 60;
    [[nodiscard]] static bool repeatWanted(int gameFps, int panelHz) noexcept;
    /// Como record() pero solo copia el backbuffer al historial (luego scheduleReal lo repite).
    /// false si no se pudo (sin recursos): entonces no se programa la repetición.
    bool recordRepeat(VkCommandBuffer cmd, VkImage src, VkFormat format, uint32_t width, uint32_t height, int gameFps);

    struct Stats {
        uint64_t generated = 0, skippedSlow = 0, realPresents = 0, cancelled = 0;
    };
    [[nodiscard]] Stats stats() const noexcept;

    // Umbrales (compartidos con los tests de la lógica en Kotlin: FrameGenLogic).
    static constexpr int64_t kSlowFrameNs = 22000000;  // escudo 4: frame real > 22 ms -> sin sintético
    static constexpr float kStaticThr = 0.02f;         // escudo 1 (|Δcolor|)
    static constexpr float kConfThr = 0.10f;           // escudo 3
    static constexpr float kCutThr = 0.12f;            // escudo 2 (Δ luminancia media)

private:
    FrameGenHost* host_;
    struct Impl;
    Impl* impl_ = nullptr;
    std::mutex implMutex_;
    Impl& impl();
};

}  // namespace hyperplay::gpu
