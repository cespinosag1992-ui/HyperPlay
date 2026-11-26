# Exedra64

<p align="center"><img src="docs/exedra64-logo.png" alt="Exedra64" width="220"></p>

**[English](#english) · [Español](#español)**

*Formerly HyperPlay. / Antes HyperPlay.*

---

## English

**PC games (Windows `.exe`) running directly on Android**: no Wine, no Linux guest, no
containers. Exedra64 loads the executable in memory, translates x86/x64 code to ARM64 with its
own JIT, and wires Direct3D, audio and controllers straight to Vulkan, AAudio and Android input.

> Status: **alpha**. Tuned for Snapdragon 8 Elite (Adreno 830). Tested games: Hollow Knight and
> Cuphead (Unity, Direct3D 11).

### What works today

| Game | Engine / API | Status |
|---|---|---|
| Hollow Knight | Unity (Mono), D3D11, x64 | **Playable**: menus, saves, gameplay, benches and physics (geo, bounces) fixed; 60-120 FPS on the GPU path |
| Cuphead | Unity (Mono), D3D11, x64 | **Playable, in testing**: title, menus, save slots, world map and a run-and-gun level with shooting at ~120 FPS; bosses not verified yet. Real widescreen (more scene at the sides, no stretching), always on |
| DmC: Devil May Cry | Unreal 3, D3D9, **32-bit** | **Not working yet**: the 32-bit path now loads the game and its DLLs (memory below 4 GB, stdcall/cdecl calls, 32-bit Windows structures) and the .NET Framework from Microsoft; still stops at startup. D3D9 → Vulkan pending |
| Celeste | XNA → FNA, .NET Framework | **Starts, in testing**: runs on Mono x64 + FNA (Vulkan passed to the phone): title, menus, file select and the prologue at 60 FPS. Exedra64 downloads its .NET runtime (33 MB) the first time. **Audio without FMOD**: Exedra64 plays the game's own sound banks with its native FMOD Studio (effects, interface, ambience and music, with its layers and stereo position; snapshots still in progress, see `tools/managed-runtime`). Options and game progress are saved |
| Hollow Knight: Silksong | Unity (Mono), D3D12/D3D11, x64 | Starts; first tests only, not verified as playable yet |
| Stardew Valley | MonoGame, .NET 6, OpenGL | **Starts, in testing**: title, menus, new game and gameplay with controller; clean audio. Not verified yet: saves, HyperFG at 120 and internal resolution |

The app itself (library, game page, settings, quick menu, controllers, updates, error reports)
works and is tested on a RedMagic 10S Pro (Android 15).

### Features

- **Direct engine** (`libhyperplay.so`): PE32/PE32+ loader, Windows IAT interception (kernel32,
  ntdll, D3D11/DXGI, XInput, WASAPI, Steam), x86/x64 → ARM64 JIT with an on-disk cache, x87 FPU,
  SSE up to 4.2 and FMA.
- **Graphics**: D3D11 → Vulkan on the GPU (DXBC → SPIR-V), SGSR upscaling and CAS sharpening,
  HyperFG frame generation.
- **Handheld-console UI**: library with official covers and game info, custom loading screen,
  Quick Resume (the game stays paused in memory) and a quick menu. The app follows the system
  language (English and Spanish).
- **Controllers**: Xbox, PlayStation, GameSir, 8BitDo and generic HID (Bluetooth or USB), with
  automatic mapping, analog triggers and vibration; touch controls appear when you touch the screen.
- **Thermal protection**, FPS cap (60/90/120) and performance overlay.
- **Display**: games always fill the screen at its native resolution without distortion. Each
  game's own video options choose resolution, window mode and frame limit; lower resolutions are
  scaled to the screen keeping the aspect ratio (black bars when it differs).
- **In-app updates** from GitHub Releases, installed over the current version without losing data.
- **Optional error reports** (the log is always kept on the phone).

### Install

1. Download `Exedra64-vX.Y.Z-Snapdragon8Elite-arm64-v8a.apk` from the latest
   [Release](../../releases/latest). The name says the processor it is built for: 64-bit ARM
   (arm64-v8a), tuned for the Snapdragon 8 Elite.
2. Install it (Android 12 or later, ARM64). The first time, Android asks you to allow apps from
   unknown sources.
3. Grant **All files access**: games open from their own folder and saves are kept next to the
   game (`_HyperPlaySaves`).
4. Press **Choose folder** and pick the folder where you keep your PC games (each game in its own
   subfolder with its `.exe`). There are no default folders; you can add more in **Settings**.

Later versions install from **Settings → Updates** inside the app.

> Exedra64 **does not include games**. Only use games you legally own.

### Build

Requirements: Android Studio / SDK 35, **NDK 28.2.13676358**, CMake 3.22.1, JDK 17 or later.

```bash
./gradlew testDebugUnitTest lintDebug assembleDebug
```
UI text lives in `app/src/main/res/values/strings.xml` (English, default) and
`values-es/strings.xml` (Spanish). Add every new string to both.

### Layout

| Path | Contents |
|---|---|
| `app/src/main/cpp/loader` | PE loader, IAT, Win32, D3D, audio, frame pacing |
| `app/src/main/cpp/cpu` | x86/x64 → ARM64 JIT (`OryonEmitterBackend`), AOT cache, scheduler |
| `app/src/main/cpp/gfx` | Vulkan, D3D11 on the GPU, DXBC → SPIR-V, SGSR, HyperFG |
| `app/src/main/java/.../` | Launcher (Compose), game session, controllers, art, updates, diagnostics |
| `app/src/test` | JVM tests and native JIT suites (`OryonEmitterBackendTest`, `JitDiff`) |
| `tools/` | JIT differential test generator, shader build and native tests |
| `server/report-worker` | Cloudflare Worker that turns error reports into Issues |
| `docs/` | Documentation and screenshots |

More technical detail in [ARCHITECTURE.md](ARCHITECTURE.md).

### Error reports

If the user agrees, the app sends each crash, unexpected session end or game that exits with an
error to a Cloudflare Worker, which opens an Issue labeled `auto-report` in a private reports
repository. Before sending, the app removes personal data (IP and MAC addresses, emails, user
folder names, device identifiers, account names and tokens); the server removes it again. See
[server/report-worker](server/report-worker/README.md). Sending starts with the next release.
To save space on the phone, the app log is cleared once reports are sent, and only the last 20
unsent reports and the last 5 game session logs are kept. To report a problem yourself, open an
[Issue](../../issues/new/choose).

### License

[GPL-3.0](LICENSE). The Mesa patches in `third_party/mesa-patches` follow Mesa's MIT license.

---

## Español

**Juegos de PC (Windows `.exe`) directamente en Android**, sin Wine, sin Linux invitado, sin
contenedores. Exedra64 carga el ejecutable en memoria, traduce el código x86/x64 a ARM64 con un
JIT propio y conecta Direct3D, audio y mandos directamente con Vulkan, AAudio y el mando de Android.

> Estado: **alfa**. Optimizado para Snapdragon 8 Elite (Adreno 830). Juegos probados: Hollow
> Knight y Cuphead (Unity, Direct3D 11).

### Qué funciona hoy

| Juego | Motor / API | Estado |
|---|---|---|
| Hollow Knight | Unity (Mono), D3D11, x64 | **Jugable**: menús, partidas, gameplay, bancos y física (geo, rebotes) corregidos; 60-120 FPS por la ruta GPU |
| Cuphead | Unity (Mono), D3D11, x64 | **Jugable, en pruebas**: título, menús, ranuras de partida, mapa del mundo y un nivel de disparos a ~120 FPS; jefes sin verificar. Panorámica real (más escenario a los lados, sin estirar), siempre activa |
| DmC: Devil May Cry | Unreal 3, D3D9, **32 bits** | **Aún no funciona**: la ruta de 32 bits ya carga el juego y sus DLL (memoria por debajo de 4 GB, llamadas stdcall/cdecl, estructuras Windows de 32 bits) y el .NET Framework de Microsoft; aún se para al arrancar. Falta D3D9 → Vulkan |
| Celeste | XNA → FNA, .NET Framework | **Arranca, en pruebas**: corre en Mono x64 + FNA (Vulkan pasado al teléfono): título, menús, selección de partida y el prólogo a 60 FPS. Exedra64 descarga su runtime .NET (33 MB) la primera vez. **Sonido sin FMOD**: Exedra64 reproduce los bancos de sonido del propio juego con su FMOD Studio nativo (efectos, interfaz, ambientes y música, con sus capas y la posición en estéreo; los snapshots siguen en curso, ver `tools/managed-runtime`). Se guardan las opciones y la partida |
| Hollow Knight: Silksong | Unity (Mono), D3D12/D3D11, x64 | Arranca; solo primeras pruebas, aún sin verificar que sea jugable |
| Stardew Valley | MonoGame, .NET 6, OpenGL | **Arranca, en pruebas**: título, menús, partida nueva y juego con mando; sonido limpio. Sin verificar aún: guardado, HyperFG a 120 y resolución interna |

La app en sí (biblioteca, ficha del juego, ajustes, menú rápido, mandos, actualizaciones,
informes de errores) funciona y está probada en un RedMagic 10S Pro (Android 15).

### Características

- **Motor directo** (`libhyperplay.so`): cargador PE32/PE32+, interceptación de la IAT de Windows
  (kernel32, ntdll, D3D11/DXGI, XInput, WASAPI, Steam), JIT x86/x64 → ARM64 con caché en disco,
  FPU x87, SSE hasta 4.2 y FMA.
- **Gráficos**: D3D11 → Vulkan en la GPU (DXBC → SPIR-V), reescalado SGSR y nitidez CAS,
  generación de fotogramas HyperFG.
- **Interfaz de consola portátil**: biblioteca con portadas e información oficial de cada juego,
  pantalla de carga propia, Quick Resume (el juego queda en pausa en memoria) y menú rápido. La app
  usa el idioma del sistema (inglés y español).
- **Mandos**: Xbox, PlayStation, GameSir, 8BitDo y HID genéricos (Bluetooth o USB), con mapeo
  automático, gatillos analógicos y vibración; mando táctil que aparece al tocar la pantalla.
- **Protección térmica**, límite de FPS (60/90/120) y overlay de rendimiento.
- **Pantalla**: los juegos llenan siempre la pantalla a su resolución nativa sin deformarse. Las
  opciones de vídeo de cada juego eligen la resolución, el modo de ventana y el límite de
  fotogramas; las resoluciones menores se escalan a la pantalla conservando la proporción (bandas
  negras si es distinta).
- **Actualizaciones dentro de la app** desde GitHub Releases, instaladas por encima sin perder datos.
- **Informes de errores** opcionales (registro siempre guardado en el teléfono).

### Instalar

1. Descarga el `Exedra64-vX.Y.Z-Snapdragon8Elite-arm64-v8a.apk` de la última
   [Release](../../releases/latest). El nombre indica el procesador para el que está hecho: ARM de
   64 bits (arm64-v8a), optimizado para el Snapdragon 8 Elite.
2. Instálalo (Android 12 o superior, ARM64). La primera vez Android pedirá permitir apps de
   origen desconocido.
3. Concede **Acceso a todos los archivos**: los juegos se abren desde su carpeta y las partidas se
   guardan junto al juego (`_HyperPlaySaves`).
4. Pulsa **Elegir carpeta** y selecciona la carpeta donde tienes tus juegos de PC (cada juego en su
   propia subcarpeta con su `.exe`). No hay carpetas por defecto; puedes añadir más en **Ajustes**.

Las siguientes versiones se instalan desde **Ajustes → Actualizaciones** dentro de la app.

> Exedra64 **no incluye juegos**. Usa solo juegos que poseas legalmente.

### Compilar

Requisitos: Android Studio / SDK 35, **NDK 28.2.13676358**, CMake 3.22.1, JDK 17 o superior.

```bash
./gradlew testDebugUnitTest lintDebug assembleDebug
```
Los textos de la interfaz están en `app/src/main/res/values/strings.xml` (inglés, por defecto) y
`values-es/strings.xml` (español). Cada texto nuevo va en los dos.

### Estructura

| Ruta | Contenido |
|---|---|
| `app/src/main/cpp/loader` | Cargador PE, IAT, Win32, D3D, audio, ritmo de frames |
| `app/src/main/cpp/cpu` | JIT x86/x64 → ARM64 (`OryonEmitterBackend`), caché AOT, planificador |
| `app/src/main/cpp/gfx` | Vulkan, D3D11 en GPU, DXBC → SPIR-V, SGSR, HyperFG |
| `app/src/main/java/.../` | Launcher (Compose), sesión de juego, mandos, arte, actualizaciones, diagnóstico |
| `app/src/test` | Tests JVM y suites nativas del JIT (`OryonEmitterBackendTest`, `JitDiff`) |
| `tools/` | Generador de pruebas diferenciales del JIT, compilación de shaders y tests nativos |
| `server/report-worker` | Cloudflare Worker que convierte los informes de errores en Issues |
| `docs/` | Documentación y capturas |

Más detalle técnico en [ARCHITECTURE.md](ARCHITECTURE.md).

### Informes de errores

Si el usuario lo acepta, la app envía cada crash, cierre inesperado o juego que termina con error
a un Worker de Cloudflare que abre un Issue con la etiqueta `auto-report` en un repositorio
privado de informes. Antes de enviar, la app quita los datos personales (direcciones IP y MAC,
correos, nombres de carpetas de usuario, identificadores del dispositivo, cuentas y tokens); el
servidor los vuelve a quitar. Ver
[server/report-worker](server/report-worker/README.md). El envío empieza con la próxima versión.
Para ahorrar espacio en el teléfono, el registro de la app se vacía al enviar los informes y solo
se guardan los 20 últimos informes sin enviar y los 5 últimos logs de sesión. Para avisar de un
fallo a mano, abre un [Issue](../../issues/new/choose).

### Licencia

[GPL-3.0](LICENSE). Los parches de Mesa de `third_party/mesa-patches` siguen la licencia MIT de Mesa.
