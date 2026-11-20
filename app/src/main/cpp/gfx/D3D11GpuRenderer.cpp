#include "D3D11GpuRenderer.h"

// Las estructuras Vulkan se inicializan con {sType} y el resto a cero (patrón habitual).
#pragma clang diagnostic ignored "-Wmissing-field-initializers"

#include <android/log.h>
#include <sys/system_properties.h>
#include <vulkan/vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "../loader/D3D11Sampler.h"
#include "DirectVulkanBridge.h"
#include "DxbcToSpirv.h"
#include "PresentSpv.h"
#include "PresentSgsrSpv.h"
#include "PresentOptions.h"
#include "HyperFrameGen.h"
#include "loader/FramePacing.h"
#include "loader/Win32DisplayInfo.h"

#define LOG_TAG "HyperPlay.GpuD3D11"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::gpu {
namespace {

// --------------------------------------------------------------------------- funciones Vulkan
#define HP_VK_DEVICE_FNS(X)                                                                                  \
    X(vkCreateBuffer) X(vkDestroyBuffer) X(vkGetBufferMemoryRequirements) X(vkAllocateMemory) X(vkFreeMemory) \
    X(vkBindBufferMemory) X(vkMapMemory) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements)   \
    X(vkBindImageMemory) X(vkCreateImageView) X(vkDestroyImageView) X(vkCreateSampler)                        \
    X(vkCreateShaderModule) X(vkDestroyShaderModule) X(vkCreateDescriptorSetLayout) X(vkCreatePipelineLayout) \
    X(vkCreateGraphicsPipelines) X(vkCreateRenderPass) X(vkCreateFramebuffer) X(vkDestroyFramebuffer)         \
    X(vkCreateDescriptorPool) X(vkResetDescriptorPool) X(vkAllocateDescriptorSets) X(vkUpdateDescriptorSets)  \
    X(vkCreateCommandPool) X(vkAllocateCommandBuffers) X(vkResetCommandBuffer) X(vkBeginCommandBuffer)        \
    X(vkEndCommandBuffer) X(vkCreateFence) X(vkWaitForFences) X(vkResetFences) X(vkCmdPipelineBarrier)        \
    X(vkCmdCopyBufferToImage) X(vkCmdCopyImage) X(vkCmdBeginRenderPass) X(vkCmdEndRenderPass)                 \
    X(vkCmdBindPipeline) X(vkCmdBindDescriptorSets) X(vkCmdBindVertexBuffers) X(vkCmdBindIndexBuffer)         \
    X(vkCmdDraw) X(vkCmdDrawIndexed) X(vkCmdSetViewport) X(vkCmdSetScissor) X(vkCmdSetBlendConstants)         \
    X(vkCmdClearColorImage) X(vkCmdClearAttachments) X(vkCmdPushConstants)

struct Fns {
#define HP_DECL(name) PFN_##name name = nullptr;
    HP_VK_DEVICE_FNS(HP_DECL)
#undef HP_DECL
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties = nullptr;
    PFN_vkGetPhysicalDeviceProperties getProperties = nullptr;
    PFN_vkGetPhysicalDeviceFormatProperties getFormatProperties = nullptr;
};

// --------------------------------------------------------------------------- formatos
struct VkFmt {
    VkFormat format = VK_FORMAT_UNDEFINED;
    bool alphaFromRed = false;  // A8_UNORM -> R8 con swizzle (0,0,0,R)
    bool block = false;
};

uint32_t baseOfTypeless(uint32_t f) {
    switch (f) {
        case 1: return 2;    // R32G32B32A32
        case 5: return 6;    // R32G32B32
        case 9: return 10;   // R16G16B16A16
        case 15: return 16;  // R32G32
        case 23: return 24;  // R10G10B10A2
        case 27: return 28;  // R8G8B8A8
        case 33: return 34;  // R16G16
        case 39: return 41;  // R32 (D32 typeless -> R32_FLOAT)
        case 47: return 49;  // R8G8
        case 53: return 54;  // R16
        case 60: return 61;  // R8
        case 70: return 71;  // BC1
        case 73: return 74;  // BC2
        case 76: return 77;  // BC3
        case 79: return 80;  // BC4
        case 82: return 83;  // BC5
        case 90: return 87;  // B8G8R8A8
        case 92: return 88;  // B8G8R8X8
        case 97: return 98;  // BC7
        default: return f;
    }
}

VkFmt textureFormat(uint32_t dxgi) {
    switch (baseOfTypeless(dxgi)) {
        case 2: return {VK_FORMAT_R32G32B32A32_SFLOAT};
        case 3: return {VK_FORMAT_R32G32B32A32_UINT};
        case 4: return {VK_FORMAT_R32G32B32A32_SINT};
        case 10: return {VK_FORMAT_R16G16B16A16_SFLOAT};
        case 11: return {VK_FORMAT_R16G16B16A16_UNORM};
        case 13: return {VK_FORMAT_R16G16B16A16_SNORM};
        case 16: return {VK_FORMAT_R32G32_SFLOAT};
        case 24: return {VK_FORMAT_A2B10G10R10_UNORM_PACK32};
        case 26: return {VK_FORMAT_B10G11R11_UFLOAT_PACK32};
        case 28: return {VK_FORMAT_R8G8B8A8_UNORM};
        case 29: return {VK_FORMAT_R8G8B8A8_SRGB};
        case 30: return {VK_FORMAT_R8G8B8A8_UINT};
        case 31: return {VK_FORMAT_R8G8B8A8_SNORM};
        case 34: return {VK_FORMAT_R16G16_SFLOAT};
        case 35: return {VK_FORMAT_R16G16_UNORM};
        case 41: return {VK_FORMAT_R32_SFLOAT};
        case 42: return {VK_FORMAT_R32_UINT};
        case 49: return {VK_FORMAT_R8G8_UNORM};
        case 54: return {VK_FORMAT_R16_SFLOAT};
        case 56: return {VK_FORMAT_R16_UNORM};
        case 61: return {VK_FORMAT_R8_UNORM};
        case 65: return {VK_FORMAT_R8_UNORM, true};
        case 71: return {VK_FORMAT_BC1_RGBA_UNORM_BLOCK, false, true};
        case 72: return {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, false, true};
        case 74: return {VK_FORMAT_BC2_UNORM_BLOCK, false, true};
        case 75: return {VK_FORMAT_BC2_SRGB_BLOCK, false, true};
        case 77: return {VK_FORMAT_BC3_UNORM_BLOCK, false, true};
        case 78: return {VK_FORMAT_BC3_SRGB_BLOCK, false, true};
        case 80: return {VK_FORMAT_BC4_UNORM_BLOCK, false, true};
        case 81: return {VK_FORMAT_BC4_SNORM_BLOCK, false, true};
        case 83: return {VK_FORMAT_BC5_UNORM_BLOCK, false, true};
        case 84: return {VK_FORMAT_BC5_SNORM_BLOCK, false, true};
        case 85: return {VK_FORMAT_R5G6B5_UNORM_PACK16};
        case 87: return {VK_FORMAT_B8G8R8A8_UNORM};
        case 88: return {VK_FORMAT_B8G8R8A8_UNORM};  // B8G8R8X8: alfa ignorado por el juego
        case 91: return {VK_FORMAT_B8G8R8A8_SRGB};
        case 98: return {VK_FORMAT_BC7_UNORM_BLOCK, false, true};
        case 99: return {VK_FORMAT_BC7_SRGB_BLOCK, false, true};
        default: return {};
    }
}

VkFormat vertexFormat(uint32_t dxgi) {
    switch (dxgi) {
        case 2: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case 3: return VK_FORMAT_R32G32B32A32_UINT;
        case 4: return VK_FORMAT_R32G32B32A32_SINT;
        case 6: return VK_FORMAT_R32G32B32_SFLOAT;
        case 7: return VK_FORMAT_R32G32B32_UINT;
        case 8: return VK_FORMAT_R32G32B32_SINT;
        case 10: return VK_FORMAT_R16G16B16A16_SFLOAT;
        case 11: return VK_FORMAT_R16G16B16A16_UNORM;
        case 12: return VK_FORMAT_R16G16B16A16_UINT;
        case 13: return VK_FORMAT_R16G16B16A16_SNORM;
        case 14: return VK_FORMAT_R16G16B16A16_SINT;
        case 16: return VK_FORMAT_R32G32_SFLOAT;
        case 17: return VK_FORMAT_R32G32_UINT;
        case 18: return VK_FORMAT_R32G32_SINT;
        case 24: return VK_FORMAT_A2B10G10R10_UNORM_PACK32;
        case 28: return VK_FORMAT_R8G8B8A8_UNORM;
        case 30: return VK_FORMAT_R8G8B8A8_UINT;
        case 31: return VK_FORMAT_R8G8B8A8_SNORM;
        case 32: return VK_FORMAT_R8G8B8A8_SINT;
        case 34: return VK_FORMAT_R16G16_SFLOAT;
        case 35: return VK_FORMAT_R16G16_UNORM;
        case 36: return VK_FORMAT_R16G16_UINT;
        case 37: return VK_FORMAT_R16G16_SNORM;
        case 38: return VK_FORMAT_R16G16_SINT;
        case 41: return VK_FORMAT_R32_SFLOAT;
        case 42: return VK_FORMAT_R32_UINT;
        case 43: return VK_FORMAT_R32_SINT;
        case 49: return VK_FORMAT_R8G8_UNORM;
        case 54: return VK_FORMAT_R16_SFLOAT;
        case 87: return VK_FORMAT_B8G8R8A8_UNORM;
        default: return VK_FORMAT_UNDEFINED;
    }
}

VkBlendFactor blendFactor(uint32_t b) {
    switch (b) {
        case 1: return VK_BLEND_FACTOR_ZERO;
        case 2: return VK_BLEND_FACTOR_ONE;
        case 3: return VK_BLEND_FACTOR_SRC_COLOR;
        case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
        case 5: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 6: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 7: return VK_BLEND_FACTOR_DST_ALPHA;
        case 8: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 9: return VK_BLEND_FACTOR_DST_COLOR;
        case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_COLOR;
        case 11: return VK_BLEND_FACTOR_SRC_ALPHA_SATURATE;
        case 14: return VK_BLEND_FACTOR_CONSTANT_COLOR;
        case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_COLOR;
        case 16: return VK_BLEND_FACTOR_SRC1_COLOR;
        case 17: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_COLOR;
        case 18: return VK_BLEND_FACTOR_SRC1_ALPHA;
        case 19: return VK_BLEND_FACTOR_ONE_MINUS_SRC1_ALPHA;
        default: return VK_BLEND_FACTOR_ONE;
    }
}
// Para el alfa, los factores de color se usan como su versión alfa (igual que D3D).
VkBlendFactor alphaFactor(uint32_t b) {
    switch (b) {
        case 3: return VK_BLEND_FACTOR_SRC_ALPHA;
        case 4: return VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        case 9: return VK_BLEND_FACTOR_DST_ALPHA;
        case 10: return VK_BLEND_FACTOR_ONE_MINUS_DST_ALPHA;
        case 14: return VK_BLEND_FACTOR_CONSTANT_ALPHA;
        case 15: return VK_BLEND_FACTOR_ONE_MINUS_CONSTANT_ALPHA;
        default: return blendFactor(b);
    }
}
VkBlendOp blendOp(uint32_t o) {
    switch (o) {
        case 2: return VK_BLEND_OP_SUBTRACT;
        case 3: return VK_BLEND_OP_REVERSE_SUBTRACT;
        case 4: return VK_BLEND_OP_MIN;
        case 5: return VK_BLEND_OP_MAX;
        default: return VK_BLEND_OP_ADD;
    }
}
VkSamplerAddressMode addressMode(uint32_t a) {
    switch (a) {
        case 2: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
        case 3: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        case 4: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
        case 5: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;  // MIRROR_ONCE sin extensión
        default: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    }
}

// --------------------------------------------------------------------------- estructuras
struct RingChunk {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    uint8_t* ptr = nullptr;
    VkDeviceSize size = 0, used = 0;
};
struct RingAlloc {
    VkBuffer buffer = VK_NULL_HANDLE;
    VkDeviceSize offset = 0;
    uint8_t* ptr = nullptr;
};

struct Frame {
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    std::vector<VkDescriptorPool> pools;
    size_t poolIndex = 0;
    std::vector<RingChunk> chunks;
    size_t chunkIndex = 0;
    // Buffers ya copiados en este frame: (clave ^ generación ^ offset) -> asignación.
    std::unordered_map<uint64_t, RingAlloc> bufferCache;
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFmt fmt;
    uint32_t dxgi = 0;
    uint32_t width = 1, height = 1, depth = 1, mips = 1, layers = 1;
    bool cube = false, attachable = false, decompressed = false, is3D = false, is1D = false;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    uint64_t uploadedGen = 0;
    bool gpuWritten = false;
    std::map<uint64_t, VkImageView> views;
    std::map<VkImageView, VkFramebuffer> framebuffers;
};

struct ShaderEntry {
    VkShaderModule module = VK_NULL_HANDLE;
    dxbc2spv::Result result;
};

struct PairLayout {
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    std::vector<VkDescriptorSetLayoutBinding> bindings;
};

inline uint64_t mix(uint64_t h, uint64_t v) { return (h ^ v) * 0x100000001B3ull + (h >> 7); }

class Impl {
public:
    std::mutex mtx;
    bool initTried = false, ok = false;
    DirectVulkanBridge::GpuContext ctx;
    Fns f;
    VkPhysicalDeviceMemoryProperties memProps{};
    VkDeviceSize uboAlign = 256;
    VkCommandPool cmdPool = VK_NULL_HANDLE;
    std::array<Frame, 2> frames;
    size_t slot = 0;
    bool recording = false;
    Renderer::Stats stats;

    std::unordered_map<const void*, std::unique_ptr<GpuImage>> images;
    std::unordered_map<const void*, ShaderEntry> vsModules;
    std::map<std::pair<const void*, const void*>, ShaderEntry> psModules;
    std::map<std::pair<const void*, const void*>, PairLayout> layouts;
    std::unordered_map<std::string, VkPipeline> pipelines;
    std::unordered_map<std::string, VkSampler> samplers;
    std::map<uint64_t, VkRenderPass> renderPasses;  // formato | (loadOp CLEAR << 40)

    // Recursos por defecto para ranuras sin enlazar.
    GpuImage dummy2D, dummy3D, dummy1D;
    VkSampler defaultSampler = VK_NULL_HANDLE, presentSampler = VK_NULL_HANDLE;
    VkBuffer zeroBuffer = VK_NULL_HANDLE;
    VkDeviceMemory zeroMemory = VK_NULL_HANDLE;

    // Render pass abierto.
    bool passOpen = false;
    VkImageView passView = VK_NULL_HANDLE;
    GpuImage* passImage = nullptr;

    // Presentación.
    VkRenderPass presentPass = VK_NULL_HANDLE;
    VkFormat presentFormat = VK_FORMAT_UNDEFINED;
    VkPipeline presentPipeline = VK_NULL_HANDLE;
    VkPipelineLayout presentLayout = VK_NULL_HANDLE;
    // SGSR v1 (mismo render pass, vértice y set; push constants del fragmento en 32..63).
    VkPipeline sgsrPipeline = VK_NULL_HANDLE;
    VkPipelineLayout sgsrLayout = VK_NULL_HANDLE;
    VkDescriptorSetLayout presentSetLayout = VK_NULL_HANDLE;
    uint64_t presentGeneration = ~0ull;
    std::map<VkImageView, VkFramebuffer> presentFbs;

    bool enabledByProp() {
        char v[PROP_VALUE_MAX] = {};
        return !(__system_property_get("debug.hyperplay.gpu", v) > 0 && v[0] == '0');
    }

    bool init() {
        if (initTried) return ok;
        if (!enabledByProp()) {
            initTried = true;
            return false;
        }
        if (!DirectVulkanBridge::shared().gpuContext(ctx)) return false;  // reintenta más tarde
        initTried = true;
#define HP_LOAD(name)                                                                       \
    f.name = reinterpret_cast<PFN_##name>(ctx.getDeviceProcAddr(ctx.device, #name));        \
    if (f.name == nullptr) {                                                                \
        LOGW("Falta %s: ruta GPU desactivada", #name);                                      \
        return false;                                                                       \
    }
        HP_VK_DEVICE_FNS(HP_LOAD)
#undef HP_LOAD
        f.getMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            ctx.getInstanceProcAddr(ctx.instance, "vkGetPhysicalDeviceMemoryProperties"));
        f.getProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(
            ctx.getInstanceProcAddr(ctx.instance, "vkGetPhysicalDeviceProperties"));
        f.getFormatProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(
            ctx.getInstanceProcAddr(ctx.instance, "vkGetPhysicalDeviceFormatProperties"));
        if (!f.getMemoryProperties || !f.getProperties || !f.getFormatProperties) return false;
        f.getMemoryProperties(ctx.physicalDevice, &memProps);
        VkPhysicalDeviceProperties props{};
        f.getProperties(ctx.physicalDevice, &props);
        uboAlign = std::max<VkDeviceSize>(props.limits.minUniformBufferOffsetAlignment, 16);

        VkCommandPoolCreateInfo cp{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
        cp.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        cp.queueFamilyIndex = ctx.queueFamily;
        if (f.vkCreateCommandPool(ctx.device, &cp, nullptr, &cmdPool) != VK_SUCCESS) return false;
        for (Frame& fr : frames) {
            VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
            ai.commandPool = cmdPool;
            ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
            ai.commandBufferCount = 1;
            if (f.vkAllocateCommandBuffers(ctx.device, &ai, &fr.cmd) != VK_SUCCESS) return false;
            VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
            fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
            if (f.vkCreateFence(ctx.device, &fi, nullptr, &fr.fence) != VK_SUCCESS) return false;
        }
        if (!createDummies()) return false;
        ok = true;
        LOGI("Ruta GPU D3D11 -> Vulkan activa (BC nativo=%d, anisotropía=%d, alineación UBO=%llu)",
             ctx.textureCompressionBC ? 1 : 0, ctx.samplerAnisotropy ? 1 : 0,
             static_cast<unsigned long long>(uboAlign));
        return true;
    }

    // ----------------------------------------------------------------------- memoria
    uint32_t memoryType(uint32_t bits, VkMemoryPropertyFlags want) {
        for (uint32_t i = 0; i < memProps.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (memProps.memoryTypes[i].propertyFlags & want) == want) return i;
        }
        return UINT32_MAX;
    }
    bool createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible, VkBuffer& buf,
                      VkDeviceMemory& mem, uint8_t** mapped) {
        VkBufferCreateInfo bi{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        bi.size = size;
        bi.usage = usage;
        bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (f.vkCreateBuffer(ctx.device, &bi, nullptr, &buf) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        f.vkGetBufferMemoryRequirements(ctx.device, buf, &req);
        const VkMemoryPropertyFlags want = hostVisible
                                               ? (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)
                                               : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memoryType(req.memoryTypeBits, want);
        if (ai.memoryTypeIndex == UINT32_MAX) return false;
        if (f.vkAllocateMemory(ctx.device, &ai, nullptr, &mem) != VK_SUCCESS) return false;
        f.vkBindBufferMemory(ctx.device, buf, mem, 0);
        if (mapped != nullptr) {
            void* p = nullptr;
            if (f.vkMapMemory(ctx.device, mem, 0, VK_WHOLE_SIZE, 0, &p) != VK_SUCCESS) return false;
            *mapped = static_cast<uint8_t*>(p);
        }
        return true;
    }

    RingAlloc ringAlloc(VkDeviceSize size, VkDeviceSize align) {
        Frame& fr = frames[slot];
        size = std::max<VkDeviceSize>(size, 4);
        for (;;) {
            if (fr.chunkIndex < fr.chunks.size()) {
                RingChunk& c = fr.chunks[fr.chunkIndex];
                const VkDeviceSize off = (c.used + align - 1) / align * align;
                if (off + size <= c.size) {
                    c.used = off + size;
                    return {c.buffer, off, c.ptr + off};
                }
                ++fr.chunkIndex;
                continue;
            }
            RingChunk c;
            c.size = std::max<VkDeviceSize>(32ull << 20, size + align);
            const VkBufferUsageFlags usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                             VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
            if (!createBuffer(c.size, usage, true, c.buffer, c.memory, &c.ptr)) {
                LOGW("Anillo: no se pudo reservar %llu bytes", static_cast<unsigned long long>(c.size));
                return {};
            }
            fr.chunks.push_back(c);
        }
    }

    // ----------------------------------------------------------------------- imágenes
    bool createImage(GpuImage& g) {
        VkImageCreateInfo ii{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ii.imageType = g.is3D ? VK_IMAGE_TYPE_3D : (g.is1D ? VK_IMAGE_TYPE_1D : VK_IMAGE_TYPE_2D);
        ii.format = g.format;
        ii.extent = {g.width, g.height, g.depth};
        ii.mipLevels = g.mips;
        ii.arrayLayers = g.layers;
        ii.samples = VK_SAMPLE_COUNT_1_BIT;
        ii.tiling = VK_IMAGE_TILING_OPTIMAL;
        ii.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        if (g.attachable) ii.usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        if (!g.fmt.block) ii.flags |= VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT;
        if (g.cube) ii.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
        ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (f.vkCreateImage(ctx.device, &ii, nullptr, &g.image) != VK_SUCCESS) return false;
        VkMemoryRequirements req{};
        f.vkGetImageMemoryRequirements(ctx.device, g.image, &req);
        VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = memoryType(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if (ai.memoryTypeIndex == UINT32_MAX) ai.memoryTypeIndex = memoryType(req.memoryTypeBits, 0);
        if (f.vkAllocateMemory(ctx.device, &ai, nullptr, &g.memory) != VK_SUCCESS) {
            f.vkDestroyImage(ctx.device, g.image, nullptr);
            g.image = VK_NULL_HANDLE;
            return false;
        }
        f.vkBindImageMemory(ctx.device, g.image, g.memory, 0);
        g.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        return true;
    }
    void destroyImage(GpuImage& g) {
        pendingClears.erase(&g);
        for (auto& [v, fb] : g.framebuffers) f.vkDestroyFramebuffer(ctx.device, fb, nullptr);
        for (auto& [k, v] : g.views) f.vkDestroyImageView(ctx.device, v, nullptr);
        if (g.image) f.vkDestroyImage(ctx.device, g.image, nullptr);
        if (g.memory) f.vkFreeMemory(ctx.device, g.memory, nullptr);
        g = GpuImage{};
    }

    VkImageView view(GpuImage& g, VkImageViewType type, VkFormat format, uint32_t baseMip, uint32_t mipCount,
                     uint32_t baseLayer, uint32_t layerCount) {
        baseMip = std::min(baseMip, g.mips - 1);
        mipCount = std::min(mipCount, g.mips - baseMip);
        baseLayer = std::min(baseLayer, g.layers - 1);
        layerCount = std::min(layerCount, g.layers - baseLayer);
        if (type == VK_IMAGE_VIEW_TYPE_CUBE && (g.layers < 6 || !g.cube)) type = VK_IMAGE_VIEW_TYPE_2D;
        if (type == VK_IMAGE_VIEW_TYPE_CUBE) layerCount = 6;
        if (type == VK_IMAGE_VIEW_TYPE_2D || type == VK_IMAGE_VIEW_TYPE_1D || type == VK_IMAGE_VIEW_TYPE_3D) {
            layerCount = 1;
        }
        if (g.is3D) type = VK_IMAGE_VIEW_TYPE_3D;
        uint64_t key = mix(mix(mix(static_cast<uint64_t>(type), static_cast<uint64_t>(format)), baseMip * 64 + mipCount),
                           baseLayer * 4096 + layerCount);
        auto it = g.views.find(key);
        if (it != g.views.end()) return it->second;
        VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        vi.image = g.image;
        vi.viewType = type;
        vi.format = format;
        if (g.fmt.alphaFromRed) {
            vi.components = {VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO,
                             VK_COMPONENT_SWIZZLE_R};
        }
        vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, baseMip, mipCount, baseLayer, layerCount};
        VkImageView v = VK_NULL_HANDLE;
        if (f.vkCreateImageView(ctx.device, &vi, nullptr, &v) != VK_SUCCESS) return VK_NULL_HANDLE;
        g.views[key] = v;
        return v;
    }

    void barrier(GpuImage& g, VkImageLayout to) {
        if (g.layout == to) return;
        endPass();
        VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
        b.oldLayout = g.layout;
        b.newLayout = to;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = g.image;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, VK_REMAINING_ARRAY_LAYERS};
        f.vkCmdPipelineBarrier(frames[slot].cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                               0, 0, nullptr, 0, nullptr, 1, &b);
        g.layout = to;
    }

    bool createDummy(GpuImage& g, bool is3D, bool is1D) {
        g.format = VK_FORMAT_R8G8B8A8_UNORM;
        g.fmt = {VK_FORMAT_R8G8B8A8_UNORM};
        g.dxgi = 28;
        g.width = g.height = 1;
        g.is3D = is3D;
        g.is1D = is1D;
        g.layers = (is3D || is1D) ? 1 : 6;
        g.cube = !is3D && !is1D;
        return createImage(g);
    }
    bool createDummies() {
        if (!createDummy(dummy2D, false, false) || !createDummy(dummy3D, true, false) ||
            !createDummy(dummy1D, false, true)) {
            return false;
        }
        if (!createBuffer(65536, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true,
                          zeroBuffer, zeroMemory, nullptr)) {
            return false;
        }
        void* p = nullptr;
        if (f.vkMapMemory(ctx.device, zeroMemory, 0, VK_WHOLE_SIZE, 0, &p) == VK_SUCCESS) std::memset(p, 0, 65536);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        si.magFilter = si.minFilter = VK_FILTER_LINEAR;
        si.addressModeU = si.addressModeV = si.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        si.maxLod = VK_LOD_CLAMP_NONE;
        if (f.vkCreateSampler(ctx.device, &si, nullptr, &defaultSampler) != VK_SUCCESS) return false;
        presentSampler = defaultSampler;
        return true;
    }
    // Las imágenes por defecto se limpian a 0 al empezar el primer frame.
    bool dummiesCleared = false;
    void clearDummies() {
        if (dummiesCleared) return;
        dummiesCleared = true;
        for (GpuImage* g : {&dummy2D, &dummy3D, &dummy1D}) {
            barrier(*g, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue c{};
            VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, g->layers};
            f.vkCmdClearColorImage(frames[slot].cmd, g->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
            barrier(*g, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }
    }

    // Reflejo GPU de una textura D3D11 (creación + subida si la CPU la cambió).
    // Clears diferidos: se aplican como LOAD_OP_CLEAR en el siguiente pase sobre la imagen (en
    // GPU por mosaicos evita escribir el RT entero y volver a cargarlo). Cualquier otro uso
    // (muestreo, copia, presentación) lo ejecuta antes con vkCmdClearColorImage.
    std::unordered_map<GpuImage*, VkClearColorValue> pendingClears;

    void flushClear(GpuImage* g) {
        auto it = pendingClears.find(g);
        if (it == pendingClears.end()) return;
        const VkClearColorValue c = it->second;
        pendingClears.erase(it);
        endPass();
        barrier(*g, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f.vkCmdClearColorImage(frames[slot].cmd, g->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
    }

    GpuImage* image(const TextureBinding& t, bool asTarget, bool keepPendingClear = false) {
        GpuImage* g = imageNoFlush(t, asTarget);
        if (g != nullptr && !keepPendingClear && !pendingClears.empty()) flushClear(g);
        return g;
    }

    GpuImage* imageNoFlush(const TextureBinding& t, bool asTarget) {
        if (!t.valid()) return nullptr;
        const d3d11::ResourceDesc& d = t.res->desc();
        if (d.dim == d3d11::Dimension::Buffer || d.dim == d3d11::Dimension::Unknown) return nullptr;
        auto& slotPtr = images[t.key];
        VkFmt fmt = textureFormat(d.format);
        bool decompress = false;
        if (fmt.block && !ctx.textureCompressionBC) {
            decompress = true;
            const uint32_t b = baseOfTypeless(d.format);
            fmt = {(b == 72 || b == 75 || b == 78 || b == 99) ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM};
        }
        if (fmt.format == VK_FORMAT_UNDEFINED) {
            static int warned = 0;
            if (warned++ < 8) LOGW("Formato de textura DXGI %u sin equivalente Vulkan", d.format);
            return nullptr;
        }
        const bool wantAttach = asTarget || (d.bindFlags & 0x20u) != 0;  // D3D11_BIND_RENDER_TARGET
        if (!slotPtr || slotPtr->width != d.width || slotPtr->height != d.height || slotPtr->format != fmt.format ||
            (wantAttach && !slotPtr->attachable)) {
            if (slotPtr) destroyImage(*slotPtr);
            slotPtr = std::make_unique<GpuImage>();
            GpuImage& g = *slotPtr;
            g.format = fmt.format;
            g.fmt = fmt;
            g.dxgi = d.format;
            g.decompressed = decompress;
            g.width = d.width;
            g.height = d.dim == d3d11::Dimension::Texture1D ? 1 : d.height;
            g.is3D = d.dim == d3d11::Dimension::Texture3D;
            g.is1D = d.dim == d3d11::Dimension::Texture1D;
            g.depth = g.is3D ? d.depth : 1;
            g.mips = std::max(1u, d.mipLevels);
            g.layers = g.is3D ? 1 : std::max(1u, d.arraySize);
            g.cube = (d.miscFlags & 0x4u) != 0 && g.layers >= 6;
            g.attachable = wantAttach && !fmt.block && !decompress;
            if (!createImage(g)) {
                LOGW("vkCreateImage %ux%u fmt %d falló", g.width, g.height, static_cast<int>(g.format));
                slotPtr.reset();
                return nullptr;
            }
            // Contenido inicial definido (D3D11 sin datos iniciales: indefinido; usamos 0).
            barrier(g, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkClearColorValue c{};
            if (!fmt.block) {
                VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, g.mips, 0, g.layers};
                f.vkCmdClearColorImage(frames[slot].cmd, g.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
            }
        }
        GpuImage& g = *slotPtr;
        if (t.generation != g.uploadedGen && t.res->hasStorage()) {
            if (g.gpuWritten) {
                static int warned = 0;
                if (warned++ < 6) {
                    LOGW("Resubida de %p (%ux%u fmt %u) con contenido de GPU: la CPU lo cambió (gen %llu -> %llu)", t.key,
                         g.width, g.height, g.dxgi, static_cast<unsigned long long>(g.uploadedGen),
                         static_cast<unsigned long long>(t.generation));
                }
            }
            upload(g, *t.res);
        }
        g.uploadedGen = t.generation;
        return &g;
    }

    void upload(GpuImage& g, const d3d11::Resource& cres) {
        auto& res = const_cast<d3d11::Resource&>(cres);  // data() no es const (reserva perezosa)
        const d3d11::ResourceDesc& d = res.desc();
        const uint32_t subs = g.mips * g.layers;
        std::vector<VkBufferImageCopy> regions;
        const d3d11::FormatInfo fi = d3d11::formatInfo(baseOfTypeless(d.format));
        barrier(g, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (uint32_t s = 0; s < subs; ++s) {
            const d3d11::SubresourceLayout* l = res.layout(s);
            const uint8_t* src = res.data(s);
            if (l == nullptr || src == nullptr) continue;
            const uint32_t mip = s % g.mips, layer = s / g.mips;
            VkBufferImageCopy r{};
            r.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, layer, 1};
            r.imageExtent = {l->width, l->height, l->depth};
            if (g.decompressed) {
                std::vector<uint8_t> rgba;
                if (!d3d11::decompressToRgba8(baseOfTypeless(d.format), src, l->width, l->height, l->rowPitch, rgba)) {
                    continue;
                }
                RingAlloc a = ringAlloc(rgba.size(), 16);
                if (a.ptr == nullptr) continue;
                std::memcpy(a.ptr, rgba.data(), rgba.size());
                r.bufferOffset = a.offset;
                regions.clear();
                regions.push_back(r);
                f.vkCmdCopyBufferToImage(frames[slot].cmd, a.buffer, g.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                         &regions[0]);
                stats.uploadBytes += rgba.size();
                continue;
            }
            const size_t bytes = static_cast<size_t>(l->depthPitch != 0 ? l->depthPitch : l->rowPitch * l->rows) * l->depth;
            RingAlloc a = ringAlloc(bytes, 16);
            if (a.ptr == nullptr) continue;
            std::memcpy(a.ptr, src, std::min(bytes, l->size));
            r.bufferOffset = a.offset;
            const uint32_t texelsPerRow = fi.block ? (l->rowPitch / fi.bytes) * 4 : l->rowPitch / std::max(1u, fi.bytes);
            r.bufferRowLength = texelsPerRow;
            r.bufferImageHeight = fi.block ? l->rows * 4 : l->rows;
            f.vkCmdCopyBufferToImage(frames[slot].cmd, a.buffer, g.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &r);
            stats.uploadBytes += bytes;
        }
        ++stats.uploads;
        g.gpuWritten = false;
    }

    // ----------------------------------------------------------------------- frame
    bool beginFrame() {
        if (recording) return true;
        Frame& fr = frames[slot];
        f.vkWaitForFences(ctx.device, 1, &fr.fence, VK_TRUE, 2000000000ull);
        f.vkResetFences(ctx.device, 1, &fr.fence);
        for (VkDescriptorPool p : fr.pools) f.vkResetDescriptorPool(ctx.device, p, 0);
        fr.poolIndex = 0;
        for (RingChunk& c : fr.chunks) c.used = 0;
        fr.chunkIndex = 0;
        fr.bufferCache.clear();
        f.vkResetCommandBuffer(fr.cmd, 0);
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        if (f.vkBeginCommandBuffer(fr.cmd, &bi) != VK_SUCCESS) return false;
        recording = true;
        clearDummies();
        return true;
    }

    VkDescriptorSet allocSet(VkDescriptorSetLayout layout) {
        Frame& fr = frames[slot];
        for (;;) {
            if (fr.poolIndex >= fr.pools.size()) {
                const VkDescriptorPoolSize sizes[] = {
                    {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 8192},
                    {VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 8192},
                    {VK_DESCRIPTOR_TYPE_SAMPLER, 8192},
                    {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 64},
                };
                VkDescriptorPoolCreateInfo pi{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
                pi.maxSets = 2048;
                pi.poolSizeCount = 4;
                pi.pPoolSizes = sizes;
                VkDescriptorPool p = VK_NULL_HANDLE;
                if (f.vkCreateDescriptorPool(ctx.device, &pi, nullptr, &p) != VK_SUCCESS) return VK_NULL_HANDLE;
                fr.pools.push_back(p);
            }
            VkDescriptorSetAllocateInfo ai{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
            ai.descriptorPool = fr.pools[fr.poolIndex];
            ai.descriptorSetCount = 1;
            ai.pSetLayouts = &layout;
            VkDescriptorSet set = VK_NULL_HANDLE;
            if (f.vkAllocateDescriptorSets(ctx.device, &ai, &set) == VK_SUCCESS) return set;
            ++fr.poolIndex;
        }
    }

    // ----------------------------------------------------------------------- render pass
    VkRenderPass renderPass(VkFormat format, bool clearOnLoad = false) {
        const uint64_t key = static_cast<uint64_t>(format) | (clearOnLoad ? (1ull << 40) : 0);
        auto it = renderPasses.find(key);
        if (it != renderPasses.end()) return it->second;
        VkAttachmentDescription a{};
        a.format = format;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = clearOnLoad ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        a.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &ref;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = 1;
        ri.pAttachments = &a;
        ri.subpassCount = 1;
        ri.pSubpasses = &sp;
        VkRenderPass rp = VK_NULL_HANDLE;
        if (f.vkCreateRenderPass(ctx.device, &ri, nullptr, &rp) != VK_SUCCESS) return VK_NULL_HANDLE;
        renderPasses[key] = rp;
        return rp;
    }

    void endPass() {
        if (!passOpen) return;
        f.vkCmdEndRenderPass(frames[slot].cmd);
        passOpen = false;
        passView = VK_NULL_HANDLE;
        passImage = nullptr;
    }

    bool beginPass(GpuImage& g, VkImageView v, uint32_t w, uint32_t h, VkFormat viewFormat) {
        if (passOpen && passView == v) return true;
        endPass();
        barrier(g, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL);
        auto pending = pendingClears.find(&g);
        const bool clearOnLoad = pending != pendingClears.end();
        VkClearValue clearValue{};
        if (clearOnLoad) {
            clearValue.color = pending->second;
            pendingClears.erase(pending);
        }
        // Los framebuffers son compatibles con ambas variantes (solo cambia loadOp).
        VkRenderPass rp = renderPass(viewFormat, clearOnLoad);
        if (rp == VK_NULL_HANDLE) return false;
        VkFramebuffer& fb = g.framebuffers[v];
        if (fb == VK_NULL_HANDLE) {
            VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
            fi.renderPass = rp;
            fi.attachmentCount = 1;
            fi.pAttachments = &v;
            fi.width = w;
            fi.height = h;
            fi.layers = 1;
            if (f.vkCreateFramebuffer(ctx.device, &fi, nullptr, &fb) != VK_SUCCESS) {
                g.framebuffers.erase(v);
                return false;
            }
        }
        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
        bi.renderPass = rp;
        bi.framebuffer = fb;
        bi.renderArea = {{0, 0}, {w, h}};
        if (clearOnLoad) {
            bi.clearValueCount = 1;
            bi.pClearValues = &clearValue;
        }
        f.vkCmdBeginRenderPass(frames[slot].cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
        passOpen = true;
        passView = v;
        passImage = &g;
        g.gpuWritten = true;
        return true;
    }

    // ----------------------------------------------------------------------- shaders y pipelines
    ShaderEntry* vsModule(const StageBindings& vs) {
        auto it = vsModules.find(vs.key);
        if (it != vsModules.end()) return it->second.module ? &it->second : nullptr;
        ShaderEntry& e = vsModules[vs.key];
        e.result = dxbc2spv::translate(*vs.program);
        if (!e.result.ok()) {
            LOGW("VS %p no traducible: %s", vs.key, e.result.error.c_str());
            return nullptr;
        }
        e.module = makeModule(e.result.spirv);
        return e.module ? &e : nullptr;
    }
    ShaderEntry* psModule(const StageBindings& ps, const StageBindings& vs) {
        const auto key = std::make_pair(ps.key, vs.key);
        auto it = psModules.find(key);
        if (it != psModules.end()) return it->second.module ? &it->second : nullptr;
        ShaderEntry& e = psModules[key];
        e.result = dxbc2spv::translate(*ps.program, vs.program);
        if (!e.result.ok()) {
            LOGW("PS %p no traducible: %s", ps.key, e.result.error.c_str());
            return nullptr;
        }
        e.module = makeModule(e.result.spirv);
        return e.module ? &e : nullptr;
    }
    VkShaderModule makeModule(const std::vector<uint32_t>& code) {
        VkShaderModuleCreateInfo si{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        si.codeSize = code.size() * 4;
        si.pCode = code.data();
        VkShaderModule m = VK_NULL_HANDLE;
        return f.vkCreateShaderModule(ctx.device, &si, nullptr, &m) == VK_SUCCESS ? m : VK_NULL_HANDLE;
    }

    PairLayout* pairLayout(const StageBindings& vs, const StageBindings& ps, const ShaderEntry& v, const ShaderEntry& p) {
        const auto key = std::make_pair(vs.key, ps.key);
        auto it = layouts.find(key);
        if (it != layouts.end()) return &it->second;
        PairLayout pl;
        auto add = [&](const dxbc2spv::Result& r, VkShaderStageFlags stage) {
            for (uint32_t s : r.cbSlots) pl.bindings.push_back({r.stageBinding + s, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, stage, nullptr});
            for (uint32_t s : r.texSlots) {
                pl.bindings.push_back({r.stageBinding + dxbc2spv::kBindingTex + s, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, 1, stage, nullptr});
            }
            for (uint32_t s : r.samplerSlots) {
                pl.bindings.push_back({r.stageBinding + dxbc2spv::kBindingSampler + s, VK_DESCRIPTOR_TYPE_SAMPLER, 1, stage, nullptr});
            }
        };
        add(v.result, VK_SHADER_STAGE_VERTEX_BIT);
        add(p.result, VK_SHADER_STAGE_FRAGMENT_BIT);
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = static_cast<uint32_t>(pl.bindings.size());
        li.pBindings = pl.bindings.data();
        if (f.vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &pl.setLayout) != VK_SUCCESS) return nullptr;
        VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pi.setLayoutCount = 1;
        pi.pSetLayouts = &pl.setLayout;
        if (f.vkCreatePipelineLayout(ctx.device, &pi, nullptr, &pl.layout) != VK_SUCCESS) return nullptr;
        return &(layouts[key] = pl);
    }

    VkPipeline pipeline(const DrawCall& dc, const ShaderEntry& v, const ShaderEntry& p, const PairLayout& pl,
                        VkFormat rtFormat) {
        // Clave: shaders, layout de entrada, strides por slot, topología, blend, rasterizador, formato.
        std::string key;
        key.reserve(160);
        auto put = [&](const void* data, size_t n) { key.append(static_cast<const char*>(data), n); };
        put(&dc.vs.key, 8);
        put(&dc.ps.key, 8);
        put(&dc.layoutKey, 8);
        for (const auto& e : dc.elements) {
            const uint32_t s = dc.vb[e.slot & 15].stride;
            put(&s, 4);
        }
        put(&dc.topology, 4);
        const uint8_t be = dc.blendEnable ? 1 : 0;
        put(&be, 1);
        put(dc.blend, sizeof(dc.blend));
        put(&dc.writeMask, 1);
        put(&dc.cullMode, 4);
        const uint8_t ccw = dc.frontCounterClockwise ? 1 : 0;
        put(&ccw, 1);
        put(&rtFormat, 4);
        auto it = pipelines.find(key);
        if (it != pipelines.end()) return it->second;

        // Entrada de vértices: una binding por slot usado; las entradas del VS sin elemento leen 0.
        std::vector<VkVertexInputBindingDescription> bindings;
        std::vector<VkVertexInputAttributeDescription> attrs;
        uint32_t usedSlots = 0;
        for (const auto& in : dc.vs.program->inputs) {
            const auto* sig = [&]() -> const dxbc::SignatureElement* {
                for (const auto& s : dc.vs.program->inputSig) if (s.reg == in.reg) return &s;
                return nullptr;
            }();
            if (sig != nullptr && (sig->systemValue == 6 || sig->systemValue == 8)) continue;  // VertexID/InstanceID
            if (in.systemValue == 6 || in.systemValue == 8) continue;
            const VertexElement* el = nullptr;
            for (const auto& e : dc.elements) if (e.location == in.reg) el = &e;
            VkVertexInputAttributeDescription a{};
            a.location = in.reg;
            if (el != nullptr && vertexFormat(el->format) != VK_FORMAT_UNDEFINED) {
                a.binding = el->slot & 15u;
                a.format = vertexFormat(el->format);
                a.offset = el->offset;
                if (!(usedSlots & (1u << a.binding))) {
                    usedSlots |= 1u << a.binding;
                    bindings.push_back({a.binding, dc.vb[a.binding].stride,
                                        el->perInstance ? VK_VERTEX_INPUT_RATE_INSTANCE : VK_VERTEX_INPUT_RATE_VERTEX});
                }
            } else {
                a.binding = 16;  // buffer de ceros
                a.format = VK_FORMAT_R32G32B32A32_SFLOAT;
                a.offset = 0;
                if (!(usedSlots & (1u << 16))) {
                    usedSlots |= 1u << 16;
                    bindings.push_back({16, 0, VK_VERTEX_INPUT_RATE_VERTEX});
                }
            }
            attrs.push_back(a);
        }
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        vi.vertexBindingDescriptionCount = static_cast<uint32_t>(bindings.size());
        vi.pVertexBindingDescriptions = bindings.data();
        vi.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
        vi.pVertexAttributeDescriptions = attrs.data();

        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        switch (dc.topology) {
            case 1: ia.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST; break;
            case 2: ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_LIST; break;
            case 3: ia.topology = VK_PRIMITIVE_TOPOLOGY_LINE_STRIP; break;
            case 5: ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP; break;
            default: ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST; break;
        }
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = 1;
        vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = dc.cullMode == 2 ? VK_CULL_MODE_FRONT_BIT : (dc.cullMode == 3 ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_NONE);
        // Con viewport de altura negativa la orientación en pantalla coincide con la de D3D.
        rs.frontFace = dc.frontCounterClockwise ? VK_FRONT_FACE_COUNTER_CLOCKWISE : VK_FRONT_FACE_CLOCKWISE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.blendEnable = dc.blendEnable ? VK_TRUE : VK_FALSE;
        ba.srcColorBlendFactor = blendFactor(dc.blend[0]);
        ba.dstColorBlendFactor = blendFactor(dc.blend[1]);
        ba.colorBlendOp = blendOp(dc.blend[2]);
        ba.srcAlphaBlendFactor = alphaFactor(dc.blend[3]);
        ba.dstAlphaBlendFactor = alphaFactor(dc.blend[4]);
        ba.alphaBlendOp = blendOp(dc.blend[5]);
        ba.colorWriteMask = dc.writeMask & 0xFu;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &ba;
        const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_BLEND_CONSTANTS};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 3;
        ds.pDynamicStates = dyn;
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = v.module;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = p.module;
        stages[1].pName = "main";
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2;
        gi.pStages = stages;
        gi.pVertexInputState = &vi;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vp;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pColorBlendState = &cb;
        gi.pDynamicState = &ds;
        gi.layout = pl.layout;
        gi.renderPass = renderPass(rtFormat);
        VkPipeline pipe = VK_NULL_HANDLE;
        if (gi.renderPass == VK_NULL_HANDLE ||
            f.vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &pipe) != VK_SUCCESS) {
            LOGW("vkCreateGraphicsPipelines falló (VS %p PS %p)", dc.vs.key, dc.ps.key);
            pipe = VK_NULL_HANDLE;
        } else {
            ++stats.pipelines;
        }
        pipelines[key] = pipe;
        return pipe;
    }

    VkSampler sampler(const uint8_t* desc) {
        if (desc == nullptr) return defaultSampler;
        const std::string key(reinterpret_cast<const char*>(desc), 52);
        auto it = samplers.find(key);
        if (it != samplers.end()) return it->second;
        uint32_t filter, au, av, aw, maxAniso, cmp;
        float bias, border[4], minLod, maxLod;
        std::memcpy(&filter, desc, 4);
        std::memcpy(&au, desc + 4, 4);
        std::memcpy(&av, desc + 8, 4);
        std::memcpy(&aw, desc + 12, 4);
        std::memcpy(&bias, desc + 16, 4);
        std::memcpy(&maxAniso, desc + 20, 4);
        std::memcpy(&cmp, desc + 24, 4);
        std::memcpy(border, desc + 28, 16);
        std::memcpy(&minLod, desc + 44, 4);
        std::memcpy(&maxLod, desc + 48, 4);
        VkSamplerCreateInfo si{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        const bool aniso = (filter & 0x40u) != 0;
        si.minFilter = (aniso || (filter & 0x10u)) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.magFilter = (aniso || (filter & 0x04u)) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        si.mipmapMode = (aniso || (filter & 0x01u)) ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
        si.addressModeU = addressMode(au);
        si.addressModeV = addressMode(av);
        si.addressModeW = addressMode(aw);
        si.mipLodBias = bias;
        si.anisotropyEnable = (aniso && ctx.samplerAnisotropy) ? VK_TRUE : VK_FALSE;
        si.maxAnisotropy = std::clamp(static_cast<float>(maxAniso), 1.0f, 16.0f);
        si.compareEnable = (filter & 0x80u) ? VK_TRUE : VK_FALSE;
        si.compareOp = static_cast<VkCompareOp>(std::clamp<int>(static_cast<int>(cmp) - 1, 0, 7));
        si.minLod = std::isfinite(minLod) ? std::max(0.0f, minLod) : 0.0f;
        si.maxLod = (!std::isfinite(maxLod) || maxLod > 1000.0f) ? VK_LOD_CLAMP_NONE : std::max(si.minLod, maxLod);
        const bool white = border[0] > 0.5f && border[3] > 0.5f;
        si.borderColor = white ? VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE
                               : (border[3] > 0.5f ? VK_BORDER_COLOR_FLOAT_OPAQUE_BLACK : VK_BORDER_COLOR_FLOAT_TRANSPARENT_BLACK);
        VkSampler s = VK_NULL_HANDLE;
        if (f.vkCreateSampler(ctx.device, &si, nullptr, &s) != VK_SUCCESS) s = defaultSampler;
        samplers[key] = s;
        return s;
    }

    // Copia (con caché por frame) de un buffer al anillo.
    RingAlloc stageBuffer(const BufferBinding& b, size_t from, size_t bytes, VkDeviceSize align) {
        uint64_t key = mix(mix(reinterpret_cast<uint64_t>(b.key), b.generation), from * 131 + bytes);
        Frame& fr = frames[slot];
        auto it = fr.bufferCache.find(key);
        if (it != fr.bufferCache.end()) return it->second;
        RingAlloc a = ringAlloc(std::max<size_t>(bytes, 16), align);
        if (a.ptr == nullptr) return a;
        const size_t avail = (b.data != nullptr && from < b.size) ? std::min(bytes, b.size - from) : 0;
        if (avail) std::memcpy(a.ptr, b.data + from, avail);
        if (avail < bytes) std::memset(a.ptr + avail, 0, bytes - avail);
        fr.bufferCache[key] = a;
        return a;
    }

    // Vista de una textura enlazada en el tipo que declara el shader para esa ranura.
    VkImageView srvView(const TextureBinding& t, uint32_t resDim, GpuImage* rtImage) {
        VkImageViewType type = VK_IMAGE_VIEW_TYPE_2D;
        GpuImage* dummy = &dummy2D;
        switch (resDim) {
            case 2: type = VK_IMAGE_VIEW_TYPE_1D; dummy = &dummy1D; break;
            case 5: type = VK_IMAGE_VIEW_TYPE_3D; dummy = &dummy3D; break;
            case 6: type = VK_IMAGE_VIEW_TYPE_CUBE; break;
            case 7: type = VK_IMAGE_VIEW_TYPE_1D_ARRAY; dummy = &dummy1D; break;
            case 8: type = VK_IMAGE_VIEW_TYPE_2D_ARRAY; break;
            case 10: type = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY; break;
            default: break;
        }
        GpuImage* g = image(t, false);
        const bool want1D = type == VK_IMAGE_VIEW_TYPE_1D || type == VK_IMAGE_VIEW_TYPE_1D_ARRAY;
        if (g == nullptr || g == rtImage || (g->is3D != (type == VK_IMAGE_VIEW_TYPE_3D)) || g->is1D != want1D) g = nullptr;
        if (g == nullptr) {
            return view(*dummy, type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY ? VK_IMAGE_VIEW_TYPE_CUBE : type, dummy->format,
                        0, 1, 0, dummy->layers);
        }
        barrier(*g, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        VkFormat fmt = g->format;
        if (t.viewFormat != 0 && !g->decompressed) {
            const VkFmt vf = textureFormat(t.viewFormat);
            if (vf.format != VK_FORMAT_UNDEFINED && vf.block == g->fmt.block) fmt = vf.format;
        }
        return view(*g, type, fmt, t.mostDetailedMip, t.mipLevels, t.firstSlice, t.arraySize);
    }

    // ----------------------------------------------------------------------- draw
    bool draw(const DrawCall& dc) {
        if (dc.vs.program == nullptr || dc.ps.program == nullptr || !dc.rt.valid()) return false;
        if (!beginFrame()) return false;
        ShaderEntry* v = vsModule(dc.vs);
        ShaderEntry* p = v ? psModule(dc.ps, dc.vs) : nullptr;
        if (v == nullptr || p == nullptr) return false;
        PairLayout* pl = pairLayout(dc.vs, dc.ps, *v, *p);
        if (pl == nullptr) return false;

        GpuImage* rt = image(dc.rt, true, /*keepPendingClear=*/true);
        if (rt == nullptr || !rt->attachable) return false;
        VkFormat rtFormat = rt->format;
        if (dc.rt.viewFormat != 0) {
            const VkFmt vf = textureFormat(dc.rt.viewFormat);
            if (vf.format != VK_FORMAT_UNDEFINED && !vf.block) rtFormat = vf.format;
        }
        const uint32_t mip = std::min(dc.rt.mostDetailedMip, rt->mips - 1);
        const uint32_t rtW = std::max(1u, rt->width >> mip), rtH = std::max(1u, rt->height >> mip);
        VkImageView rtView = view(*rt, VK_IMAGE_VIEW_TYPE_2D, rtFormat, mip, 1, dc.rt.firstSlice, 1);
        if (rtView == VK_NULL_HANDLE) return false;
        VkPipeline pipe = pipeline(dc, *v, *p, *pl, rtFormat);
        if (pipe == VK_NULL_HANDLE) return false;

        // Descriptores (texturas antes de abrir el render pass: pueden necesitar barreras).
        std::vector<VkWriteDescriptorSet> writes;
        std::vector<VkDescriptorBufferInfo> bufInfos;
        std::vector<VkDescriptorImageInfo> imgInfos;
        bufInfos.reserve(pl->bindings.size());
        imgInfos.reserve(pl->bindings.size());
        for (const auto& b : pl->bindings) {
            const bool isPs = b.binding >= dxbc2spv::kBindingBasePs;
            const StageBindings& st = isPs ? dc.ps : dc.vs;
            const uint32_t local = b.binding - (isPs ? dxbc2spv::kBindingBasePs : dxbc2spv::kBindingBaseVs);
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) {
                uint32_t declared = 1;
                for (const auto& c : st.program->constantBuffers) if (c.slot == local) declared = std::max(1u, c.size);
                const size_t bytes = static_cast<size_t>(declared) * 16;
                RingAlloc a = stageBuffer(st.cb[local], 0, bytes, uboAlign);
                if (a.ptr == nullptr) return false;
                bufInfos.push_back({a.buffer, a.offset, bytes});
            } else if (b.descriptorType == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE) {
                const uint32_t slotIdx = local - dxbc2spv::kBindingTex;
                uint32_t resDim = 3;
                for (const auto& r : st.program->resources) if (r.slot == slotIdx) resDim = r.dim;
                imgInfos.push_back({VK_NULL_HANDLE, srvView(st.srv[slotIdx], resDim, rt), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL});
            } else {
                const uint32_t slotIdx = local - dxbc2spv::kBindingSampler;
                imgInfos.push_back({sampler(st.sampler[slotIdx & 15u]), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED});
            }
        }
        // La imagen por defecto 2D necesita estar en SHADER_READ_ONLY (lo está tras clearDummies).
        VkDescriptorSet set = allocSet(pl->setLayout);
        if (set == VK_NULL_HANDLE) return false;
        size_t bi = 0, ii = 0;
        for (const auto& b : pl->bindings) {
            VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
            w.dstSet = set;
            w.dstBinding = b.binding;
            w.descriptorCount = 1;
            w.descriptorType = b.descriptorType;
            if (b.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER) w.pBufferInfo = &bufInfos[bi++];
            else w.pImageInfo = &imgInfos[ii++];
            writes.push_back(w);
        }
        f.vkUpdateDescriptorSets(ctx.device, static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);

        // Buffers de vértices e índices al anillo.
        VkBuffer vbufs[17] = {};
        VkDeviceSize voffs[17] = {};
        uint32_t usedMask = 0;
        for (const auto& in : dc.vs.program->inputs) {
            const VertexElement* el = nullptr;
            for (const auto& e : dc.elements) if (e.location == in.reg) el = &e;
            if (el != nullptr && vertexFormat(el->format) != VK_FORMAT_UNDEFINED) usedMask |= 1u << (el->slot & 15u);
            else usedMask |= 1u << 16;
        }
        // Solo el rango que usa el dibujo: Unity escribe en buffers dinámicos de varios MB y
        // copiar "desde el offset hasta el final" en cada dibujo era ~1/3 del hilo gráfico.
        RingAlloc ia{};
        uint32_t minIdx = 0, maxIdx = 0;
        if (dc.indexed) {
            const size_t isz = dc.ibFormat == 42 ? 4 : 2;
            const size_t from = static_cast<size_t>(dc.ib.offset) + static_cast<size_t>(dc.start) * isz;
            const size_t bytes = std::max<size_t>(static_cast<size_t>(dc.count) * isz, 16);
            ia = stageBuffer(dc.ib, from, bytes, 16);
            if (ia.ptr == nullptr) return false;
            // Rango de vértices referenciado (sobre la copia: ya está acotada y rellena).
            if (dc.count > 0) {
                minIdx = UINT32_MAX;
                for (uint32_t i = 0; i < dc.count; ++i) {
                    uint32_t v;
                    if (isz == 4) std::memcpy(&v, ia.ptr + i * 4, 4);
                    else { uint16_t v16; std::memcpy(&v16, ia.ptr + i * 2, 2); v = v16; }
                    minIdx = std::min(minIdx, v);
                    maxIdx = std::max(maxIdx, v);
                }
            }
        }
        // Primer vértice copiado (en vértices desde el offset del slot) y cuántos.
        const int64_t firstV = dc.indexed ? std::max<int64_t>(0, static_cast<int64_t>(dc.baseVertex) + minIdx) : 0;
        const int64_t endV = dc.indexed ? static_cast<int64_t>(dc.baseVertex) + maxIdx + 1
                                        : static_cast<int64_t>(dc.start) + dc.count;
        uint32_t instanceSlots = 0;
        for (const auto& e : dc.elements) if (e.perInstance) instanceSlots |= 1u << (e.slot & 15u);
        // Recorte de los slots por vértice: o todos empiezan en firstV o ninguno (vertexOffset es
        // común a todos los slots).
        bool trim = endV > firstV;
        for (uint32_t s = 0; s < 16 && trim; ++s) {
            if (!(usedMask & (1u << s)) || (instanceSlots & (1u << s))) continue;
            const BufferBinding& vb = dc.vb[s];
            if (vb.stride == 0 || vb.offset + static_cast<size_t>(firstV) * vb.stride >= vb.size) trim = false;
        }
        const bool shifted = trim && firstV > 0;
        for (uint32_t s = 0; s < 16; ++s) {
            if (!(usedMask & (1u << s))) continue;
            const BufferBinding& vb = dc.vb[s];
            size_t from = vb.offset;
            size_t bytes = vb.size > from ? vb.size - from : 16;
            if (trim && !(instanceSlots & (1u << s))) {
                from = vb.offset + static_cast<size_t>(firstV) * vb.stride;
                const size_t want = static_cast<size_t>(endV - firstV) * vb.stride;
                bytes = std::max<size_t>(std::min(want, vb.size - from), 16);  // stageBuffer rellena con ceros
            }
            RingAlloc a = stageBuffer(vb, from, bytes, 16);
            if (a.ptr == nullptr) return false;
            vbufs[s] = a.buffer;
            voffs[s] = a.offset;
        }
        if (usedMask & (1u << 16)) {
            vbufs[16] = zeroBuffer;
            voffs[16] = 0;
        }

        if (!beginPass(*rt, rtView, rtW, rtH, rtFormat)) return false;
        VkCommandBuffer cmd = frames[slot].cmd;
        f.vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
        f.vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pl->layout, 0, 1, &set, 0, nullptr);
        for (uint32_t s = 0; s < 17; ++s) {
            if (usedMask & (1u << s)) f.vkCmdBindVertexBuffers(cmd, s, 1, &vbufs[s], &voffs[s]);
        }
        // Viewport con altura negativa: misma orientación que D3D sin tocar los shaders.
        float vx = 0, vy = 0, vw = static_cast<float>(rtW), vh = static_cast<float>(rtH), z0 = 0, z1 = 1;
        if (dc.hasViewport && dc.viewport[2] > 0 && dc.viewport[3] > 0) {
            vx = dc.viewport[0];
            vy = dc.viewport[1];
            vw = dc.viewport[2];
            vh = dc.viewport[3];
            z0 = dc.viewport[4];
            z1 = dc.viewport[5];
        }
        VkViewport vpt{vx, vy + vh, vw, -vh, z0, z1};
        f.vkCmdSetViewport(cmd, 0, 1, &vpt);
        VkRect2D sc{{0, 0}, {rtW, rtH}};
        if (dc.scissorEnable) {
            const int32_t l = std::clamp(dc.scissor[0], 0, static_cast<int32_t>(rtW));
            const int32_t t = std::clamp(dc.scissor[1], 0, static_cast<int32_t>(rtH));
            const int32_t r = std::clamp(dc.scissor[2], l, static_cast<int32_t>(rtW));
            const int32_t b = std::clamp(dc.scissor[3], t, static_cast<int32_t>(rtH));
            sc = {{l, t}, {static_cast<uint32_t>(r - l), static_cast<uint32_t>(b - t)}};
        }
        f.vkCmdSetScissor(cmd, 0, 1, &sc);
        f.vkCmdSetBlendConstants(cmd, dc.blendFactor);
        if (dc.indexed) {
            f.vkCmdBindIndexBuffer(cmd, ia.buffer, ia.offset, dc.ibFormat == 42 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
            // Índices copiados desde dc.start (firstIndex 0); vértices desde firstV si se recortaron.
            const int32_t vertexOffset = shifted ? static_cast<int32_t>(dc.baseVertex - firstV) : dc.baseVertex;
            f.vkCmdDrawIndexed(cmd, dc.count, std::max(1u, dc.instanceCount), 0, vertexOffset, dc.startInstance);
        } else {
            f.vkCmdDraw(cmd, dc.count, std::max(1u, dc.instanceCount), dc.start, dc.startInstance);
        }
        rt->uploadedGen = dc.rt.generation;
        ++stats.draws;
        return true;
    }

    void clear(const TextureBinding& t, const float rgba[4]) {
        if (!beginFrame()) return;
        GpuImage* g = image(t, true, /*keepPendingClear=*/true);
        if (g == nullptr) return;
        VkClearColorValue c{};
        std::memcpy(c.float32, rgba, 16);
        const uint32_t mip = std::min(t.mostDetailedMip, g->mips - 1);
        if (!(passOpen && passImage == g) && mip == 0 && g->mips == 1 && g->layers == 1) {
            pendingClears[g] = c;  // se aplicará como LOAD_OP_CLEAR en el próximo pase
            g->gpuWritten = true;
            g->uploadedGen = t.generation;
            return;
        }
        pendingClears.erase(g);
        if (passOpen && passImage == g) {
            // Dentro del pase: vkCmdClearAttachments sobre toda la vista actual.
            VkClearAttachment a{VK_IMAGE_ASPECT_COLOR_BIT, 0, {}};
            a.clearValue.color = c;
            VkClearRect r{{{0, 0}, {std::max(1u, g->width >> mip), std::max(1u, g->height >> mip)}}, 0, 1};
            f.vkCmdClearAttachments(frames[slot].cmd, 1, &a, 1, &r);
        } else {
            barrier(*g, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
            VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, mip, 1, std::min(t.firstSlice, g->layers - 1), 1};
            f.vkCmdClearColorImage(frames[slot].cmd, g->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &c, 1, &r);
        }
        g->gpuWritten = true;
        g->uploadedGen = t.generation;
    }

    // Las copias entre texturas se hacen en la GPU salvo que el destino sea STAGING (lectura por
    // CPU) o alguna no sea una textura: entonces el llamador copia en CPU.
    static bool gpuCopyable(const TextureBinding& t) {
        if (!t.valid()) return false;
        const d3d11::ResourceDesc& d = t.res->desc();
        return d.dim != d3d11::Dimension::Buffer && d.dim != d3d11::Dimension::Unknown && d.usage != 3;
    }

    bool copyRegion(const TextureBinding& dst, uint32_t dstSub, uint32_t x, uint32_t y, uint32_t z,
                    const TextureBinding& src, uint32_t srcSub, const uint32_t* box) {
        if (!gpuCopyable(dst) || !gpuCopyable(src)) return false;
        if (!beginFrame()) return false;
        GpuImage* s = image(src, false);
        GpuImage* d = image(dst, false);
        if (s == nullptr || d == nullptr) return false;
        const uint32_t sm = srcSub % s->mips, sl = srcSub / s->mips;
        const uint32_t dm = dstSub % d->mips, dl = dstSub / d->mips;
        if (sl >= s->layers || dl >= d->layers) return false;
        const uint32_t sw = std::max(1u, s->width >> sm), sh = std::max(1u, s->height >> sm), sd = std::max(1u, s->depth >> sm);
        uint32_t l = 0, t = 0, fr = 0, r = sw, b = sh, bk = sd;
        if (box != nullptr) {
            l = box[0]; t = box[1]; fr = box[2]; r = std::min(box[3], sw); b = std::min(box[4], sh); bk = std::min(box[5], sd);
        }
        if (r <= l || b <= t || bk <= fr) return true;  // caja vacía: nada que copiar
        const uint32_t dw = std::max(1u, d->width >> dm), dh = std::max(1u, d->height >> dm);
        const uint32_t w = std::min(r - l, dw > x ? dw - x : 0), h = std::min(b - t, dh > y ? dh - y : 0);
        if (w == 0 || h == 0) return true;
        if (s == d && sm == dm && sl == dl) return false;  // misma subresource: no soportado en GPU
        barrier(*s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        if (d != s) barrier(*d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageCopy rg{};
        rg.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, sm, sl, 1};
        rg.srcOffset = {static_cast<int32_t>(l), static_cast<int32_t>(t), static_cast<int32_t>(fr)};
        rg.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, dm, dl, 1};
        rg.dstOffset = {static_cast<int32_t>(x), static_cast<int32_t>(y), static_cast<int32_t>(z)};
        rg.extent = {w, h, bk - fr};
        const VkImageLayout dstLayout = d == s ? VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL : VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        if (d == s) {
            // Copia dentro de la misma imagen (otra subresource): layout GENERAL.
            barrier(*s, VK_IMAGE_LAYOUT_GENERAL);
            f.vkCmdCopyImage(frames[slot].cmd, s->image, VK_IMAGE_LAYOUT_GENERAL, d->image, VK_IMAGE_LAYOUT_GENERAL, 1, &rg);
        } else {
            f.vkCmdCopyImage(frames[slot].cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image, dstLayout, 1, &rg);
        }
        d->gpuWritten = true;
        d->uploadedGen = dst.generation;
        return true;
    }

    bool copy(const TextureBinding& dst, const TextureBinding& src) {
        if (!gpuCopyable(dst) || !gpuCopyable(src)) return false;
        if (!beginFrame()) return false;
        GpuImage* s = image(src, false);
        GpuImage* d = image(dst, false);
        if (s == nullptr || d == nullptr || s == d || s->width != d->width || s->height != d->height) return false;
        barrier(*s, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        barrier(*d, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        std::vector<VkImageCopy> regions;
        const uint32_t mips = std::min(s->mips, d->mips), layers = std::min(s->layers, d->layers);
        for (uint32_t m = 0; m < mips; ++m) {
            VkImageCopy r{};
            r.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, m, 0, layers};
            r.dstSubresource = r.srcSubresource;
            r.extent = {std::max(1u, s->width >> m), std::max(1u, s->height >> m), std::max(1u, s->depth >> m)};
            regions.push_back(r);
        }
        f.vkCmdCopyImage(frames[slot].cmd, s->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, d->image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, static_cast<uint32_t>(regions.size()), regions.data());
        d->gpuWritten = true;
        d->uploadedGen = dst.generation;
        return true;
    }

    // ----------------------------------------------------------------------- presentación
    bool ensurePresentPipeline(VkFormat swapFormat) {
        if (presentPipeline != VK_NULL_HANDLE && presentFormat == swapFormat) return true;
        presentFormat = swapFormat;
        // Otro formato de swapchain = otro render pass: SGSR se recrea con él (como el pase normal,
        // el anterior no se destruye; el formato no cambia en una sesión).
        sgsrPipeline = VK_NULL_HANDLE;
        sgsrLayout = VK_NULL_HANDLE;
        VkAttachmentDescription a{};
        a.format = swapFormat;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        a.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        VkAttachmentReference ref{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        VkSubpassDescription sp{};
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
        sp.colorAttachmentCount = 1;
        sp.pColorAttachments = &ref;
        VkSubpassDependency dep{};
        dep.srcSubpass = VK_SUBPASS_EXTERNAL;
        dep.dstSubpass = 0;
        dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
        VkRenderPassCreateInfo ri{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
        ri.attachmentCount = 1;
        ri.pAttachments = &a;
        ri.subpassCount = 1;
        ri.pSubpasses = &sp;
        ri.dependencyCount = 1;
        ri.pDependencies = &dep;
        if (f.vkCreateRenderPass(ctx.device, &ri, nullptr, &presentPass) != VK_SUCCESS) return false;
        VkDescriptorSetLayoutBinding b{0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo li{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        li.bindingCount = 1;
        li.pBindings = &b;
        if (f.vkCreateDescriptorSetLayout(ctx.device, &li, nullptr, &presentSetLayout) != VK_SUCCESS) return false;
        VkPushConstantRange pc{VK_SHADER_STAGE_VERTEX_BIT, 0, 32};
        VkPipelineLayoutCreateInfo pi{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
        pi.setLayoutCount = 1;
        pi.pSetLayouts = &presentSetLayout;
        pi.pushConstantRangeCount = 1;
        pi.pPushConstantRanges = &pc;
        if (f.vkCreatePipelineLayout(ctx.device, &pi, nullptr, &presentLayout) != VK_SUCCESS) return false;
        const std::vector<uint32_t> vsCode(std::begin(kPresentVertSpv), std::end(kPresentVertSpv));
        const std::vector<uint32_t> fsCode(std::begin(kPresentFragSpv), std::end(kPresentFragSpv));
        VkShaderModule vsm = makeModule(vsCode), fsm = makeModule(fsCode);
        if (!vsm || !fsm) return false;
        VkPipelineShaderStageCreateInfo stages[2]{};
        stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vsm;
        stages[0].pName = "main";
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fsm;
        stages[1].pName = "main";
        VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
        VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
        ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo vp{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
        vp.viewportCount = vp.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
        rs.polygonMode = VK_POLYGON_MODE_FILL;
        rs.cullMode = VK_CULL_MODE_NONE;
        rs.lineWidth = 1.0f;
        VkPipelineMultisampleStateCreateInfo ms{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
        ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineColorBlendAttachmentState ba{};
        ba.colorWriteMask = 0xF;
        VkPipelineColorBlendStateCreateInfo cb{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
        cb.attachmentCount = 1;
        cb.pAttachments = &ba;
        const VkDynamicState dyn[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo ds{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
        ds.dynamicStateCount = 2;
        ds.pDynamicStates = dyn;
        VkGraphicsPipelineCreateInfo gi{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
        gi.stageCount = 2;
        gi.pStages = stages;
        gi.pVertexInputState = &vi;
        gi.pInputAssemblyState = &ia;
        gi.pViewportState = &vp;
        gi.pRasterizationState = &rs;
        gi.pMultisampleState = &ms;
        gi.pColorBlendState = &cb;
        gi.pDynamicState = &ds;
        gi.layout = presentLayout;
        gi.renderPass = presentPass;
        const bool okp = f.vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &presentPipeline) == VK_SUCCESS;
        // SGSR v1: se crea aquí, una vez, junto al pase normal (nunca compilaciones en partida). Si
        // falla, se presenta con el pase bilineal de siempre.
        if (okp && sgsrPipeline == VK_NULL_HANDLE) {
            const VkPushConstantRange ranges[2] = {{VK_SHADER_STAGE_VERTEX_BIT, 0, 32},
                                                   {VK_SHADER_STAGE_FRAGMENT_BIT, 32, 32}};
            VkPipelineLayoutCreateInfo si{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            si.setLayoutCount = 1;
            si.pSetLayouts = &presentSetLayout;
            si.pushConstantRangeCount = 2;
            si.pPushConstantRanges = ranges;
            const std::vector<uint32_t> sgCode(std::begin(kPresentSgsrFragSpv), std::end(kPresentSgsrFragSpv));
            VkShaderModule sgm = makeModule(sgCode);
            if (sgm != VK_NULL_HANDLE && f.vkCreatePipelineLayout(ctx.device, &si, nullptr, &sgsrLayout) == VK_SUCCESS) {
                stages[1].module = sgm;
                gi.layout = sgsrLayout;
                if (f.vkCreateGraphicsPipelines(ctx.device, VK_NULL_HANDLE, 1, &gi, nullptr, &sgsrPipeline) != VK_SUCCESS) {
                    sgsrPipeline = VK_NULL_HANDLE;
                }
            }
            if (sgm != VK_NULL_HANDLE) f.vkDestroyShaderModule(ctx.device, sgm, nullptr);
            LOGI("Pase SGSR v1: %s", sgsrPipeline != VK_NULL_HANDLE ? "listo" : "no disponible (bilineal)");
        }
        f.vkDestroyShaderModule(ctx.device, vsm, nullptr);
        f.vkDestroyShaderModule(ctx.device, fsm, nullptr);
        if (!okp) presentPipeline = VK_NULL_HANDLE;
        return okp;
    }

    bool present(const TextureBinding& bb) {
        if (!beginFrame()) return false;
        GpuImage* g = image(bb, true);
        Frame& fr = frames[slot];
        bool presented = false;
        endPass();
        if (g != nullptr) {
            barrier(*g, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
            // HyperFG (apagado por defecto): si genera, se presenta ahora el intermedio y el frame
            // real se programa a medio intervalo en su hilo. Apagado no añade nada a este pase.
            VkImageView fgView = VK_NULL_HANDLE;
            int gameFps = 0;
            bool repeat = false;
            if (HyperFrameGen::enabled()) {
                gameFps = pacing::effectiveCap();
                fgView = HyperFrameGen::shared().record(fr.cmd, g->image, g->format, g->width, g->height, gameFps);
            } else if (const int cap = pacing::effectiveCap();
                       !pacing::thermalRelaxed() &&
                       HyperFrameGen::repeatWanted(cap, static_cast<int>(::hyperplay::win32::refreshHz()))) {
                // Repetición a 120 presentaciones/s (ver HyperFrameGen.h). Con calor se apaga y el
                // sistema vuelve a ahorrar.
                gameFps = cap;
                repeat = HyperFrameGen::shared().recordRepeat(fr.cmd, g->image, g->format, g->width, g->height, cap);
            } else {
                HyperFrameGen::shared().releaseIfIdle();
            }
            DirectVulkanBridge::SwapTarget target;
            if (DirectVulkanBridge::shared().beginPresent(target)) {
                bool recorded = false;
                if (ensurePresentPipeline(target.format)) {
                    if (target.generation != presentGeneration) {
                        for (auto& [v, fb] : presentFbs) f.vkDestroyFramebuffer(ctx.device, fb, nullptr);
                        presentFbs.clear();
                        presentGeneration = target.generation;
                    }
                    VkFramebuffer& fb = presentFbs[target.view];
                    if (fb == VK_NULL_HANDLE) {
                        VkFramebufferCreateInfo fi{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
                        fi.renderPass = presentPass;
                        fi.attachmentCount = 1;
                        fi.pAttachments = &target.view;
                        fi.width = target.extent.width;
                        fi.height = target.extent.height;
                        fi.layers = 1;
                        f.vkCreateFramebuffer(ctx.device, &fi, nullptr, &fb);
                    }
                    VkDescriptorSet set = allocSet(presentSetLayout);
                    VkImageView srcView = fgView != VK_NULL_HANDLE ? fgView : view(*g, VK_IMAGE_VIEW_TYPE_2D, g->format, 0, 1, 0, 1);
                    if (fb != VK_NULL_HANDLE && set != VK_NULL_HANDLE && srcView != VK_NULL_HANDLE) {
                        VkDescriptorImageInfo info{presentSampler, srcView, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
                        VkWriteDescriptorSet w{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
                        w.dstSet = set;
                        w.dstBinding = 0;
                        w.descriptorCount = 1;
                        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
                        w.pImageInfo = &info;
                        f.vkUpdateDescriptorSets(ctx.device, 1, &w, 0, nullptr);
                        VkRenderPassBeginInfo bi{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
                        bi.renderPass = presentPass;
                        bi.framebuffer = fb;
                        bi.renderArea = {{0, 0}, target.extent};
                        f.vkCmdBeginRenderPass(fr.cmd, &bi, VK_SUBPASS_CONTENTS_INLINE);
                        // SGSR v1 si el modo lo pide (Auto: solo por debajo de la nativa); si no, el
                        // pase bilineal de siempre, sin coste extra.
                        const bool sgsr = sgsrPipeline != VK_NULL_HANDLE &&
                                          presentopt::useSgsr(g->width, g->height, target.extent.width, target.extent.height);
                        const VkPipelineLayout layout = sgsr ? sgsrLayout : presentLayout;
                        f.vkCmdBindPipeline(fr.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, sgsr ? sgsrPipeline : presentPipeline);
                        f.vkCmdBindDescriptorSets(fr.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, layout, 0, 1, &set, 0, nullptr);
                        if (sgsr) {
                            const float w = static_cast<float>(g->width), h = static_cast<float>(g->height);
                            const float fp[8] = {1.0f / w, 1.0f / h, w, h,
                                                 presentopt::g_sharpness.load(std::memory_order_relaxed), 0, 0, 0};
                            f.vkCmdPushConstants(fr.cmd, layout, VK_SHADER_STAGE_FRAGMENT_BIT, 32, 32, fp);
                        }
                        VkViewport vp{0, 0, static_cast<float>(target.extent.width), static_cast<float>(target.extent.height), 0, 1};
                        VkRect2D sc{{0, 0}, target.extent};
                        f.vkCmdSetViewport(fr.cmd, 0, 1, &vp);
                        f.vkCmdSetScissor(fr.cmd, 0, 1, &sc);
                        // uv a partir de la posición en el buffer del swapchain (orientación nativa).
                        float pc[8] = {1, 0, 0, 1, 0, 0, 0, 0};
                        switch (target.transform) {
                            case VK_SURFACE_TRANSFORM_ROTATE_90_BIT_KHR: { const float m[8] = {0, 1, -1, 0, 0, 1, 0, 0}; std::memcpy(pc, m, 32); break; }
                            case VK_SURFACE_TRANSFORM_ROTATE_180_BIT_KHR: { const float m[8] = {-1, 0, 0, -1, 1, 1, 0, 0}; std::memcpy(pc, m, 32); break; }
                            case VK_SURFACE_TRANSFORM_ROTATE_270_BIT_KHR: { const float m[8] = {0, -1, 1, 0, 1, 0, 0, 0}; std::memcpy(pc, m, 32); break; }
                            default: break;
                        }
                        presentopt::applyFit(pc, g->width, g->height, target.extent.width, target.extent.height);
                        f.vkCmdPushConstants(fr.cmd, layout, VK_SHADER_STAGE_VERTEX_BIT, 0, 32, pc);
                        f.vkCmdDraw(fr.cmd, 3, 1, 0, 0);
                        f.vkCmdEndRenderPass(fr.cmd);
                        recorded = true;
                    }
                }
                if (!recorded) {
                    // Sin pase: dejar la imagen en PRESENT_SRC igualmente.
                    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
                    b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
                    b.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
                    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
                    b.image = target.image;
                    b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
                    f.vkCmdPipelineBarrier(fr.cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0,
                                           0, nullptr, 0, nullptr, 1, &b);
                }
                f.vkEndCommandBuffer(fr.cmd);
                presented = DirectVulkanBridge::shared().endPresent(target, fr.cmd, fr.fence);
                recording = false;
                if ((fgView != VK_NULL_HANDLE || repeat) && presented && gameFps > 0) {
                    timespec t{};
                    clock_gettime(CLOCK_MONOTONIC, &t);
                    const int64_t now = static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
                    HyperFrameGen::shared().scheduleReal(now + 500000000LL / gameFps);  // + medio intervalo
                }
            }
        }
        if (recording) {  // no se pudo presentar: enviar igualmente para no bloquear recursos
            f.vkEndCommandBuffer(fr.cmd);
            DirectVulkanBridge::shared().submit(fr.cmd, fr.fence);
            recording = false;
        }
        slot = (slot + 1) % frames.size();
        ++stats.presents;
        return presented;
    }

    void release(const void* key) {
        auto it = images.find(key);
        if (it == images.end()) return;
        // Puede estar en uso por la GPU: se retira al terminar los frames en vuelo.
        for (Frame& fr : frames) f.vkWaitForFences(ctx.device, 1, &fr.fence, VK_TRUE, 1000000000ull);
        if (recording) return;  // el frame actual podría referenciarla: se mantiene
        if (passImage == it->second.get()) endPass();
        if (it->second) destroyImage(*it->second);
        images.erase(it);
    }
};

Impl& impl() {
    static Impl* i = new Impl();
    return *i;
}

}  // namespace

Renderer& Renderer::shared() {
    static Renderer r;
    return r;
}

bool Renderer::available() {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    return i.init();
}

bool Renderer::draw(const DrawCall& dc) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    if (!i.init()) return false;
    const bool ok = i.draw(dc);
    if (!ok) ++i.stats.failedDraws;
    return ok;
}

void Renderer::clearRenderTarget(const TextureBinding& rt, const float rgba[4]) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    if (i.init()) i.clear(rt, rgba);
}

bool Renderer::copyTexture(const TextureBinding& dst, const TextureBinding& src) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    return i.init() && i.copy(dst, src);
}

bool Renderer::copyTextureRegion(const TextureBinding& dst, uint32_t dstSub, uint32_t x, uint32_t y, uint32_t z,
                                 const TextureBinding& src, uint32_t srcSub, const uint32_t* box) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    return i.init() && i.copyRegion(dst, dstSub, x, y, z, src, srcSub, box);
}

bool Renderer::isGpuWritten(const void* key) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    auto it = i.images.find(key);
    return it != i.images.end() && it->second && it->second->gpuWritten;
}

bool Renderer::present(const TextureBinding& backbuffer) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    return i.init() && i.present(backbuffer);
}

void Renderer::release(const void* key) {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    if (i.ok) i.release(key);
}

Renderer::Stats Renderer::stats() const {
    Impl& i = impl();
    std::lock_guard<std::mutex> lock(i.mtx);
    return i.stats;
}

}  // namespace hyperplay::gpu
