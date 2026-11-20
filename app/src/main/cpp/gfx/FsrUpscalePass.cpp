#include "FsrUpscalePass.h"
#include "FsrCasSpv.h"

#include <android/log.h>

#define LOG_TAG "HyperPlay.FSR"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {

FsrUpscalePass::~FsrUpscalePass() {
    shutdown();
}

bool FsrUpscalePass::init(VkDevice device, VkPhysicalDevice) {
    shutdown();
    device_ = device;
    if (device_ == VK_NULL_HANDLE) return false;

    // 1. Crear VkShaderModule con el SPIR-V validado de fsr_cas.comp
    VkShaderModuleCreateInfo smInfo{};
    smInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    smInfo.codeSize = kFsrCasSpvSizeBytes;
    smInfo.pCode = kFsrCasSpv;
    VkResult res = vkCreateShaderModule(device_, &smInfo, nullptr, &shaderModule_);
    if (res != VK_SUCCESS) {
        LOGW("FSR: Error creando VkShaderModule: %d", res);
        shutdown();
        return false;
    }

    // 2. Crear VkDescriptorSetLayout (binding 0: combined sampler, binding 1: storage image)
    VkDescriptorSetLayoutBinding bindings[2]{};
    bindings[0].binding = 0;
    bindings[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    bindings[0].descriptorCount = 1;
    bindings[0].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    bindings[1].binding = 1;
    bindings[1].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    bindings[1].descriptorCount = 1;
    bindings[1].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

    VkDescriptorSetLayoutCreateInfo layoutInfo{};
    layoutInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layoutInfo.bindingCount = 2;
    layoutInfo.pBindings = bindings;
    res = vkCreateDescriptorSetLayout(device_, &layoutInfo, nullptr, &descSetLayout_);
    if (res != VK_SUCCESS) {
        LOGW("FSR: Error creando VkDescriptorSetLayout: %d", res);
        shutdown();
        return false;
    }

    // 3. Crear VkPipelineLayout con push constants para FsrPushConstants
    VkPushConstantRange pcRange{};
    pcRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
    pcRange.offset = 0;
    pcRange.size = sizeof(FsrPushConstants);

    VkPipelineLayoutCreateInfo pipeLayoutInfo{};
    pipeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    pipeLayoutInfo.setLayoutCount = 1;
    pipeLayoutInfo.pSetLayouts = &descSetLayout_;
    pipeLayoutInfo.pushConstantRangeCount = 1;
    pipeLayoutInfo.pPushConstantRanges = &pcRange;
    res = vkCreatePipelineLayout(device_, &pipeLayoutInfo, nullptr, &pipelineLayout_);
    if (res != VK_SUCCESS) {
        LOGW("FSR: Error creando VkPipelineLayout: %d", res);
        shutdown();
        return false;
    }

    // 4. Crear VkComputePipeline
    VkComputePipelineCreateInfo compInfo{};
    compInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    compInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    compInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
    compInfo.stage.module = shaderModule_;
    compInfo.stage.pName = "main";
    compInfo.layout = pipelineLayout_;
    res = vkCreateComputePipelines(device_, VK_NULL_HANDLE, 1, &compInfo, nullptr, &pipeline_);
    if (res != VK_SUCCESS) {
        LOGW("FSR: Error creando VkComputePipeline: %d", res);
        shutdown();
        return false;
    }

    supported_ = true;
    enabled_ = true;
    LOGI("FSR/CAS Compute Pipeline inicializado exitosamente (SPIR-V %zu bytes, 120Hz ready)", kFsrCasSpvSizeBytes);
    return true;
}

void FsrUpscalePass::recordCommands(VkCommandBuffer cmd, VkImageView, VkImageView,
                                    uint32_t srcWidth, uint32_t srcHeight,
                                    uint32_t dstWidth, uint32_t dstHeight, float sharpness) {
    if (!supported_ || !enabled_ || pipeline_ == VK_NULL_HANDLE || cmd == VK_NULL_HANDLE) return;

    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline_);
    FsrPushConstants pc{
        .srcWidth = srcWidth,
        .srcHeight = srcHeight,
        .dstWidth = dstWidth,
        .dstHeight = dstHeight,
        .sharpness = sharpness,
    };
    vkCmdPushConstants(cmd, pipelineLayout_, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), &pc);

    // Despachar grupos de trabajo de 8x8 hilos
    const uint32_t groupX = (dstWidth + 7) / 8;
    const uint32_t groupY = (dstHeight + 7) / 8;
    vkCmdDispatch(cmd, groupX, groupY, 1);
}

void FsrUpscalePass::shutdown() {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        if (pipeline_ != VK_NULL_HANDLE) {
            vkDestroyPipeline(device_, pipeline_, nullptr);
            pipeline_ = VK_NULL_HANDLE;
        }
        if (pipelineLayout_ != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
            pipelineLayout_ = VK_NULL_HANDLE;
        }
        if (descSetLayout_ != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device_, descSetLayout_, nullptr);
            descSetLayout_ = VK_NULL_HANDLE;
        }
        if (shaderModule_ != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device_, shaderModule_, nullptr);
            shaderModule_ = VK_NULL_HANDLE;
        }
    }
    device_ = VK_NULL_HANDLE;
    supported_ = false;
    enabled_ = false;
}

}  // namespace hyperplay
