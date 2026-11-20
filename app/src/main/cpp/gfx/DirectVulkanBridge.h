#pragma once

#include <android/native_window.h>
#include <vulkan/vulkan.h>

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <vector>

#include "FsrUpscalePass.h"
#include "TurnipLoader.h"

namespace hyperplay {

/// Presentador Vulkan mínimo: instancia + dispositivo + swapchain sobre un ANativeWindow.
/// Todas las operaciones de ciclo de vida están serializadas por `mutex_`.
class DirectVulkanBridge {
public:
    DirectVulkanBridge();
    ~DirectVulkanBridge();

    DirectVulkanBridge(const DirectVulkanBridge&) = delete;
    DirectVulkanBridge& operator=(const DirectVulkanBridge&) = delete;

    /// Instancia única del proceso: la superficie que vincula Kotlin (jni_bridge) y la que
    /// presenta el juego (NativeDllInterceptor: DXGI/D3D11) deben ser la MISMA.
    static DirectVulkanBridge& shared();

    /// Carga el driver y crea instancia/dispositivo físico. El dispositivo lógico se crea al
    /// vincular la primera superficie (necesita comprobar el soporte de presentación).
    bool init(const std::string& customTurnipPath = "");

    /// Vincula la ventana nativa de Android (ANativeWindow*). Toma su propia referencia.
    bool attachWindow(ANativeWindow* window);

    /// Desvincula la superficie y libera la ventana. Seguro desde cualquier hilo.
    void detachWindow();

    /// Presenta un frame de prueba (sin contenido de juego) para validar el swapchain.
    /// Recrea el swapchain ante OUT_OF_DATE/SURFACE_LOST.
    bool presentTestFrame();

    /// Presenta una imagen RGBA8 del juego (backbuffer DXGI): se escala al tamaño del swapchain,
    /// se aplica la rotación de la superficie y se copia por transferencia a la imagen adquirida.
    bool presentPixels(const uint8_t* rgba, uint32_t width, uint32_t height);

    void shutdown();

    // --- Interfaz para el renderizador D3D11 -> Vulkan (ruta GPU) ---------------------------
    struct GpuContext {
        VkInstance instance = VK_NULL_HANDLE;
        VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
        VkDevice device = VK_NULL_HANDLE;
        VkQueue queue = VK_NULL_HANDLE;
        uint32_t queueFamily = 0;
        PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
        PFN_vkGetDeviceProcAddr getDeviceProcAddr = nullptr;
        bool textureCompressionBC = false;
        bool samplerAnisotropy = false;
        bool independentBlend = false;
    };
    /// false si el dispositivo lógico aún no existe (sin superficie vinculada).
    bool gpuContext(GpuContext& out);

    /// Envía un command buffer a la cola (acceso a la cola serializado con el presentador).
    bool submit(VkCommandBuffer cmd, VkFence fence);

    struct SwapTarget {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        VkSurfaceTransformFlagBitsKHR transform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        uint32_t index = 0;
        uint64_t generation = 0;  // cambia al recrear el swapchain (invalida framebuffers)
    };
    /// Adquiere la siguiente imagen del swapchain. Si devuelve true, el llamador DEBE llamar a
    /// endPresent (el mutex del presentador queda tomado entre ambas llamadas).
    bool beginPresent(SwapTarget& out);
    /// Envía `cmd` (que debe dejar la imagen en PRESENT_SRC_KHR) esperando la adquisición, la
    /// presenta y libera el mutex. `fence` se señaliza al terminar la GPU.
    /// `countFrame` = false para una repetición del mismo frame (no cuenta en FPS/HUD).
    bool endPresent(const SwapTarget& target, VkCommandBuffer cmd, VkFence fence, bool countFrame = true);

    [[nodiscard]] float presentFps() const noexcept { return currentFps_.load(std::memory_order_relaxed); }
    [[nodiscard]] uint64_t totalFramesPresented() const noexcept {
        return totalFrames_.load(std::memory_order_relaxed);
    }
    [[nodiscard]] std::string gpuName() const noexcept { return gpuName_; }
    [[nodiscard]] bool isTurnip() const noexcept { return loader_.isTurnip(); }

    void setFsrEnabled(bool enabled) noexcept { fsrPass_.setEnabled(enabled); }
    [[nodiscard]] bool isFsrEnabled() const noexcept { return fsrPass_.isEnabled(); }

private:
    TurnipLoader loader_;
    FsrUpscalePass fsrPass_;

    VkInstance instance_ = VK_NULL_HANDLE;
    PFN_vkGetInstanceProcAddr getInstanceProcAddr_ = nullptr;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr_ = nullptr;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    std::string gpuName_;

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamilyIndex_ = UINT32_MAX;

    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapchainFormat_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR swapchainColorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D swapchainExtent_ = {0, 0};

    std::vector<VkImage> swapchainImages_;
    std::vector<VkImageView> swapchainViews_;

    VkSemaphore imageAvailable_ = VK_NULL_HANDLE;
    VkFence inFlightFence_ = VK_NULL_HANDLE;
    // true si el fence está asociado a una adquisición de imagen pendiente: solo entonces se
    // puede esperar; un acquire fallido deja el fence sin señalizar y no debe esperarse.
    bool fencePending_ = false;

    // Subida de frames del juego (presentPixels).
    bool swapchainTransferDst_ = false;
    VkSurfaceTransformFlagBitsKHR surfaceTransform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    VkPhysicalDeviceFeatures enabledFeatures_{};
    uint64_t swapchainGeneration_ = 0;
    bool presentGeometryLogged_ = false;
    uint32_t presentLoggedW_ = 0, presentLoggedH_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkSemaphore renderFinished_ = VK_NULL_HANDLE;
    VkFence submitFence_ = VK_NULL_HANDLE;
    VkBuffer stagingBuffer_ = VK_NULL_HANDLE;
    VkDeviceMemory stagingMemory_ = VK_NULL_HANDLE;
    uint8_t* stagingMapped_ = nullptr;
    VkDeviceSize stagingSize_ = 0;

    ANativeWindow* window_ = nullptr;
    mutable std::mutex mutex_;

    std::atomic<float> currentFps_{0.0f};
    std::atomic<uint64_t> totalFrames_{0};
    uint64_t lastFpsCheckTime_ = 0;
    uint32_t frameCounter_ = 0;

    bool initLocked(const std::string& customTurnipPath);
    bool createInstance();
    bool selectPhysicalDevice();
    bool createSurface();
    bool ensureDevice();
    bool createSwapchain();
    bool recreateSwapchain();
    void cleanupSwapchainLocked();
    void detachWindowLocked();
    bool ensureUploadResourcesLocked(VkDeviceSize bytes);
    void destroyUploadResourcesLocked();
    bool acquireImageLocked(uint32_t& imageIndex);
    void countFrameLocked();

    template <typename T>
    T instProc(const char* name) const {
        return getInstanceProcAddr_ != nullptr
                   ? reinterpret_cast<T>(getInstanceProcAddr_(instance_, name))
                   : nullptr;
    }
    template <typename T>
    T devProc(const char* name) const {
        return getDeviceProcAddr_ != nullptr
                   ? reinterpret_cast<T>(getDeviceProcAddr_(device_, name))
                   : nullptr;
    }
};

}  // namespace hyperplay
