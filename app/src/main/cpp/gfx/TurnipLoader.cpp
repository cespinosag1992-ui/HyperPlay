#include "TurnipLoader.h"

#include <android/log.h>
#include <dlfcn.h>
#include <cstdlib>
#include <string>

#include "loader/BionicLibraryStager.h"

#define LOG_TAG "HyperPlay.TurnipLoader"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {

TurnipLoader::~TurnipLoader() {
    unload();
}

bool TurnipLoader::load(const std::string& customDriverPath) {
    unload();

    // Entorno recomendado por Mesa/Freedreno para Adreno.
    setenv("TU_DEBUG", "noconform,sysmem", 1);
    setenv("MESA_VK_WSI_PRESENT_MODE", "mailbox", 1);
    setenv("FD_DEV_FEATURES", "enable_tp_ubwc_flag_hint=1", 1);

    if (!customDriverPath.empty()) {
        // El .so puede estar en /sdcard o /storage, donde Android 10+ no permite dlopen.
        std::string stagedPath;
        driverHandle_ = BionicLibraryStager::dlopenStaged(
            customDriverPath, RTLD_NOW | RTLD_LOCAL, &stagedPath);
        if (driverHandle_ != nullptr) {
            loadedPath_ = stagedPath;
            isTurnip_ = true;
            LOGI("Driver Turnip cargado desde %s", loadedPath_.c_str());
        } else {
            LOGW("No se pudo cargar Turnip desde %s; se usa el driver del sistema",
                 customDriverPath.c_str());
        }
    }

    if (driverHandle_ == nullptr) {
        const char* systemLibs[] = {
            "libvulkan.so",
            "/system/lib64/libvulkan.so",
            "/vendor/lib64/hw/vulkan.adreno.so",
            "/vendor/lib64/hw/vulkan.adreno.vendor.so",
        };
        for (const char* lib : systemLibs) {
            dlerror();
            driverHandle_ = dlopen(lib, RTLD_NOW | RTLD_LOCAL);
            if (driverHandle_ != nullptr) {
                loadedPath_ = lib;
                isTurnip_ = false;
                LOGI("Vulkan del sistema cargado: %s", lib);
                break;
            }
            BionicLibraryStager::logDlError("dlopen del driver de sistema falló", lib);
        }
    }

    if (driverHandle_ == nullptr) {
        LOGW("No hay ningún driver Vulkan utilizable");
        return false;
    }

    // Un ICD puro (Turnip) solo exporta vk_icdGetInstanceProcAddr; libvulkan.so exporta
    // vkGetInstanceProcAddr. Se aceptan ambos.
    dlerror();
    auto getInstanceProcAddr =
        reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driverHandle_, "vkGetInstanceProcAddr"));
    if (getInstanceProcAddr == nullptr) {
        getInstanceProcAddr =
            reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(driverHandle_, "vk_icdGetInstanceProcAddr"));
    }
    if (getInstanceProcAddr == nullptr) {
        BionicLibraryStager::logDlError("El driver no exporta vkGetInstanceProcAddr", loadedPath_);
        unload();
        return false;
    }
    vkGetInstanceProcAddr_ = getInstanceProcAddr;
    return true;
}

void TurnipLoader::unload() {
    if (driverHandle_ != nullptr) {
        dlclose(driverHandle_);
        driverHandle_ = nullptr;
    }
    vkGetInstanceProcAddr_ = nullptr;
    isTurnip_ = false;
    loadedPath_.clear();
}

void* TurnipLoader::getProcAddress(const char* name) const noexcept {
    if (vkGetInstanceProcAddr_ == nullptr || name == nullptr) return nullptr;
    return reinterpret_cast<void*>(vkGetInstanceProcAddr_(VK_NULL_HANDLE, name));
}

}  // namespace hyperplay
