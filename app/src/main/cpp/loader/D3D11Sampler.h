#pragma once

// Muestreo de texturas D3D11 para el rasterizador por software.
//
// - D3D11_SAMPLER_DESC (52 B): filtro (punto/lineal), direccionamiento wrap/mirror/clamp/border.
// - Lectura de texeles por formato DXGI (RGBA8/BGRA8/sRGB, R8, A8, RG8, RGBA16F, RGBA32F, R32F,
//   R16F, RG16F, RGB10A2).
// - Decodificación de bloques BC1, BC2, BC3, BC4 y BC5 a RGBA8 (los mips comprimidos se
//   descomprimen una vez y se cachean fuera de aquí) y BC7 (Bc7Decoder.h). BC6H aún no.

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

#include "Bc7Decoder.h"
#include "DxbcVm.h"
#include "SoftRaster.h"

namespace hyperplay::d3d11 {

using dxbc::Vec4;

struct SamplerDesc {
    uint32_t filter = 0x15;  // MIN_MAG_MIP_LINEAR
    uint32_t address[3] = {1, 1, 1};
    float mipBias = 0.0f;
    float border[4] = {0, 0, 0, 0};
    float minLod = 0.0f, maxLod = 3.402823e38f;
};

inline SamplerDesc parseSamplerDesc(const uint8_t* d) {
    SamplerDesc s;
    if (d == nullptr) return s;
    std::memcpy(&s.filter, d, 4);
    std::memcpy(s.address, d + 4, 12);
    std::memcpy(&s.mipBias, d + 16, 4);
    std::memcpy(s.border, d + 28, 16);
    std::memcpy(&s.minLod, d + 44, 4);
    std::memcpy(&s.maxLod, d + 48, 4);
    return s;
}

inline bool isBlockCompressed(uint32_t f) {
    return (f >= 70 && f <= 84) || (f >= 94 && f <= 99);
}

// --- BC1..BC5 -------------------------------------------------------------------------------

namespace bc {

inline void rgb565(uint16_t c, uint8_t* out) {
    out[0] = static_cast<uint8_t>(((c >> 11) & 31u) * 255u / 31u);
    out[1] = static_cast<uint8_t>(((c >> 5) & 63u) * 255u / 63u);
    out[2] = static_cast<uint8_t>((c & 31u) * 255u / 31u);
}

// Bloque de color BC1 (8 B) -> 16 píxeles RGBA8. alphaMode: BC1 admite transparencia (c0 <= c1).
inline void colorBlock(const uint8_t* b, uint8_t* px, bool bc1Alpha) {
    uint16_t c0, c1;
    std::memcpy(&c0, b, 2);
    std::memcpy(&c1, b + 2, 2);
    uint8_t pal[4][4];
    rgb565(c0, pal[0]);
    rgb565(c1, pal[1]);
    pal[0][3] = pal[1][3] = 255;
    if (c0 > c1 || !bc1Alpha) {
        for (int i = 0; i < 3; ++i) {
            pal[2][i] = static_cast<uint8_t>((2 * pal[0][i] + pal[1][i]) / 3);
            pal[3][i] = static_cast<uint8_t>((pal[0][i] + 2 * pal[1][i]) / 3);
        }
        pal[2][3] = pal[3][3] = 255;
    } else {
        for (int i = 0; i < 3; ++i) pal[2][i] = static_cast<uint8_t>((pal[0][i] + pal[1][i]) / 2);
        pal[2][3] = 255;
        pal[3][0] = pal[3][1] = pal[3][2] = pal[3][3] = 0;
    }
    uint32_t idx;
    std::memcpy(&idx, b + 4, 4);
    for (int i = 0; i < 16; ++i) std::memcpy(px + i * 4, pal[(idx >> (2 * i)) & 3u], 4);
}

// Bloque de 8 B estilo BC3/BC4 (dos extremos + índices de 3 bits) -> 16 valores.
inline void interpBlock(const uint8_t* b, uint8_t* out16) {
    const uint32_t a0 = b[0], a1 = b[1];
    uint8_t pal[8];
    pal[0] = static_cast<uint8_t>(a0);
    pal[1] = static_cast<uint8_t>(a1);
    if (a0 > a1) {
        for (int i = 1; i < 7; ++i) pal[i + 1] = static_cast<uint8_t>(((7 - i) * a0 + i * a1) / 7);
    } else {
        for (int i = 1; i < 5; ++i) pal[i + 1] = static_cast<uint8_t>(((5 - i) * a0 + i * a1) / 5);
        pal[6] = 0;
        pal[7] = 255;
    }
    uint64_t bits = 0;
    for (int i = 0; i < 6; ++i) bits |= static_cast<uint64_t>(b[2 + i]) << (8 * i);
    for (int i = 0; i < 16; ++i) out16[i] = pal[(bits >> (3 * i)) & 7u];
}

// Un bloque 4x4 del formato -> 64 bytes RGBA8. false si el formato no se soporta.
inline bool decodeBlock(uint32_t f, const uint8_t* b, uint8_t* px) {
    switch (f) {
        case 71:
        case 72:  // BC1
            colorBlock(b, px, true);
            return true;
        case 74:
        case 75: {  // BC2: alfa explícito de 4 bits
            colorBlock(b + 8, px, false);
            for (int i = 0; i < 16; ++i) {
                const uint8_t a4 = static_cast<uint8_t>((b[i / 2] >> ((i & 1) * 4)) & 0xFu);
                px[i * 4 + 3] = static_cast<uint8_t>(a4 * 17);
            }
            return true;
        }
        case 77:
        case 78: {  // BC3
            uint8_t a[16];
            interpBlock(b, a);
            colorBlock(b + 8, px, false);
            for (int i = 0; i < 16; ++i) px[i * 4 + 3] = a[i];
            return true;
        }
        case 80: {  // BC4_UNORM -> R
            uint8_t r[16];
            interpBlock(b, r);
            for (int i = 0; i < 16; ++i) {
                px[i * 4] = r[i];
                px[i * 4 + 1] = px[i * 4 + 2] = 0;
                px[i * 4 + 3] = 255;
            }
            return true;
        }
        case 83: {  // BC5_UNORM -> RG
            uint8_t r[16], g[16];
            interpBlock(b, r);
            interpBlock(b + 8, g);
            for (int i = 0; i < 16; ++i) {
                px[i * 4] = r[i];
                px[i * 4 + 1] = g[i];
                px[i * 4 + 2] = 0;
                px[i * 4 + 3] = 255;
            }
            return true;
        }
        case 98:
        case 99:  // BC7_UNORM / BC7_UNORM_SRGB
            bc7::decodeBlock(b, px);
            return true;
        default:
            return false;
    }
}

inline uint32_t blockBytes(uint32_t f) {
    return (f >= 70 && f <= 72) || (f >= 79 && f <= 81) ? 8u : 16u;
}

}  // namespace bc

// Descomprime un mip BC a RGBA8 (w*h*4). false si el formato BC no se soporta.
inline bool decompressToRgba8(uint32_t format, const uint8_t* src, uint32_t width, uint32_t height,
                              uint32_t rowPitch, std::vector<uint8_t>& out) {
    out.assign(static_cast<size_t>(width) * height * 4, 0);
    const uint32_t bw = (width + 3) / 4, bh = (height + 3) / 4, bb = bc::blockBytes(format);
    uint8_t px[64];
    for (uint32_t by = 0; by < bh; ++by) {
        for (uint32_t bx = 0; bx < bw; ++bx) {
            if (!bc::decodeBlock(format, src + static_cast<size_t>(by) * rowPitch + bx * bb, px)) return false;
            for (uint32_t y = 0; y < 4; ++y) {
                const uint32_t yy = by * 4 + y;
                if (yy >= height) break;
                for (uint32_t x = 0; x < 4; ++x) {
                    const uint32_t xx = bx * 4 + x;
                    if (xx >= width) break;
                    std::memcpy(&out[(static_cast<size_t>(yy) * width + xx) * 4], px + (y * 4 + x) * 4, 4);
                }
            }
        }
    }
    return true;
}

// Un mip 2D listo para leer (formato DXGI sin comprimir; los BC llegan ya en RGBA8).
struct TexView {
    const uint8_t* data = nullptr;
    uint32_t width = 0, height = 0, rowPitch = 0, format = 0;
    bool srgb = false;  // para BC descomprimidos de formatos _SRGB
};

inline bool fetchTexel(const TexView& t, uint32_t x, uint32_t y, Vec4& out) {
    const uint8_t* row = t.data + static_cast<size_t>(y) * t.rowPitch;
    out = Vec4();
    out.f[3] = 1.0f;
    switch (t.format) {
        case 28:
        case 29:
        case 87:
        case 91:
        case 88:  // B8G8R8X8
        case 10:
        case 2:
        case 24:
            if (t.format == 88) {
                const uint8_t* p = row + x * 4;
                out.f[0] = raster::unorm8(p[2]);
                out.f[1] = raster::unorm8(p[1]);
                out.f[2] = raster::unorm8(p[0]);
                return true;
            }
            return raster::readPixel(t.format, row + x * raster::renderTargetBytes(t.format), out.f);
        case 61:  // R8_UNORM
            out.f[0] = raster::unorm8(row[x]);
            return true;
        case 65:  // A8_UNORM
            out.f[3] = raster::unorm8(row[x]);
            return true;
        case 49:  // R8G8_UNORM
            out.f[0] = raster::unorm8(row[x * 2]);
            out.f[1] = raster::unorm8(row[x * 2 + 1]);
            return true;
        case 41:  // R32_FLOAT
            std::memcpy(out.f, row + x * 4, 4);
            return true;
        case 54: {  // R16_FLOAT
            uint16_t h;
            std::memcpy(&h, row + x * 2, 2);
            out.f[0] = raster::halfToFloat(h);
            return true;
        }
        case 34: {  // R16G16_FLOAT
            uint16_t h[2];
            std::memcpy(h, row + x * 4, 4);
            out.f[0] = raster::halfToFloat(h[0]);
            out.f[1] = raster::halfToFloat(h[1]);
            return true;
        }
        case 0xFFFFFFFFu: {  // RGBA8 descomprimido de BC
            const uint8_t* p = row + x * 4;
            for (int i = 0; i < 4; ++i) out.f[i] = raster::unorm8(p[i]);
            if (t.srgb) {
                for (int i = 0; i < 3; ++i) out.f[i] = raster::srgb8ToLinear(p[i]);
            }
            return true;
        }
        default:
            return false;
    }
}

// Coordenada de texel según el modo de direccionamiento; -1 = color de borde.
inline int addressTexel(int i, int size, uint32_t mode) {
    if (size <= 0) return -1;
    switch (mode) {
        case 1:  // WRAP
            i %= size;
            return i < 0 ? i + size : i;
        case 2: {  // MIRROR
            const int period = 2 * size;
            int m = i % period;
            if (m < 0) m += period;
            return m < size ? m : period - 1 - m;
        }
        case 4:  // BORDER
            return (i < 0 || i >= size) ? -1 : i;
        case 5: {  // MIRROR_ONCE
            const int a = i < 0 ? -i - 1 : i;
            return std::min(a, size - 1);
        }
        default:  // CLAMP
            return std::clamp(i, 0, size - 1);
    }
}

inline Vec4 texelAt(const TexView& t, const SamplerDesc& s, int x, int y) {
    const int ax = addressTexel(x, static_cast<int>(t.width), s.address[0]);
    const int ay = addressTexel(y, static_cast<int>(t.height), s.address[1]);
    Vec4 v;
    if (ax < 0 || ay < 0) {
        std::memcpy(v.f, s.border, 16);
        return v;
    }
    fetchTexel(t, static_cast<uint32_t>(ax), static_cast<uint32_t>(ay), v);
    return v;
}

// Camino rápido para texeles de 8 bits por canal (RGBA8/BGRA8, sRGB y BC descomprimido) sin
// modo borde: lee los bytes directamente (el genérico pasaba por 3 switch por texel).
inline bool rgba8Layout(const TexView& t, int& r, int& b, bool& srgb) {
    switch (t.format) {
        case 28: r = 0; b = 2; srgb = false; return true;
        case 29: r = 0; b = 2; srgb = true; return true;
        case 87: r = 2; b = 0; srgb = false; return true;
        case 91: r = 2; b = 0; srgb = true; return true;
        case 0xFFFFFFFFu: r = 0; b = 2; srgb = t.srgb; return true;
        default: return false;
    }
}

inline void texel8(const uint8_t* p, int ri, int bi, bool srgb, float* out) {
    if (srgb) {
        out[0] = raster::srgb8ToLinear(p[ri]);
        out[1] = raster::srgb8ToLinear(p[1]);
        out[2] = raster::srgb8ToLinear(p[bi]);
    } else {
        out[0] = raster::unorm8(p[ri]);
        out[1] = raster::unorm8(p[1]);
        out[2] = raster::unorm8(p[bi]);
    }
    out[3] = raster::unorm8(p[3]);
}

// Muestreo 2D sin derivadas: punto o bilineal según el filtro (mag/min lineal o anisótropo).
inline Vec4 sample2D(const TexView& t, const SamplerDesc& s, float u, float v, const int8_t* offset) {
    const float fu = u * static_cast<float>(t.width), fv = v * static_cast<float>(t.height);
    const int ox = offset != nullptr ? offset[0] : 0, oy = offset != nullptr ? offset[1] : 0;
    const bool linear = (s.filter & 0x14u) != 0 || (s.filter & 0x40u) != 0;
    int ri = 0, bi = 2;
    bool srgb = false;
    if (s.address[0] != 4 && s.address[1] != 4 && rgba8Layout(t, ri, bi, srgb) && std::isfinite(fu) &&
        std::isfinite(fv) && std::fabs(fu) < 1e8f && std::fabs(fv) < 1e8f) {
        const int w = static_cast<int>(t.width), h = static_cast<int>(t.height);
        Vec4 r;
        if (!linear) {
            const int x = addressTexel(static_cast<int>(std::floor(fu)) + ox, w, s.address[0]);
            const int y = addressTexel(static_cast<int>(std::floor(fv)) + oy, h, s.address[1]);
            texel8(t.data + static_cast<size_t>(y) * t.rowPitch + static_cast<size_t>(x) * 4, ri, bi, srgb, r.f);
            return r;
        }
        const float xf = fu - 0.5f, yf = fv - 0.5f;
        const int x0 = static_cast<int>(std::floor(xf)), y0 = static_cast<int>(std::floor(yf));
        const float ax = xf - static_cast<float>(x0), ay = yf - static_cast<float>(y0);
        const int xa = addressTexel(x0 + ox, w, s.address[0]), xb = addressTexel(x0 + 1 + ox, w, s.address[0]);
        const int ya = addressTexel(y0 + oy, h, s.address[1]), yb = addressTexel(y0 + 1 + oy, h, s.address[1]);
        const uint8_t* rowA = t.data + static_cast<size_t>(ya) * t.rowPitch;
        const uint8_t* rowB = t.data + static_cast<size_t>(yb) * t.rowPitch;
        float c00[4], c10[4], c01[4], c11[4];
        texel8(rowA + xa * 4, ri, bi, srgb, c00);
        texel8(rowA + xb * 4, ri, bi, srgb, c10);
        texel8(rowB + xa * 4, ri, bi, srgb, c01);
        texel8(rowB + xb * 4, ri, bi, srgb, c11);
        for (int c = 0; c < 4; ++c) {
            const float top = c00[c] + (c10[c] - c00[c]) * ax;
            const float bot = c01[c] + (c11[c] - c01[c]) * ax;
            r.f[c] = top + (bot - top) * ay;
        }
        return r;
    }
    if (!linear) {
        return texelAt(t, s, static_cast<int>(std::floor(fu)) + ox, static_cast<int>(std::floor(fv)) + oy);
    }
    const float x = fu - 0.5f, y = fv - 0.5f;
    const int x0 = static_cast<int>(std::floor(x)), y0 = static_cast<int>(std::floor(y));
    const float ax = x - static_cast<float>(x0), ay = y - static_cast<float>(y0);
    const Vec4 c00 = texelAt(t, s, x0 + ox, y0 + oy), c10 = texelAt(t, s, x0 + 1 + ox, y0 + oy);
    const Vec4 c01 = texelAt(t, s, x0 + ox, y0 + 1 + oy), c11 = texelAt(t, s, x0 + 1 + ox, y0 + 1 + oy);
    Vec4 r;
    for (int c = 0; c < 4; ++c) {
        const float top = c00.f[c] + (c10.f[c] - c00.f[c]) * ax;
        const float bot = c01.f[c] + (c11.f[c] - c01.f[c]) * ax;
        r.f[c] = top + (bot - top) * ay;
    }
    return r;
}

}  // namespace hyperplay::d3d11
