#pragma once

// Rasterizador D3D11 por software sobre los recursos en memoria (D3D11ResourceModel.h).
//
// Pipeline por DrawIndexed/Draw: fetch de vértices (input layout -> registros v# del VS),
// VS (DxbcVm), recorte contra el plano cercano (z >= 0) y w > 0, viewport, descarte de caras,
// rasterización por funciones de arista con regla top-left e interpolación con corrección de
// perspectiva (o constante), PS (DxbcVm), blending D3D11 del RT0 y escritura con write mask.
// Sin profundidad/estencil todavía (los juegos 2D de Unity dibujan ordenados).

#include <algorithm>
#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#include "DxbcShader.h"
#include "DxbcVm.h"

namespace hyperplay::raster {

using dxbc::Vec4;

// --- Conversión de formatos -----------------------------------------------------------------

inline float halfToFloat(uint16_t h) {
    const uint32_t s = (h >> 15) & 1u, e = (h >> 10) & 0x1Fu, m = h & 0x3FFu;
    uint32_t bits;
    if (e == 0) {
        if (m == 0) {
            bits = s << 31;
        } else {  // subnormal
            int ee = -1;
            uint32_t mm = m;
            do {
                ++ee;
                mm <<= 1;
            } while ((mm & 0x400u) == 0);
            bits = (s << 31) | (static_cast<uint32_t>(127 - 15 - ee) << 23) | ((mm & 0x3FFu) << 13);
        }
    } else if (e == 31) {
        bits = (s << 31) | 0x7F800000u | (m << 13);
    } else {
        bits = (s << 31) | ((e + 112u) << 23) | (m << 13);
    }
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

inline uint16_t floatToHalf(float f) {
    uint32_t x;
    std::memcpy(&x, &f, 4);
    const uint32_t s = (x >> 16) & 0x8000u;
    const int e = static_cast<int>((x >> 23) & 0xFFu) - 127 + 15;
    uint32_t m = x & 0x7FFFFFu;
    if (((x >> 23) & 0xFFu) == 0xFFu) return static_cast<uint16_t>(s | 0x7C00u | (m ? 0x200u : 0u));
    if (e >= 31) return static_cast<uint16_t>(s | 0x7C00u);
    if (e <= 0) {
        if (e < -10) return static_cast<uint16_t>(s);
        m |= 0x800000u;
        return static_cast<uint16_t>(s | (m >> (14 - e)));
    }
    return static_cast<uint16_t>(s | (static_cast<uint32_t>(e) << 10) | (m >> 13));
}

inline float srgbToLinearExact(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}
inline float linearToSrgbExact(float c) {
    c = std::fmin(std::fmax(c, 0.0f), 1.0f);
    return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

// Tablas: pow() por canal y texel dominaba el coste del muestreo sRGB.
struct SrgbTables {
    float toLinear[256];
    float toSrgb[4097];
    SrgbTables() {
        for (int i = 0; i < 256; ++i) toLinear[i] = srgbToLinearExact(static_cast<float>(i) / 255.0f);
        for (int i = 0; i <= 4096; ++i) toSrgb[i] = linearToSrgbExact(static_cast<float>(i) / 4096.0f);
    }
};
inline const SrgbTables& srgbTables() {
    static const SrgbTables t;
    return t;
}
inline float srgb8ToLinear(uint8_t v) { return srgbTables().toLinear[v]; }
inline float srgbToLinear(float c) {
    c = std::fmin(std::fmax(c, 0.0f), 1.0f);
    return srgbTables().toLinear[static_cast<int>(c * 255.0f + 0.5f)];
}
inline float linearToSrgb(float c) {
    c = std::fmin(std::fmax(c, 0.0f), 1.0f);
    const float f = c * 4096.0f;
    const int i = static_cast<int>(f);
    if (i >= 4096) return 1.0f;
    const float a = f - static_cast<float>(i);
    const auto& t = srgbTables().toSrgb;
    return t[i] + (t[i + 1] - t[i]) * a;
}

inline float unorm8(uint8_t v) { return static_cast<float>(v) * (1.0f / 255.0f); }
inline uint8_t toUnorm8(float f) {
    if (!(f > 0.0f)) return 0;
    if (f >= 1.0f) return 255;
    return static_cast<uint8_t>(f * 255.0f + 0.5f);
}

// Atributo de vértice -> vec4 (componentes ausentes = 0, w = 1). false si el formato no se soporta.
inline bool fetchAttribute(uint32_t format, const uint8_t* p, Vec4& out) {
    out = Vec4();
    out.f[3] = 1.0f;
    switch (format) {
        case 2:  // R32G32B32A32_FLOAT
            std::memcpy(out.f, p, 16);
            return true;
        case 3:  // R32G32B32A32_UINT
        case 4:  // SINT
            std::memcpy(out.u, p, 16);
            return true;
        case 6:  // R32G32B32_FLOAT
            std::memcpy(out.f, p, 12);
            return true;
        case 10: {  // R16G16B16A16_FLOAT
            uint16_t h[4];
            std::memcpy(h, p, 8);
            for (int i = 0; i < 4; ++i) out.f[i] = halfToFloat(h[i]);
            return true;
        }
        case 16:  // R32G32_FLOAT
            std::memcpy(out.f, p, 8);
            return true;
        case 28:  // R8G8B8A8_UNORM
            for (int i = 0; i < 4; ++i) out.f[i] = unorm8(p[i]);
            return true;
        case 30:  // R8G8B8A8_UINT
            for (int i = 0; i < 4; ++i) out.u[i] = p[i];
            return true;
        case 34: {  // R16G16_FLOAT
            uint16_t h[2];
            std::memcpy(h, p, 4);
            out.f[0] = halfToFloat(h[0]);
            out.f[1] = halfToFloat(h[1]);
            return true;
        }
        case 35: {  // R16G16_UNORM
            uint16_t h[2];
            std::memcpy(h, p, 4);
            out.f[0] = h[0] / 65535.0f;
            out.f[1] = h[1] / 65535.0f;
            return true;
        }
        case 41:  // R32_FLOAT
            std::memcpy(out.f, p, 4);
            return true;
        case 42:  // R32_UINT
            std::memcpy(out.u, p, 4);
            return true;
        case 87:  // B8G8R8A8_UNORM
            out.f[0] = unorm8(p[2]);
            out.f[1] = unorm8(p[1]);
            out.f[2] = unorm8(p[0]);
            out.f[3] = unorm8(p[3]);
            return true;
        default:
            return false;
    }
}

inline uint32_t attributeBytes(uint32_t format) {
    switch (format) {
        case 2: case 3: case 4: return 16;
        case 6: return 12;
        case 10: case 16: return 8;
        default: return 4;
    }
}

// Píxel de render target <-> RGBA float.
inline bool readPixel(uint32_t format, const uint8_t* p, float* rgba) {
    switch (format) {
        case 28:
            for (int i = 0; i < 4; ++i) rgba[i] = unorm8(p[i]);
            return true;
        case 29:
            for (int i = 0; i < 3; ++i) rgba[i] = srgb8ToLinear(p[i]);
            rgba[3] = unorm8(p[3]);
            return true;
        case 87:
        case 91: {
            const bool s = format == 91;
            rgba[0] = s ? srgb8ToLinear(p[2]) : unorm8(p[2]);
            rgba[1] = s ? srgb8ToLinear(p[1]) : unorm8(p[1]);
            rgba[2] = s ? srgb8ToLinear(p[0]) : unorm8(p[0]);
            rgba[3] = unorm8(p[3]);
            return true;
        }
        case 10: {
            uint16_t h[4];
            std::memcpy(h, p, 8);
            for (int i = 0; i < 4; ++i) rgba[i] = halfToFloat(h[i]);
            return true;
        }
        case 2:
            std::memcpy(rgba, p, 16);
            return true;
        case 24: {  // R10G10B10A2_UNORM
            uint32_t v;
            std::memcpy(&v, p, 4);
            rgba[0] = (v & 1023u) / 1023.0f;
            rgba[1] = ((v >> 10) & 1023u) / 1023.0f;
            rgba[2] = ((v >> 20) & 1023u) / 1023.0f;
            rgba[3] = (v >> 30) / 3.0f;
            return true;
        }
        default:
            return false;
    }
}

inline void writePixel(uint32_t format, uint8_t* p, const float* rgba, uint8_t writeMask) {
    float c[4] = {rgba[0], rgba[1], rgba[2], rgba[3]};
    if (format == 29 || format == 91) {
        for (int i = 0; i < 3; ++i) c[i] = linearToSrgb(c[i]);
    }
    switch (format) {
        case 28:
        case 29:
            for (int i = 0; i < 4; ++i) {
                if (writeMask & (1u << i)) p[i] = toUnorm8(c[i]);
            }
            break;
        case 87:
        case 91: {
            static const int map[4] = {2, 1, 0, 3};
            for (int i = 0; i < 4; ++i) {
                if (writeMask & (1u << i)) p[map[i]] = toUnorm8(c[i]);
            }
            break;
        }
        case 10: {
            uint16_t h[4];
            std::memcpy(h, p, 8);
            for (int i = 0; i < 4; ++i) {
                if (writeMask & (1u << i)) h[i] = floatToHalf(c[i]);
            }
            std::memcpy(p, h, 8);
            break;
        }
        case 2: {
            float f[4];
            std::memcpy(f, p, 16);
            for (int i = 0; i < 4; ++i) {
                if (writeMask & (1u << i)) f[i] = c[i];
            }
            std::memcpy(p, f, 16);
            break;
        }
        case 24: {
            uint32_t v;
            std::memcpy(&v, p, 4);
            auto q = [](float f, float m) {
                return static_cast<uint32_t>(std::fmin(std::fmax(f, 0.0f), 1.0f) * m + 0.5f);
            };
            const uint32_t comp[4] = {q(c[0], 1023.0f), q(c[1], 1023.0f), q(c[2], 1023.0f), q(c[3], 3.0f)};
            const uint32_t shift[4] = {0, 10, 20, 30}, bits[4] = {1023u, 1023u, 1023u, 3u};
            for (int i = 0; i < 4; ++i) {
                if (writeMask & (1u << i)) v = (v & ~(bits[i] << shift[i])) | (comp[i] << shift[i]);
            }
            std::memcpy(p, &v, 4);
            break;
        }
        default:
            break;
    }
}

inline bool isRenderTargetFormat(uint32_t f) {
    return f == 28 || f == 29 || f == 87 || f == 91 || f == 10 || f == 2 || f == 24;
}
inline uint32_t renderTargetBytes(uint32_t f) { return f == 10 ? 8 : f == 2 ? 16 : 4; }

// --- Estado de dibujo -----------------------------------------------------------------------

struct VertexElement {
    uint32_t vsReg = 0;
    uint32_t format = 0;
    uint32_t slot = 0;
    uint32_t offset = 0;
    bool perInstance = false;
};

struct VertexBufferBinding {
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint32_t stride = 0;
    uint32_t offset = 0;
};

struct RenderTarget {
    uint8_t* data = nullptr;
    uint32_t width = 0, height = 0, rowPitch = 0, format = 0;
};

struct Viewport {
    float x = 0, y = 0, w = 0, h = 0, minZ = 0, maxZ = 1;
};

struct BlendRt {
    bool enable = false;
    uint32_t src = 2, dst = 1, op = 1, srcA = 2, dstA = 1, opA = 1;
    uint8_t writeMask = 0xF;
};

struct DrawState {
    const dxbc::Program* vs = nullptr;
    const dxbc::Program* ps = nullptr;
    std::vector<VertexElement> elements;
    VertexBufferBinding vb[16];
    const uint8_t* ib = nullptr;
    size_t ibSize = 0;
    uint32_t ibFormat = 57;  // R16_UINT (57) o R32_UINT (42)
    uint32_t ibOffset = 0;
    uint32_t topology = 4;   // TRIANGLELIST (4), TRIANGLESTRIP (5)
    dxbc::ConstantBufferView vsCb[14], psCb[14];
    dxbc::TextureAccess* vsTex = nullptr;
    dxbc::TextureAccess* psTex = nullptr;
    Viewport viewport;
    bool scissorEnable = false;
    int32_t scissor[4] = {0, 0, 0, 0};  // left, top, right, bottom
    uint32_t cullMode = 3;               // 1 NONE, 2 FRONT, 3 BACK
    bool frontCounterClockwise = false;
    BlendRt blend;
    float blendFactor[4] = {1, 1, 1, 1};
    RenderTarget rt;
};

struct DrawStats {
    uint64_t vertices = 0, triangles = 0, pixels = 0, culled = 0, clipped = 0, unsupportedFormat = 0;
    // Diagnóstico: rango de la posición de salida del VS (espacio de recorte, x/w e y/w).
    float minX = 1e30f, maxX = -1e30f, minY = 1e30f, maxY = -1e30f;
    float inMin[2] = {1e30f, 1e30f}, inMax[2] = {-1e30f, -1e30f};
    float outMin[2] = {1e30f, 1e30f}, outMax[2] = {-1e30f, -1e30f};
};

namespace detail {

struct ClipVertex {
    float pos[4];        // espacio de recorte
    Vec4 attr[16];       // salidas del VS
};

inline float blendFactorValue(uint32_t f, int c, const float* s, const float* d, const float* k) {
    switch (f) {
        case 1: return 0.0f;
        case 2: return 1.0f;
        case 3: return s[c];
        case 4: return 1.0f - s[c];
        case 5: return s[3];
        case 6: return 1.0f - s[3];
        case 7: return d[3];
        case 8: return 1.0f - d[3];
        case 9: return d[c];
        case 10: return 1.0f - d[c];
        case 11: return c == 3 ? 1.0f : std::fmin(s[3], 1.0f - d[3]);
        case 14: return k[c];
        case 15: return 1.0f - k[c];
        default: return 1.0f;
    }
}

inline float blendOp(uint32_t op, float a, float b) {
    switch (op) {
        case 2: return a - b;
        case 3: return b - a;
        case 4: return std::fmin(a, b);
        case 5: return std::fmax(a, b);
        default: return a + b;
    }
}

}  // namespace detail

class SoftRasterizer {
public:
    DrawStats stats;

    // DrawIndexed / Draw (ib == nullptr). Devuelve false si el estado no es dibujable.
    bool draw(const DrawState& s, uint32_t count, uint32_t start, int32_t baseVertex, bool indexed) {
        if (s.vs == nullptr || s.ps == nullptr || s.rt.data == nullptr || !isRenderTargetFormat(s.rt.format)) {
            return false;
        }
        if (s.topology != 4 && s.topology != 5) return false;
        prepareLinkage(s);
        dxbc::Vm vsVm(*s.vs);
        for (int i = 0; i < 14; ++i) vsVm.cbuffers[i] = s.vsCb[i];
        vsVm.textures = s.vsTex;

        // Caché de vértices por índice (los quads de sprites comparten vértices).
        cacheIndex_.clear();
        cacheVerts_.clear();
        auto vertexFor = [&](uint32_t vertexId) -> const detail::ClipVertex* {
            auto it = std::find(cacheIndex_.begin(), cacheIndex_.end(), vertexId);
            if (it != cacheIndex_.end()) return &cacheVerts_[static_cast<size_t>(it - cacheIndex_.begin())];
            detail::ClipVertex v;
            if (!shadeVertex(s, vsVm, vertexId, v)) return nullptr;
            if (cacheIndex_.size() >= 64) {
                cacheIndex_.erase(cacheIndex_.begin());
                cacheVerts_.erase(cacheVerts_.begin());
            }
            cacheIndex_.push_back(vertexId);
            cacheVerts_.push_back(v);
            return &cacheVerts_.back();
        };
        auto indexAt = [&](uint32_t i, uint32_t& out) -> bool {
            if (!indexed) {
                out = start + i;
                return true;
            }
            const uint32_t bytes = s.ibFormat == 42 ? 4 : 2;
            const size_t off = s.ibOffset + static_cast<size_t>(start + i) * bytes;
            if (s.ib == nullptr || off + bytes > s.ibSize) return false;
            if (bytes == 4) {
                std::memcpy(&out, s.ib + off, 4);
            } else {
                uint16_t v;
                std::memcpy(&v, s.ib + off, 2);
                out = v;
            }
            out = static_cast<uint32_t>(static_cast<int64_t>(out) + baseVertex);
            return true;
        };

        tris_.clear();
        const uint32_t triCount = s.topology == 4 ? count / 3 : (count >= 3 ? count - 2 : 0);
        for (uint32_t t = 0; t < triCount; ++t) {
            uint32_t idx[3];
            bool okIdx = true;
            for (int k = 0; k < 3; ++k) {
                const uint32_t i = s.topology == 4 ? t * 3 + static_cast<uint32_t>(k) : t + static_cast<uint32_t>(k);
                okIdx = okIdx && indexAt(i, idx[k]);
            }
            if (!okIdx) break;
            if (s.topology == 5 && (t & 1u)) std::swap(idx[1], idx[2]);  // tiras: orden alterno
            detail::ClipVertex tri[3];
            bool okV = true;
            for (int k = 0; k < 3 && okV; ++k) {
                const detail::ClipVertex* v = vertexFor(idx[k]);
                if (v == nullptr) okV = false;
                else tri[k] = *v;
            }
            if (!okV) continue;
            ++stats.triangles;
            clipToList(tri);
        }
        rasterAll(s);
        return true;
    }

    // Hilos del pool para el raster (el que llama también trabaja). 0 = secuencial.
    static void setWorkerCount(unsigned n) { pool().resize(n); }

private:
    struct SetupTri {
        detail::ClipVertex v[3];
        int minY = 0, maxY = -1;
        int64_t area = 0;
    };
    std::vector<SetupTri> tris_;

    // Pool persistente: parallelFor(n, fn) reparte índices entre los hilos y el llamante.
    class Pool {
    public:
        ~Pool() { resize(0); }
        void resize(unsigned n) {
            {
                std::lock_guard<std::mutex> lock(m_);
                quit_ = true;
            }
            cv_.notify_all();
            for (auto& t : threads_) t.join();
            threads_.clear();
            quit_ = false;
            for (unsigned i = 0; i < n; ++i) threads_.emplace_back([this] { loop(); });
        }
        unsigned size() const { return static_cast<unsigned>(threads_.size()); }
        template <typename F>
        void parallelFor(int n, F&& fn) {
            if (threads_.empty() || n <= 1) {
                for (int i = 0; i < n; ++i) fn(i);
                return;
            }
            std::unique_lock<std::mutex> lock(m_);
            job_ = [&fn](int i) { fn(i); };
            next_.store(0);
            total_ = n;
            remaining_.store(n);
            ++epoch_;
            lock.unlock();
            cv_.notify_all();
            work();
            lock.lock();
            done_.wait(lock, [this] { return remaining_.load() == 0; });
            job_ = nullptr;
        }

    private:
        void work() {
            for (;;) {
                const int i = next_.fetch_add(1);
                if (i >= total_) return;
                job_(i);
                if (remaining_.fetch_sub(1) == 1) {
                    std::lock_guard<std::mutex> lock(m_);
                    done_.notify_all();
                }
            }
        }
        void loop() {
            uint64_t seen = 0;
            std::unique_lock<std::mutex> lock(m_);
            for (;;) {
                cv_.wait(lock, [&] { return quit_ || epoch_ != seen; });
                if (quit_) return;
                seen = epoch_;
                lock.unlock();
                work();
                lock.lock();
            }
        }
        std::vector<std::thread> threads_;
        std::mutex m_;
        std::condition_variable cv_, done_;
        std::function<void(int)> job_;
        std::atomic<int> next_{0}, remaining_{0};
        int total_ = 0;
        uint64_t epoch_ = 0;
        bool quit_ = false;
    };
    static Pool& pool() {
        static Pool p;
        return p;
    }
    std::vector<uint32_t> cacheIndex_;
    std::vector<detail::ClipVertex> cacheVerts_;
    // Linkage: registro PS de entrada <- registro VS de salida (-1 sin origen; -2 = SV_Position).
    int psFromVs_[32];
    int vsPosReg_ = 0;
    uint32_t psInterp_[32];

    void prepareLinkage(const DrawState& s) {
        vsPosReg_ = 0;
        for (const auto& e : s.vs->outputSig) {
            if (e.systemValue == 1) vsPosReg_ = static_cast<int>(e.reg);
        }
        for (int i = 0; i < 32; ++i) {
            psFromVs_[i] = -1;
            psInterp_[i] = 2;  // lineal
        }
        for (const auto& in : s.ps->inputSig) {
            if (in.reg >= 32) continue;
            if (in.systemValue == 1) {
                psFromVs_[in.reg] = -2;
                continue;
            }
            for (const auto& out : s.vs->outputSig) {
                if (out.semanticIndex == in.semanticIndex && out.name.size() == in.name.size() &&
                    std::equal(out.name.begin(), out.name.end(), in.name.begin(),
                               [](char a, char b) { return (a | 0x20) == (b | 0x20); })) {
                    psFromVs_[in.reg] = static_cast<int>(out.reg);
                    break;
                }
            }
        }
        for (const auto& d : s.ps->inputs) {
            if (d.reg < 32) psInterp_[d.reg] = d.interpolation;
        }
    }

    bool shadeVertex(const DrawState& s, dxbc::Vm& vm, uint32_t vertexId, detail::ClipVertex& out) {
        for (auto& in : vm.inputs) in = Vec4();
        for (const auto& e : s.elements) {
            if (e.vsReg >= 32 || e.slot >= 16) continue;
            const VertexBufferBinding& b = s.vb[e.slot];
            if (b.data == nullptr) continue;
            const size_t off = b.offset + static_cast<size_t>(vertexId) * b.stride + e.offset;
            if (off + attributeBytes(e.format) > b.size) continue;
            Vec4 v;
            if (!fetchAttribute(e.format, b.data + off, v)) {
                ++stats.unsupportedFormat;
                continue;
            }
            vm.inputs[e.vsReg] = v;
        }
        for (auto& o : vm.outputs) o = Vec4();
        if (!vm.run()) return false;
        ++stats.vertices;
        std::memcpy(out.pos, vm.outputs[vsPosReg_].f, 16);
        if (out.pos[3] != 0.0f) {
            const float nx = out.pos[0] / out.pos[3], ny = out.pos[1] / out.pos[3];
            stats.minX = std::fmin(stats.minX, nx);
            stats.maxX = std::fmax(stats.maxX, nx);
            stats.minY = std::fmin(stats.minY, ny);
            stats.maxY = std::fmax(stats.maxY, ny);
        }
        {
            // Diagnóstico: rango xy de la entrada 1 y de la primera salida que no es la posición.
            const int uvReg = vsPosReg_ == 0 ? 1 : 0;
            for (int c = 0; c < 2; ++c) {
                stats.inMin[c] = std::fmin(stats.inMin[c], vm.inputs[1].f[c]);
                stats.inMax[c] = std::fmax(stats.inMax[c], vm.inputs[1].f[c]);
                stats.outMin[c] = std::fmin(stats.outMin[c], vm.outputs[uvReg].f[c]);
                stats.outMax[c] = std::fmax(stats.outMax[c], vm.outputs[uvReg].f[c]);
            }
        }
        for (int i = 0; i < 16; ++i) out.attr[i] = vm.outputs[i];
        return true;
    }

    static detail::ClipVertex lerp(const detail::ClipVertex& a, const detail::ClipVertex& b, float t) {
        detail::ClipVertex r;
        for (int i = 0; i < 4; ++i) r.pos[i] = a.pos[i] + (b.pos[i] - a.pos[i]) * t;
        for (int k = 0; k < 16; ++k) {
            for (int i = 0; i < 4; ++i) r.attr[k].f[i] = a.attr[k].f[i] + (b.attr[k].f[i] - a.attr[k].f[i]) * t;
        }
        return r;
    }

    void clipToList(const detail::ClipVertex* tri) {
        // Recorte Sutherland-Hodgman contra z >= 0 (plano cercano D3D) y w >= epsilon.
        detail::ClipVertex bufA[9], bufB[9];
        int n = 3;
        for (int i = 0; i < 3; ++i) bufA[i] = tri[i];
        bool clipped = false;
        auto clipPlane = [&](detail::ClipVertex* in, int count, detail::ClipVertex* out, auto dist) {
            int m = 0;
            for (int i = 0; i < count; ++i) {
                const detail::ClipVertex& a = in[i];
                const detail::ClipVertex& b = in[(i + 1) % count];
                const float da = dist(a), db = dist(b);
                if (da >= 0) out[m++] = a;
                if ((da >= 0) != (db >= 0)) {
                    out[m++] = lerp(a, b, da / (da - db));
                    clipped = true;
                }
                if (m >= 8) break;
            }
            return m;
        };
        n = clipPlane(bufA, n, bufB, [](const detail::ClipVertex& v) { return v.pos[2]; });
        n = clipPlane(bufB, n, bufA, [](const detail::ClipVertex& v) { return v.pos[3] - 1e-6f; });
        if (clipped) ++stats.clipped;
        for (int i = 1; i + 1 < n; ++i) {
            SetupTri t;
            t.v[0] = bufA[0];
            t.v[1] = bufA[i];
            t.v[2] = bufA[i + 1];
            tris_.push_back(t);
        }
    }

    // Reparte el raster en franjas horizontales: cada píxel pertenece a una sola franja, así que
    // el orden de los triángulos (blending) se conserva por píxel.
    void rasterAll(const DrawState& s) {
        if (tris_.empty()) return;
        const int height = static_cast<int>(s.rt.height);
        const unsigned workers = pool().size();
        const int bands = (workers == 0 || height < 64) ? 1 : static_cast<int>(std::min<unsigned>(workers + 1, 16) * 2);
        struct BandStats {
            uint64_t pixels = 0, culled = 0;
        };
        std::vector<BandStats> bs(static_cast<size_t>(bands));
        pool().parallelFor(bands, [&](int b) {
            const int y0 = height * b / bands, y1 = height * (b + 1) / bands;
            dxbc::Vm psVm(*s.ps);
            for (int i = 0; i < 14; ++i) psVm.cbuffers[i] = s.psCb[i];
            psVm.textures = s.psTex;
            DrawStats local;
            for (const SetupTri& t : tris_) rasterTriangle(s, psVm, t.v[0], t.v[1], t.v[2], y0, y1, local);
            bs[static_cast<size_t>(b)].pixels = local.pixels;
            bs[static_cast<size_t>(b)].culled = b == 0 ? local.culled : 0;
        });
        for (const auto& b : bs) {
            stats.pixels += b.pixels;
            stats.culled += b.culled;
        }
    }

    void rasterTriangle(const DrawState& s, dxbc::Vm& ps, const detail::ClipVertex& v0, const detail::ClipVertex& v1,
                        const detail::ClipVertex& v2, int bandY0, int bandY1, DrawStats& st) {
        const detail::ClipVertex* v[3] = {&v0, &v1, &v2};
        float sx[3], sy[3], sz[3], invW[3];
        for (int i = 0; i < 3; ++i) {
            const float w = v[i]->pos[3];
            invW[i] = 1.0f / w;
            const float nx = v[i]->pos[0] * invW[i], ny = v[i]->pos[1] * invW[i], nz = v[i]->pos[2] * invW[i];
            sx[i] = s.viewport.x + (nx + 1.0f) * 0.5f * s.viewport.w;
            sy[i] = s.viewport.y + (1.0f - ny) * 0.5f * s.viewport.h;
            sz[i] = s.viewport.minZ + nz * (s.viewport.maxZ - s.viewport.minZ);
        }
        float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sx[2] - sx[0]) * (sy[1] - sy[0]);
        if (!(std::fabs(area) > 1e-12f)) return;
        const bool clockwise = area > 0;  // y hacia abajo: área positiva = horario en pantalla
        const bool front = s.frontCounterClockwise ? !clockwise : clockwise;
        if ((s.cullMode == 3 && !front) || (s.cullMode == 2 && front)) {
            ++st.culled;
            return;
        }
        // Orden horario para las funciones de arista.
        int o[3] = {0, 1, 2};
        if (!clockwise) {
            std::swap(o[1], o[2]);
            area = -area;
        }
        int minX = static_cast<int>(std::floor(std::min({sx[0], sx[1], sx[2]})));
        int maxX = static_cast<int>(std::ceil(std::max({sx[0], sx[1], sx[2]})));
        int minY = static_cast<int>(std::floor(std::min({sy[0], sy[1], sy[2]})));
        int maxY = static_cast<int>(std::ceil(std::max({sy[0], sy[1], sy[2]})));
        int clipL = 0, clipT = 0, clipR = static_cast<int>(s.rt.width), clipB = static_cast<int>(s.rt.height);
        clipL = std::max(clipL, static_cast<int>(std::floor(s.viewport.x)));
        clipT = std::max(clipT, static_cast<int>(std::floor(s.viewport.y)));
        clipR = std::min(clipR, static_cast<int>(std::ceil(s.viewport.x + s.viewport.w)));
        clipB = std::min(clipB, static_cast<int>(std::ceil(s.viewport.y + s.viewport.h)));
        if (s.scissorEnable) {
            clipL = std::max(clipL, s.scissor[0]);
            clipT = std::max(clipT, s.scissor[1]);
            clipR = std::min(clipR, s.scissor[2]);
            clipB = std::min(clipB, s.scissor[3]);
        }
        minX = std::max(minX, clipL);
        minY = std::max({minY, clipT, bandY0});
        maxX = std::min(maxX, clipR - 1);
        maxY = std::min({maxY, clipB - 1, bandY1 - 1});
        if (minX > maxX || minY > maxY) return;

        const float ax = sx[o[0]], ay = sy[o[0]], bx = sx[o[1]], by = sy[o[1]], cx = sx[o[2]], cy = sy[o[2]];
        auto edge = [](float x0, float y0, float x1, float y1, float px, float py) {
            return (x1 - x0) * (py - y0) - (y1 - y0) * (px - x0);
        };
        // Regla top-left (aristas horarias en y hacia abajo): arista superior o izquierda incluye 0.
        auto isTopLeft = [](float x0, float y0, float x1, float y1) {
            return (y0 == y1 && x1 > x0) || (y1 < y0);
        };
        const bool tl0 = isTopLeft(bx, by, cx, cy), tl1 = isTopLeft(cx, cy, ax, ay), tl2 = isTopLeft(ax, ay, bx, by);
        const float invArea = 1.0f / area;
        const auto& rt = s.rt;
        const uint32_t bpp = renderTargetBytes(rt.format);

        for (int y = minY; y <= maxY; ++y) {
            const float py = static_cast<float>(y) + 0.5f;
            uint8_t* row = rt.data + static_cast<size_t>(y) * rt.rowPitch;
            for (int x = minX; x <= maxX; ++x) {
                const float px = static_cast<float>(x) + 0.5f;
                const float w0 = edge(bx, by, cx, cy, px, py);
                const float w1 = edge(cx, cy, ax, ay, px, py);
                const float w2 = edge(ax, ay, bx, by, px, py);
                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                if ((w0 == 0 && !tl0) || (w1 == 0 && !tl1) || (w2 == 0 && !tl2)) continue;
                float b[3];
                b[o[0]] = w0 * invArea;
                b[o[1]] = w1 * invArea;
                b[o[2]] = w2 * invArea;
                if (shadePixel(s, ps, v, b, invW, sz, px, py, row + static_cast<size_t>(x) * bpp)) ++st.pixels;
            }
        }
    }

    // true si el píxel se escribió (no descartado).
    bool shadePixel(const DrawState& s, dxbc::Vm& ps, const detail::ClipVertex* const* v, const float* b,
                    const float* invW, const float* sz, float px, float py, uint8_t* dst) const {
        // Corrección de perspectiva: atributos/w interpolados y divididos por 1/w interpolado.
        const float iw = b[0] * invW[0] + b[1] * invW[1] + b[2] * invW[2];
        const float pb[3] = {b[0] * invW[0] / iw, b[1] * invW[1] / iw, b[2] * invW[2] / iw};
        for (int r = 0; r < 32; ++r) {
            const int src = psFromVs_[r];
            if (src == -1) continue;
            Vec4& in = ps.inputs[r];
            if (src == -2) {
                in.f[0] = px;
                in.f[1] = py;
                in.f[2] = b[0] * sz[0] + b[1] * sz[1] + b[2] * sz[2];
                in.f[3] = iw;
                continue;
            }
            const uint32_t interp = psInterp_[r];
            if (interp == 1) {  // constante: vértice provocador (el primero)
                in = v[0]->attr[src];
            } else if (interp == 4 || interp == 5) {  // sin perspectiva
                for (int c = 0; c < 4; ++c) {
                    in.f[c] = b[0] * v[0]->attr[src].f[c] + b[1] * v[1]->attr[src].f[c] + b[2] * v[2]->attr[src].f[c];
                }
            } else {
                for (int c = 0; c < 4; ++c) {
                    in.f[c] = pb[0] * v[0]->attr[src].f[c] + pb[1] * v[1]->attr[src].f[c] + pb[2] * v[2]->attr[src].f[c];
                }
            }
        }
        if (!ps.run() || ps.discarded) return false;
        float srcC[4];
        std::memcpy(srcC, ps.outputs[0].f, 16);
        const BlendRt& bl = s.blend;
        if (bl.enable) {
            float dstC[4] = {0, 0, 0, 0};
            readPixel(s.rt.format, dst, dstC);
            float outC[4];
            for (int c = 0; c < 3; ++c) {
                const float sf = detail::blendFactorValue(bl.src, c, srcC, dstC, s.blendFactor);
                const float df = detail::blendFactorValue(bl.dst, c, srcC, dstC, s.blendFactor);
                outC[c] = detail::blendOp(bl.op, srcC[c] * sf, dstC[c] * df);
            }
            const float sa = detail::blendFactorValue(bl.srcA, 3, srcC, dstC, s.blendFactor);
            const float da = detail::blendFactorValue(bl.dstA, 3, srcC, dstC, s.blendFactor);
            outC[3] = detail::blendOp(bl.opA, srcC[3] * sa, dstC[3] * da);
            writePixel(s.rt.format, dst, outC, bl.writeMask);
        } else {
            writePixel(s.rt.format, dst, srcC, bl.writeMask);
        }
        return true;
    }
};

}  // namespace hyperplay::raster
