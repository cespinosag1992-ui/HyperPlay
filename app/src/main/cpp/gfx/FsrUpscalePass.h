#pragma once

#include <vulkan/vulkan.h>
#include <cstdint>

namespace hyperplay {

struct FsrPushConstants {
    uint32_t srcWidth;
    uint32_t srcHeight;
    uint32_t dstWidth;
    uint32_t dstHeight;
    float sharpness; // 0.0f a 1.0f (CAS contrast adaptive sharpening)
};

/// Pase de escalado FSR/CAS. Requiere un SPIR-V de compute validado; mientras no haya uno
/// empaquetado, el pase se reporta como no disponible en lugar de ejecutar un pipeline vacío.
class FsrUpscalePass {
public:
    FsrUpscalePass() = default;
    ~FsrUpscalePass();

    FsrUpscalePass(const FsrUpscalePass&) = delete;
    FsrUpscalePass& operator=(const FsrUpscalePass&) = delete;

    /// Intenta crear el pipeline de compute. Devuelve false si el shader no está disponible.
    bool init(VkDevice device, VkPhysicalDevice physDevice);
    void shutdown();

    void recordCommands(VkCommandBuffer cmd, VkImageView srcView, VkImageView dstView,
                        uint32_t srcWidth, uint32_t srcHeight,
                        uint32_t dstWidth, uint32_t dstHeight, float sharpness);

    [[nodiscard]] bool isSupported() const noexcept { return supported_; }
    [[nodiscard]] bool isEnabled() const noexcept { return enabled_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled && supported_; }

    [[nodiscard]] VkDescriptorSetLayout descriptorSetLayout() const noexcept { return descSetLayout_; }
    [[nodiscard]] VkPipelineLayout pipelineLayout() const noexcept { return pipelineLayout_; }
    [[nodiscard]] VkPipeline pipeline() const noexcept { return pipeline_; }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkShaderModule shaderModule_ = VK_NULL_HANDLE;
    bool supported_ = false;
    bool enabled_ = false;
};

}  // namespace hyperplay
