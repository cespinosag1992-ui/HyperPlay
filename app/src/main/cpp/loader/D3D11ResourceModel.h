#pragma once

// Modelo de recursos D3D11 en memoria (buffers y texturas 1D/2D/3D) independiente de COM.
//
// - Tamaños de formato DXGI reales (incluidos bloques BC1..BC7).
// - Layout por subrecurso (índice = mip + slice * MipLevels) con RowPitch/DepthPitch reales.
// - Inicialización (D3D11_SUBRESOURCE_DATA), Map, UpdateSubresource, Copy* y limpieza con color.
// - Conversión del backbuffer a RGBA8 para presentarlo.
// Header-only para que la suite nativa valide exactamente el código que usan los trampolines.

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

namespace hyperplay::d3d11 {

enum class Dimension : uint32_t { Unknown = 0, Buffer = 1, Texture1D = 2, Texture2D = 3, Texture3D = 4 };

struct FormatInfo {
    uint32_t bytes;      // bytes por elemento (o por bloque si block == true)
    bool block;          // compresión BC en bloques de 4x4
    bool depth;          // formato de profundidad/estencil
};

// DXGI_FORMAT -> tamaño. Formatos desconocidos o de vídeo: 4 bytes (conservador).
inline FormatInfo formatInfo(uint32_t f) noexcept {
    if (f >= 1 && f <= 4) return {16, false, false};
    if (f >= 5 && f <= 8) return {12, false, false};
    if (f >= 9 && f <= 18) return {8, false, false};
    if (f == 19 || f == 20) return {8, false, f == 20};
    if (f == 21 || f == 22) return {8, false, false};
    if (f >= 23 && f <= 39) return {4, false, false};
    if (f == 40) return {4, false, true};
    if (f >= 41 && f <= 44) return {4, false, false};
    if (f == 45) return {4, false, true};
    if (f >= 46 && f <= 47) return {4, false, false};
    if (f >= 48 && f <= 54) return {2, false, false};
    if (f == 55) return {2, false, true};
    if (f >= 56 && f <= 59) return {2, false, false};
    if (f >= 60 && f <= 66) return {1, false, false};
    if (f == 67) return {4, false, false};
    if (f == 68 || f == 69) return {2, false, false};
    if ((f >= 70 && f <= 72) || (f >= 79 && f <= 81)) return {8, true, false};   // BC1, BC4
    if ((f >= 73 && f <= 78) || (f >= 82 && f <= 84) || (f >= 94 && f <= 99)) {
        return {16, true, false};                                                // BC2/3/5/6H/7
    }
    if (f == 85 || f == 86 || f == 115) return {2, false, false};
    if (f >= 87 && f <= 93) return {4, false, false};
    return {4, false, false};
}

inline uint32_t fullMipCount(uint32_t w, uint32_t h, uint32_t d) noexcept {
    uint32_t m = std::max({w, h, d, 1u});
    uint32_t levels = 1;
    while (m > 1) {
        m >>= 1;
        ++levels;
    }
    return levels;
}

struct SubresourceLayout {
    size_t offset = 0;
    uint32_t width = 1, height = 1, depth = 1;
    uint32_t rowPitch = 0;    // bytes por fila (de bloques en BC)
    uint32_t rows = 1;        // filas (de bloques en BC)
    uint32_t depthPitch = 0;  // bytes por corte
    size_t size = 0;
};

struct ResourceDesc {
    Dimension dim = Dimension::Unknown;
    uint32_t width = 1, height = 1, depth = 1;
    uint32_t mipLevels = 1, arraySize = 1;
    uint32_t format = 0;
    uint32_t sampleCount = 1;
    uint32_t usage = 0, bindFlags = 0, cpuAccess = 0, miscFlags = 0;
    uint32_t structureStride = 0;
};

// D3D11_*_DESC exactos (Windows x64).
inline ResourceDesc parseBufferDesc(const uint32_t* d) noexcept {
    ResourceDesc r;
    r.dim = Dimension::Buffer;
    r.width = d[0];
    r.usage = d[1];
    r.bindFlags = d[2];
    r.cpuAccess = d[3];
    r.miscFlags = d[4];
    r.structureStride = d[5];
    return r;
}
inline ResourceDesc parseTexture1DDesc(const uint32_t* d) noexcept {
    ResourceDesc r;
    r.dim = Dimension::Texture1D;
    r.width = d[0];
    r.mipLevels = d[1];
    r.arraySize = d[2];
    r.format = d[3];
    r.usage = d[4];
    r.bindFlags = d[5];
    r.cpuAccess = d[6];
    r.miscFlags = d[7];
    return r;
}
inline ResourceDesc parseTexture2DDesc(const uint32_t* d) noexcept {
    ResourceDesc r;
    r.dim = Dimension::Texture2D;
    r.width = d[0];
    r.height = d[1];
    r.mipLevels = d[2];
    r.arraySize = d[3];
    r.format = d[4];
    r.sampleCount = d[5];
    r.usage = d[7];
    r.bindFlags = d[8];
    r.cpuAccess = d[9];
    r.miscFlags = d[10];
    return r;
}
inline ResourceDesc parseTexture3DDesc(const uint32_t* d) noexcept {
    ResourceDesc r;
    r.dim = Dimension::Texture3D;
    r.width = d[0];
    r.height = d[1];
    r.depth = d[2];
    r.mipLevels = d[3];
    r.format = d[4];
    r.usage = d[5];
    r.bindFlags = d[6];
    r.cpuAccess = d[7];
    r.miscFlags = d[8];
    return r;
}

// Escribe el D3D11_*_DESC que devuelve GetDesc (MipLevels ya resuelto).
inline size_t writeDesc(const ResourceDesc& r, uint32_t* out) noexcept {
    switch (r.dim) {
        case Dimension::Buffer:
            out[0] = r.width; out[1] = r.usage; out[2] = r.bindFlags; out[3] = r.cpuAccess;
            out[4] = r.miscFlags; out[5] = r.structureStride;
            return 24;
        case Dimension::Texture1D:
            out[0] = r.width; out[1] = r.mipLevels; out[2] = r.arraySize; out[3] = r.format;
            out[4] = r.usage; out[5] = r.bindFlags; out[6] = r.cpuAccess; out[7] = r.miscFlags;
            return 32;
        case Dimension::Texture2D:
            out[0] = r.width; out[1] = r.height; out[2] = r.mipLevels; out[3] = r.arraySize;
            out[4] = r.format; out[5] = r.sampleCount; out[6] = 0; out[7] = r.usage;
            out[8] = r.bindFlags; out[9] = r.cpuAccess; out[10] = r.miscFlags;
            return 44;
        case Dimension::Texture3D:
            out[0] = r.width; out[1] = r.height; out[2] = r.depth; out[3] = r.mipLevels;
            out[4] = r.format; out[5] = r.usage; out[6] = r.bindFlags; out[7] = r.cpuAccess;
            out[8] = r.miscFlags;
            return 36;
        default:
            return 0;
    }
}

class Resource {
public:
    // Valida y normaliza el descriptor; false = E_INVALIDARG.
    bool init(const ResourceDesc& in) {
        desc_ = in;
        if (desc_.dim == Dimension::Unknown || desc_.width == 0) return false;
        if (desc_.dim == Dimension::Buffer) {
            desc_.height = desc_.depth = desc_.mipLevels = desc_.arraySize = 1;
        } else {
            if (desc_.dim != Dimension::Texture3D) desc_.depth = 1;
            if (desc_.dim == Dimension::Texture1D) desc_.height = 1;
            if (desc_.height == 0 || desc_.depth == 0 || desc_.width > 16384 || desc_.height > 16384 ||
                desc_.depth > 2048) {
                return false;
            }
            if (desc_.arraySize == 0) desc_.arraySize = 1;
            if (desc_.arraySize > 2048) return false;
            const uint32_t maxMips = fullMipCount(desc_.width, desc_.height, desc_.depth);
            if (desc_.mipLevels == 0 || desc_.mipLevels > maxMips) desc_.mipLevels = maxMips;
        }
        if (desc_.sampleCount == 0) desc_.sampleCount = 1;
        buildLayout();
        return true;
    }

    const ResourceDesc& desc() const noexcept { return desc_; }
    uint32_t subresourceCount() const noexcept { return static_cast<uint32_t>(layout_.size()); }
    const SubresourceLayout* layout(uint32_t sub) const noexcept {
        return sub < layout_.size() ? &layout_[sub] : nullptr;
    }
    size_t totalSize() const noexcept { return total_; }
    bool hasStorage() const noexcept { return !storage_.empty(); }

    // La memoria se reserva al primer uso (muchas texturas solo se crean y se muestrean).
    uint8_t* data(uint32_t sub) {
        const SubresourceLayout* l = layout(sub);
        if (l == nullptr) return nullptr;
        ensureStorage();
        return storage_.data() + l->offset;
    }

    void releaseStorage() {
        std::vector<uint8_t>().swap(storage_);
    }

    // D3D11_SUBRESOURCE_DATA[] = {pSysMem (8), SysMemPitch (4), SysMemSlicePitch (4)}.
    void initialize(const uint8_t* subresourceData) {
        if (subresourceData == nullptr) return;
        for (uint32_t s = 0; s < subresourceCount(); ++s) {
            const uint8_t* entry = subresourceData + s * 16;
            const uint8_t* src = nullptr;
            uint32_t pitch = 0, slice = 0;
            std::memcpy(&src, entry, 8);
            std::memcpy(&pitch, entry + 8, 4);
            std::memcpy(&slice, entry + 12, 4);
            if (src != nullptr) writeRegion(s, nullptr, src, pitch, slice);
        }
    }

    // UpdateSubresource: box = D3D11_BOX{left, top, front, right, bottom, back} o nullptr.
    // En buffers left/right son bytes. Coordenadas en píxeles (múltiplos de 4 en BC).
    bool writeRegion(uint32_t sub, const uint32_t* box, const uint8_t* src, uint32_t srcRowPitch,
                     uint32_t srcDepthPitch) {
        const SubresourceLayout* l = layout(sub);
        if (l == nullptr || src == nullptr) return false;
        uint8_t* dst = data(sub);
        if (desc_.dim == Dimension::Buffer) {
            const uint32_t left = box != nullptr ? box[0] : 0;
            const uint32_t right = box != nullptr ? std::min(box[3], desc_.width) : desc_.width;
            if (left >= right) return false;
            std::memcpy(dst + left, src, right - left);
            return true;
        }
        const FormatInfo fi = formatInfo(desc_.format);
        const uint32_t div = fi.block ? 4 : 1;
        uint32_t x0 = 0, y0 = 0, z0 = 0, x1 = l->width, y1 = l->height, z1 = l->depth;
        if (box != nullptr) {
            x0 = box[0]; y0 = box[1]; z0 = box[2];
            x1 = std::min(box[3], l->width); y1 = std::min(box[4], l->height); z1 = std::min(box[5], l->depth);
        }
        if (x0 >= x1 || y0 >= y1 || z0 >= z1) return false;
        const uint32_t bx0 = x0 / div, by0 = y0 / div;
        const uint32_t bx1 = (x1 + div - 1) / div, by1 = (y1 + div - 1) / div;
        const size_t rowBytes = static_cast<size_t>(bx1 - bx0) * fi.bytes;
        const uint32_t srcPitch = srcRowPitch != 0 ? srcRowPitch : static_cast<uint32_t>(rowBytes);
        const uint32_t srcSlice = srcDepthPitch != 0 ? srcDepthPitch : srcPitch * (by1 - by0);
        const size_t copy = std::min<size_t>(rowBytes, srcPitch);
        for (uint32_t z = z0; z < z1; ++z) {
            for (uint32_t by = by0; by < by1; ++by) {
                uint8_t* d = dst + static_cast<size_t>(z) * l->depthPitch +
                             static_cast<size_t>(by) * l->rowPitch + static_cast<size_t>(bx0) * fi.bytes;
                const uint8_t* s = src + static_cast<size_t>(z - z0) * srcSlice +
                                   static_cast<size_t>(by - by0) * srcPitch;
                std::memcpy(d, s, copy);
            }
        }
        return true;
    }

    // CopySubresourceRegion: copia src[srcSub] (box opcional) en this[dstSub] en (dx, dy, dz).
    bool copyRegion(uint32_t dstSub, uint32_t dx, uint32_t dy, uint32_t dz, Resource& src,
                    uint32_t srcSub, const uint32_t* box) {
        const SubresourceLayout* dl = layout(dstSub);
        const SubresourceLayout* sl = src.layout(srcSub);
        if (dl == nullptr || sl == nullptr) return false;
        if (desc_.dim == Dimension::Buffer || src.desc_.dim == Dimension::Buffer) {
            const uint32_t left = box != nullptr ? box[0] : 0;
            const uint32_t right = box != nullptr ? box[3] : src.desc_.width;
            if (left >= right || right > sl->size || dx + (right - left) > dl->size) return false;
            std::memcpy(data(dstSub) + dx, src.data(srcSub) + left, right - left);
            return true;
        }
        const FormatInfo fi = formatInfo(desc_.format);
        const uint32_t div = fi.block ? 4 : 1;
        uint32_t x0 = 0, y0 = 0, z0 = 0, x1 = sl->width, y1 = sl->height, z1 = sl->depth;
        if (box != nullptr) {
            x0 = box[0]; y0 = box[1]; z0 = box[2];
            x1 = std::min(box[3], sl->width); y1 = std::min(box[4], sl->height); z1 = std::min(box[5], sl->depth);
        }
        if (x0 >= x1 || y0 >= y1 || z0 >= z1) return false;
        const uint32_t dstCols = (dl->width + div - 1) / div;
        if (dx / div >= dstCols || dy / div >= dl->rows || dz >= dl->depth) return false;
        const uint32_t w = std::min((x1 - x0 + div - 1) / div, dstCols - dx / div);
        const uint32_t h = std::min((y1 - y0 + div - 1) / div, dl->rows - dy / div);
        const uint32_t d = std::min(z1 - z0, dl->depth - dz);
        uint8_t* dst = data(dstSub);
        const uint8_t* s = src.data(srcSub);
        const size_t rowBytes = static_cast<size_t>(w) * fi.bytes;
        for (uint32_t z = 0; z < d; ++z) {
            for (uint32_t y = 0; y < h; ++y) {
                std::memcpy(dst + static_cast<size_t>(dz + z) * dl->depthPitch + static_cast<size_t>(dy / div + y) * dl->rowPitch +
                                static_cast<size_t>(dx / div) * fi.bytes,
                            s + static_cast<size_t>(z0 + z) * sl->depthPitch + static_cast<size_t>(y0 / div + y) * sl->rowPitch +
                                static_cast<size_t>(x0 / div) * fi.bytes,
                            rowBytes);
            }
        }
        return true;
    }

    // CopyResource: mismo tipo y tamaño; si difieren se copia el prefijo común.
    void copyFrom(Resource& src) {
        if (&src == this) return;
        ensureStorage();
        src.ensureStorage();
        std::memcpy(storage_.data(), src.storage_.data(), std::min(storage_.size(), src.storage_.size()));
    }

    // ClearRenderTargetView: rellena [firstSlice, firstSlice+slices) del mip con el color.
    void clear(uint32_t mip, uint32_t firstSlice, uint32_t slices, const float rgba[4]) {
        uint8_t texel[16] = {};
        const uint32_t texelBytes = packColor(desc_.format, rgba, texel);
        if (texelBytes == 0) return;
        const uint64_t end = std::min<uint64_t>(desc_.arraySize, static_cast<uint64_t>(firstSlice) + slices);
        for (uint32_t s = firstSlice; s < end; ++s) {
            const uint32_t sub = mip + s * desc_.mipLevels;
            const SubresourceLayout* l = layout(sub);
            if (l == nullptr) continue;
            uint8_t* base = data(sub);
            const size_t count = l->size / texelBytes;
            for (size_t i = 0; i < count; ++i) std::memcpy(base + i * texelBytes, texel, texelBytes);
        }
    }

    // Empaqueta un color float en el formato; 0 si no es un formato de color soportado.
    static uint32_t packColor(uint32_t format, const float c[4], uint8_t* out) noexcept {
        auto u8 = [](float v) {
            v = std::clamp(v, 0.0f, 1.0f);
            return static_cast<uint8_t>(std::lround(v * 255.0f));
        };
        switch (format) {
            case 27: case 28: case 29: case 30:  // R8G8B8A8_*
                out[0] = u8(c[0]); out[1] = u8(c[1]); out[2] = u8(c[2]); out[3] = u8(c[3]);
                return 4;
            case 87: case 90: case 91:  // B8G8R8A8_*
                out[0] = u8(c[2]); out[1] = u8(c[1]); out[2] = u8(c[0]); out[3] = u8(c[3]);
                return 4;
            case 88: case 92: case 93:  // B8G8R8X8_*
                out[0] = u8(c[2]); out[1] = u8(c[1]); out[2] = u8(c[0]); out[3] = 255;
                return 4;
            case 23: case 24: case 25: {  // R10G10B10A2
                auto u10 = [](float v) { return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0f, 1.0f) * 1023.0f)); };
                const uint32_t a = static_cast<uint32_t>(std::lround(std::clamp(c[3], 0.0f, 1.0f) * 3.0f));
                const uint32_t v = u10(c[0]) | (u10(c[1]) << 10) | (u10(c[2]) << 20) | (a << 30);
                std::memcpy(out, &v, 4);
                return 4;
            }
            case 9: case 10: {  // R16G16B16A16_FLOAT
                for (int i = 0; i < 4; ++i) {
                    const uint16_t h = floatToHalf(c[i]);
                    std::memcpy(out + i * 2, &h, 2);
                }
                return 8;
            }
            case 1: case 2:  // R32G32B32A32_FLOAT
                std::memcpy(out, c, 16);
                return 16;
            case 41:  // R32_FLOAT
                std::memcpy(out, c, 4);
                return 4;
            case 60: case 61: case 65:  // R8_UNORM / A8
                out[0] = u8(format == 65 ? c[3] : c[0]);
                return 1;
            default:
                return 0;
        }
    }

    // Convierte el mip 0 / slice 0 a RGBA8 (para presentar). false si el formato no se soporta.
    bool toRgba8(std::vector<uint8_t>& out, uint32_t& w, uint32_t& h) {
        const SubresourceLayout* l = layout(0);
        if (l == nullptr || desc_.dim != Dimension::Texture2D) return false;
        w = l->width;
        h = l->height;
        out.resize(static_cast<size_t>(w) * h * 4);
        const uint8_t* src = data(0);
        for (uint32_t y = 0; y < h; ++y) {
            const uint8_t* row = src + static_cast<size_t>(y) * l->rowPitch;
            uint8_t* o = out.data() + static_cast<size_t>(y) * w * 4;
            for (uint32_t x = 0; x < w; ++x, o += 4) {
                switch (desc_.format) {
                    case 27: case 28: case 29: case 30:
                        std::memcpy(o, row + x * 4, 4);
                        break;
                    case 87: case 88: case 90: case 91: case 92: case 93:
                        o[0] = row[x * 4 + 2]; o[1] = row[x * 4 + 1]; o[2] = row[x * 4 + 0]; o[3] = 255;
                        break;
                    case 23: case 24: case 25: {
                        uint32_t v;
                        std::memcpy(&v, row + x * 4, 4);
                        o[0] = static_cast<uint8_t>(((v >> 0) & 1023) * 255 / 1023);
                        o[1] = static_cast<uint8_t>(((v >> 10) & 1023) * 255 / 1023);
                        o[2] = static_cast<uint8_t>(((v >> 20) & 1023) * 255 / 1023);
                        o[3] = 255;
                        break;
                    }
                    case 9: case 10:
                        for (int i = 0; i < 4; ++i) {
                            uint16_t hv;
                            std::memcpy(&hv, row + x * 8 + i * 2, 2);
                            o[i] = static_cast<uint8_t>(std::lround(std::clamp(halfToFloat(hv), 0.0f, 1.0f) * 255.0f));
                        }
                        break;
                    default:
                        return false;
                }
            }
        }
        return true;
    }

    static uint16_t floatToHalf(float f) noexcept {
        uint32_t x;
        std::memcpy(&x, &f, 4);
        const uint32_t sign = (x >> 16) & 0x8000;
        int32_t exp = static_cast<int32_t>((x >> 23) & 0xFF) - 127 + 15;
        uint32_t mant = x & 0x7FFFFF;
        if (((x >> 23) & 0xFF) == 0xFF) return static_cast<uint16_t>(sign | 0x7C00 | (mant ? 0x200 : 0));
        if (exp <= 0) return static_cast<uint16_t>(sign);
        if (exp >= 31) return static_cast<uint16_t>(sign | 0x7C00);
        return static_cast<uint16_t>(sign | (static_cast<uint32_t>(exp) << 10) | (mant >> 13));
    }

    static float halfToFloat(uint16_t h) noexcept {
        const uint32_t sign = (h & 0x8000u) << 16;
        const uint32_t exp = (h >> 10) & 0x1F;
        const uint32_t mant = h & 0x3FF;
        uint32_t x;
        if (exp == 0) x = sign;  // subnormales -> 0
        else if (exp == 31) x = sign | 0x7F800000u | (mant << 13);
        else x = sign | ((exp - 15 + 127) << 23) | (mant << 13);
        float f;
        std::memcpy(&f, &x, 4);
        return f;
    }

private:
    void buildLayout() {
        layout_.clear();
        total_ = 0;
        if (desc_.dim == Dimension::Buffer) {
            SubresourceLayout l;
            l.width = desc_.width;
            l.rowPitch = l.depthPitch = desc_.width;
            l.size = desc_.width;
            layout_.push_back(l);
            total_ = (l.size + 15) & ~size_t{15};
            return;
        }
        const FormatInfo fi = formatInfo(desc_.format);
        for (uint32_t slice = 0; slice < desc_.arraySize; ++slice) {
            for (uint32_t mip = 0; mip < desc_.mipLevels; ++mip) {
                SubresourceLayout l;
                l.width = std::max(1u, desc_.width >> mip);
                l.height = std::max(1u, desc_.height >> mip);
                l.depth = std::max(1u, desc_.depth >> mip);
                const uint32_t cols = fi.block ? std::max(1u, (l.width + 3) / 4) : l.width;
                l.rows = fi.block ? std::max(1u, (l.height + 3) / 4) : l.height;
                l.rowPitch = cols * fi.bytes;
                l.depthPitch = l.rowPitch * l.rows;
                l.size = static_cast<size_t>(l.depthPitch) * l.depth;
                l.offset = total_;
                total_ += (l.size + 15) & ~size_t{15};
                layout_.push_back(l);
            }
        }
    }

    void ensureStorage() {
        if (storage_.empty() && total_ > 0) storage_.assign(total_, 0);
    }

    ResourceDesc desc_;
    std::vector<SubresourceLayout> layout_;
    size_t total_ = 0;
    std::vector<uint8_t> storage_;
};

}  // namespace hyperplay::d3d11
