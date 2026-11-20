cmake_minimum_required(VERSION 3.22.1)
project(hyperplay LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# ---- libhyperplay.so: motor nativo directo ZVDS cargado por la app (JNI) ----
add_library(hyperplay SHARED
    jni_bridge.cpp
    gpu/TurnipDriverInspector.cpp
    gpu/turnip_jni.cpp
    loader/PeHeaderInspector.cpp
    loader/BionicLibraryStager.cpp
    loader/BionicPeLoader.cpp
    loader/NativeDllInterceptor.cpp
    loader/VulkanPassthrough.cpp
    loader/NativeSteamStub.cpp
    loader/FramePacing.cpp
    cpu/OryonSiliconScheduler.cpp
    cpu/HyperAotCache.cpp
    cpu/GuestProfiler.cpp
    gfx/D3D11GpuRenderer.cpp
    cpu/OryonCpuTranslator.cpp
    cpu/OryonEmitterBackend.cpp
    gfx/TurnipLoader.cpp
    gfx/FsrUpscalePass.cpp
    gfx/HyperFrameGen.cpp
    gfx/DirectVulkanBridge.cpp
    audio/DirectAAudioEngine.cpp
    input/DirectXInputBridge.cpp
    media/MediaFoundationBridge.cpp
)

target_include_directories(hyperplay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(hyperplay vulkan aaudio android nativewindow mediandk log dl)
# El motor (JIT, despachador, intÃ©rprete DXBC, rasterizador) es inutilizable a -O0: tambiÃ©n la
# variante debug se compila optimizada (los sÃ­mbolos -g se conservan).
target_compile_options(hyperplay PRIVATE -O2)
# Falla el enlace si queda algÃºn sÃ­mbolo sin resolver.
target_link_options(hyperplay PRIVATE "-Wl,--no-undefined")
