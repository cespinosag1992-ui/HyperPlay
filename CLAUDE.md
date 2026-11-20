# HYPERPLAY (`com.murdoc820.hyperplay`) - PURE BIONIC DIRECT-SILICON ARCHITECTURE (ZVDS)
**Target Hardware:** Qualcomm Snapdragon 8 Elite (Oryon 64-bit CPU: 6x Perf @ 3.53GHz + 2x Prime @ 4.32GHz | Adreno 830 GPU)
**Target OS:** Android 14/15/16 (`arm64-v8a` exclusive, Pure 64-bit Silicon - NO `aarch32` hardware support)
**Core Philosophy:** ZERO Linux Guest OS, ZERO Ubuntu/Alpine Rootfs, ZERO PRoot/Chroot, ZERO X11/Wayland Servers, ZERO Wine, ZERO Proton, ZERO FEX, ZERO Box64. Pure in-house native execution of portable 32-bit (`x86`) and 64-bit (`x86_64`) Windows `.exe` games directly on Android Bionic via `libhyperplay.so`.

---

## 1. THE 5 PILLARS OF THE PURE DIRECT ENGINE (ZVDS)

### 1. Direct Bionic PE32 / PE32+ In-Memory Loader (`BionicPeLoader.cpp` & `PeHeaderInspector.cpp`)
- Opens any portable `.exe` and its local `.dll` files directly from Android storage (`/sdcard/...`).
- Parses DOS/NT Headers (`0x014c` for 32-bit x86, `0x8664` for 64-bit x64).
- Maps `.text`, `.data`, `.rdata`, and `.pdata` sections directly into the Android app's virtual memory space using `mmap`.
- **32-bit on 64-bit Silicon (`WOW64 Arena`):** Since Snapdragon 8 Elite lacks `aarch32`, 32-bit games are allocated inside a deterministic low-4GB virtual memory arena (`< 0x100000000` with `LARGE_ADDRESS_AWARE` forced ON), while pointer calls are seamlessly zero-extended to 64-bit `aarch64` registers.
- **Zero-Install Portable Drive `D:\` Mapping:** Mounts the portable game folder directly and isolates save files inside `<Game_Folder>/_HyperPlaySaves/`.

---

### 2. PE Import Address Table (IAT) Hijacking & Native C++ Grafting (`NativeDllInterceptor.cpp`)
Instead of loading emulated Windows system DLLs or running a Wine container, `NativeDllInterceptor` intercepts the `.exe`'s Import Address Table (IAT) in RAM and replaces function pointers with direct native `aarch64` C++ trampolines compiled inside `libhyperplay.so`:
- **Direct3D 11 & Direct3D 9:** Intercepts `D3D11CreateDeviceAndSwapChain` and `Direct3DCreate9`. Delivers mock device and swap chain vtables connected directly to `DirectVulkanBridge::presentTestFrame()` at 120Hz.
- **Core Win32 APIs:** Intercepts `kernel32.dll` and `ntdll.dll` functions:
  - Memory: `VirtualAlloc`, `VirtualFree`, `VirtualProtect`, `HeapCreate`, `HeapAlloc`, `HeapFree`.
  - Modules: `GetModuleHandleW/A`, `GetProcAddress`, `LoadLibraryW/A`.
  - Threads & Sync: `CreateThread`, `GetCurrentThreadId` (`gettid()`), `InitializeCriticalSection`, `EnterCriticalSection`, `LeaveCriticalSection`, `TlsAlloc`, `TlsGetValue`, `TlsSetValue`.
  - Timing: `QueryPerformanceCounter`, `QueryPerformanceFrequency`, `GetTickCount64`.
  - Windowing: `CreateWindowExW/A`, `ShowWindow`, `PeekMessageW`, `DispatchMessageW`, `DefWindowProcW`.
- **Steam DRM Stubbing (`NativeSteamStub.cpp`):** Stubs `steam_api.dll` and `steam_api64.dll` (`SteamAPI_Init`, `SteamAPI_RestartAppIfNecessary`), allowing portable games to execute without requiring the Steam client.

---

### 3. In-House JIT Recompiler `x86/x64 -> ARMv9 AArch64` (`OryonCpuTranslator.cpp` & `OryonEmitterBackend.cpp`)
HyperPlay implements its own native translation engine in C++20 registered by default as the active `CpuTranslationBackend`:
- **Real x86/x64 Instruction Decoder:** Decodes REX prefixes (`0x40..0x4F`), ModR/M bytes, SIB addressing, displacements, and immediate values.
- **Instruction Support:** Translates `MOV` (reg-reg, reg-imm64/32, load/store), stack operations (`PUSH`, `POP`), arithmetic/logic (`ADD`, `SUB`, `CMP`, `TEST`, `AND`, `OR`, `XOR`), control flow (`JMP`, `Jcc`, `CALL`, `RET`), and SSE vector moves (`MOVAPS`, `MOVUPS`).
- **GPR Mapping to ARMv9:** Maps x86 registers `RAX..R15` directly to AArch64 registers `X0..X15`.
- **Dedicated Virtual Stack:** Allocates an 8MB virtual stack (`mmap`) with RSP pointing to stack top.
- **W^X JIT Memory Pool:** Allocates a 64MB executable buffer (`mmap` `PROT_READ|PROT_WRITE` switched to `PROT_READ|PROT_EXEC`) with explicit cache invalidation via `__builtin___clear_cache` for Snapdragon 8 Elite Oryon CPU cores.
- **AOT Block Cache (`HyperAotCache.cpp`):** Caches compiled basic blocks in `<Game_Folder>/_HyperPlaySaves/hypercache/` to eliminate runtime stutter.

---

### 4. Direct Hardware Vulkan Bridge & FSR CAS (`DirectVulkanBridge.cpp` & `FsrUpscalePass.cpp`)
- Directly acquires and manages the Android `SurfaceView` `ANativeWindow*` through a low-latency Vulkan swapchain.
- Prioritizes `VK_PRESENT_MODE_MAILBOX_KHR` (120Hz unlocked) with fallback to `VK_PRESENT_MODE_FIFO_KHR`.
- **Contrast Adaptive Sharpening (CAS):** Real Compute Shader GLSL 450 compiled into SPIR-V bytecode (`0x07230203`, 3,136 bytes in `FsrCasSpv.h`), providing adaptive sharpening and spatial upscaling directly on the Adreno 830 GPU.
- **Turnip Adreno 830 Driver Integration (`TurnipLoader.cpp`):** Direct hardware driver access via `/dev/kgsl-3d0`.

---

### 5. Direct AAudio, XInput Bridge & Silicon Scheduler (`DirectAAudioEngine.cpp`, `DirectXInputBridge.cpp`, `OryonSiliconScheduler.cpp`)
- **Direct Audio:** AAudio low-latency stream (`AAUDIO_PERFORMANCE_MODE_LOW_LATENCY`, 48kHz stereo, wait-for-STOPPED safe teardown).
- **Direct Gamepad (0.0ms):** Shared-memory `XInput` state updated directly by Android `InputDevice` events, read by games in zero cycles.
- **Oryon Silicon Scheduler:** Automatically identifies Snapdragon 8 Elite CPU topology via sysfs and pins game execution threads to the 2x Oryon Prime cores (`cpu6`, `cpu7` @ 4.32GHz), paired with Android Dynamic Performance Framework (ADPF / `APerformanceHintSession`).

---

## 2. PRODUCTION PROJECT STRUCTURE

```text
HyperPlay/
├── ARCHITECTURE.md
├── CLAUDE.md
├── progreso.md
├── settings.gradle.kts
├── build.gradle.kts
├── gradle.properties
└── app/
    ├── build.gradle.kts
    └── src/
        ├── main/
        │   ├── AndroidManifest.xml
        │   ├── cpp/
        │   │   ├── CMakeLists.txt
        │   │   ├── jni_bridge.cpp
        │   │   ├── loader/
        │   │   │   ├── PeHeaderInspector.cpp / .h
        │   │   │   ├── BionicPeLoader.cpp / .h
        │   │   │   ├── NativeDllInterceptor.cpp / .h
        │   │   │   ├── NativeSteamStub.cpp / .h
        │   │   │   └── BionicLibraryStager.cpp / .h
        │   │   ├── cpu/
        │   │   │   ├── OryonEmitterBackend.cpp / .h
        │   │   │   ├── OryonCpuTranslator.cpp / .h
        │   │   │   ├── HyperAotCache.cpp / .h
        │   │   │   └── OryonSiliconScheduler.cpp / .h
        │   │   ├── gfx/
        │   │   │   ├── DirectVulkanBridge.cpp / .h
        │   │   │   ├── FsrUpscalePass.cpp / .h
        │   │   │   ├── FsrCasSpv.h
        │   │   │   └── TurnipLoader.cpp / .h
        │   │   ├── audio/
        │   │   │   └── DirectAAudioEngine.cpp / .h
        │   │   └── input/
        │   │       └── DirectXInputBridge.cpp / .h
        │   └── java/com/murdoc820/hyperplay/
        │       ├── MainActivity.kt
        │       ├── GameSessionActivity.kt
        │       ├── model/PortableGame.kt
        │       ├── engine/
        │       │   ├── HyperPlayNative.kt
        │       │   ├── GameLauncher.kt
        │       │   ├── PortableExeScanner.kt
        │       │   └── TurnipDriverManager.kt
        │       ├── input/
        │       │   ├── ControllerManager.kt
        │       │   └── VirtualTouchOverlayView.kt
        │       └── audio/ConsoleUiSoundEngine.kt
        └── test/
            ├── cpp/
            │   └── OryonEmitterBackendTest.cpp
            └── java/com/murdoc820/hyperplay/engine/
                ├── OryonJitTranslatorTest.kt
                ├── FsrSpvAndCpuBackendTest.kt
                └── PortableExeScannerTest.kt
```
