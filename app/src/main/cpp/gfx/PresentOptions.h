#pragma once

#include <sys/system_properties.h>

#include <atomic>
#include <cstring>

// Opciones del pase de presentación que el Quick Menu cambia en caliente (sin recrear nada).
namespace hyperplay::gpu::presentopt {

enum Upscaler : int { kAuto = 0, kSgsr = 1, kOff = 2 };

inline std::atomic<int> g_upscaler{kAuto};
inline std::atomic<float> g_sharpness{0.30f};

/// Modo en vigor: `debug.hyperplay.upscaler` (auto/sgsr/off) tiene prioridad para pruebas A/B.
inline int upscaler() noexcept {
    static const int s_prop = [] {
        char v[PROP_VALUE_MAX] = {};
        if (__system_property_get("debug.hyperplay.upscaler", v) <= 0) return -1;
        if (std::strcmp(v, "sgsr") == 0) return static_cast<int>(kSgsr);
        if (std::strcmp(v, "off") == 0) return static_cast<int>(kOff);
        if (std::strcmp(v, "auto") == 0) return static_cast<int>(kAuto);
        return -1;
    }();
    return s_prop >= 0 ? s_prop : g_upscaler.load(std::memory_order_relaxed);
}

/// Ajuste a la pantalla de un juego que dibuja en 16:9 con barras negras dentro de un backbuffer
/// más ancho (p. ej. Cuphead a 2688x1216): Original (tal cual), Estirar (el área 16:9 a toda la
/// pantalla) o Ampliar (llena el ancho sin deformar; recorta arriba y abajo).
enum Fit : int { kFitOriginal = 0, kFitStretch = 1, kFitZoom = 2 };
inline std::atomic<int> g_fit{kFitOriginal};

/// Recorta las UV del pase de presentación. `pc` = {m.xy, m.zw, o.xy, ...}: uv = M·q + o sobre el
/// backbuffer de srcW x srcH; el resultado es uv' = c + s·uv. dstW x dstH = swapchain (puede venir
/// rotado: se usa su lado largo / corto).
inline void applyFit(float pc[8], uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH) noexcept {
    const int fit = g_fit.load(std::memory_order_relaxed);
    if (fit == kFitOriginal || srcW == 0 || srcH == 0 || dstW == 0 || dstH == 0) return;
    constexpr float kContent = 16.0f / 9.0f;
    const float srcAspect = static_cast<float>(srcW) / static_cast<float>(srcH);
    if (srcAspect <= kContent * 1.01f) return;  // ya es 16:9 o más estrecho: nada que llenar
    const float cw = kContent / srcAspect;      // ancho del área 16:9 dentro del backbuffer
    const float longSide = static_cast<float>(dstW > dstH ? dstW : dstH);
    const float shortSide = static_cast<float>(dstW > dstH ? dstH : dstW);
    const float fh = fit == kFitZoom ? kContent / (longSide / shortSide) : 1.0f;
    const float sx = cw, sy = fh, cx = (1.0f - cw) * 0.5f, cy = (1.0f - fh) * 0.5f;
    pc[0] *= sx; pc[1] *= sx; pc[2] *= sy; pc[3] *= sy;
    pc[4] = cx + sx * pc[4];
    pc[5] = cy + sy * pc[5];
}

/// ¿SGSR para un backbuffer de srcW x srcH presentado en un swapchain de dstW x dstH?
/// Auto: solo si el juego renderiza por debajo del swapchain (por área: el swapchain puede estar
/// rotado respecto al backbuffer).
inline bool useSgsr(uint32_t srcW, uint32_t srcH, uint32_t dstW, uint32_t dstH) noexcept {
    switch (upscaler()) {
        case kSgsr: return true;
        case kOff: return false;
        default: return static_cast<uint64_t>(srcW) * srcH < static_cast<uint64_t>(dstW) * dstH;
    }
}

}  // namespace hyperplay::gpu::presentopt
