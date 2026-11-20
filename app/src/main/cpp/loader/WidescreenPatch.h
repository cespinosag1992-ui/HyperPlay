#pragma once

// Panorámica real para juegos que se fijan a 16:9 con barras (sin estirar ni recortar).
//
// Cuphead: `AbstractCupheadCamera.UpdateRect()` (Assembly-CSharp.dll, cada LateUpdate) calcula
//   aspect = Screen.width / Screen.height; s = 1 - 0.1 * overscan
//   aspect > 16/9 ? rect(w = s * (16/9) / aspect, h = s) : rect(w = s, h = s * aspect / (16/9))
// y se lo pone a todas sus cámaras (juego, fondo, UI). Cambiando las tres constantes 16/9 por la
// proporción real de la pantalla, el rectángulo queda a pantalla completa con la misma altura:
// la cámara ortográfica conserva su tamaño vertical y Unity dibuja más escenario a los lados.
//
// El parche se busca por firma de IL (con comodines en los tokens de metadatos), no por offset,
// así que sirve para otras compilaciones del juego mientras el método no cambie. Si la firma no
// aparece, no se toca nada.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hyperplay::widescreen {

/// Firma del principio de UpdateRect. -1 = comodín (tokens de metadatos de esa compilación).
inline constexpr int kCupheadUpdateRectSig[] = {
    0x28, -1, -1, -1, 0x0A,              // call Screen.get_width
    0x6B,                                // conv.r4
    0x28, -1, -1, -1, 0x0A,              // call Screen.get_height
    0x6B, 0x5B, 0x0A,                    // conv.r4; div; stloc.0
    0x22, 0x00, 0x00, 0x80, 0x3F,        // ldc.r4 1.0
    0x22, 0xCD, 0xCC, 0xCC, 0x3D,        // ldc.r4 0.1
    0x28, -1, -1, -1, 0x06,              // call SettingsData.get_Data
    0x7B, -1, -1, -1, 0x04,              // ldfld overscan
    0x5A, 0x59, 0x0B,                    // mul; sub; stloc.1
    0x06,                                // ldloc.0
    0x22, 0x39, 0x8E, 0xE3, 0x3F,        // ldc.r4 1.7777778 (16/9)
};
inline constexpr size_t kCupheadUpdateRectIlSize = 231;
inline constexpr uint8_t kLdcR4_16by9[] = {0x22, 0x39, 0x8E, 0xE3, 0x3F};

/// Busca la firma en `bytes`. @return offset o SIZE_MAX.
inline size_t findSignature(const uint8_t* bytes, size_t size, const int* sig, size_t sigLen) noexcept {
    if (sigLen == 0 || size < sigLen) return SIZE_MAX;
    for (size_t i = 0; i + sigLen <= size; ++i) {
        size_t k = 0;
        while (k < sigLen && (sig[k] < 0 || bytes[i + k] == static_cast<uint8_t>(sig[k]))) ++k;
        if (k == sigLen) return i;
    }
    return SIZE_MAX;
}

/// Cambia las constantes 16/9 de UpdateRect por `aspect` (ancho / alto de la pantalla).
/// @return número de constantes cambiadas (3 si el parche se aplicó; 0 si no se encontró).
inline int patchCupheadUpdateRect(uint8_t* bytes, size_t size, float aspect) noexcept {
    if (!(aspect > 1.0f) || aspect > 8.0f) return 0;
    const size_t sigLen = sizeof(kCupheadUpdateRectSig) / sizeof(kCupheadUpdateRectSig[0]);
    const size_t at = findSignature(bytes, size, kCupheadUpdateRectSig, sigLen);
    if (at == SIZE_MAX) return 0;
    // Solo una aparición: si hubiera otra igual, no se sabe cuál es y no se toca nada.
    if (findSignature(bytes + at + 1, size - at - 1, kCupheadUpdateRectSig, sigLen) != SIZE_MAX) return 0;
    const size_t end = at + kCupheadUpdateRectIlSize <= size ? at + kCupheadUpdateRectIlSize : size;
    size_t hits[3];
    int n = 0;
    for (size_t i = at; i + sizeof(kLdcR4_16by9) <= end; ++i) {
        if (std::memcmp(bytes + i, kLdcR4_16by9, sizeof(kLdcR4_16by9)) != 0) continue;
        if (n == 3) return 0;  // más de 3: no es el método esperado
        hits[n++] = i;
    }
    if (n != 3) return 0;
    for (size_t h : hits) std::memcpy(bytes + h + 1, &aspect, sizeof(float));
    return n;
}

/// ¿Es el ensamblado del juego de Cuphead? (`.../Cuphead_Data/Managed/Assembly-CSharp.dll`)
inline bool isCupheadAssembly(const char* path) noexcept {
    static constexpr char kSuffix[] = "Cuphead_Data/Managed/Assembly-CSharp.dll";
    const size_t n = std::strlen(path), m = sizeof(kSuffix) - 1;
    if (n < m) return false;
    for (size_t i = 0; i < m; ++i) {
        char a = path[n - m + i], b = kSuffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a == '\\') a = '/';
        if (a != b) return false;
    }
    return true;
}

}  // namespace hyperplay::widescreen
