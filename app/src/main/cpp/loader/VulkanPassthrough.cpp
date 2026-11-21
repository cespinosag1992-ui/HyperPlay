#include "VulkanPassthrough.h"

#include "gfx/DirectVulkanBridge.h"

#include <android/log.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include <atomic>
#include <cstring>
#include <mutex>
#include <string>
#include <vector>

#define LOG_TAG "HyperPlay.VkPass"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay::vkpass {
namespace {

constexpr char kWin32Surface[] = "VK_KHR_win32_surface";
constexpr char kAndroidSurface[] = "VK_KHR_android_surface";

RegisterFn s_register = nullptr;
std::atomic<uint64_t> s_presents{0};
std::atomic<VkInstance> s_instance{VK_NULL_HANDLE};
PFN_vkQueuePresentKHR s_queuePresent = nullptr;

PFN_vkGetInstanceProcAddr loader() {
    static PFN_vkGetInstanceProcAddr s = DirectVulkanBridge::shared().guestLoader();
    return s;
}

template <typename T>
T globalProc(const char* name) {
    auto gipa = loader();
    return gipa != nullptr ? reinterpret_cast<T>(gipa(VK_NULL_HANDLE, name)) : nullptr;
}

bool hiddenExtension(const char* name) {
    return std::strcmp(name, "VK_EXT_debug_utils") == 0 || std::strcmp(name, "VK_EXT_debug_report") == 0 ||
           std::strcmp(name, "VK_EXT_debug_marker") == 0 || std::strcmp(name, "VK_EXT_validation_features") == 0;
}

void* expose(void* fn, const char* name) {
    if (fn != nullptr && s_register != nullptr) s_register(fn, (std::string("vulkan-1.dll!") + name).c_str());
    return fn;
}

// --- Envoltorios (mismas firmas que Vulkan) ---------------------------------------------------

VKAPI_ATTR VkResult VKAPI_CALL hpEnumerateInstanceExtensionProperties(const char* layer, uint32_t* count,
                                                                     VkExtensionProperties* props) {
    if (count == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
    if (layer != nullptr) { *count = 0; return VK_ERROR_LAYER_NOT_PRESENT; }
    auto fn = globalProc<PFN_vkEnumerateInstanceExtensionProperties>("vkEnumerateInstanceExtensionProperties");
    if (fn == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
    uint32_t n = 0;
    fn(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> all(n);
    fn(nullptr, &n, all.data());
    std::vector<VkExtensionProperties> out;
    for (auto e : all) {
        if (hiddenExtension(e.extensionName)) continue;
        if (std::strcmp(e.extensionName, kAndroidSurface) == 0) {
            std::strncpy(e.extensionName, kWin32Surface, VK_MAX_EXTENSION_NAME_SIZE);
        }
        out.push_back(e);
    }
    if (props == nullptr) { *count = static_cast<uint32_t>(out.size()); return VK_SUCCESS; }
    const uint32_t copy = std::min<uint32_t>(*count, static_cast<uint32_t>(out.size()));
    std::memcpy(props, out.data(), copy * sizeof(VkExtensionProperties));
    *count = copy;
    return copy < out.size() ? VK_INCOMPLETE : VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL hpEnumerateInstanceLayerProperties(uint32_t* count, VkLayerProperties*) {
    if (count != nullptr) *count = 0;
    return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL hpCreateInstance(const VkInstanceCreateInfo* ci, const VkAllocationCallbacks*,
                                                VkInstance* out) {
    auto fn = globalProc<PFN_vkCreateInstance>("vkCreateInstance");
    if (fn == nullptr || ci == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
    VkInstanceCreateInfo info = *ci;
    std::vector<const char*> exts;
    for (uint32_t i = 0; i < ci->enabledExtensionCount; ++i) {
        const char* e = ci->ppEnabledExtensionNames[i];
        if (hiddenExtension(e)) continue;
        exts.push_back(std::strcmp(e, kWin32Surface) == 0 ? kAndroidSurface : e);
    }
    info.enabledExtensionCount = static_cast<uint32_t>(exts.size());
    info.ppEnabledExtensionNames = exts.data();
    info.enabledLayerCount = 0;  // sin capas de validación (no hay en el teléfono)
    info.ppEnabledLayerNames = nullptr;
    // Una cadena pNext con mensajeros de depuración lleva callbacks del juego (código x86): se
    // descarta entera (en la creación de instancia solo suele llevar eso).
    for (auto* p = static_cast<const VkBaseInStructure*>(ci->pNext); p != nullptr; p = p->pNext) {
        if (p->sType == VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT ||
            p->sType == VK_STRUCTURE_TYPE_DEBUG_REPORT_CALLBACK_CREATE_INFO_EXT ||
            p->sType == VK_STRUCTURE_TYPE_VALIDATION_FEATURES_EXT) {
            info.pNext = nullptr;
            break;
        }
    }
    const VkResult r = fn(&info, nullptr, out);
    if (r == VK_SUCCESS && out != nullptr) s_instance.store(*out);
    LOGI("vkCreateInstance(%u extensiones) -> %d", info.enabledExtensionCount, r);
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL hpCreateWin32SurfaceKHR(VkInstance instance, const void*, const VkAllocationCallbacks*,
                                                       VkSurfaceKHR* out) {
    ANativeWindow* window = DirectVulkanBridge::shared().releaseWindowForGuest();
    if (window == nullptr) {
        LOGW("vkCreateWin32SurfaceKHR: sin ventana de Android");
        return VK_ERROR_INITIALIZATION_FAILED;
    }
    auto fn = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(loader()(instance, "vkCreateAndroidSurfaceKHR"));
    if (fn == nullptr) return VK_ERROR_EXTENSION_NOT_PRESENT;
    VkAndroidSurfaceCreateInfoKHR info{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, nullptr, 0, window};
    const VkResult r = fn(instance, &info, nullptr, out);
    LOGI("vkCreateWin32SurfaceKHR -> superficie Android %d", r);
    return r;
}

VKAPI_ATTR VkBool32 VKAPI_CALL hpGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice, uint32_t) {
    return VK_TRUE;
}

// Rotación de Android: la superficie llega con currentTransform = ROTATE_90/270 (el panel es
// vertical de fábrica). Un juego de PC no pre-rota lo que dibuja; si pasa ese transform en
// preTransform, la imagen sale girada 90° y aplastada. Al juego se le dice IDENTITY y el
// swapchain se crea con IDENTITY: el compositor de Android gira la imagen.
PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR s_getCaps = nullptr;
PFN_vkCreateSwapchainKHR s_createSwapchain = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL hpGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice pd, VkSurfaceKHR surface,
                                                                         VkSurfaceCapabilitiesKHR* caps) {
    if (s_getCaps == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
    const VkResult r = s_getCaps(pd, surface, caps);
    if (r == VK_SUCCESS && caps != nullptr) {
        caps->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
        caps->supportedTransforms |= VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    }
    return r;
}

VKAPI_ATTR VkResult VKAPI_CALL hpCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* ci,
                                                    const VkAllocationCallbacks*, VkSwapchainKHR* out) {
    if (s_createSwapchain == nullptr || ci == nullptr) return VK_ERROR_INITIALIZATION_FAILED;
    VkSwapchainCreateInfoKHR info = *ci;
    info.preTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    const VkResult r = s_createSwapchain(device, &info, nullptr, out);
    LOGI("vkCreateSwapchainKHR %ux%u formato %d modo %d (preTransform IDENTITY) -> %d", info.imageExtent.width,
         info.imageExtent.height, info.imageFormat, info.presentMode, r);
    return r;
}

// Con preTransform IDENTITY sobre una superficie girada, Android responde VK_SUBOPTIMAL_KHR en
// cada adquisición y presentación. SDL lo toma como "recrea el swapchain" y se queda en bucle
// (imagen congelada). Para quien no pre-rota, SUBOPTIMAL es correcto: se devuelve SUCCESS.
PFN_vkAcquireNextImageKHR s_acquire = nullptr;

VKAPI_ATTR VkResult VKAPI_CALL hpAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                                     VkSemaphore semaphore, VkFence fence, uint32_t* index) {
    if (s_acquire == nullptr) return VK_ERROR_DEVICE_LOST;
    const VkResult r = s_acquire(device, swapchain, timeout, semaphore, fence, index);
    return r == VK_SUBOPTIMAL_KHR ? VK_SUCCESS : r;
}

VKAPI_ATTR VkResult VKAPI_CALL hpQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* info) {
    if (s_queuePresent == nullptr) return VK_ERROR_DEVICE_LOST;
    VkResult r = s_queuePresent(queue, info);
    if (r == VK_SUBOPTIMAL_KHR) r = VK_SUCCESS;
    if (info != nullptr && info->pResults != nullptr) {
        for (uint32_t i = 0; i < info->swapchainCount; ++i) {
            if (info->pResults[i] == VK_SUBOPTIMAL_KHR) info->pResults[i] = VK_SUCCESS;
        }
    }
    DirectVulkanBridge::shared().countGuestPresent();
    s_presents.fetch_add(1, std::memory_order_relaxed);
    return r;
}

// Funciones con argumentos float: en Win64 van en XMM, no en los registros enteros que pasa el
// despachador. Si el juego las usa, se ve en el log (pendiente de un puente con XMM).
VKAPI_ATTR void VKAPI_CALL hpCmdSetLineWidth(VkCommandBuffer, float) {
    static std::atomic<bool> s_once{false};
    if (!s_once.exchange(true)) LOGW("vkCmdSetLineWidth (float) no soportado aún: se ignora");
}
VKAPI_ATTR void VKAPI_CALL hpCmdSetDepthBias(VkCommandBuffer, float, float, float) {
    static std::atomic<bool> s_once{false};
    if (!s_once.exchange(true)) LOGW("vkCmdSetDepthBias (float) no soportado aún: se ignora");
}
VKAPI_ATTR void VKAPI_CALL hpCmdSetDepthBounds(VkCommandBuffer, float, float) {
    static std::atomic<bool> s_once{false};
    if (!s_once.exchange(true)) LOGW("vkCmdSetDepthBounds (float) no soportado aún: se ignora");
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hpGetInstanceProcAddr(VkInstance instance, const char* name);
VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hpGetDeviceProcAddr(VkDevice device, const char* name);

void* wrapperFor(const char* name) {
    struct Entry { const char* name; void* fn; };
    static const Entry kWrappers[] = {
        {"vkGetInstanceProcAddr", reinterpret_cast<void*>(&hpGetInstanceProcAddr)},
        {"vkGetDeviceProcAddr", reinterpret_cast<void*>(&hpGetDeviceProcAddr)},
        {"vkEnumerateInstanceExtensionProperties", reinterpret_cast<void*>(&hpEnumerateInstanceExtensionProperties)},
        {"vkEnumerateInstanceLayerProperties", reinterpret_cast<void*>(&hpEnumerateInstanceLayerProperties)},
        {"vkCreateInstance", reinterpret_cast<void*>(&hpCreateInstance)},
        {"vkCreateWin32SurfaceKHR", reinterpret_cast<void*>(&hpCreateWin32SurfaceKHR)},
        {"vkGetPhysicalDeviceWin32PresentationSupportKHR",
         reinterpret_cast<void*>(&hpGetPhysicalDeviceWin32PresentationSupportKHR)},
        {"vkQueuePresentKHR", reinterpret_cast<void*>(&hpQueuePresentKHR)},
        {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", reinterpret_cast<void*>(&hpGetPhysicalDeviceSurfaceCapabilitiesKHR)},
        {"vkCreateSwapchainKHR", reinterpret_cast<void*>(&hpCreateSwapchainKHR)},
        {"vkAcquireNextImageKHR", reinterpret_cast<void*>(&hpAcquireNextImageKHR)},
        {"vkCmdSetLineWidth", reinterpret_cast<void*>(&hpCmdSetLineWidth)},
        {"vkCmdSetDepthBias", reinterpret_cast<void*>(&hpCmdSetDepthBias)},
        {"vkCmdSetDepthBounds", reinterpret_cast<void*>(&hpCmdSetDepthBounds)},
    };
    for (const auto& e : kWrappers) {
        if (std::strcmp(e.name, name) == 0) return e.fn;
    }
    if (std::strncmp(name, "vkCreateDebug", 13) == 0 || std::strncmp(name, "vkDestroyDebug", 14) == 0 ||
        std::strncmp(name, "vkSetDebug", 10) == 0 || std::strncmp(name, "vkCmdBeginDebug", 15) == 0 ||
        std::strncmp(name, "vkCmdEndDebug", 13) == 0 || std::strncmp(name, "vkCmdInsertDebug", 16) == 0) {
        return reinterpret_cast<void*>(-1);  // oculto
    }
    return nullptr;
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hpGetInstanceProcAddr(VkInstance instance, const char* name) {
    if (name == nullptr || loader() == nullptr) return nullptr;
    if (void* w = wrapperFor(name)) {
        if (w == reinterpret_cast<void*>(-1)) return nullptr;
        if (instance != VK_NULL_HANDLE) {
            // Punteros reales que usan los envoltorios (nivel de instancia).
            if (std::strcmp(name, "vkQueuePresentKHR") == 0 && s_queuePresent == nullptr) {
                s_queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(loader()(instance, name));
            } else if (std::strcmp(name, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR") == 0) {
                s_getCaps = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(loader()(instance, name));
            } else if (std::strcmp(name, "vkCreateSwapchainKHR") == 0 && s_createSwapchain == nullptr) {
                s_createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(loader()(instance, name));
            } else if (std::strcmp(name, "vkAcquireNextImageKHR") == 0 && s_acquire == nullptr) {
                s_acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(loader()(instance, name));
            }
        }
        return reinterpret_cast<PFN_vkVoidFunction>(expose(w, name));
    }
    return reinterpret_cast<PFN_vkVoidFunction>(expose(reinterpret_cast<void*>(loader()(instance, name)), name));
}

VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL hpGetDeviceProcAddr(VkDevice device, const char* name) {
    const VkInstance instance = s_instance.load();
    if (name == nullptr || loader() == nullptr || instance == VK_NULL_HANDLE) return nullptr;
    static auto gdpa = reinterpret_cast<PFN_vkGetDeviceProcAddr>(loader()(instance, "vkGetDeviceProcAddr"));
    if (gdpa == nullptr) return nullptr;
    if (void* w = wrapperFor(name)) {
        if (w == reinterpret_cast<void*>(-1)) return nullptr;
        if (std::strcmp(name, "vkQueuePresentKHR") == 0) {
            s_queuePresent = reinterpret_cast<PFN_vkQueuePresentKHR>(gdpa(device, name));
        } else if (std::strcmp(name, "vkCreateSwapchainKHR") == 0) {
            s_createSwapchain = reinterpret_cast<PFN_vkCreateSwapchainKHR>(gdpa(device, name));
        } else if (std::strcmp(name, "vkAcquireNextImageKHR") == 0) {
            s_acquire = reinterpret_cast<PFN_vkAcquireNextImageKHR>(gdpa(device, name));
        }
        return reinterpret_cast<PFN_vkVoidFunction>(expose(w, name));
    }
    return reinterpret_cast<PFN_vkVoidFunction>(expose(reinterpret_cast<void*>(gdpa(device, name)), name));
}

}  // namespace

void init(RegisterFn reg) { s_register = reg; }

uint64_t presentCount() { return s_presents.load(std::memory_order_relaxed); }

void* guestProcAddr(const char* name) {
    if (name == nullptr) return nullptr;
    // vulkan-1.dll exporta todo el núcleo; el juego suele pedir solo vkGetInstanceProcAddr.
    return reinterpret_cast<void*>(hpGetInstanceProcAddr(VK_NULL_HANDLE, name));
}

}  // namespace hyperplay::vkpass
