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
