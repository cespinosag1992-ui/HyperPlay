# HyperPlay - Logs del telefono

- Dispositivo: RedMagic 10 Pro NX789J, Android 16. ADB inalámbrico: el puerto cambia al reactivar la depuración; descubrir con `adb mdns services` (actual `192.168.0.9:33145`, antes `192.168.0.11:45023`).
- Paquete: com.murdoc820.hyperplay.
- Generado: 2026-11-17 17:48:28. Actualizado: 2026-11-17 20:30.
- Archivos de log de la app: 114.

## Corridas 2026-11-18 17:20-18:40 (HK en partida, rendimiento, mando nuevo) — ADB `192.168.0.9:33145`
| Corrida | Resultado | Notas |
|---|---|---|
| HK + MF bridge | intro en vídeo reproducida (H.264 por AMediaCodec) y partida en el Paso del Rey; el Caballero camina con la cruceta | `docs/capturas/2026-11-18_hollow_knight_intro_video_mf.png`, `..._partida_paso_del_rey.png` |
| HK partida, sin encadenado | 19-20 FPS · 50 ms; principal ~88 % CPU; perfil plano (despachador ~24 %) | mezclador de audio ~54 % de muestras |
| HK `tso 0` | primer Present +97 s (igual que con TSO); ~70 s sin presentar al cargar el Paso del Rey (también con TSO) | TSO no explica la carga |
| HK encadenado (1ª) | crash al arrancar Unity: `0xc0000005` en 0x1819a1c22 (puntero corrupto) | `exitSite` viejo tras fallo/suspensión -> enlace erróneo; arreglado |
| HK encadenado | primer Present +81 s; menú 58 FPS | — |
| HK encadenado + flags muertos | primer Present +74,5 s / +82,6 s; **partida 43 FPS · 23 ms** (antes 20); 31 000 enlaces en el hilo principal | una muestra intermedia dio 4 FPS (sin explicar; posible escena pesada) |
| Gate | `testDebugUnitTest lintDebug assembleDebug` BUILD SUCCESSFUL; suite nativa 1025/1025 | — |
| Mando minimalista | menú de HK completamente visible; LT/LB/L3 y RT/RB/R3 en los bordes, cruceta de pétalos, sticks tenues | `docs/capturas/2026-11-18_mando_minimalista_hk_menu.png` |

## Corridas 2026-11-18 16:13-17:03 (Hollow Knight: cuelgue tras el idioma) — ADB `192.168.0.11:45023`
| Corrida | Resultado | Causa / arreglo |
|---|---|---|
| HK + TSO, 150 s | se cuelga antes del logo; principal en `WaitOnAddress` @0x18051f65e, workers ociosos | TSO no lo arregla |
| HK, volcado de pilas | SIGSEGV del propio vigilante al leer el tope de la pila | lectura con `process_vm_readv` |
| HK, 150 s | `opcode 0F E7 no soportado` en 0x180e1028d; workers "finalizó con código -2" | MOVNTDQ/MOVNTPS |
| HK, 120 s | `opcode 0F 6B no soportado` | escáner `jit_scan` + lote SSE2 de enteros (suite 1013/1013) |
| HK, 145 s | logo Team Cherry, menú principal a 59-60 FPS, 0 errores de traducción | — |
| HK, mando | A (150 ms) -> perfil -> calibración -> brillo -> poema del prólogo; luego negro a 60 FPS | intro en vídeo: *"Initializing Microsoft Media Foundation failed"* |
| HK + MF bridge | sin resultado: el teléfono dejó de responder a ADB (10060) | — |

## Corridas 2026-11-18 09:30-12:30 (Cuphead: de crash a pantalla de título) — ADB `192.168.0.11:45023`
| Corrida | Resultado | Causa / arreglo |
|---|---|---|
| Cuphead 300 s | `opcode 0F C3 no soportado` tras `Begin MonoManager ReloadAssembly` | MOVNTI añadido |
| Cuphead 300 s | `opcode 0F 38 3E no soportado` tras *Input initialized* | tabla SSE4.1 38..40 reescrita (estaba mal mapeada) |
| Cuphead 300 s | 5 Present, pantalla negra; *"The AssetBundle 'StreamingAssets\AssetBundles\atlas_player' can't be loaded because it was not built with the right version or build target"* (y `tex_screen_fx`) + 2 NullReferenceException | memscan: tabla de bloques parseada `{u=0x20000, c=0x11ccf, flags=0}` (debía ser 3) -> `rol ax, 8` de 16 bits ejecutado como de 32 |
| Cuphead 300 s | bundles OK; `No se pudo traducir el bloque en RIP=0xbddca: RIP fuera del rango` desde 0x674b5365dc (steam_api64.dll real, emulador) | Steam: nunca cargar la DLL real; imports de CSteamworks en modo sin Steam |
| Cuphead 300 s | vivo, 0 excepciones, `[Steamworks.NET] SteamAPI_Init() failed`, `Build version 1.3.4`, **título visible** (desplazado media pantalla) | — |
| Cuphead 600 s | vivo, draws 497 -> 1740, título **animado** a 4 FPS (252 ms), Rasterizador sin rechazados | los draws se paran ~5 min durante la carga de escena (no es cuelgue: hilos en `WaitForSingleObjectEx`, JIT activo en 0x180f3d3b0/0x1808ed188) |

| Volcados RT Cuphead (draws 1400-1412) | escena correcta ("Press Any Button"), cadena de post-proceso y backbuffer 1344x608 correctos; la pantalla mostraba la imagen desplazada media altura | bug de rotación en `presentPixels` (ew/eh cruzados en 90/270) |
| Cuphead 620 s tras el arreglo | vivo, 0 excepciones, **título a pantalla completa** con "Press Any Button" (captura `docs/capturas/2026-11-18_cuphead_titulo_completo.png`); overlay FPS 0 en ese instante y **CPU a 87 °C** (posible estrangulamiento térmico: vigilar) | — |
| HK 200 s tras el arreglo | menú de idiomas a **pantalla completa** (English..한국어), FPS 4 · 255 ms | captura `docs/capturas/2026-11-18_hollow_knight_menu_completo.png` |

| Perfil Cuphead 290 s (`debug.hyperplay.prof`) | 4,6 núcleos de media; ~85 % de muestras en el rasterizador por software (`dxbc::Vm`, `sample2D`, `texel8`), 9,3 % en código del juego | ruta GPU prioritaria |
| Cuphead `debug.hyperplay.noraster 1` | "Build version" a +37,1 s (con raster +150 s); 3000 Present en 120 s | la carga avanza por frame |
| Perfil sin raster 40 s | 1,62 núcleos; top: LZ4 de UnityPlayer (+0xb852c2), +0xf3cedf, +0x8ed3b0, memcpy (+0x10c52b0); `executeEntry`+`suspensionPoint` 18 %, `presentPixels` 6,5 % | — |
| `dxbc_spirv_test` (342 shaders HK+Cuphead) | 341 traducidos, **341/341 válidos con spirv-val vulkan1.1**; 1 fallo: opcode 111 (`ps_091` de HK) | — |

| HK 120 s ruta GPU | `Render D3D11: ruta GPU (Vulkan) activa`; `GPU D3D11: draws=74377 fallidos=0 pipelines=5 subidas=10 (6.3 MB) presents=2400`; overlay **FPS 122 · 8,2 ms**, CPU 26 %, GPU 14 % | — |
| Cuphead 120 s ruta GPU (1ª) | 13 800 presents, 160 FPS, pero escena negra; 1 subida de ~3 MB por frame | `CopySubresourceRegion(origen GPU)` sobre el RT 1280x608 fmt 27 se hacía en CPU |
| Cuphead 75 s ruta GPU (copias en GPU) | título completo, **157 FPS · 6,3 ms**, CPU 38 %, GPU 42 %, 140 subidas en total; `Build version` +36,1 s | — |
| Gate + suite | gate verde; `OryonEmitterBackendTest: 954 comprobaciones, 0 fallos` | — |
| HK 110 s, mando Xbox táctil | `input swipe` (pulsación 400 ms) en cruceta abajo x2 y en A: la selección baja dos idiomas y se abre "是否确认?"; 124-125 FPS | mando OK |
| Cuphead 70 s, mando Xbox táctil | `input tap` en A sobre "Press Any Button": sin respuesta | Rewired cae a la entrada de Unity: no lee nuestro XInput |
| Cuphead 70 s, 120 Hz + FIFO | overlay **FPS 118 · 8,5 ms**, CPU 34 %, GPU 31 %, título correcto; gate verde | pacing estable |

Otras líneas: `FMOD failed to initialize the output device, attempting to initialize the null output.`; `Rewired: [WARNING] Unable to initialize input source! ... will fall back to Unity input.`; `Fallback handler could not load library D:/Cuphead_Data/Mono/data-0000006C86BBF8B0.dll` (x3, P/Invoke de Mono, sin efecto visible). Volcado de RT: los 4 pases por frame son blits a pantalla completa (NDC ±1, UV 0..1); el backbuffer sale volteado en Y respecto al RT previo (flip normal de Unity).

## Corridas 2026-11-18 07:00-09:30 (rasterizador por software, primera imagen) — ADB `192.168.0.11:45023`

| Hora | Build | Hollow Knight |
|---|---|---|
| 07:10 | histograma de slots | `D3D11 DeviceContext llamadas por slot (hasta Present #600): 7:10794 8:22177 9:4802 10:18580 11:4802 12:19174 14:28204 ...`: solo `DrawIndexed` (~32/frame), VB dinámicos con Map. 12 shaders volcados (SM4.0). |
| 07:25 | AV intermitentes | `0xc0000005 en RIP=0x18149214d (addr=0x435f6e69505f747c)` (puntero = texto "|t_Pin_C"); `RIP=0x1811d8030 (addr=0x8605c81d0c)` tras `QueryDisplayConfig(0x4, ...)` (índice basura `RSI=0x69572072`) -> QueryDisplayConfig real. |
| 07:55 | rasterizador conectado | Draws a un RT de **1x1** y blit al backbuffer con viewport 1216x1216: registro `Screenmanager Resolution Width/Height = 1`, `Use Native = 0`. Registro borrado -> resolución nativa. |
| 08:30 | viewport | `RSSetViewports(1): 0.00 0.00 2688.00 0.00 0.00 1.00` (alto 0) en el primer frame; 786 avisos NaN -> test SSE: `movss r,r`, `cmpltss`, `cvtss2si` fallan. Tras corregir: 0 avisos NaN, viewport 2688x1216. `Rasterizador: formato de textura 98 no soportado` (BC7). |
| 08:50 | 2688x1216, 1 hilo | Un pase a pantalla completa: `px=3268608 ... 819.78 ms`; 0 Present en el tiempo de prueba. |
| 09:10 | 1344x608 + 6 hilos | `Draw: ... 1344x608 ... px=1627843 ... 557.44 ms`; **primera imagen: menú de idiomas (English/Français/Deutsch/Español/Italiano)**. |
| 09:30 | -O2 en el motor nativo | Mismo draw `123.54 ms`; `Rasterizador: draws=20857 ... px=2709423718`; overlay **FPS 4 · 262 ms**, 0 excepciones. |

Suite nativa: `793 comprobaciones, 0 fallos`.

## Corridas 2026-11-18 04:05-06:45 (TLS dinámico, rendimiento, sincronización, primer Present) — ADB `192.168.0.11:45023`

| Hora | Build | Hollow Knight |
|---|---|---|
| 04:08 | TLS dinámico | `TLS dinámico: mono-2.0-bdwgc.dll -> índice 1 (56064 bytes)`; `WASAPI: Initialize(share=0 flags=0x0 buffer=400000 hns) -> 1920 frames`; `WASAPI: bombeo iniciado (48000 Hz, 2 canales pcm16, ..., AAudio=1)`; `Steam initializing` / `Steam failed to initialize` / `Selected online subsystem SteamOnlineSubsystem`; escena: `Couldn't find a UIManager` x11. Sin Present. Hilo principal ~2 M bloques/s. |
| 05:55 | contadores | Por cada 2 M bloques: 300-1800 ms, `lento=+100k..+380k` con `smc=+100`: cada fallo SMC vaciaba toda la caché rápida. |
| 06:00-06:10 | invalidación selectiva + caché 64 K + thunks sin locks | 80-160 ms por 2 M bloques (15-25 M/s). Aparece AV `RIP=0x6aa4d26d9c` (escritura de Mono en su código RWX) por carrera SMC y bucle de `TerminateProcess(0xc0000417)` hasta `RSP ... fuera de la pila`. |
| 06:15-06:30 | carrera SMC + CV/SRW/WaitOnAddress | 0 excepciones; congelado en `Unloading 5 Unused Serialized files`. Vigilante: workers en `WaitOnAddress` (normal) y el hilo principal muerto: `opcode 0F B8 no soportado` (POPCNT), luego `opcode 0F C5 no soportado` (PEXTRW). |
| 06:45 | POPCNT + PINSRW/PEXTRW | **`DXGI Present #1 ... #600: 2688x1216 -> Vulkan OK (pixel0 RGBA=00000000)`**, overlay `FPS 4 · 251 ms`, pantalla negra (sin rasterizador). 786 x `The bounds contain one of the following values: NaN, float.PositiveInfinity...`. Vivo a 300 s, 0 excepciones. |

Suite nativa: `668 comprobaciones, 0 fallos`. Benchmark aislado: 38 M bloques/s.

## Corridas 2026-11-18 01:40-02:35 (atómicos, SMC, VirtualAlloc reserva) — ADB `192.168.0.11:45023`

| Hora | Build | Hollow Knight |
|---|---|---|
| ~02:00 | LOCK CASAL + SMC + SleepEx | `DXGI swapchain: backbuffer 2688x1216 formato 28`; `Using XInput`; `Input System polling thread started`; `QueryInterface(IDXGISwapChain2) failed (80004002)`. Después AV con punteros que son bytes x86: `0xc0000005 en RIP=0x6aab214321 (addr=0x48000000f890ff0e)` (Boehm) y `RCX=0x480000010090ff06`. |
| 02:30 | reserva solapada -> 487 | **0 `GuestException`**, 0 MessageBox. Consola: `Loaded All Assemblies, in 60.226 seconds`, PhysX 4.1.2, input XInput, `Restored language code 'EN'`, `PlayerPrefsException: Could not store preference value` (x2), `Unloading 14 unused Assets`, luego `[Physics::Module] Cleanup current backned` e `Input System module state changed to: Shutdown`. 0 `DXGI Present #`. 2 hilos de UnityPlayer (`0x1819c7df4`) mueren: `RIP fuera del rango` tras `GetModuleHandleExW(4, ...)` (bug de FROM_ADDRESS, corregido). 520 M bloques JIT en ~150 s. |

| 02:44 | GetModuleHandleExW FROM_ADDRESS + log de primeras llamadas | Igual: `ShowWindow(SW_SHOWDEFAULT)`, `timeBeginPeriod`, consultas DXGI y a los 2,5 s del bucle principal `Cleanup current backned`; al final `APP CRASH(NATIVE)` status=6 durante el desmontaje. |
| 02:59 | vigilancia `debug.hyperplay.watch=0x18005cf40` | `[Watch] RIP=0x18005cf40 ejecutada (tid=5259) RCX=0x0 ... [RSP]=0x6dd2115e3a` -> `Application.Quit(0)` llamado desde JIT de Mono. Assembly-CSharp: `DesktopPlatform.CreateOnlineSubsystem` -> `Multiple online subsystems packaged.` -> `Application.Quit`. Stubs vistos: `kernel32.dll!GetFileAttributesExWW` (4), `kernel32.dll!FindFirstFileExWW` (4), `bcrypt.dll!UnityPluginLoad`, `ADVAPI32.dll!RegQueryValueExA` (23), `ntdll.dll!RtlGrowFunctionTable` (2412). En disco solo existe `Plugins/x86_64/steam_api64.dll`. |

| 03:05 | GetProcAddress -> NULL | Atasco en `Begin MonoManager ReloadAssembly`: `Mono assertion formateada: * Assertion: should not be reached at ...mono\mini\mini-windows.c:1153` (sin `RtlAddGrowableFunctionTable`/`RtlInstallFunctionTableCallback`). NULL también para `mono_profiler_startup`, `UnityPluginLoad` en bcrypt, `SetThreadErrorMode`, etc. |
| 03:18 | exportaciones Rtl*/kernel32 reales | Pasa ReloadAssembly; PhysX, XInput, swapchain 2688x1216. Ya no aparece `Multiple online subsystems packaged.` Luego `[GuestException] 0xc0000005 en RIP=0x6dcbf41fda (addr=0x10) sin manejar` dentro de `DllMain anidado de steam_api64.dll finalizo: -4` y `HyperPlaySession: sesión terminada: código -3`. |

| 03:26 | WASAPI + Steam sin cliente | `CoCreateInstance(MMDeviceEnumerator) -> ... (WASAPI -> AAudio)`; FMOD hace QI de `IMMEndpoint` (`{1BE09788-...}`), recibe E_NOINTERFACE y desreferencia NULL: `0xc0000005 en RIP=0x1817ba095 (addr=0x0)` -> `TerminateProcess(0xc0000417)`. |
| 03:42 | IMMEndpoint | 0 excepciones, vivo a 300 s; `WASAPI: IMMDevice::Activate(IAudioClient) -> 0x6f06b35790`. Un hilo de Mono muere: `RIP=0x6cb8e9cc14` (= `SteamAPI_Init` host sin registrar como trampolín; bytes `48 b8 14 cc e9 b8 6c 00 00 00 ... ff d0`). |
| 03:51 | trampolines Steam | 740 M bloques; FMOD escribe por `IAudioRenderClient` y espera eventos. 2 M líneas de `[UnityHook] MemoryManager`. AV `RIP=0x180310a0b (addr=0x0)`: `call *(%rax)` sobre el global `0x181f4f930` con vtable nula (magic static MSVC dado por construido) -> VEH de Mono -> `TerminateProcess(0xc0000417)` -> sesión -4. Causa: DLL dinámicas sin índice TLS propio. |

Suite nativa: `622 comprobaciones, 0 fallos` -> `656 comprobaciones, 0 fallos` (registro).

## Corridas 2026-11-18 00:50-01:35 (VEH, GC de Mono, rendimiento) — ADB `192.168.0.11:45023`

| Hora | Build | Hollow Knight |
|---|---|---|
| 00:50 | VEH real | `AddVectoredExceptionHandler(first=1, handler=0x6aa96530a0)`; `[GuestException] 0xc0000005 en RIP=0x6ddf630a36 (addr=0x18) -> VEH devuelve -1`; consola: `NullReferenceException: Object reference not set to an instance of an object`; vivo a 150 s y a 6 min (antes moría a ~116 s). Pantalla negra (FPS 0). |
| 01:02 | caché rápida | 587.623 `MessageBoxA[Fatal error in GC]: SuspendThread loop failed` (hilo 15493). |
| 01:08 | traza de SuspendThread | `SuspendThread(0x7e004245): tid=16965 sin backend invitado`; el hilo 16965 había muerto: `No se pudo traducir el bloque en RIP=0x6dd93cb820: RIP fuera del rango de código PE / DLLs` (JIT de Mono en VirtualAlloc no visible para hilos). |
| 01:16 | rangos de proceso | 0 MessageBox, 0 SuspendThread fallidos, 0 hilos muertos. Consola parada en `Dedicated video D3D11 device creation failed (0x80004002)`. Hilo de audio FMOD (`0x1817b2c..`, float->PCM16) a ritmo de tiempo real (~250 K bloques/s), no es lentitud. Benchmark aislado del despachador: 80,6 M bloques/s. `simpleperf`: eventos `cpu-cycles`/`cpu-clock` no soportados en este dispositivo. |
| 01:23 | invalidación selectiva | `- Loaded All Assemblies, in 53.379 seconds`; 4 excepciones entregadas (`devuelve -1`); después `Couldn't extract exception string ... another NullReferenceException was thrown while processing the stack trace` y `ExitProcess(1)` desde el VEH de Mono (por eso `devuelve -2`). |
| 01:31 | VirtualQuery real | Un hilo de Mono saltó a `RIP=0x70a5456039` (región `rw-p` anónima de 16 KB del host) -> muerto; no reproducido después. |
| 01:35 | diagnóstico de traducción | Hilo principal: `0F 3A opcode no soportado`, bytes `66 0F 3A 09 C0 02` = `ROUNDPD xmm0,xmm0,2` -> implementado (`testSse41Round`). |

Suite nativa: `611 comprobaciones, 0 fallos`, `EXIT:0`.

## Corridas 2026-11-17 19:24-20:27 (D3D11 real, Present Vulkan, suspensión de hilos, VirtualAlloc) — ADB `192.168.0.11:45023`

Herramientas: `run_game.sh` en `/data/local/tmp` (lanza, espera, `screencap`, `logcat -d -f`, filtra en el teléfono; filtrar en Windows un log de 600 MB no es viable), `dumpsys activity exit-info com.murdoc820.hyperplay`, `dumpsys dropbox --print data_app_native_crash`, `run-as com.murdoc820.hyperplay cat files/guest_crash.txt`.

| Hora | Build | Resultado en Hollow Knight |
|---|---|---|
| 19:24 | D3D11 reales | Vivo 60 s, `failed to create 3D texture = 0` (antes `[80004001]`). `surfaceCreated: attach=false` (Vulkan sin inicializar). Sin swapchain todavía (Mono en `ReloadAssembly`). |
| 19:38 | `attachWindow` inicializa Vulkan | Bucle `SuspendThread(0xfffffffffffffffe)` -> `GetThreadContext` (stub) -> `ResumeThread` -> `MessageBoxA`: 410.553 iteraciones; log de 931 MB en 2 min. |
| 19:56 | Suspensión real de hilos | `surfaceCreated: attach=true`, sin bucle de suspensión. Muere a los ~116 s: `exit-info reason=5 APP CRASH(NATIVE) status=11`; tombstone: `SIGSEGV SEGV_MAPERR fault 0x18` en `/memfd:oryon_jit` (hilo `hyperplay-direc`, `executeEntry+728`). |
| 20:15-20:21 | Informe de crash vía libsigchain | `status=4`: `SIGILL ILL_ILLOPC` en `liblog.so GetDefaultTag()+0` del hilo de UI (Compose `PerfOverlay`): texto de liblog pisado. Mono registra `AddVectoredExceptionHandler(first=1, handler=0x6aa36010a0)`; Unity/CRT `SetUnhandledExceptionFilter(0x1400017d0, 0x180cbabb0, 0x1809f5680, 0x6aa6288d00)`. |
| 20:27 | `VirtualAlloc` sin `MAP_FIXED` ni `mprotect` ajeno | `win_VirtualAlloc: rango 0x708d25a000+0x10000 ocupado -> ERROR_INVALID_ADDRESS` (zona de liblog) y `0x6dd9ac0000+0x10000 ocupado`. Sin SIGILL. `SIGSEGV` del huésped capturado en `guest_crash.txt`. |

`files/guest_crash.txt` (20:27):
```
=== HyperPlay guest crash ===
signal 000000000000000b fault 0000000000000018 tid 0000000000005f31
RIP(bloque) 0000006dd7f1ea25
RAX=0000000000000002 RCX=0000006db5dfd510 RDX=0000000000000003 RBX=0000000000000000
RSP=0000006c8d5707c0 RBP=0000006c8d570880 RSI=0000000000000000 RDI=0000000000000001
R8 =0000000000000003 R9 =0000000000000000 R10=0000006aa7633000 R11=000000708d27e220
R12=0000006c8d571190 R13=0000006c8d570ee0 R14=0000006db5dc7d20 R15=0000006db5dece00
bytes@RIP: 48 63 45 98 03 c6 89 45 b0 48 8b 4d 80 48 8b 51 18 48 63 4a 18 3b c1 0f 8d 3f 02 00 00 48 8d 6d 00 49 bb d0 bf 6b a7 6a 00 00 00 41 ff d3 48 8b
```
- Los GPR son los del inicio del bloque (en el fallo `x2`=RDX=0 según el tombstone).
- Código JIT de Mono: `movsxd rax,[rbp-0x68]; add eax,esi; mov [rbp-0x50],eax; mov rcx,[rbp-0x80]; mov rdx,[rcx+0x18]; movsxd rcx,[rdx+0x18]` (longitud de un array nulo) -> `NullReferenceException` que Mono espera recibir por su VEH.
- **Siguiente trabajo:** entrega de excepciones al huésped (VEH/SEH) con `CONTEXT` y RIP exactos.

Suite nativa: `591 comprobaciones, 0 fallos`, `EXIT:0`. Gate Gradle: `BUILD SUCCESSFUL`.

## Corrida 2026-11-17 19:20 (WinSafeHeap por clases de tamaño) — ADB `192.168.0.11:45023`

- `oryon_test`: `531 comprobaciones, 0 fallos`, `EXIT:0`.
- Hollow Knight: vivo 60 s (RSS 357 -> 475 MB, estable), `Direct3D 11.0 [level 11.0]`, `Renderer: Qualcomm Adreno 830`, `Begin MonoManager ReloadAssembly`, PlayerLoop. `Fatal signal=0`, `ExitProcess=0`.
- Cuphead: vivo 80 s, `Direct3D 11.0`, RSS congelada en 298.936 kB tras `ReloadAssembly` + `mono-dl.c:125`.

## Corrida 2026-11-17 18:53 (GetTempPathW WCHAR)

- Hollow Knight (`%TEMP%\opencode\logcat_wchar_HK.txt`): `win_GetTempPathW -> C:\users\default\AppData\Local\Temp\` (`RAX=0x24`), ya no `'D'`. El proceso desaparece a los ~6 s tras `Mono config path` sin `Fatal signal`/`ExitProcess`; el log muestra 28.128 `win_HeapAlloc` de 16-65 B, cada uno con un `mmap` propio. Corregido con `WinSafeHeap` por clases de tamaño; **pendiente de verificar** porque ADB Wi-Fi se desconectó (`10061`, conexión rechazada).

## Corrida 2026-11-17 18:40 (thunks 12 args, SHLWAPI, SETcc, Map)

- Hollow Knight (`%TEMP%\opencode\logcat_utf8_HK.txt`): `Forcing GfxDevice: Null=0`. Unity crea `GfxDevice` D3D11 (`kGfxThreadingModeThreaded`), llama `CreateDXGIFactory2` y `D3D11CreateDevice(adapter, driverType=0, flags=0x20, levels=4, sdk=7, ...)` -> `S_OK` nivel `0xB000`; 121 llamadas a `ID3D11DeviceContext`. `Fatal signal=0`, `SIGABRT=0`, `ExitProcess=0`. Termina en silencio tras `GetFileAttributesExW('...unity default resources.resG')` (carácter extra de origen invitado).
- Cuphead (`logcat_utf8_Cuphead.txt`): `PathCanonicalizeW` real, `Mono path[0] = 'D:/Cuphead_Data/Managed'`, `boot.config` encontrado, `CreateWindowExW(style=0xca0000, 320x240)`, `D3D11CreateDevice` real, `CreateDirectoryW` crea las carpetas de guardado, llega a `Begin MonoManager ReloadAssembly`; vivo a los 80 s.
- Causa de `Forcing GfxDevice: Null`: `SETcc m8` del JIT almacenaba el byte bajo de la dirección (flag `nographics` = `0x31`). Corregido y cubierto por `testSetccMemory`.
- Suite nativa en silicio: `514 comprobaciones, 0 fallos`, `EXIT:0`. Gate Gradle: `BUILD SUCCESSFUL`.
- Pendiente: texturas/SRV D3D11 devuelven `E_NOTIMPL` (`failed to create 3D texture ... [80004001]`).

## Resumen de la ultima corrida

- WINMM: `timeGetTime` real, `timeBeginPeriod`/`timeEndPeriod` y `timeGetDevCaps` implementados; `isNoisy` filtra timers y critical sections por nombre exacto.
- `GetConsoleCP` devuelve `65001`; `SystemFunction036`/`RtlGenRandom` registrados tambien en BCRYPT.
- `X18` eliminado como scratch en el JIT: BT/BTS/BTR/BTC y SHLD/SHRD usan X23; CMPXCHG8B/16B y CMPS usan X24.
- Gate: `443 comprobaciones, 0 fallos`; Gradle `GATE_EXIT=0`, `BUILD SUCCESSFUL in 27s`; APK 39.821.624 bytes instalado.
- Hollow Knight PID `23072`: sin VEX 3B/2B no soportado, sin `Fatal signal`, `SIGABRT`, `ExitProcess` ni `TerminateProcess`. Sigue `Forcing GfxDevice: Null`; no se invoca D3D11/DXGI.
- Cuphead PID `25214`: `IAT: 67 entradas, 67 resueltas, 0 sin resolver`; motor directo en ejecucion continua.

## Indice

| Archivo | Bytes | Ultima modificacion |
|---|---:|---|
| Cuphead-20261117-174230.log | 866 | 2026-11-17 17:42:31 |
| Hollow_Knight-20261117-171139.log | 878 | 2026-11-17 17:11:39 |
| Hollow_Knight-20261117-171126.log | 878 | 2026-11-17 17:11:26 |
| Cuphead-20261117-171112.log | 975 | 2026-11-17 17:11:13 |
| Hollow_Knight-20261117-161656.log | 878 | 2026-11-17 16:16:56 |
| Hollow_Knight-20261117-155608.log | 2366 | 2026-11-17 15:56:13 |
| Hollow_Knight-20261117-155355.log | 2366 | 2026-11-17 15:54:00 |
| Hollow_Knight-20261117-155152.log | 2366 | 2026-11-17 15:52:02 |
| Hollow_Knight-20261117-154947.log | 2366 | 2026-11-17 15:49:57 |
| Hollow_Knight-20261117-154518.log | 2366 | 2026-11-17 15:45:28 |
| Hollow_Knight-20261117-153924.log | 2366 | 2026-11-17 15:39:32 |
| Hollow_Knight-20261117-153600.log | 2366 | 2026-11-17 15:36:05 |
| Hollow_Knight-20261117-153040.log | 2366 | 2026-11-17 15:30:45 |
| Hollow_Knight-20261117-152721.log | 2366 | 2026-11-17 15:27:26 |
| Hollow_Knight-20261117-152516.log | 954 | 2026-11-17 15:25:26 |
| Hollow_Knight-20261117-152122.log | 954 | 2026-11-17 15:21:27 |
| Hollow_Knight-20261117-151238.log | 999 | 2026-11-17 15:12:43 |
| Hollow_Knight-20261117-150326.log | 999 | 2026-11-17 15:03:31 |
| Hollow_Knight-20261117-145900.log | 954 | 2026-11-17 14:59:10 |
| Hollow_Knight-20261117-145722.log | 954 | 2026-11-17 14:57:33 |
| Hollow_Knight-20261117-145337.log | 954 | 2026-11-17 14:53:53 |
| Hollow_Knight-20261116-174205.log | 988 | 2026-11-16 17:42:23 |
| Hollow_Knight-20261116-173558.log | 878 | 2026-11-16 17:35:58 |
| Hollow_Knight-20261116-172737.log | 878 | 2026-11-16 17:27:37 |
| Hollow_Knight-20261116-171144.log | 878 | 2026-11-16 17:11:44 |
| Hollow_Knight-20261116-170759.log | 1001 | 2026-11-16 17:08:02 |
| Hollow_Knight-20261116-170339.log | 995 | 2026-11-16 17:03:43 |
| Hollow_Knight-20261116-163428.log | 878 | 2026-11-16 16:34:28 |
| Hollow_Knight-20261116-162802.log | 878 | 2026-11-16 16:28:02 |
| Hollow_Knight-20261116-162357.log | 954 | 2026-11-16 16:23:59 |
| Hollow_Knight-20261116-161508.log | 1003 | 2026-11-16 16:15:09 |
| Hollow_Knight-20261116-161023.log | 1000 | 2026-11-16 16:10:24 |
| Hollow_Knight-20261116-160335.log | 1000 | 2026-11-16 16:03:36 |
| Hollow_Knight-20261116-155918.log | 1000 | 2026-11-16 15:59:19 |
| Hollow_Knight-20261116-154721.log | 954 | 2026-11-16 15:47:22 |
| Hollow_Knight-20261116-154153.log | 954 | 2026-11-16 15:41:54 |
| Hollow_Knight-20261116-153831.log | 954 | 2026-11-16 15:38:31 |
| Hollow_Knight-20261116-151535.log | 878 | 2026-11-16 15:15:35 |
| Hollow_Knight-20261116-145911.log | 878 | 2026-11-16 14:59:11 |
| Hollow_Knight-20261116-143423.log | 878 | 2026-11-16 14:34:23 |
| Hollow_Knight-20261116-143249.log | 878 | 2026-11-16 14:32:49 |
| Hollow_Knight-20261116-141111.log | 1000 | 2026-11-16 14:11:12 |
| Hollow_Knight-20261116-123905.log | 878 | 2026-11-16 12:39:05 |
| Hollow_Knight-20261116-122347.log | 1000 | 2026-11-16 12:23:47 |
| Hollow_Knight-20261116-122144.log | 1000 | 2026-11-16 12:21:44 |
| Hollow_Knight-20261116-121916.log | 1000 | 2026-11-16 12:19:16 |
| Hollow_Knight-20261116-121325.log | 1000 | 2026-11-16 12:13:26 |
| Hollow_Knight-20261116-120413.log | 972 | 2026-11-16 12:04:14 |
| Hollow_Knight-20261116-115916.log | 878 | 2026-11-16 11:59:16 |
| Hollow_Knight-20261116-115732.log | 878 | 2026-11-16 11:57:32 |
| Hollow_Knight-20261116-115335.log | 2549 | 2026-11-16 11:53:36 |
| Hollow_Knight-20261116-114411.log | 2549 | 2026-11-16 11:44:11 |
| Hollow_Knight-20261116-114136.log | 1009 | 2026-11-16 11:41:37 |
| Hollow_Knight-20261116-113801.log | 999 | 2026-11-16 11:38:01 |
| Hollow_Knight-20261116-112247.log | 1000 | 2026-11-16 11:22:47 |
| Hollow_Knight-20261116-111952.log | 878 | 2026-11-16 11:19:52 |
| Hollow_Knight-20261116-102119.log | 878 | 2026-11-16 10:21:19 |
| Hollow_Knight-20261116-100556.log | 878 | 2026-11-16 10:05:56 |
| Hollow_Knight-20261116-100414.log | 983 | 2026-11-16 10:04:16 |
| Hollow_Knight-20261116-100059.log | 999 | 2026-11-16 10:00:59 |
| Hollow_Knight-20261116-095550.log | 878 | 2026-11-16 09:55:50 |
| Hollow_Knight-20261116-094050.log | 878 | 2026-11-16 09:40:51 |
| Hollow_Knight-20261116-085516.log | 878 | 2026-11-16 08:55:16 |
| Hollow_Knight-20261116-082420.log | 878 | 2026-11-16 08:24:20 |
| Hollow_Knight-20261116-075057.log | 878 | 2026-11-16 07:50:57 |
| Win32-20261115-233843.log | 765 | 2026-11-15 23:38:43 |
| Cuphead-20261115-233838.log | 1008 | 2026-11-15 23:38:38 |
| Win32-20261115-233836.log | 765 | 2026-11-15 23:38:36 |
| Cuphead-20261115-233827.log | 1008 | 2026-11-15 23:38:27 |
| Hollow_Knight-20261115-233824.log | 878 | 2026-11-15 23:38:24 |
| Cuphead-20261115-233813.log | 1008 | 2026-11-15 23:38:13 |
| Hollow_Knight-20261115-231704.log | 878 | 2026-11-15 23:17:04 |
| Hollow_Knight-20261115-230954.log | 999 | 2026-11-15 23:09:55 |
| Hollow_Knight-20261115-230552.log | 999 | 2026-11-15 23:05:52 |
| Hollow_Knight-20261115-230302.log | 878 | 2026-11-15 23:03:02 |
| Hollow_Knight-20261115-225748.log | 878 | 2026-11-15 22:57:48 |
| Hollow_Knight-20261115-225000.log | 999 | 2026-11-15 22:50:01 |
| Hollow_Knight-20261115-223932.log | 878 | 2026-11-15 22:39:32 |
| Hollow_Knight-20261115-223523.log | 878 | 2026-11-15 22:35:23 |
| Hollow_Knight-20261115-222911.log | 878 | 2026-11-15 22:29:11 |
| Hollow_Knight-20261115-222635.log | 878 | 2026-11-15 22:26:35 |
| Hollow_Knight-20261115-222240.log | 1159 | 2026-11-15 22:22:40 |
| Hollow_Knight-20261115-221425.log | 878 | 2026-11-15 22:14:25 |
| Hollow_Knight-20261115-220817.log | 878 | 2026-11-15 22:08:18 |
| Hollow_Knight-20261115-215911.log | 878 | 2026-11-15 21:59:11 |
| Hollow_Knight-20261115-214921.log | 878 | 2026-11-15 21:49:21 |
| Hollow_Knight-20261115-214224.log | 878 | 2026-11-15 21:42:25 |
| Hollow_Knight-20261115-212710.log | 999 | 2026-11-15 21:27:10 |
| Hollow_Knight-20261115-211844.log | 878 | 2026-11-15 21:18:44 |
| Hollow_Knight-20261115-211239.log | 1159 | 2026-11-15 21:12:39 |
| Hollow_Knight-20261115-210837.log | 878 | 2026-11-15 21:08:37 |
| Hollow_Knight-20261115-210106.log | 1000 | 2026-11-15 21:01:06 |
| Cuphead-20261115-203210.log | 1008 | 2026-11-15 20:32:10 |
| Win32-20261115-203205.log | 765 | 2026-11-15 20:32:05 |
| Hollow_Knight-20261115-202943.log | 999 | 2026-11-15 20:29:43 |
| Hollow_Knight-20261115-202430.log | 999 | 2026-11-15 20:24:31 |
| Hollow_Knight-20261115-201236.log | 999 | 2026-11-15 20:12:36 |
| Hollow_Knight-20261115-195410.log | 878 | 2026-11-15 19:54:10 |
| Hollow_Knight-20261115-195143.log | 878 | 2026-11-15 19:51:43 |
| Hollow_Knight-20261115-194544.log | 1023 | 2026-11-15 19:45:44 |
| Hollow_Knight-20261115-193738.log | 980 | 2026-11-15 19:37:38 |
| Hollow_Knight-20261115-185339.log | 1000 | 2026-11-15 18:53:39 |
| Hollow_Knight-20261115-185138.log | 999 | 2026-11-15 18:51:38 |
| Hollow_Knight-20261115-184845.log | 999 | 2026-11-15 18:48:45 |
| Hollow_Knight-20261115-184529.log | 999 | 2026-11-15 18:45:30 |
| Hollow_Knight-20261115-184356.log | 977 | 2026-11-15 18:43:56 |
| Hollow_Knight-20261115-183342.log | 933 | 2026-11-15 18:33:42 |
| Hollow_Knight-20261115-170855.log | 2746 | 2026-11-15 17:08:55 |
| Hollow_Knight-20261115-170643.log | 884 | 2026-11-15 17:06:43 |
| Hollow_Knight-20261115-170532.log | 884 | 2026-11-15 17:05:32 |
| Hollow_Knight-20261115-162642.log | 884 | 2026-11-15 16:26:42 |
| Hollow_Knight-20261115-162257.log | 884 | 2026-11-15 16:22:57 |
| Win32-20261115-162252.log | 927 | 2026-11-15 16:22:52 |
| Cuphead-20261115-162244.log | 860 | 2026-11-15 16:22:44 |

## Ultimos logs de la app

### Hollow_Knight-20261117-171139.log

```text
[hyperplay] ========================================================
[hyperplay] INICIANDO MOTOR NATIVO DIRECTO BIONIC (ZVDS)
[hyperplay] Silicio: Qualcomm Snapdragon 8 Elite (SM8750) Pure 64-bit ARMv9
[hyperplay] Afinidad: 2x Oryon Prime Cores (cpu6, cpu7 @ 4.32GHz) LOCKED
[hyperplay] Recompilador JIT: OryonEmitterBackend (x86/x64 -> AArch64)
[hyperplay] GPU: DirectVulkanBridge (120Hz MAILBOX + FSR CAS Compute Shader)
[hyperplay] Audio: DirectAAudioEngine (48kHz LOW_LATENCY)
[hyperplay] Input: XInput Direct Struct in RAM (0.0ms)
[hyperplay] Ejecutable: /storage/emulated/0/PC games/Hollow Knight/hollow_knight.exe (64-BIT X64 -> ORYON 64 DIRECT, LAA=true, is64=true)
[hyperplay] ========================================================
[hyperplay] HyperAOT: bloques=0, cacheados=0, size=0B
[hyperplay] Motor Directo Bionic en ejecución continua (res=1280x720, limit=0).
```

### Cuphead-20261117-174230.log

```text
[hyperplay] ========================================================
[hyperplay] INICIANDO MOTOR NATIVO DIRECTO BIONIC (ZVDS)
[hyperplay] Silicio: Qualcomm Snapdragon 8 Elite (SM8750) Pure 64-bit ARMv9
[hyperplay] Afinidad: 2x Oryon Prime Cores (cpu6, cpu7 @ 4.32GHz) LOCKED
[hyperplay] Recompilador JIT: OryonEmitterBackend (x86/x64 -> AArch64)
[hyperplay] GPU: DirectVulkanBridge (120Hz MAILBOX + FSR CAS Compute Shader)
[hyperplay] Audio: DirectAAudioEngine (48kHz LOW_LATENCY)
[hyperplay] Input: XInput Direct Struct in RAM (0.0ms)
[hyperplay] Ejecutable: /storage/emulated/0/PC games/Cuphead/Cuphead.exe (64-BIT X64 -> ORYON 64 DIRECT, LAA=true, is64=true)
[hyperplay] ========================================================
[hyperplay] HyperAOT: bloques=0, cacheados=0, size=0B
[hyperplay] Motor Directo Bionic en ejecución continua (res=1280x720, limit=0).
```

## Hollow Knight - lineas clave

```text
11-17 17:11:49.464 23072 23144 I HyperPlay.DllInterceptor: [Guest Console] Initialize engine version: 6000.0.61f1 (74a0adb02c31)
11-17 17:11:49.471 23072 23144 I HyperPlay.DllInterceptor: [Guest Console] Forcing GfxDevice: Null
11-17 17:11:49.474 23072 23144 I HyperPlay.DllInterceptor: [Guest Console] NullGfxDevice:
```

## Cuphead - lineas clave

```text
11-17 17:42:31.044 25214 25271 I HyperPlay.DllInterceptor: IAT: 67 entradas, 67 resueltas, 0 sin resolver, 0 fuera de imagen
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [Guest Console] no boot config - using default values
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LoadLibraryW(0x1811ba738, 0x2d, 0x44, 0x6c86cbbb79) from RIP=0x180a63b2c
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LoadLibraryW -> RAX=0x7fff0006
```

## Logcat reciente sin ruido

```text
11-17 17:42:31.106 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateEventA(0x0, 0x0, 0x0, 0x0) from RIP=0x1803b06d2
11-17 17:42:31.106 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateEventA -> RAX=0x107c
11-17 17:42:31.130 25214 25214 I HyperPlaySession: surfaceChanged 2688x1216
11-17 17:42:31.133 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetSystemInfo(0x6c86cbbba0, 0x6c7804f8d0, 0x6c7804f8d0, 0x6c7842ee50) from RIP=0x180a63ed7
11-17 17:42:31.133 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetSystemInfo -> RAX=0x5c4
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA(0x0, 0x0, 0x7fffffff, 0x0) from RIP=0x18090e2c5
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA -> RAX=0x501c
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA(0x0, 0x0, 0x7fffffff, 0x0) from RIP=0x18090e2dc
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA -> RAX=0x5020
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c7803fad0, 0xc8, 0x6c7803fad0, 0x6c7842eed0) from RIP=0x18053e384
11-17 17:42:31.140 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.142 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapReAlloc(0xcafe0001, 0x0, 0x70ac25a020, 0x100) from RIP=0x1810764fb
11-17 17:42:31.142 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapReAlloc -> RAX=0x70a3aa7020
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapReAlloc(0xcafe0001, 0x0, 0x70a3aa7020, 0x200) from RIP=0x1810764fb
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapReAlloc -> RAX=0x709d471020
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree(0xcafe0001, 0x0, 0x709d474020, 0x1813ffc67) from RIP=0x181073ad8
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree -> RAX=0x1
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree(0xcafe0001, 0x0, 0x709d470020, 0x112) from RIP=0x181073ad8
11-17 17:42:31.143 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2878, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b28a0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b28c8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b28f0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2918, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2940, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2968, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2990, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b29b8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b29e0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2a08, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2a30, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2a58, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2a80, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2aa8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2ad0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2af8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2b20, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2b48, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2b70, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2b98, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2bc0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2be8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2c10, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2c38, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2c60, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2c88, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2cb0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2cd8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2d00, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2d28, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2d50, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2d78, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2da0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2dc8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2df0, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2e18, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2e40, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2e68, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2e90, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c772b2eb8, 0xc8, 0xe, 0x7) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c76d2bc08, 0xc8, 0x1, 0xe) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c76d2bc58, 0xc8, 0x1, 0xe) from RIP=0x18053e384
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA(0x0, 0x0, 0x7fffffff, 0x0) from RIP=0x180ab9455
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateSemaphoreA -> RAX=0x5024
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateEventA(0x0, 0x1, 0x1, 0x0) from RIP=0x180ab94bd
11-17 17:42:31.144 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!CreateEventA -> RAX=0x1080
11-17 17:42:31.146 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount(0x6c78037e80, 0xc8, 0x6c86cbbc30, 0x0) from RIP=0x18053e384
11-17 17:42:31.146 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!InitializeCriticalSectionAndSpinCount -> RAX=0x1
11-17 17:42:31.147 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW(0x0, 0x6c86cbb890, 0x104, 0x6c7842edd0) from RIP=0x180a586d3
11-17 17:42:31.147 25214 25307 I HyperPlay.DllInterceptor: win_GetModuleFileNameW -> len=14
11-17 17:42:31.147 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW -> RAX=0xe
11-17 17:42:31.147 25214 25307 I HyperPlay.DllInterceptor: [NativeStub] Called SHLWAPI.dll!PathCanonicalizeW (RCX=0x6c86cbb680, RDX=0x6c86cbb890, R8=0x104, R9=0x6c7842edd0) -> returning 0
11-17 17:42:31.148 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW(0x0, 0x6c86cbb830, 0x104, 0x180000000) from RIP=0x180a586d3
11-17 17:42:31.148 25214 25307 I HyperPlay.DllInterceptor: win_GetModuleFileNameW -> len=14
11-17 17:42:31.148 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW -> RAX=0xe
11-17 17:42:31.148 25214 25307 I HyperPlay.DllInterceptor: [NativeStub] Called SHLWAPI.dll!PathCanonicalizeW (RCX=0x6c86cbb620, RDX=0x6c86cbb830, R8=0x104, R9=0x180000000) -> returning 0
11-17 17:42:31.149 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW(0x0, 0x6c86cbb830, 0x104, 0x180000000) from RIP=0x180a586d3
11-17 17:42:31.149 25214 25307 I HyperPlay.DllInterceptor: win_GetModuleFileNameW -> len=14
11-17 17:42:31.149 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetModuleFileNameW -> RAX=0xe
11-17 17:42:31.149 25214 25307 I HyperPlay.DllInterceptor: [NativeStub] Called SHLWAPI.dll!PathCanonicalizeW (RCX=0x6c86cbb620, RDX=0x6c86cbb830, R8=0x104, R9=0x180000000) -> returning 0
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetCommandLineW(0x6c86cbbcd8, 0x6c86cbbd00, 0x6e6f632e746f6f62, 0x67) from RIP=0x180a67eb6
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetCommandLineW -> RAX=0x6cb894e280
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] SHELL32.dll!CommandLineToArgvW(0x6cb894e280, 0x6c86cbbc98, 0x6e6f632e746f6f62, 0x67) from RIP=0x180a67ec3
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] SHELL32.dll!CommandLineToArgvW -> RAX=0x6cb894bbc8
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!InitializeCriticalSectionEx(0x709d411050, 0xfa0, 0x0, 0x181299538) from RIP=0x181076daa
11-17 17:42:31.150 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!InitializeCriticalSectionEx -> RAX=0x1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0xc0, 0x80000000, 0x3, 0x6c86cbb988) from RIP=0x1810729bb
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x2, 0x80000000, 0x3, 0x6c86cbb988) from RIP=0x1810739d1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181076ae4
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue -> RAX=0x70ac9f9020
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181073a45
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError -> RAX=0x432ec77d0
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x1810739d1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181076ae4
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue -> RAX=0x70ac9f9020
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181073a45
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError -> RAX=0x432ec77d0
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x2, 0x181294558, 0x8, 0x181294550) from RIP=0x1810739d1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181076ae4
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue -> RAX=0x70ac9f9020
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181073a45
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError -> RAX=0x432ec77d0
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree(0xcafe0001, 0x0, 0x709d470020, 0x181299458) from RIP=0x181073ad8
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree -> RAX=0x1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree(0xcafe0001, 0x0, 0x709d46f020, 0x181299458) from RIP=0x181073ad8
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!HeapFree -> RAX=0x1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x0, 0x6c86cbb310, 0x4, 0x18112fb90) from RIP=0x1810739d1
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181076ae4
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue -> RAX=0x70ac9f9020
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181073a45
11-17 17:42:31.151 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError -> RAX=0x432ec77d0
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x40, 0x1814ea0a0, 0x40, 0x6c86cbb368) from RIP=0x181073938
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x181076ae4
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] kernel32.dll!FlsGetValue -> RAX=0x70ac9f9020
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError(0x2, 0x6cb880d5b4, 0x181299450, 0x181299458) from RIP=0x1810739a0
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!SetLastError -> RAX=0x432ec77d0
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [Guest Console] no boot config - using default values

11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor:  

11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: (Filename:  Line: 669)

11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: 

11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LocalFree(0x6cb894bbd0, 0x6c86cbbc50, 0x0, 0x6c7803f760) from RIP=0x180a68002
11-17 17:42:31.152 25214 25307 W HyperPlay.DllInterceptor: safeHeapFree: puntero 0x6cb894bbd0 no pertenece al WinSafeHeap (ignorado de forma segura sin abort)
11-17 17:42:31.152 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LocalFree -> RAX=0x0
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x6c7803bf80, 0x0, 0x6c86cbb200, 0x8) from RIP=0x180a584b7
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError(0x6c7803bf80, 0x120000, 0x7, 0x0) from RIP=0x180a584b7
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetLastError -> RAX=0x2
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LoadLibraryW(0x1811ba738, 0x2d, 0x44, 0x6c86cbbb79) from RIP=0x180a63b2c
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!LoadLibraryW -> RAX=0x7fff0006
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetProcAddress(0x7fff0006, 0x181130e08, 0x44, 0x6c86cbbb79) from RIP=0x180a63b44
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!GetProcAddress -> RAX=0x6cb8812a84
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] shell32.dll!SHGetKnownFolderPath(0x181130dd8, 0x0, 0x0, 0x6c86cbbbd8) from RIP=0x180a63b5c
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: win_SHGetKnownFolderPath: rfid=0x181130dd8 -> 'C:\users\default\AppData\LocalLow' (ptr=0x709d410020, S_OK)
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] shell32.dll!SHGetKnownFolderPath -> RAX=0x0
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!FreeLibrary(0x7fff0006, 0x0, 0x0, 0x6c86cbbbd8) from RIP=0x180a63b67
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!FreeLibrary -> RAX=0x1
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!WideCharToMultiByte(0xfde9, 0x0, 0x709d410020, 0x21) from RIP=0x180a562c6
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!WideCharToMultiByte -> RAX=0x21
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!WideCharToMultiByte(0xfde9, 0x0, 0x709d410020, 0x21) from RIP=0x180a56502
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] KERNEL32.dll!WideCharToMultiByte -> RAX=0x21
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] ole32.dll!CoTaskMemFree(0x709d410020, 0xffffff9387fab480, 0x21, 0x21) from RIP=0x180a63cd8
11-17 17:42:31.154 25214 25307 I HyperPlay.DllInterceptor: [NativeThunk] ole32.dll!CoTaskMemFree -> RAX=0x1
11-17 17:42:32.247 25214 25307 I HyperPlay.OryonEmitter: Oryon: Ejecutados 2097152 bloques JIT (RIP=0x18105a561)
11-17 17:42:33.957 25214 25307 I HyperPlay.OryonEmitter: Oryon: Ejecutados 4194304 bloques JIT (RIP=0x1810c5390)
11-17 17:42:35.654 25214 25307 I HyperPlay.OryonEmitter: Oryon: Ejecutados 6291456 bloques JIT (RIP=0x1810c52b0)
11-17 17:42:37.396 25214 25307 I HyperPlay.OryonEmitter: Oryon: Ejecutados 8388608 bloques JIT (RIP=0x18054bdec)
11-17 17:42:38.498 25214 25307 I HyperPlay.Dl
```

## Logcat crudo

- Hollow Knight: `%TEMP%\opencode\logcat_winmm_x18.txt` (19.498.818 bytes, 17:12:18).
- Cuphead: `%TEMP%\opencode\logcat_cuphead_consolecp.txt` (60.994.308 bytes, 17:43:15).
- App logs extraidos de `files/logs/` con `run-as` + `tar`; 114 archivos.

## 2026-11-19 — Caché JIT persistente

| Prueba | Resultado |
|---|---|
| `jit_scan UnityPlayer.dll 20000` | 189 138 bloques, 7,1 µs/bloque, 492 B ARM64/bloque |
| `jit_scan mono-2.0-bdwgc.dll 8000` | 101 753 bloques, 7,6 µs/bloque |
| `oryon_test` (con `testPersistentJitCache`) | 1109 comprobaciones, 0 fallos (2 ejecuciones) |
| `assembleDebug` | OK |
| En el dispositivo | `JIT total` ~4 s de CPU por arranque; la caché no es el cuello de botella |

## 2026-11-19 — Arranque, fusión de flags y juego real

| Prueba | Resultado |
|---|---|
| Arranque HK antes (sondeos `*.dll.dll` con recorrido del árbol en FUSE) | primer Present 60 s, menú 76 s |
| Arranque HK con índice de archivos | menú 17,7 s (primer Present 15,6 s) |
| A/B fusión de flags | con fusión 16,8 / 18,0 s; sin ella 18,3 / 20,7 s |
| CPU por hilo en el arranque (`/proc`) | 1 hilo al 100 % de 0-8 s, 2 hilos al 100 % de 8-16 s; 0 fallos de página mayores |
| Perfil de esperas en gameplay (10 s) | principal: JIT 74 %, `WaitForSingleObjectEx` 21 %; render: `WaitOnAddress` 53 % |
| Juego por adb 3 min (sin limitador) | 70-82 fps, peor frame 16-29 ms, 0 >33 ms en régimen estable; 11 tirones, todos en la carga inicial |
| Con limitador (panel de 144 Hz) | 72,0 fps, peor frame 14-21 ms |
| `oryon_test` | 17 237 comprobaciones, 0 fallos |
| `assembleDebug` | OK |
| Sesión en el dispositivo (`game.sh`, 03:13-03:18) | menú 16,1 s; gameplay 03:16:03-03:18:36 a 70-72 fps, peor frame 17-27 ms, 1 frame >33 ms |
| `MoveFileExW` (guardado) | `Player.log -> Player-prev.log` -> 1; `shared.dat` creado desde `.tmp` |
| Puerta Gradle completa | OK (119 pruebas) |

## 2026-11-19 (mañana) — Validación en vivo de HK (ruta `hk_well.sh`, Cruce Olvidado)

| Prueba | Resultado |
|---|---|
| Cierre de Cuphead a los 2 s (tombstone) | SIGABRT de ART: "futex wait failed … Bad address", `tagged_addr_ctrl: 0` -> quitado el `prctl` |
| Persistencia de HK | ranura 1 "Paso del Rey 2m"; `MoveFileExW` -> 1; `shared.dat` creado |
| Antes de los cambios del JIT (08:02) | transición 1486 ms; Cruce 54-58 fps |
| `tso 0` frente a `tso 1` | 58,5 frente a 56,5 fps de media: la TSO no es el cuello de botella |
| `resscale 50` | 23,6 fps (no sube): no es límite de GPU |
| Térmico tras ~1 h | estado 3, CPU 1,55/1,69 GHz: mediciones descartadas |
| GC incremental (`boot.config`) | Mono se cae al iniciar -> desactivado por defecto |
| **Frío, versión actual (09:20)** | **quieto 71,9 fps (tope 72), peor 19 ms, 0 >33 ms; paseo 68,6 fps (mín. 54); transición 1122 ms; tirones GC ~310 ms y carga ~320 ms** |
| `oryon_test` | 39 089 comprobaciones, 0 fallos |
| Puerta Gradle | OK |

## 2026-11-19 (tarde) — Modo eficiente, SGSR v1 y HyperFG en el RedMagic 10 Pro (Hollow Knight)

| Prueba | Resultado |
|---|---|
| Cambio en caliente 60 -> 72 -> 60 por el Quick Menu | sin cierre ni congelación |
| ADPF antes del arreglo | `undefined symbol: APerformanceHintManager_createSession` (nunca funcionó) |
| A/B Cruce Olvidado (tope 60 / 72): Prime + ADPF | 57,4 / 62,6 fps |
| A/B: sin anclaje + ADPF | 57,3 / 66,8 fps -> anclaje a Prime apagado por defecto |
| A/B: sin anclaje ni ADPF (comportamiento anterior) | 48,0 (135 frames > 33 ms) / 61,6 fps |
| Sin tope (capacidad de la CPU en el Cruce, frío) | 71,1 fps |
| Tope 60 + ADPF 80 % | 57,5-58,1 fps, peor frame 26 ms, 0 > 33 ms; una ventana de 2 s con > 20 ms en cada muestra |
| Protección térmica en vivo | a 41 °C: "anclaje relajado", tope 60, ▼ y BAT en rojo en el HUD, sin tirones |
| Consumo de la base (launcher, sin juego, brillo 33) | 692 mA × 8,22 V ≈ 5,7 W |
| Escala 100 % / 85 % SGSR / 75 % SGSR / 75 % bilineal (banco, tope 60) | GPU 79 / 58 / 63 / 75 %; 6,05 / 5,63 / 4,31 / 5,31 W |
| Estrangulamiento del fabricante (estado Android 0) | CPU limitada a 1,36 GHz (perf) y 1,02 GHz (Prime): fps de esa serie no comparables |
| SGSR 75 % frente a bilineal 75 % (HUD) | bordes más nítidos en "500" y máscaras, sin halos; orientación idéntica a la nativa |
| HyperFG en partida (activado antes del pozo) | 1440 sintéticos + 1440 reales, 1323 omitidos por frame lento (> 22 ms, CPU a 1 GHz), 0 cancelados; apagado/encendido en caliente libera y recrea recursos; sin errores Vulkan ni cierres; HUD intacto |
| `spirv-val --target-env vulkan1.1` | present_sgsr 7 488 B, fg_pyramid 4 152 B, fg_flow 10 300 B, fg_interp 3 212 B: OK |
| Puerta Gradle (`testDebugUnitTest lintDebug assembleDebug`) | OK |
## 2026-11-19 (noche) — Quick Resume en memoria (HK, `scratchpad/qr.sh`)
- Arranque en frío: primer frame a 11,7 s (`arranque: juego dibujando a 51 FPS tras 11684 ms`); CPU del proceso 203 %.
- Inicio (HOME): `Juego EN PAUSA (Quick Resume)` y `surfaceDestroyed`; la CPU baja a 11,5 % y luego a 7,4 %. Al volver, `Juego reanudado` y el siguiente `[Ritmo]` ya da 60 fps (peor 21 ms).
- Quick Menu → Salir: `Quick Resume: juego suspendido en memoria`, `sesión cerrada`; mismo PID 7586 con 3,8 % de CPU.
- Reabrir con `am start`: `reanudando juego en memoria tras 2 ms`, `juego dibujando a 58 FPS tras 503 ms`; después 60 fps con el peor frame en 16,8–26 ms.
- `dumpsys activity services`: QuickResumeService `isForeground=true`, tipo specialUse, notificación con 1 acción.

## 2026-11-19 (noche) — JIT: encadenado de bloques cargados de disco
- Antes (`profile.txt`): CPU 71,0 s en 30 s; N `executeEntry` 13,06 %, `linkExit` 5,62 %, `suspensionPoint` 5,38 %.
- Después (`profile2.txt`): CPU 41,9 s en 30 s; ya no salen en el top. Arriba quedan libc (6,5 %) y `syscall` (5,7 %).
- `arranque: juego dibujando a 59 FPS tras 11671 ms` (igual que antes). Menú hacia los 36 s según las capturas `bs_*` comparadas con `bscmp.ps1`.
- `oryon_test`: 39089 comprobaciones, 0 fallos, EXIT:0.

## 2026-11-19 (noche 2) — HK a tope 60: frecuencia limitada por nubia perf
- `freq.sh 60`: p0 media 1731 MHz con max0=1555, max6=3283; `juego med 15-16 ms`, 58 fps.
- `freq.sh 0`: max0=3532, max6=4320; 76 fps, peor frame 17,5 ms, `juego med 12,6`.
- `freq.sh 66` y `72`: sin límite; 66,0 y 72,0 fps; peor frame 19-20 y 17-18 ms.
- `adpf 0` con tope 60: max6=1017 MHz.
- `cap72.sh 72` (app, panel 144 Hz, caminando): 63,8-71,9 fps; picos de 34-142 ms al caminar.
- A/B `xflags` 1/0/1 y `tso` 0/1: sin diferencia medible.

## 2026-11-19 (tarde-noche) — Guardado, audio, térmica, HyperFG e interfaz
- Guardado de HK: `EntryPointNotFoundException: NtQueryDirectoryFile` en `FileSystemEnumerator.FindNextEntry` justo después de escribir `user2.dat.new`. `user2.dat` se quedaba con fecha de las 07:54 y al cargar salía "Temp file ... likely corrupted. The file has been deleted". Implementado. Copia de seguridad en `_HyperPlaySaves/backup_20261119_1540`; el `.new` de las 15:35 se pasó a `user2.dat`.
- Audio: la línea `WASAPI ritmo` alterna entre ejecuciones de 32 000 y 48 000 frames/s. Con la versión de PSHUFLW: `48019 frames/s ... GetBuffer med 945 max 1920, padding med 975, silencio 0 frames/s, padding/s 51, eventos/s 0`.
- PSHUFLW: `strchr('incremental=3','=')` devolvía 0x0 con alineaciones 0, 3, 4, 7, 8... (el '=' en los bytes 2-3 de cada grupo de 4). Después del arreglo: `OryonEmitterBackendTest: 39510 comprobaciones, 0 fallos`.
- Protección térmica: captura del usuario a 41 °C con HyperFG (GPU 93 %). Ahora hay tope de emergencia de 40 FPS a partir de 41,5 °C y HyperFG limitado a 30→60 con calor.
- Interfaz nueva estilo macOS: capturas `ui2.png` (launcher) y `ui4.png` (menú rápido en HK con la protección térmica activa a 40,0 °C y 60 FPS de 60).
- Anclaje a Prime: frames de más de 18 ms en paseos sin anclaje 935 y 356, con anclaje 136. Los de más de 33 ms quedan en 12-16 en ambos casos.

## 2026-11-19 (17:10) — Audio al salir, física y otros
- Audio tras cerrar un juego: en Cuphead (pid 13862) `Juego reanudado` del hilo 13939 (KILL_AND_LAUNCH) a las 16:49:37.676 y `DirectAAudioEngine iniciado` a las 16:49:37.686, sin `detenido` después. Al minimizar en HK (16:55:29) sí aparece `DirectAAudioEngine detenido` a los 230 ms.
- Muerte en HK (pid 8338): guardado a las 16:10:20 (con `NtQueryDirectoryFile` funcionando: `-> RAX=0x0` y `0x80000006`), escena del banco a las 16:10:25, sin excepciones; la sesión termina con código -3 a las 16:12:55 (Forzar cierre).
- Caché del JIT: HK `oryon_jit.dat` 340 MB y `.idx` 8,8 MB; Cuphead 126 MB y 3,3 MB.

## 2026-11-19 (18:20) — Pruebas diferenciales y muerte en HK por adb
- `jit_diff` final: `instrucciones=666 con_fallos=8 no_soportadas=17`; `oryon_test` 39510 comprobaciones, 0 fallos. Gradle OK; APK instalado ("Performing Streamed Install / Success").
- `dietest.sh` (17:53): tras morir, caballero sentado con el letrero DESCANSAR; R, L, U y Start→Start no lo levantan.
- A/B `fuse=0 deadflags=0 aotcache=0` (18:01): igual. `fpscap=30 fg=0` (18:13): igual. `dietest2.sh` (sin entradas desde la muerte): igual.
- `sit.sh` (18:05, cargar partida): sentado sin letrero; R lo levanta y entonces aparece DESCANSAR.
- Logcat al morir: `Audio Player NoDestroy`, 2× `Persistent Bool Item - Couldn't reset value on FSM because it's missing`, `Unloading 464 unused Assets ... Total: 190.042812 ms`. Sin excepciones. `Player.log` 0 bytes.
- `level7`: FSM "Bench Control" con `Pause Frame` (NextFrameEvent) -> `Check Start State` (BoolTest `RespawnResting` -> START SITTING / START IDLE).
- No se pudo instalar UnityPy (permiso denegado); se leyeron las cadenas del nivel a mano.

## 2026-11-19 (21:05) — Trazado de Mono y causa del banco
- `monotrace` activo: `mono_jit_set_trace_options(...) -> 1`. Con un iterador `<Respawn>d__491` en la especificación devuelve 0.
- Orden tras morir: activación de escena F7525-7527, `UnloadUnusedAssets` F7528, `EnterHero`/`Respawn` F7529. Al cargar es el mismo orden: F3824 → F3826.
- Estados del banco tras morir: `Init Resting` (F7415, 20:02:19.021) → `Inert` (F7451, 20:02:19.659) → `Idle` (F7846) → `In Range`. Al cargar: `Init Resting` → `Startle` (+1,2 s) → `Update Map Silently` → `Resting`.
- Transiciones globales de la FSM del banco: `BIG TITLE START` → `Inert` y `RESPAWN` → `Check Start State 2`. `Inert` sale con `BIG TITLE END` → `Idle`.
- `AreaTitleController.Play` en F7244: `GetBool visitedDirtmouth` = 0, y luego `SetBool visitedDirtmouth 1`.
- `user2.dat`: `visitedDirtmouth=false`, `visitedCrossroads=true`, `respawnScene="Town"`. Corregido a true (copia en `_backup_20261119_visited`).

## 2026-11-19 (22:55) — Geo y x87
- Antes (21:19): geo `7567EC7A50` con 72 choques entre 21:19:10.929 y 21:19:38.472, intervalo de 363-387 ms. Con `fuse=0 deadflags=0`: 72 y 71 choques.
- `set_linearVelocity` en `ObjectBounce`: antes=(0, −27) → escribe=(0, 1026,228); antes=(−4,79, −12,27) → (−36,393, 78,988); y escaladas hasta (9,9e13, 1,4e12).
- `Vector2.get_magnitude` (JIT de Mono): `fldl -0x8(%rsp); fsqrt; fstpl -0x8(%rsp)`. Rama x87 del emisor: todo NOP.
- `jit_diff` tras el arreglo: `x87 ... instrucciones=16 con_fallos=0`. Completo: `instrucciones=932`, con fallos solo rcp/rsqrt (aprox.) y 5 sin soporte.
- Después (22:53): 11 objetos con rebote, 1-5 choques; velocidades (5,057; 3,429) → (2,111; 1,818) → (0,721; 0,367).
- Pila de prueba: el caballero murió y se levantó del banco con la partida corregida.

## 2026-11-19 (23:05) — Frontend
- `assembleDebug` exit=0; `testDebugUnitTest lintDebug` exit=0; `adb install` Success.
- Captura a las 23:05: pantalla de bloqueo ("Presione el ícono para verificar su rostro"), batería 22 %. Launcher sin verificar.

## 2026-11-20 — GitHub
- `gh auth status`: cuenta `cespinosag1992-ui`, scopes `gist, read:org, repo`.
- `gh repo create HyperPlay --public`: creado. Push rechazado: `refusing to allow an OAuth App to create or update workflow .github/workflows/ci.yml without workflow scope`.
- `gh secret list`: HP_KEYSTORE_BASE64, HP_KEYSTORE_PASSWORD, HP_KEY_ALIAS, HP_KEY_PASSWORD (2026-11-20T07:23Z).
- `apksigner verify --print-certs`: CN=HyperPlay, O=Studio 820, C=CO; SHA-256 c812b319a7aa9539dc4ce5d87b23cce5886e95beefb5253425282c02f67c696a.

## 2026-11-20 — Launcher con mando
- `assembleRelease`: falló una vez en `:app:mergeDexRelease`. Al repetir, exit=0.
- ADB `input keyevent DPAD_RIGHT x2`: anillo en la 3.ª cápsula (DmC). `BUTTON_A`: abre la ficha, foco en JUGAR.
- La capa Game Space de RedMagic puede abrirse encima del launcher. BACK con la app enfocada la cierra (B = Salir).

## 2026-11-20 — Idiomas
- `testDebugUnitTest lintDebug assembleRelease`: exit=0 (lint pedía `@SuppressLint("RestrictedApi")` en `MainActivity.dispatchKeyEvent`: falso positivo de androidx).
- `cmd locale set-app-locales com.murdoc820.hyperplay --locales en-US`: Home/Settings en inglés. `--locales ''`: de nuevo en español (`get-app-locales` = []).

## 2026-11-20 — Pantalla, pulsación mínima y flechas del menú
- Cuphead, menú rápido (BACK) → Pantalla: Estirar = imagen a toda la pantalla; Ampliar = sin deformar, recorta abajo. 58-62 FPS.
- `input keyevent DPAD_DOWN` x2: el menú de título no se mueve (fuente teclado, el puente XInput lo ignora). `input gamepad keyevent DPAD_DOWN`: START → OPTIONS.
- Bug: tocar ‹ en Pantalla avanzaba (Ampliar → Original). Arreglado; `testDebugUnitTest lintDebug assembleRelease` exit=0, `adb install -r` Success. Verificado: Original → ‹ Ampliar → ‹ Estirar.
- Aviso de RedMagic "Aprendizaje conductual" (gravity X) encima del launcher: cerrado con BACK sin aceptar.

## 2026-11-20 — Panorámica real en Cuphead
- IL de `AbstractCupheadCamera.UpdateRect` (offset 554820, 231 bytes): 3 × `22 39 8E E3 3F` (16/9) en +38, +59, +105.
- `wstest` (NDK, en el teléfono): `hits=3 changedBytes=12 isCuphead=1/0`, `second=0 bad_aspect=0`.
- `testDebugUnitTest lintDebug assembleRelease` exit=0; `adb install -r` Success.
- Logcat: `Panorámica: Cuphead UpdateRect 16:9 -> 2.2105 (2688x1216), 5606912 bytes en memfd`.
- Forzar cierre + relanzar: `Fatal signal 11` en `decodeAndEmitOne` (pid 8655, 50 ms tras abrir la sesión). Tras `am force-stop`: arranca bien.

## 2026-11-20 — Proceso :game y foco del launcher
- `testDebugUnitTest lintDebug assembleRelease` exit=0; `adb install -r` Success.
- `ps -A | grep hyperplay` en juego: `com.murdoc820.hyperplay` (launcher) + `com.murdoc820.hyperplay:game`. Tras Forzar cierre: solo el launcher.
- Relanzar Cuphead tras Forzar cierre: sin crash (`logcat -b crash` vacío), `Panorámica: ... -> 2.2105`.
- Quick Resume: `Quick Resume: reanudando juego en memoria tras 2 ms`.
- Cuphead suspendido → abrir HK: `HyperPlayProc: matando el proceso del juego anterior (pid 19738)`; HK en pid 21490, título a 60 FPS.
- Al abrir la app aparece la capa "ENERGY CUBE" de RedMagic (bloqueo de notificaciones, motor de IA) encima del launcher.

## 2026-11-20 — Celeste (Mono x64 + FNA + Vulkan directo)
- PC: `mono.exe Celeste.exe` con MONO_PATH=xna, fnalibs x64 y shims → "GAME DISPLAYED (1490 ms)", "DONE LOADING (2207 ms)", 25 s sin errores. SDL_GPU: Vulkan (RTX 3070).
- Teléfono, en orden: IAT de mono.exe incompleta (mono-2.0-sgen/CRT) → DllMain de Mono antes que el CRT (RIP basura) → "Usage is: mono" (CommandLineToArgvW) → `System` no encontrado (MONO_PATH) → CSteamworks (sin .reloc) → `SetProp() failed` → `msvcrt!memcpy` stub → ContentTypeReader (GetStringTypeW) → `SleepConditionVariableCS failed with error 0` → `NtCreateFile` → `refcount.h:44` (ExitThread) → `mono-mmap-windows.c:132` (VirtualFree) → imagen girada 90° → congelado (SUBOPTIMAL) → funciona.
- MonoTest.exe (Type.GetType): antes `NULL System.Int32, mscorlib`; tras GetStringTypeW, igual que en el PC.
- Final: intro, título, menú, selección de partida y prólogo a 60-63 FPS, GPU 12-23 %, 2.0-2.6 W.

## 2026-11-20 — Silksong sin internet, pila, informes al servidor
- Red de HyperPlay cortada con `cmd connectivity set-package-networking-enabled false`: pantalla negra 1 de cada ~5 arranques (también pasa con red). `GetThreadContext: el hilo N no llegó a un punto seguro en 2 s` en bucle (GC de Mono).
- Diagnóstico nuevo (`[Punto seguro]`: estado del kernel + pila nativa por señal): `kernel=S estado=0`, pila `syscall` ← `win_TryEnterCriticalSection+0x3c`. TryEnterCriticalSection llamaba a EnterCriticalSection (esperaba) y va en la lista rápida como no bloqueante.
- Arreglo: `pthread_mutex_trylock`. También `callGuestNested`/`callDllEntryNested` restauran el estado del hilo al volver.
- 10/10 arranques sin red llegan al menú (`Unloading ... unused Assets` x6), ~115 FPS.
- Partida nueva: `Fatal signal 11 (SEGV_ACCERR)`, "stack pointer is not in a rw map", en `wcharToUtf8` ← `win_CreateFileW`: búfer de 64 KB en la pila del overlay panorámico de Cuphead (integrado en CreateFileW). Arreglado (lectura al vector), pila del hilo del juego 64 MB y mínimo 8 MB en CreateThread.
- Tras el arreglo: partida nueva sin cierre, jugable a 60 FPS. La cinemática de inicio sale negra: `WindowsVideoMedia error 0xc00d36c4` (Media Foundation, `CreateObjectFromByteStream`).
- Silksong: no hay ningún `user*.dat` en `_HyperPlaySaves` (solo AppConfig.ini); guarda en bancos y la partida de prueba no llegó a ninguno.
- adb inalámbrico se cae varias veces durante las pruebas sin red (puertos 44421 → 35691 → 192.168.0.2:33237).
- Servidor de informes: `wrangler kv namespace create REPORTS_KV` (id 4eba172d…), `wrangler deploy` → `https://hyperplay-reports.cespinosag1992.workers.dev` responde `HyperPlay report worker OK`. Variable `HP_REPORT_URL` en el repo. Etiquetas `auto-report`, `kind:*` creadas. Falta el secreto `GITHUB_TOKEN` (lo pone el usuario).
- LogStorage: 5/5 tests. `testDebugUnitTest lintDebug assembleRelease` exit=0.
## 2026-11-20 — Guardado de Celeste, pantalla de carga, DmC
- Celeste, Opciones → Rumble y salir: `CreateDirectory('\\?\D:\Saves') -> '.../Celeste/?/D:/Saves' errno=2`, `DirectoryNotFoundException`, "Save Failed". Arreglo: `resolveVirtualPath` quita `\\?\`, `\??\`, `\\.\`. Tras el arreglo: `Saves/settings.celeste` (5077 B); Rumble 50 % sigue tras reiniciar.
- `-Wtrigraphs`: "/??/" en un literal es un trigrafo; escrito "/?\?/".
- `adb install` (streamed) se corta por wifi: `adb push` + `pm install -r` funciona.
- DmC (rights 2) arrancaba con Mono: `DMC-DevilMayCry.exe` tiene CLR (RVA 0x22A6E14, 72 B) pero es mixto. `isManagedExe` exige COMIMAGE_FLAGS_ILONLY.
- DmC después: SIGSEGV en `hijackLocalDllIat` (memcpy de 8 B en la última palabra de la página): `dllModule.is32Bit` nunca se ponía. Además NTPhysX_* en 0x7466xxxxxx (>4 GB). `allocateArena` de 32 bits con MAP_FIXED_NOREPLACE y pasos de 16 MB: NTPhysX en 0x18000000, wx en 0x1a-0x21000000.
- DmC ahora: "IAT incompleta: 1036/3206 resueltas, 2170 sin resolver" (WSOCK32 y más). No es regresión.
- Regresión: Celeste por Mono OK; HK título a 120 FPS sin fallos. Pantalla de carga: "Ya casi está… · 0:07 / Calentando los shaders (y tus manos)".