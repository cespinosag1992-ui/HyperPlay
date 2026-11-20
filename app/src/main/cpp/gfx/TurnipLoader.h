#pragma once

#include <vulkan/vulkan.h>
#include <string>

namespace hyperplay {

class TurnipLoader {
public:
    TurnipLoader() = default;
    ~TurnipLoader();

    TurnipLoader(const TurnipLoader&) = delete;
    TurnipLoader& operator=(const TurnipLoader&) = delete;

    /// Carga el driver Vulkan en el proceso: intenta Turnip (libvulkan_freedreno.so) o el Vulkan nativo del sistema.
    bool load(const std::string& customDriverPath = "");

    void unload();

    [[nodiscard]] bool isLoaded() const noexcept { return driverHandle_ != nullptr; }
    [[nodiscard]] bool isTurnip() const noexcept { return isTurnip_; }
    [[nodiscard]] const std::string& driverPath() const noexcept { return loadedPath_; }

    [[nodiscard]] PFN_vkGetInstanceProcAddr getInstanceProcAddr() const noexcept { return vkGetInstanceProcAddr_; }

    void* getProcAddress(const char* name) const noexcept;

private:
    void* driverHandle_ = nullptr;
    std::string loadedPath_;
    bool isTurnip_ = false;
    PFN_vkGetInstanceProcAddr vkGetInstanceProcAddr_ = nullptr;
};

}  // namespace hyperplay
