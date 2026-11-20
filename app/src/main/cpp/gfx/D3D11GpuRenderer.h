#pragma once

// Renderizador D3D11 -> Vulkan (ruta GPU).
//
// El interceptor (NativeDllInterceptor) traduce el estado del ID3D11DeviceContext inmediato a
// estructuras de datos simples (DrawCall, TextureBinding...) y llama a este renderizador, que:
// - refleja en la GPU los recursos D3D11 (texturas -> VkImage, VB/IB/CB -> anillo por frame),
//   subiéndolos cuando cambia su `generation` de CPU;
// - traduce los shaders DXBC a SPIR-V (dxbc2spv) y crea pipelines por combinación de estado;
// - graba los draws en un command buffer por frame con render passes por render target;
// - en Present dibuja el backbuffer en el swapchain con la rotación de la superficie.
// Los render targets viven en la GPU: los draws no tocan la copia de CPU.

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../loader/D3D11ResourceModel.h"
#include "../loader/DxbcShader.h"

namespace hyperplay::gpu {

// Una textura D3D11 vista a través de un SRV/RTV.
struct TextureBinding {
    const void* key = nullptr;                // identidad del recurso (D3DResourceObj*)
    const d3d11::Resource* res = nullptr;     // datos de CPU (para subir)
    uint64_t generation = 0;                  // generación de CPU (cambia en Map/Update/Copy)
    uint32_t viewFormat = 0;                  // DXGI de la vista (0 = el del recurso)
    uint32_t viewDim = 0;                     // D3D11_SRV_DIMENSION / RTV_DIMENSION (0 = por defecto)
    uint32_t mostDetailedMip = 0, mipLevels = 0xFFFFFFFFu;
    uint32_t firstSlice = 0, arraySize = 0xFFFFFFFFu;
    bool valid() const { return key != nullptr && res != nullptr; }
};

struct BufferBinding {
    const void* key = nullptr;
    const uint8_t* data = nullptr;
    size_t size = 0;
    uint64_t generation = 0;
    uint32_t stride = 0, offset = 0;
};

struct VertexElement {
    uint32_t location = 0;  // registro v# del VS
    uint32_t format = 0;    // DXGI
    uint32_t slot = 0;
    uint32_t offset = 0;
    bool perInstance = false;
};

struct StageBindings {
    const dxbc::Program* program = nullptr;
    const void* key = nullptr;  // identidad del shader (D3DDescObj*)
    BufferBinding cb[14];
    TextureBinding srv[32];
    const uint8_t* sampler[16] = {};  // D3D11_SAMPLER_DESC (52 bytes) o nullptr
};

struct DrawCall {
    StageBindings vs, ps;
    const void* layoutKey = nullptr;
    std::vector<VertexElement> elements;
    BufferBinding vb[16];
    BufferBinding ib;
    uint32_t ibFormat = 57;  // DXGI_FORMAT_R16_UINT / R32_UINT (42)
    uint32_t topology = 4;
    uint32_t count = 0, start = 0;
    int32_t baseVertex = 0;
    bool indexed = false;
    uint32_t instanceCount = 1, startInstance = 0;
    TextureBinding rt;
    float viewport[6] = {0, 0, 0, 0, 0, 1};  // x, y, w, h, minZ, maxZ (0 = todo el RT)
    bool hasViewport = false;
    bool scissorEnable = false;
    int32_t scissor[4] = {0, 0, 0, 0};  // left, top, right, bottom
    uint32_t cullMode = 3;              // D3D11_CULL_MODE (1 none, 2 front, 3 back)
    bool frontCounterClockwise = false;
    bool blendEnable = false;
    uint32_t blend[6] = {2, 1, 1, 2, 1, 1};  // src, dst, op, srcA, dstA, opA (D3D11_BLEND / _OP)
    uint8_t writeMask = 0xF;
    float blendFactor[4] = {1, 1, 1, 1};
};

class Renderer {
public:
    static Renderer& shared();

    // true si la ruta GPU está activa (propiedad debug.hyperplay.gpu != 0 y dispositivo listo).
    bool available();

    // Graba un draw. false si no se pudo (shader no traducible, formato no soportado...).
    bool draw(const DrawCall& dc);
    // ClearRenderTargetView sobre la GPU.
    void clearRenderTarget(const TextureBinding& rt, const float rgba[4]);
    // CopyResource / CopySubresourceRegion completas entre texturas del mismo tamaño y formato.
    // false si alguna no es una textura reflejada en la GPU (el llamador copia en CPU).
    bool copyTexture(const TextureBinding& dst, const TextureBinding& src);
    // CopySubresourceRegion entre texturas en la GPU. box = D3D11_BOX (left, top, front, right,
    // bottom, back) o nullptr. false si no aplica (el llamador copia en CPU).
    bool copyTextureRegion(const TextureBinding& dst, uint32_t dstSub, uint32_t x, uint32_t y, uint32_t z,
                           const TextureBinding& src, uint32_t srcSub, const uint32_t* box);
    // true si la textura tiene contenido escrito por la GPU (la copia de CPU está obsoleta).
    bool isGpuWritten(const void* key);
    // Present: dibuja el backbuffer en el swapchain y envía el frame.
    bool present(const TextureBinding& backbuffer);
    // El recurso se destruye: libera su reflejo en la GPU.
    void release(const void* key);

    struct Stats {
        uint64_t draws = 0, failedDraws = 0, pipelines = 0, uploads = 0, presents = 0;
        uint64_t uploadBytes = 0;
    };
    Stats stats() const;

private:
    Renderer() = default;
};

}  // namespace hyperplay::gpu
