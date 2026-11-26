#pragma once

// Modelo real del escritorio Win32 expuesto a los motores Windows.
//
// El dispositivo es un RedMagic 10 Pro (NX789J) en orientación horizontal:
//   - panel físico: 1216 x 2688, usado como 2688 x 1216
//   - frecuencia: 120 Hz
//
// Todas las estructuras se escriben con el layout exacto de Windows (WCHAR = 16 bits),
// nunca con el wchar_t de Bionic (32 bits). Este archivo es header-only para que las
// pruebas nativas del dispositivo validen exactamente el mismo código que usan los
// trampolines de NativeDllInterceptor.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace hyperplay::win32 {

// Modo del escritorio que ve el juego: la resolución NATIVA de la superficie del teléfono (en
// horizontal) y su frecuencia. Lo fija setDisplayMode() al conectar la superficie; hasta entonces
// vale el panel del RedMagic 10 Pro. Sin límites de resolución: el juego puede usar la nativa.
inline std::atomic<int32_t> g_screenWidth{2688};
inline std::atomic<int32_t> g_screenHeight{1216};
inline std::atomic<uint32_t> g_refreshHz{120};

inline int32_t screenWidth() noexcept { return g_screenWidth.load(std::memory_order_relaxed); }
inline int32_t screenHeight() noexcept { return g_screenHeight.load(std::memory_order_relaxed); }
inline uint32_t refreshHz() noexcept { return g_refreshHz.load(std::memory_order_relaxed); }

// Superficie de w x h (cualquier orientación) a hz: el escritorio Win32 es siempre horizontal.
inline void setDisplayMode(int32_t w, int32_t h, uint32_t hz) noexcept {
    if (w <= 0 || h <= 0) return;
    g_screenWidth.store(w > h ? w : h, std::memory_order_relaxed);
    g_screenHeight.store(w > h ? h : w, std::memory_order_relaxed);
    if (hz > 0) g_refreshHz.store(hz, std::memory_order_relaxed);
}
inline constexpr uintptr_t kPrimaryMonitor = 0xCAFE0010;
inline constexpr uintptr_t kPrimaryDeviceContext = 0xCAFE0020;
inline constexpr uint32_t kDisplayDeviceActive = 0x00000001;
inline constexpr uint32_t kDisplayDevicePrimary = 0x00000004;
inline constexpr uint32_t kMonitorInfoPrimary = 0x00000001;

inline constexpr int32_t kCwUseDefault = static_cast<int32_t>(0x80000000u);

// CreateWindowEx: CW_USEDEFAULT o tamaños <= 0 se convierten al tamaño real del panel.
inline constexpr int32_t normalizeWindowExtent(int32_t value, int32_t fallback) noexcept {
    return (value == kCwUseDefault || value <= 0) ? fallback : value;
}

// CreateWindowEx: CW_USEDEFAULT en la posición equivale a la esquina superior izquierda.
inline constexpr int32_t normalizeWindowOrigin(int32_t value) noexcept {
    return value == kCwUseDefault ? 0 : value;
}

inline void copyAsciiToWchar16(char16_t* dst, size_t capacity, const char* src) noexcept {
    if (dst == nullptr || capacity == 0) return;
    size_t i = 0;
    if (src != nullptr) {
        for (; i + 1 < capacity && src[i] != '\0'; ++i) {
            dst[i] = static_cast<char16_t>(static_cast<unsigned char>(src[i]));
        }
    }
    dst[i] = u'\0';
}

inline void fillRect(void* lpRect, int32_t left, int32_t top, int32_t right, int32_t bottom) noexcept {
    if (lpRect == nullptr) return;
    auto* r = static_cast<int32_t*>(lpRect);
    r[0] = left;
    r[1] = top;
    r[2] = right;
    r[3] = bottom;
}

inline void fillScreenRect(void* lpRect) noexcept {
    fillRect(lpRect, 0, 0, screenWidth(), screenHeight());
}

inline int32_t systemMetric(int32_t index) noexcept {
    switch (index) {
        case 0:   // SM_CXSCREEN
        case 16:  // SM_CXFULLSCREEN
        case 59:  // SM_CXMAXTRACK
        case 61:  // SM_CXMAXIMIZED
        case 78:  // SM_CXVIRTUALSCREEN
            return screenWidth();
        case 1:   // SM_CYSCREEN
        case 60:  // SM_CYMAXTRACK
        case 62:  // SM_CYMAXIMIZED
        case 79:  // SM_CYVIRTUALSCREEN
            return screenHeight();
        case 76:  // SM_XVIRTUALSCREEN (origen del escritorio virtual)
        case 77:  // SM_YVIRTUALSCREEN
            return 0;
        case 81:  // SM_SAMEDISPLAYFORMAT
            return 1;
        case 80:  // SM_CMONITORS
        case 19:  // SM_MOUSEPRESENT
            return 1;
        case 4:   // SM_CYCAPTION
        case 32:  // SM_CXFRAME
        case 33:  // SM_CYFRAME
        case 6:   // SM_CXBORDER
        case 7:   // SM_CYBORDER
            return 0;
        case 43:  // SM_CXICON
        case 44:  // SM_CYICON
        case 49:  // SM_CXCURSOR
        case 50:  // SM_CYCURSOR
            return 32;
        case 2:   // SM_CXVSCROLL
        case 3:   // SM_CYHSCROLL
            return 17;
        case 15:  // SM_CYMENU
            return 0;
        case 17:  // SM_CYFULLSCREEN
            return screenHeight();
        default:
            return 0;
    }
}

// Resoluciones que el juego puede elegir en sus opciones (como los modos de un monitor de PC),
// de menor a mayor como las enumera Windows: la nativa del teléfono y fracciones con su mismo
// aspecto (llenan la pantalla, reescaladas), más los 16:9 habituales (se ven con bandas negras a
// los lados, sin deformar). La última es siempre la nativa.
struct DisplayModeSize {
    int32_t w, h;
};
inline constexpr int kMaxDisplayModes = 8;

inline int displayModes(DisplayModeSize out[kMaxDisplayModes]) noexcept {
    const int32_t nw = screenWidth(), nh = screenHeight();
    auto even = [](int32_t v) { return v & ~1; };
    DisplayModeSize all[kMaxDisplayModes] = {
        {1280, 720}, {1600, 900}, {1920, 1080},                       // 16:9
        {even(nw / 2), even(nh / 2)}, {even(nw * 5 / 8), even(nh * 5 / 8)},  // aspecto nativo
        {even(nw * 3 / 4), even(nh * 3 / 4)},
        {nw, nh},
    };
    int n = 0;
    for (const auto& m : all) {
        if (m.w <= 0 || m.h <= 0 || m.w > nw || m.h > nh) continue;
        bool dup = false;
        for (int i = 0; i < n; ++i) dup = dup || (out[i].w == m.w && out[i].h == m.h);
        if (!dup && n < kMaxDisplayModes) out[n++] = m;
    }
    // Orden ascendente (ancho, después alto).
    for (int i = 1; i < n; ++i) {
        for (int j = i; j > 0 && (out[j].w < out[j - 1].w || (out[j].w == out[j - 1].w && out[j].h < out[j - 1].h)); --j) {
            const DisplayModeSize t = out[j];
            out[j] = out[j - 1];
            out[j - 1] = t;
        }
    }
    return n;
}

// DEVMODEW (Windows x64): sizeof = 220. modeNum: índice de displayModes(), o ENUM_CURRENT_SETTINGS
// (-1) / ENUM_REGISTRY_SETTINGS (-2) = la nativa. Devuelve false si no existe ese modo.
inline bool fillDevMode(void* lpDevMode, uint32_t modeNum) noexcept {
    if (lpDevMode == nullptr) return false;
    int32_t w = screenWidth(), h = screenHeight();
    if (modeNum != 0xFFFFFFFFu && modeNum != 0xFFFFFFFEu) {
        DisplayModeSize modes[kMaxDisplayModes];
        const int n = displayModes(modes);
        if (modeNum >= static_cast<uint32_t>(n)) return false;
        w = modes[modeNum].w;
        h = modes[modeNum].h;
    }
    auto* bytes = static_cast<uint8_t*>(lpDevMode);
    std::memset(bytes, 0, 220);
    *reinterpret_cast<uint16_t*>(bytes + 68) = 220;          // dmSize
    // dmFields: DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT | DM_DISPLAYFREQUENCY
    *reinterpret_cast<uint32_t*>(bytes + 72) = 0x005C0000u;
    *reinterpret_cast<uint32_t*>(bytes + 168) = 32;          // dmBitsPerPel
    *reinterpret_cast<uint32_t*>(bytes + 172) = static_cast<uint32_t>(w);
    *reinterpret_cast<uint32_t*>(bytes + 176) = static_cast<uint32_t>(h);
    *reinterpret_cast<uint32_t*>(bytes + 184) = refreshHz();  // dmDisplayFrequency
    return true;
}

// DISPLAY_DEVICEA (Windows x86/x64): sizeof = 424.
inline void fillDisplayDeviceA(void* lpDisplayDevice) noexcept {
    if (lpDisplayDevice == nullptr) return;
    auto* bytes = static_cast<uint8_t*>(lpDisplayDevice);
    std::memset(bytes, 0, 424);
    *reinterpret_cast<uint32_t*>(bytes + 0) = 424;  // cb
    std::strncpy(reinterpret_cast<char*>(bytes + 4), "\\\\.\\DISPLAY1", 31);
    std::strncpy(reinterpret_cast<char*>(bytes + 36), "Adreno 830 Direct Bionic", 127);
    *reinterpret_cast<uint32_t*>(bytes + 164) =
        kDisplayDeviceActive | kDisplayDevicePrimary;
    std::strncpy(reinterpret_cast<char*>(bytes + 168), "PCI\\VEN_5143&DEV_830", 127);
}

// DISPLAY_DEVICEW (Windows x86/x64): sizeof = 840, WCHAR = 16 bits.
inline void fillDisplayDeviceW(void* lpDisplayDevice) noexcept {
    if (lpDisplayDevice == nullptr) return;
    auto* bytes = static_cast<uint8_t*>(lpDisplayDevice);
    std::memset(bytes, 0, 840);
    *reinterpret_cast<uint32_t*>(bytes + 0) = 840;  // cb
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 4), 32, "\\\\.\\DISPLAY1");
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 68), 128, "Adreno 830 Direct Bionic");
    *reinterpret_cast<uint32_t*>(bytes + 324) =
        kDisplayDeviceActive | kDisplayDevicePrimary;
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 328), 128, "PCI\\VEN_5143&DEV_830");
}

// EnumDisplayDevices(lpDevice="\\.\DISPLAY1", 0): monitor conectado al adaptador.
inline constexpr uint32_t kDisplayDeviceAttached = 0x00000002;

inline void fillMonitorDeviceA(void* lpDisplayDevice) noexcept {
    if (lpDisplayDevice == nullptr) return;
    auto* bytes = static_cast<uint8_t*>(lpDisplayDevice);
    std::memset(bytes, 0, 424);
    *reinterpret_cast<uint32_t*>(bytes + 0) = 424;
    std::strncpy(reinterpret_cast<char*>(bytes + 4), "\\\\.\\DISPLAY1\\Monitor0", 31);
    std::strncpy(reinterpret_cast<char*>(bytes + 36), "Generic PnP Monitor", 127);
    *reinterpret_cast<uint32_t*>(bytes + 164) = kDisplayDeviceActive | kDisplayDeviceAttached;
    std::strncpy(reinterpret_cast<char*>(bytes + 168), "MONITOR\\NX789J", 127);
}

inline void fillMonitorDeviceW(void* lpDisplayDevice) noexcept {
    if (lpDisplayDevice == nullptr) return;
    auto* bytes = static_cast<uint8_t*>(lpDisplayDevice);
    std::memset(bytes, 0, 840);
    *reinterpret_cast<uint32_t*>(bytes + 0) = 840;
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 4), 32, "\\\\.\\DISPLAY1\\Monitor0");
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 68), 128, "Generic PnP Monitor");
    *reinterpret_cast<uint32_t*>(bytes + 324) = kDisplayDeviceActive | kDisplayDeviceAttached;
    copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 328), 128, "MONITOR\\NX789J");
}

// MONITORINFO / MONITORINFOEXW. rcMonitor@4, rcWork@20, dwFlags@36, szDevice@40.
inline void fillMonitorInfo(void* lpMonitorInfo) noexcept {
    if (lpMonitorInfo == nullptr) return;
    auto* bytes = static_cast<uint8_t*>(lpMonitorInfo);
    const uint32_t cbSize = *reinterpret_cast<const uint32_t*>(bytes);
    fillRect(bytes + 4, 0, 0, screenWidth(), screenHeight());   // rcMonitor
    fillRect(bytes + 20, 0, 0, screenWidth(), screenHeight());  // rcWork
    *reinterpret_cast<uint32_t*>(bytes + 36) = kMonitorInfoPrimary;
    if (cbSize >= 104) {
        copyAsciiToWchar16(reinterpret_cast<char16_t*>(bytes + 40), 32, "\\\\.\\DISPLAY1");
    }
}

// RECT de una ventana fullscreen sin bordes.
inline void fillWindowRect(void* lpRect) noexcept {
    fillScreenRect(lpRect);
}

inline int32_t deviceCaps(int32_t index) noexcept {
    switch (index) {
        case 8:   // HORZRES
        case 117: // DESKTOPHORZRES
            return screenWidth();
        case 10:  // VERTRES
        case 118: // DESKTOPVERTRES
            return screenHeight();
        case 12:  // BITSPIXEL
            return 32;
        case 14:  // PLANES
            return 1;
        case 88:  // LOGPIXELSX
        case 90:  // LOGPIXELSY
            return 96;
        case 116: // VREFRESH
            return static_cast<int32_t>(refreshHz());
        case 0:   // DRIVER_VERSION
            return 0x0001;
        case 2:   // TECHNOLOGY
            return 1;  // DT_RASDISPLAY
        default:
            return 1;
    }
}

// ClientToScreen/ScreenToClient sobre una ventana fullscreen en (0,0).
inline void translatePoint(void* lpPoint, int32_t dx, int32_t dy) noexcept {
    if (lpPoint == nullptr) return;
    auto* p = static_cast<int32_t*>(lpPoint);
    p[0] += dx;
    p[1] += dy;
}

}  // namespace hyperplay::win32
