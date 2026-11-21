#pragma once

// Swapchain virtual para juegos con Vulkan propio (FNA3D/SDL GPU en Celeste).
//
// El juego cree que presenta en su swapchain, pero dibuja en imágenes de HyperPlay del mismo
// dispositivo. En cada vkQueuePresentKHR, HyperPlay copia (o interpola, con HyperFG) esa imagen
// al swapchain real de la ventana. Así se pueden presentar fotogramas generados entre los del
// juego (Celeste va a 60 fijos; con generación, 120 en un panel de 120 Hz).
//
// El acceso a la cola del juego se serializa (el juego y el hilo de HyperFG la comparten).

#include <vulkan/vulkan.h>

#include <cstdint>
#include <mutex>

namespace hyperplay::gpu {

class GuestSwapchain {
public:
    static GuestSwapchain& shared();

    /// `setprop debug.hyperplay.vswap 1` (o HyperFG activo): virtualizar. Se decide al crear el
    /// swapchain del juego.
    [[nodiscard]] static bool wanted() noexcept;

    void setLoader(PFN_vkGetInstanceProcAddr gipa, VkInstance instance);
    void onCreateDevice(VkPhysicalDevice pd, VkDevice device, uint32_t queueFamily);
    void onGetDeviceQueue(VkDevice device, uint32_t family, VkQueue queue);

    /// Envoltorios de las funciones de swapchain del juego. Si el swapchain no está virtualizado
    /// llaman a las reales.
    VkResult createSwapchain(VkDevice device, const VkSwapchainCreateInfoKHR* ci, VkSwapchainKHR* out,
                             PFN_vkCreateSwapchainKHR real);
    VkResult getImages(VkDevice device, VkSwapchainKHR sc, uint32_t* count, VkImage* images);
    VkResult acquire(VkDevice device, VkSwapchainKHR sc, uint64_t timeout, VkSemaphore sem, VkFence fence,
                     uint32_t* index);
    VkResult present(VkQueue queue, const VkPresentInfoKHR* info);
    void destroySwapchain(VkDevice device, VkSwapchainKHR sc);

    /// Candado de la cola del juego (vkQueueSubmit / WaitIdle / Present de los envoltorios).
    std::mutex& queueMutex() noexcept { return queueMutex_; }

    [[nodiscard]] bool active() const noexcept;

private:
    GuestSwapchain() = default;
    struct Impl;
    Impl* impl_ = nullptr;
    Impl& impl();
    std::mutex queueMutex_;
};

}  // namespace hyperplay::gpu
