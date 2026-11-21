#include "GuestSwapchain.h"

#include "DirectVulkanBridge.h"
#include "HyperFrameGen.h"

#include <android/log.h>
#include <sys/system_properties.h>
#include <time.h>

#include <algorithm>
#include <cstring>
#include <vector>

#define LOG_TAG "HyperPlay.GuestSwap"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::gpu {
namespace {

#define GS_DEVICE_FNS(X)                                                                                              \
    X(vkCreateSwapchainKHR) X(vkDestroySwapchainKHR) X(vkGetSwapchainImagesKHR) X(vkAcquireNextImageKHR)            \
    X(vkQueuePresentKHR) X(vkQueueSubmit) X(vkCreateImage) X(vkDestroyImage) X(vkGetImageMemoryRequirements)        \
    X(vkAllocateMemory) X(vkFreeMemory) X(vkBindImageMemory) X(vkCreateCommandPool) X(vkDestroyCommandPool)          \
    X(vkAllocateCommandBuffers) X(vkBeginCommandBuffer) X(vkEndCommandBuffer) X(vkResetCommandBuffer)               \
    X(vkCmdPipelineBarrier) X(vkCmdCopyImage) X(vkCmdBlitImage) X(vkCreateSemaphore) X(vkDestroySemaphore)          \
    X(vkCreateFence) X(vkDestroyFence) X(vkWaitForFences) X(vkResetFences) X(vkDeviceWaitIdle)                      \
    X(vkCreateImageView) X(vkDestroyImageView)

struct Fns {
#define GS_DECL(name) PFN_##name name = nullptr;
    GS_DEVICE_FNS(GS_DECL)
#undef GS_DECL
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
};

constexpr int kSlots = 3;
constexpr int kGameFps = 60;  // Celeste (FNA) va a 60 fijos; HyperFG presenta 120

template <typename T>
T vkInit(VkStructureType s) {
    T t{};
    t.sType = s;
    return t;
}

int64_t nowNs() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return static_cast<int64_t>(t.tv_sec) * 1000000000LL + t.tv_nsec;
}

}  // namespace

struct GuestSwapchain::Impl final : FrameGenHost {
    GuestSwapchain* owner = nullptr;
    PFN_vkGetInstanceProcAddr gipa = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice pd = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t family = 0;
    VkQueue queue = VK_NULL_HANDLE;
    Fns f;
    bool loaded = false;

    VkSwapchainKHR real = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    uint64_t generation = 0;
    std::vector<VkImage> realImages;
    std::vector<VkImageView> realViews;
    std::vector<VkImage> virt;
    std::vector<VkDeviceMemory> virtMem;
    std::vector<int> virtSlot;  // último slot de presentación que leyó la imagen virtual (-1: libre)
    uint32_t nextVirt = 0;

    // Presentación al swapchain real (juego y hilo de HyperFG), bajo presentMutex.
    std::mutex presentMutex;
    std::unique_lock<std::mutex> hostLock;  // entre beginPresent y endPresent de HyperFG
    int hostSlot = 0;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer cmd[kSlots]{};
    VkSemaphore acquired[kSlots]{}, rendered[kSlots]{};
    VkFence fence[kSlots]{};
    int nextSlot = 0;
    uint64_t presents = 0;

    // Trabajo de HyperFG del hilo del juego (historial, flujo, interpolación): envío propio.
    VkCommandBuffer recCmd[kSlots]{};
    VkFence recFence[kSlots]{};
    int recSlot = 0;
    HyperFrameGen* fg = nullptr;
    bool fgLogged = false;

    // ------------------------------------------------------------------ FrameGenHost
    bool gpuContext(DirectVulkanBridge::GpuContext& out) override {
        if (!loaded || queue == VK_NULL_HANDLE) return false;
        out.instance = instance;
        out.physicalDevice = pd;
        out.device = device;
        out.queue = queue;
        out.queueFamily = family;
        out.getInstanceProcAddr = gipa;
        out.getDeviceProcAddr = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance, "vkGetDeviceProcAddr"));
        return out.getDeviceProcAddr != nullptr;
    }

    bool beginPresent(DirectVulkanBridge::SwapTarget& out) override {
        hostLock = std::unique_lock<std::mutex>(presentMutex);
        uint32_t r = 0;
        if (real == VK_NULL_HANDLE || !beginReal(hostSlot, r)) {
            hostLock.unlock();
            return false;
        }
        out.image = realImages[r];
        out.view = realViews[r];
        out.extent = extent;
        out.format = format;
        out.transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        out.index = r;
        out.generation = generation;
        return true;
    }

    bool endPresent(const DirectVulkanBridge::SwapTarget& t, VkCommandBuffer c, VkFence done, bool countFrame) override {
        const VkResult r = finishReal(hostSlot, t.index, c, nullptr, 0, done);
        hostLock.unlock();
        if (countFrame) DirectVulkanBridge::shared().countGuestPresent();
        return r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR;
    }

    // ------------------------------------------------------------------ utilidades
    bool load() {
        if (loaded) return true;
        if (gipa == nullptr || instance == VK_NULL_HANDLE || device == VK_NULL_HANDLE) return false;
        auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance, "vkGetDeviceProcAddr"));
        if (gdpa == nullptr) return false;
#define GS_LOAD(name) f.name = reinterpret_cast<PFN_##name>(gdpa(device, #name));
        GS_DEVICE_FNS(GS_LOAD)
#undef GS_LOAD
        f.vkGetPhysicalDeviceMemoryProperties = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(
            gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
        loaded = f.vkCreateSwapchainKHR && f.vkQueueSubmit && f.vkCmdCopyImage && f.vkCreateImageView &&
                 f.vkGetPhysicalDeviceMemoryProperties;
        return loaded;
    }

    uint32_t memType(uint32_t bits, VkMemoryPropertyFlags want) {
        VkPhysicalDeviceMemoryProperties mp{};
        f.vkGetPhysicalDeviceMemoryProperties(pd, &mp);
        for (uint32_t i = 0; i < mp.memoryTypeCount; ++i) {
            if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) return i;
        }
        return UINT32_MAX;
    }

    bool ensureSync() {
        if (pool != VK_NULL_HANDLE) return true;
        auto pi = vkInit<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        pi.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pi.queueFamilyIndex = family;
        if (f.vkCreateCommandPool(device, &pi, nullptr, &pool) != VK_SUCCESS) return false;
        auto ai = vkInit<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = kSlots;
        if (f.vkAllocateCommandBuffers(device, &ai, cmd) != VK_SUCCESS) return false;
        if (f.vkAllocateCommandBuffers(device, &ai, recCmd) != VK_SUCCESS) return false;
        auto si = vkInit<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        auto fi = vkInit<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        for (int i = 0; i < kSlots; ++i) {
            if (f.vkCreateSemaphore(device, &si, nullptr, &acquired[i]) != VK_SUCCESS ||
                f.vkCreateSemaphore(device, &si, nullptr, &rendered[i]) != VK_SUCCESS ||
                f.vkCreateFence(device, &fi, nullptr, &fence[i]) != VK_SUCCESS ||
                f.vkCreateFence(device, &fi, nullptr, &recFence[i]) != VK_SUCCESS) {
                return false;
            }
        }
        return true;
    }

    void destroyVirtual() {
        for (size_t i = 0; i < virt.size(); ++i) {
            if (virt[i] != VK_NULL_HANDLE) f.vkDestroyImage(device, virt[i], nullptr);
            if (virtMem[i] != VK_NULL_HANDLE) f.vkFreeMemory(device, virtMem[i], nullptr);
        }
        for (VkImageView v : realViews) {
            if (v != VK_NULL_HANDLE) f.vkDestroyImageView(device, v, nullptr);
        }
        virt.clear();
        virtMem.clear();
        virtSlot.clear();
        realViews.clear();
    }

    bool createVirtual(const VkSwapchainCreateInfoKHR& ci, uint32_t count) {
        destroyVirtual();
        for (uint32_t i = 0; i < count; ++i) {
            auto ii = vkInit<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
            ii.imageType = VK_IMAGE_TYPE_2D;
            ii.format = ci.imageFormat;
            ii.extent = {ci.imageExtent.width, ci.imageExtent.height, 1};
            ii.mipLevels = 1;
            ii.arrayLayers = 1;
            ii.samples = VK_SAMPLE_COUNT_1_BIT;
            ii.tiling = VK_IMAGE_TILING_OPTIMAL;
            ii.usage = ci.imageUsage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
            ii.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
            ii.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            VkImage img = VK_NULL_HANDLE;
            if (f.vkCreateImage(device, &ii, nullptr, &img) != VK_SUCCESS) return false;
            VkMemoryRequirements mr{};
            f.vkGetImageMemoryRequirements(device, img, &mr);
            auto ma = vkInit<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
            ma.allocationSize = mr.size;
            ma.memoryTypeIndex = memType(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
            VkDeviceMemory mem = VK_NULL_HANDLE;
            if (ma.memoryTypeIndex == UINT32_MAX || f.vkAllocateMemory(device, &ma, nullptr, &mem) != VK_SUCCESS ||
                f.vkBindImageMemory(device, img, mem, 0) != VK_SUCCESS) {
                f.vkDestroyImage(device, img, nullptr);
                if (mem != VK_NULL_HANDLE) f.vkFreeMemory(device, mem, nullptr);
                return false;
            }
            virt.push_back(img);
            virtMem.push_back(mem);
            virtSlot.push_back(-1);
        }
        for (VkImage img : realImages) {
            auto vi = vkInit<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
            vi.image = img;
            vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
            vi.format = ci.imageFormat;
            vi.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
            VkImageView v = VK_NULL_HANDLE;
            if (f.vkCreateImageView(device, &vi, nullptr, &v) != VK_SUCCESS) return false;
            realViews.push_back(v);
        }
        return true;
    }

    void barrier(VkCommandBuffer c, VkImage img, VkImageLayout from, VkImageLayout to, VkAccessFlags src,
                 VkAccessFlags dst) {
        auto b = vkInit<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
        b.oldLayout = from;
        b.newLayout = to;
        b.srcAccessMask = src;
        b.dstAccessMask = dst;
        b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        b.image = img;
        b.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        f.vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
                               nullptr, 1, &b);
    }

    // Con presentMutex tomado: espera el slot y adquiere una imagen real.
    bool beginReal(int& slot, uint32_t& r) {
        slot = nextSlot;
        nextSlot = (nextSlot + 1) % kSlots;
        f.vkWaitForFences(device, 1, &fence[slot], VK_TRUE, UINT64_MAX);
        const VkResult ar = f.vkAcquireNextImageKHR(device, real, UINT64_MAX, acquired[slot], VK_NULL_HANDLE, &r);
        if (ar != VK_SUCCESS && ar != VK_SUBOPTIMAL_KHR) {
            LOGW("Swapchain virtual: vkAcquireNextImageKHR -> %d", ar);
            return false;
        }
        return true;
    }

    // Con presentMutex tomado: envía `c` (deja la imagen real en PRESENT_SRC) y la presenta.
    VkResult finishReal(int slot, uint32_t r, VkCommandBuffer c, const VkSemaphore* extraWaits, uint32_t nExtra,
                        VkFence extFence) {
        std::vector<VkSemaphore> waits(extraWaits, extraWaits + nExtra);
        waits.push_back(acquired[slot]);
        std::vector<VkPipelineStageFlags> stages(waits.size(), VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto si = vkInit<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        si.waitSemaphoreCount = static_cast<uint32_t>(waits.size());
        si.pWaitSemaphores = waits.data();
        si.pWaitDstStageMask = stages.data();
        si.commandBufferCount = 1;
        si.pCommandBuffers = &c;
        si.signalSemaphoreCount = 1;
        si.pSignalSemaphores = &rendered[slot];
        auto pi = vkInit<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        pi.waitSemaphoreCount = 1;
        pi.pWaitSemaphores = &rendered[slot];
        pi.swapchainCount = 1;
        pi.pSwapchains = &real;
        pi.pImageIndices = &r;
        std::lock_guard<std::mutex> lock(owner->queueMutex_);
        f.vkResetFences(device, 1, &fence[slot]);
        VkResult res = f.vkQueueSubmit(queue, 1, &si, fence[slot]);
        if (extFence != VK_NULL_HANDLE) f.vkQueueSubmit(queue, 0, nullptr, extFence);  // se señaliza tras lo anterior
        if (res == VK_SUCCESS) res = f.vkQueuePresentKHR(queue, &pi);
        return res;
    }

    void copyToReal(VkCommandBuffer c, VkImage src, VkImageLayout srcLayout, uint32_t r, bool blit) {
        barrier(c, src, srcLayout, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_MEMORY_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT);
        barrier(c, realImages[r], VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
                VK_ACCESS_TRANSFER_WRITE_BIT);
        if (blit) {
            VkImageBlit b{};
            b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            b.dstSubresource = b.srcSubresource;
            b.srcOffsets[1] = {static_cast<int32_t>(extent.width), static_cast<int32_t>(extent.height), 1};
            b.dstOffsets[1] = b.srcOffsets[1];
            f.vkCmdBlitImage(c, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, realImages[r],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
        } else {
            VkImageCopy copy{};
            copy.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
            copy.dstSubresource = copy.srcSubresource;
            copy.extent = {extent.width, extent.height, 1};
            f.vkCmdCopyImage(c, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, realImages[r],
                             VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
        }
        barrier(c, src, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, srcLayout, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT);
        barrier(c, realImages[r], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                VK_ACCESS_TRANSFER_WRITE_BIT, 0);
    }

    // Hilo del juego, sin presentMutex (record() puede esperar al hilo de HyperFG, que lo toma):
    // copia el frame al historial de HyperFG e interpola. Devuelve si hay frame intermedio.
    bool recordFrameGen(uint32_t k, const VkPresentInfoKHR* info) {
        if (fg == nullptr) fg = new HyperFrameGen(this);
        const int s = recSlot;
        recSlot = (recSlot + 1) % kSlots;
        f.vkWaitForFences(device, 1, &recFence[s], VK_TRUE, UINT64_MAX);
        f.vkResetFences(device, 1, &recFence[s]);
        VkCommandBuffer c = recCmd[s];
        f.vkResetCommandBuffer(c, 0);
        auto bi = vkInit<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        f.vkBeginCommandBuffer(c, &bi);
        barrier(c, virt[k], VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT);
        const VkImageView view = fg->record(c, virt[k], format, extent.width, extent.height, kGameFps);
        f.vkEndCommandBuffer(c);
        std::vector<VkPipelineStageFlags> stages(info->waitSemaphoreCount, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        auto si = vkInit<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
        si.waitSemaphoreCount = info->waitSemaphoreCount;
        si.pWaitSemaphores = info->pWaitSemaphores;
        si.pWaitDstStageMask = stages.data();
        si.commandBufferCount = 1;
        si.pCommandBuffers = &c;
        {
            std::lock_guard<std::mutex> lock(owner->queueMutex_);
            f.vkQueueSubmit(queue, 1, &si, recFence[s]);
        }
        if (!fgLogged) {
            fgLogged = true;
            LOGI("HyperFG en el swapchain del juego: %ux%u, juego a %d -> %d", extent.width, extent.height, kGameFps,
                 kGameFps * 2);
        }
        return view != VK_NULL_HANDLE;
    }
};

GuestSwapchain& GuestSwapchain::shared() {
    static GuestSwapchain s;
    return s;
}

GuestSwapchain::Impl& GuestSwapchain::impl() {
    if (impl_ == nullptr) {
        impl_ = new Impl();
        impl_->owner = this;
    }
    return *impl_;
}

bool GuestSwapchain::wanted() noexcept {
    // Activo por defecto (la copia cuesta muy poco y permite HyperFG); `setprop debug.hyperplay.vswap 0` lo apaga.
    char v[PROP_VALUE_MAX] = {};
    __system_property_get("debug.hyperplay.vswap", v);
    return v[0] != '0';
}

bool GuestSwapchain::active() const noexcept { return impl_ != nullptr && impl_->real != VK_NULL_HANDLE; }

void GuestSwapchain::setLoader(PFN_vkGetInstanceProcAddr gipa, VkInstance instance) {
    impl().gipa = gipa;
    impl().instance = instance;
}

void GuestSwapchain::onCreateDevice(VkPhysicalDevice pd, VkDevice device, uint32_t queueFamily) {
    Impl& m = impl();
    m.pd = pd;
    m.device = device;
    m.family = queueFamily;
    m.queue = VK_NULL_HANDLE;
    m.loaded = false;
}

void GuestSwapchain::onGetDeviceQueue(VkDevice device, uint32_t family, VkQueue queue) {
    Impl& m = impl();
    if (device == m.device && family == m.family && m.queue == VK_NULL_HANDLE) m.queue = queue;
}

VkResult GuestSwapchain::createSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* ci, VkSwapchainKHR* out,
                                         PFN_vkCreateSwapchainKHR real) {
    Impl& m = impl();
    if (!wanted() || device != m.device || !m.load() || m.queue == VK_NULL_HANDLE) return real(device, ci, nullptr, out);
    std::lock_guard<std::mutex> plock(m.presentMutex);
    if (!m.virt.empty()) {
        // Recreación (cambio de tamaño): las imágenes virtuales viejas pueden estar en uso.
        std::lock_guard<std::mutex> lock(queueMutex_);
        m.f.vkDeviceWaitIdle(device);
    }
    m.destroyVirtual();
    m.real = VK_NULL_HANDLE;
    VkSwapchainCreateInfoKHR info = *ci;
    info.imageUsage |= VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.minImageCount = std::max<uint32_t>(info.minImageCount, 3);
    VkResult r = real(device, &info, nullptr, out);
    if (r != VK_SUCCESS) return real(device, ci, nullptr, out);  // sin el uso extra: como siempre
    uint32_t n = 0;
    m.f.vkGetSwapchainImagesKHR(device, *out, &n, nullptr);
    m.realImages.assign(n, VK_NULL_HANDLE);
    m.f.vkGetSwapchainImagesKHR(device, *out, &n, m.realImages.data());
    if (!m.ensureSync() || !m.createVirtual(*ci, n)) {
        LOGW("Swapchain virtual: no se pudieron crear las imágenes; se usa el real");
        m.destroyVirtual();
        return VK_SUCCESS;
    }
    m.real = *out;
    m.format = ci->imageFormat;
    m.extent = ci->imageExtent;
    ++m.generation;
    LOGI("Swapchain virtual: %u imágenes %ux%u formato %d", n, m.extent.width, m.extent.height, m.format);
    return VK_SUCCESS;
}

VkResult GuestSwapchain::getImages(VkDevice device, VkSwapchainKHR sc, uint32_t* count, VkImage* images) {
    Impl& m = impl();
    if (sc != m.real || m.real == VK_NULL_HANDLE) return m.f.vkGetSwapchainImagesKHR(device, sc, count, images);
    const uint32_t n = static_cast<uint32_t>(m.virt.size());
    if (images == nullptr) {
        *count = n;
        return VK_SUCCESS;
    }
    const uint32_t c = std::min(*count, n);
    std::memcpy(images, m.virt.data(), c * sizeof(VkImage));
    *count = c;
    return c < n ? VK_INCOMPLETE : VK_SUCCESS;
}

VkResult GuestSwapchain::acquire(VkDevice device, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore sem,
                                 VkFence fence, uint32_t* index) {
    Impl& m = impl();
    if (sc != m.real || m.real == VK_NULL_HANDLE) {
        const VkResult r = m.f.vkAcquireNextImageKHR(device, sc, timeout, sem, fence, index);
        return r == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : r;
    }
    const uint32_t k = m.nextVirt++ % static_cast<uint32_t>(m.virt.size());
    if (m.virtSlot[k] >= 0) m.f.vkWaitForFences(device, 1, &m.fence[m.virtSlot[k]], VK_TRUE, UINT64_MAX);
    // La imagen virtual está libre: se señaliza lo que el juego espera con un envío vacío.
    auto si = vkInit<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    si.signalSemaphoreCount = sem != VK_NULL_HANDLE ? 1 : 0;
    si.pSignalSemaphores = &sem;
    std::lock_guard<std::mutex> lock(queueMutex_);
    const VkResult r = m.f.vkQueueSubmit(m.queue, 1, &si, fence);
    if (r != VK_SUCCESS) return r;
    *index = k;
    return VK_SUCCESS;
}

VkResult GuestSwapchain::present(VkQueue queue, const VkPresentInfoKHR* info) {
    Impl& m = impl();
    if (m.real == VK_NULL_HANDLE || info == nullptr || info->swapchainCount != 1 || info->pSwapchains[0] != m.real) {
        std::lock_guard<std::mutex> lock(queueMutex_);
        VkResult r = m.f.vkQueuePresentKHR(queue, info);
        return r == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : r;
    }
    const uint32_t k = info->pImageIndices[0];
    const bool fgOn = HyperFrameGen::enabled() && queue == m.queue;
    const bool generated = fgOn && m.recordFrameGen(k, info);
    if (!fgOn && m.fg != nullptr) m.fg->releaseIfIdle();

    VkResult res = VK_SUCCESS;
    {
        std::lock_guard<std::mutex> plock(m.presentMutex);
        int slot = 0;
        uint32_t r = 0;
        if (!m.beginReal(slot, r)) return VK_SUCCESS;  // el juego sigue; se pierde este frame
        VkCommandBuffer c = m.cmd[slot];
        m.f.vkResetCommandBuffer(c, 0);
        auto bi = vkInit<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        m.f.vkBeginCommandBuffer(c, &bi);
        if (generated) {
            // Ahora el intermedio interp(N-1, N); el real N lo presenta el hilo de HyperFG a medio intervalo.
            m.copyToReal(c, m.fg->interpImage(), VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, r, true);
        } else {
            m.copyToReal(c, m.virt[k], fgOn ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                         r, false);
        }
        if (fgOn) {
            // La virtual vuelve a PRESENT_SRC (lo que el juego dio por hecho al presentar).
            m.barrier(c, m.virt[k], VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                      VK_ACCESS_SHADER_READ_BIT, 0);
        }
        m.f.vkEndCommandBuffer(c);
        // Sin HyperFG, este envío espera el render del juego; con HyperFG ya lo esperó recordFrameGen.
        res = m.finishReal(slot, r, c, fgOn ? nullptr : info->pWaitSemaphores, fgOn ? 0 : info->waitSemaphoreCount,
                           VK_NULL_HANDLE);
        m.virtSlot[k] = slot;
    }
    if (generated && (res == VK_SUCCESS || res == VK_SUBOPTIMAL_KHR)) {
        m.fg->scheduleReal(nowNs() + 500000000LL / kGameFps);  // + medio intervalo
    }
    if (++m.presents == 1) LOGI("Swapchain virtual: primer frame presentado (copia)");
    if (info->pResults != nullptr) info->pResults[0] = VK_SUCCESS;
    return res == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : res;
}

void GuestSwapchain::destroySwapchain(VkDevice device, VkSwapchainKHR sc) {
    Impl& m = impl();
    if (sc == m.real && m.real != VK_NULL_HANDLE) {
        std::lock_guard<std::mutex> plock(m.presentMutex);
        {
            std::lock_guard<std::mutex> lock(queueMutex_);
            m.f.vkDeviceWaitIdle(device);
        }
        m.destroyVirtual();
        m.real = VK_NULL_HANDLE;
    }
    if (m.f.vkDestroySwapchainKHR) m.f.vkDestroySwapchainKHR(device, sc, nullptr);
}

}  // namespace hyperplay::gpu
