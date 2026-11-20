# HyperPlay

**[English](#english) · [Español](#español)**

---

## English

**PC games (Windows `.exe`) running directly on Android**: no Wine, no Linux guest, no
containers. HyperPlay loads the executable in memory, translates x86/x64 code to ARM64 with its
own JIT, and wires Direct3D, audio and controllers straight to Vulkan, AAudio and Android input.

> Status: **alpha**. Tuned for Snapdragon 8 Elite (Adreno 830). Tested games: Hollow Knight and
> Cuphead (Unity, Direct3D 11).

### What works today

| Game | Engine / API | Status |
|---|---|---|
| Hollow Knight | Unity (Mono), D3D11, x64 | **Playable**: menus, saves, gameplay, benches and physics (geo, bounces) fixed; 60-120 FPS on the GPU path |
| Cuphead | Unity (Mono), D3D11, x64 | **Playable, in testing**: title, menus, save slots, world map and a run-and-gun level with shooting at ~120 FPS; bosses not verified yet. Real widescreen (more scene at the sides, no stretching) with Quick menu → Image → Screen = Widescreen, the default |
| DmC: Devil May Cry | Unreal 3, D3D9, **32-bit** | **Not working yet**: needs the 32-bit (WOW64) path and D3D9 → Vulkan |
| Celeste | XNA/FNA, .NET Framework | **Not working yet**: needs the .NET Framework runtime (`mscoree.dll`) |
| Hollow Knight: Silksong | Unity (Mono), D3D12/D3D11, x64 | Starts; first tests only, not verified as playable yet |
| Stardew Valley | MonoGame, .NET | Shows up in the library with its info; not tested yet |

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
- **Thermal protection**, FPS cap and performance overlay.
- **In-app updates** from GitHub Releases, installed over the current version without losing data.
- **Optional error reports** (the log is always kept on the phone).

### Install

1. Download `HyperPlay-vX.Y.Z.apk` from the latest [Release](../../releases/latest).
2. Install it (Android 12 or later, ARM64). The first time, Android asks you to allow apps from
   unknown sources.
3. Grant **All files access**: games open from their own folder and saves are kept next to the
   game (`_HyperPlaySaves`).
4. Press **Choose folder** and pick the folder where you keep your PC games (each game in its own
   subfolder with its `.exe`). There are no default folders; you can add more in **Settings**.

Later versions install from **Settings → Updates** inside the app.

> HyperPlay **does not include games**. Only use games you legally own.

### Build

Requirements: Android Studio / SDK 35, **NDK 28.2.13676358**, CMake 3.22.1, JDK 17 or later.

```bash
./gradlew testDebugUnitTest lintDebug assembleDebug
```

The signed release APK needs `keystore.properties` in the project root (not versioned); see
[docs/PUBLICAR.md](docs/PUBLICAR.md).

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

More technical detail in [ARCHITECTURE.md](ARCHITECTURE.md). The work log (in Spanish) is in
[progreso.md](progreso.md) and [logs.md](logs.md).

### Error reports

If the user agrees, the app sends each crash, unexpected session end or game that exits with an
error to a Cloudflare Worker, which opens an Issue labeled `auto-report`. See
[server/report-worker](server/report-worker/README.md). To report a problem yourself, open an
[Issue](../../issues/new/choose).

### License

[GPL-3.0](LICENSE). The Mesa patches in `third_party/mesa-patches` follow Mesa's MIT license.

---

## Español

**Juegos de PC (Windows `.exe`) directamente en Android**, sin Wine, sin Linux invitado, sin
contenedores. HyperPlay carga el ejecutable en memoria, traduce el código x86/x64 a ARM64 con un
JIT propio y conecta Direct3D, audio y mandos directamente con Vulkan, AAudio y el mando de Android.

> Estado: **alfa**. Optimizado para Snapdragon 8 Elite (Adreno 830). Juegos probados: Hollow
> Knight y Cuphead (Unity, Direct3D 11).

### Qué funciona hoy

| Juego | Motor / API | Estado |
|---|---|---|
| Hollow Knight | Unity (Mono), D3D11, x64 | **Jugable**: menús, partidas, gameplay, bancos y física (geo, rebotes) corregidos; 60-120 FPS por la ruta GPU |
| Cuphead | Unity (Mono), D3D11, x64 | **Jugable, en pruebas**: título, menús, ranuras de partida, mapa del mundo y un nivel de disparos a ~120 FPS; jefes sin verificar. Panorámica real (más escenario a los lados, sin estirar) con Menú rápido → Imagen → Pantalla = Panorámica, por defecto |
| DmC: Devil May Cry | Unreal 3, D3D9, **32 bits** | **Aún no funciona**: falta la ruta de 32 bits (WOW64) y D3D9 → Vulkan |
| Celeste | XNA/FNA, .NET Framework | **Aún no funciona**: necesita el runtime de .NET Framework (`mscoree.dll`) |
| Hollow Knight: Silksong | Unity (Mono), D3D12/D3D11, x64 | Arranca; solo primeras pruebas, aún sin verificar que sea jugable |
| Stardew Valley | MonoGame, .NET | Aparece en la biblioteca con su ficha; sin probar todavía |

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
- **Protección térmica**, límite de FPS y overlay de rendimiento.
- **Actualizaciones dentro de la app** desde GitHub Releases, instaladas por encima sin perder datos.
- **Informes de errores** opcionales (registro siempre guardado en el teléfono).

### Instalar

1. Descarga el `HyperPlay-vX.Y.Z.apk` de la última [Release](../../releases/latest).
2. Instálalo (Android 12 o superior, ARM64). La primera vez Android pedirá permitir apps de
   origen desconocido.
3. Concede **Acceso a todos los archivos**: los juegos se abren desde su carpeta y las partidas se
   guardan junto al juego (`_HyperPlaySaves`).
4. Pulsa **Elegir carpeta** y selecciona la carpeta donde tienes tus juegos de PC (cada juego en su
   propia subcarpeta con su `.exe`). No hay carpetas por defecto; puedes añadir más en **Ajustes**.

Las siguientes versiones se instalan desde **Ajustes → Actualizaciones** dentro de la app.

> HyperPlay **no incluye juegos**. Usa solo juegos que poseas legalmente.

### Compilar

Requisitos: Android Studio / SDK 35, **NDK 28.2.13676358**, CMake 3.22.1, JDK 17 o superior.

```bash
./gradlew testDebugUnitTest lintDebug assembleDebug
```

El APK firmado de release necesita `keystore.properties` en la raíz (no se versiona); ver
[docs/PUBLICAR.md](docs/PUBLICAR.md).

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

Más detalle técnico en [ARCHITECTURE.md](ARCHITECTURE.md); historial de trabajo en
[progreso.md](progreso.md) y [logs.md](logs.md).

### Informes de errores

Si el usuario lo acepta, la app envía cada crash, cierre inesperado o juego que termina con error
a un Worker de Cloudflare que abre un Issue con la etiqueta `auto-report`. Ver
[server/report-worker](server/report-worker/README.md). Para avisar de un fallo a mano, abre un
[Issue](../../issues/new/choose).

### Licencia

[GPL-3.0](LICENSE). Los parches de Mesa de `third_party/mesa-patches` siguen la licencia MIT de Mesa.
