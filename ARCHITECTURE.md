# EXEDRA64 (`com.murdoc820.hyperplay`) - PURE BIONIC DIRECT-SILICON ARCHITECTURE (ZVDS)

> **Name / Nombre:** the project was called HyperPlay until v1.2.0. Internal identifiers keep the
> old name on purpose so installs, saves and updates keep working: package
> `com.murdoc820.hyperplay`, `libhyperplay.so`, the `/sdcard/HyperPlay/` folder, `_HyperPlaySaves`,
> log tags and `debug.hyperplay.*` properties. / El proyecto se llamó HyperPlay hasta la v1.2.0.
> Los identificadores internos conservan el nombre antiguo a propósito para que instalaciones,
> partidas y actualizaciones sigan funcionando.
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
- **Steam DRM Stubbing (`NativeSteamStub.cpp`):** Stubs `steam_api.dll` and `steam_api64.dll` (`SteamAPI_Init`, `SteamAPI_RestartAppIfNecessary`), allowing portable games to execute without requiring the Steam client. 32-bit games get their own interface objects in low memory (`SteamEmu32.cpp`: Apps, User, Friends, Utils, UserStats, RemoteStorage), leaving the 64-bit path untouched.
- **Game-folder write guard:** paths that leave the `.exe` folder through `..\` (Unreal install root) are read/create only; delete, move and replace return `ERROR_ACCESS_DENIED`.

---

### 3. In-House JIT Recompiler `x86/x64 -> ARMv9 AArch64` (`OryonCpuTranslator.cpp` & `OryonEmitterBackend.cpp`)
Exedra64 implements its own native translation engine in C++20 registered by default as the active `CpuTranslationBackend`:
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

## 2. SUBSYSTEMS BUILT ON TOP OF THE 5 PILLARS / SUBSISTEMAS SOBRE LOS 5 PILARES

**English**
- **GPU Direct3D 11 (`gfx/D3D11GpuRenderer`, `gfx/DxbcToSpirv`, `gfx/SpirvBuilder`, `loader/D3D11ResourceModel`):** D3D11 calls are recorded and drawn on the Adreno with Vulkan; DXBC shaders are translated to SPIR-V. `loader/DxbcVm` and `loader/SoftRaster` are the CPU fallback.
- **Games with their own Vulkan (`loader/VulkanPassthrough`, `gfx/GuestSwapchain`):** the game draws into virtual swapchain images; every present is copied (or scaled, aspect-preserved, with black bars) to the real swapchain, which always has the native screen size.
- **HyperFG frame generation (`gfx/HyperFrameGen`):** motion-interpolated frames between real ones (for example 60 -> 120 FPS), with thermal protection.
- **Display (`gfx/PresentOptions`, `loader/Win32DisplayInfo`, `loader/WidescreenPatch`):** games always run at the native resolution, full screen and undistorted. Resolution, window mode and frame limit come from each game's own video options: `EnumDisplaySettings` and DXGI `GetDisplayModeList` report a list of real modes (720p, 900p, 1080p, fractions of the screen and native).
- **OpenGL (`gfx/GuestOpenGl`):** `opengl32.dll` and WGL over EGL / OpenGL ES 3.2, plus the GDI that SDL needs.
- **Windows x64 exceptions (`loader/GuestSeh`, `loader/Win64Context`):** `.pdata` and dynamic function tables, vectored handlers, `RtlUnwindEx`, `RtlVirtualUnwind`, `RtlRestoreContext`, as C++, Mono and CoreCLR expect.
- **.NET (`loader/ManagedHost`, `engine/ManagedRuntime.kt`):** Mono (Unity) and CoreCLR (.NET 6 self-contained games) run as ordinary Windows code on the translator.
- **Audio (`audio/FmodStudioHost`, `audio/FmodBank`, `audio/FsbVorbis`):** an in-house reader for FMOD Studio banks (events, buses, snapshots) over AAudio. FMOD itself is not included.
- **Frame pacing (`loader/FramePacing`) and FPS cap 60 / 90 / 120** chosen in the quick menu.
- **App (`LauncherScreen.kt`, `GameSessionActivity.kt`, `session/`):** console-style launcher, quick menu with controller navigation, Quick Resume, performance overlay, error reports (`diag/`) and in-app updates (`update/`).

**Español**
- **Direct3D 11 en la GPU (`gfx/D3D11GpuRenderer`, `gfx/DxbcToSpirv`, `gfx/SpirvBuilder`, `loader/D3D11ResourceModel`):** las llamadas D3D11 se graban y se dibujan en la Adreno con Vulkan; los shaders DXBC se traducen a SPIR-V. `loader/DxbcVm` y `loader/SoftRaster` son la alternativa por CPU.
- **Juegos con Vulkan propio (`loader/VulkanPassthrough`, `gfx/GuestSwapchain`):** el juego dibuja en imágenes virtuales; cada present se copia (o se escala conservando la proporción, con bandas negras) al swapchain real, que siempre tiene el tamaño nativo de la pantalla.
- **Generación de fotogramas HyperFG (`gfx/HyperFrameGen`):** fotogramas interpolados por movimiento entre los reales (por ejemplo, de 60 a 120 FPS), con protección térmica.
- **Pantalla (`gfx/PresentOptions`, `loader/Win32DisplayInfo`, `loader/WidescreenPatch`):** los juegos van siempre a la resolución nativa, a pantalla completa y sin deformarse. La resolución, el modo de ventana y el límite de fotogramas se eligen en las opciones de vídeo de cada juego: `EnumDisplaySettings` y `GetDisplayModeList` de DXGI ofrecen una lista de modos reales (720p, 900p, 1080p, fracciones de la pantalla y la nativa).
- **OpenGL (`gfx/GuestOpenGl`):** `opengl32.dll` y WGL sobre EGL / OpenGL ES 3.2, más el GDI que necesita SDL.
- **Excepciones de Windows x64 (`loader/GuestSeh`, `loader/Win64Context`):** `.pdata` y tablas dinámicas, manejadores vectoriales, `RtlUnwindEx`, `RtlVirtualUnwind` y `RtlRestoreContext`, como esperan C++, Mono y CoreCLR.
- **.NET (`loader/ManagedHost`, `engine/ManagedRuntime.kt`):** Mono (Unity) y CoreCLR (juegos .NET 6 autocontenidos) se ejecutan como código Windows normal en el traductor.
- **Audio (`audio/FmodStudioHost`, `audio/FmodBank`, `audio/FsbVorbis`):** lector propio de bancos de FMOD Studio (eventos, buses, snapshots) sobre AAudio. FMOD no se incluye.
- **Ritmo de fotogramas (`loader/FramePacing`) y tope de 60 / 90 / 120 FPS** elegido en el menú rápido.
- **App (`LauncherScreen.kt`, `GameSessionActivity.kt`, `session/`):** launcher estilo consola, menú rápido con navegación por mando, Quick Resume, overlay de rendimiento, informes de errores (`diag/`) y actualizaciones dentro de la app (`update/`).

---

## 3. PROJECT STRUCTURE / ESTRUCTURA DEL PROYECTO

```text
Exedra64/
├── ARCHITECTURE.md
├── CHANGELOG.md
├── README.md
├── docs/
├── server/report-worker/          (optional error-report server / servidor opcional de informes)
├── tools/managed-runtime/
├── settings.gradle.kts
├── build.gradle.kts
└── app/
    ├── build.gradle.kts
    └── src/
        ├── main/
        │   ├── AndroidManifest.xml
        │   ├── cpp/
        │   │   ├── CMakeLists.txt
        │   │   ├── jni_bridge.cpp
        │   │   ├── loader/   BionicPeLoader, PeHeaderInspector, NativeDllInterceptor, NativeSteamStub,
        │   │   │             BionicLibraryStager, GuestSeh, Win64Context, Win64ThunkAbi, ManagedHost,
        │   │   │             VulkanPassthrough, D3D11ResourceModel, D3D11Sampler, DxbcShader, DxbcVm,
        │   │   │             SoftRaster, Bc7Decoder, FramePacing, WidescreenPatch, Win32DisplayInfo,
        │   │   │             Win32KnownFolders, Win32PathUtils, Win32Registry, WinSafeHeap
        │   │   ├── cpu/      OryonEmitterBackend, OryonCpuTranslator, HyperAotCache,
        │   │   │             OryonSiliconScheduler, GuestProfiler
        │   │   ├── gfx/      DirectVulkanBridge, D3D11GpuRenderer, DxbcToSpirv, SpirvBuilder,
        │   │   │             GuestSwapchain, HyperFrameGen, GuestOpenGl, PresentOptions,
        │   │   │             FsrUpscalePass, TurnipLoader (+ SPIR-V headers)
        │   │   ├── audio/    DirectAAudioEngine, FmodStudioHost, FmodBank, FsbVorbis
        │   │   └── input/    DirectXInputBridge
        │   └── java/com/murdoc820/hyperplay/
        │       ├── MainActivity.kt, LauncherScreen.kt, GameSessionActivity.kt, QuickResumeService.kt
        │       ├── engine/   HyperPlayNative, GameLauncher, PortableExeScanner, ManagedRuntime, Turnip*
        │       ├── session/  EfficiencyMode, SessionOverlay, PerfMonitor, PerfOverlay, QuickResume
        │       ├── input/    ControllerManager, XInputPad*, VirtualTouch*
        │       ├── art/, audio/, diag/, model/, ui/, update/
        └── test/
            ├── cpp/OryonEmitterBackendTest.cpp
            └── java/com/murdoc820/hyperplay/...
```