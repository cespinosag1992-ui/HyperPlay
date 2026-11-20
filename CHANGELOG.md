# Changelog / Cambios

Each version is described in English first, then in Spanish. The release workflow copies the
section of the tagged version into the GitHub Release notes.

Cada versión se describe primero en inglés y luego en español. El workflow de release copia la
sección de la versión etiquetada en las notas de la Release de GitHub.

## Unreleased

### English

- **.NET games (Celeste)**: HyperPlay runs .NET Framework games with Mono x64 for Windows
  (translated) and FNA instead of XNA. The game's Vulkan renderer (FNA3D/SDL GPU) is passed to
  the phone's Vulkan almost as is (Win32 surface → Android surface, orientation handled). Tested
  on the phone: Celeste title, menus, file select and the prologue at 60 FPS with the controller.
  Not yet: audio (FMOD is a silent shim) and downloading the runtime from the app (for now it is
  copied to `HyperPlay/runtime/mono`, see `tools/managed-runtime`).
- Engine fixes found on the way: DLL init order (dependencies first), real
  `CommandLineToArgvW`, `GetStringTypeW` (the CRT thought no character was a space),
  `ExitThread`, `ERROR_TIMEOUT` from condition variables, `NtCreateFile`/`NtQueryDirectoryFile`,
  `SetProp`/`GetProp`, `msvcrt.dll` routed to the UCRT, 32-bit DLLs refused in 64-bit games,
  game console output in logcat.
- **Real widescreen for Cuphead** (Quick menu → Image → Screen = **Widescreen**, the default):
  the game draws more of the scene at the sides, at native resolution, with no stretching and
  no cropping. HyperPlay patches the 16:9 limit of the game camera in memory when the game
  loads (the game files are not changed). Tested on the phone: menus, world map and a
  run-and-gun level fill the 20:9 screen at 60 FPS. Known detail: in some levels a haze layer of
  the sky was drawn for 16:9 and shows a faint seam near the old edges.
- Quick menu → Image → **Screen**: Original / Stretch / Zoom, saved per game. Fills the screen
  for games that draw 16:9 with side bars (Cuphead on a 20:9 phone). Stretch widens the image;
  Zoom keeps the shape and crops top and bottom. Tested on the device in Cuphead.
- Each game now runs in its own process. Force closing a game and opening it (or another one)
  again no longer crashes the app. Quick Resume still works (the held game resumes instantly);
  opening a different game closes the held one first.
- Launcher: a D-pad press is no longer lost after the RedMagic Game Space layer shows up when
  the app opens.
- Quick menu: tapping ‹ now steps a setting back and › steps it forward (before, any tap moved
  forward).
- Gamepad: very short button presses are held for at least 40 ms, so games that read the pad
  once per frame no longer miss them.

### Español

- **Juegos .NET (Celeste)**: HyperPlay ejecuta juegos de .NET Framework con Mono x64 para Windows
  (traducido) y FNA en lugar de XNA. El renderizador Vulkan del juego (FNA3D/SDL GPU) se pasa al
  Vulkan del teléfono casi tal cual (superficie Win32 → Android, orientación resuelta). Probado en
  el teléfono: título, menús, selección de partida y el prólogo de Celeste a 60 FPS con mando.
  Falta: sonido (FMOD es un shim mudo) y descargar el runtime desde la app (por ahora se copia a
  `HyperPlay/runtime/mono`, ver `tools/managed-runtime`).
- Arreglos del motor encontrados por el camino: orden de inicio de las DLL (dependencias
  primero), `CommandLineToArgvW` real, `GetStringTypeW` (el CRT creía que ningún carácter era un
  espacio), `ExitThread`, `ERROR_TIMEOUT` en variables de condición,
  `NtCreateFile`/`NtQueryDirectoryFile`, `SetProp`/`GetProp`, `msvcrt.dll` al UCRT, DLL de 32 bits
  rechazadas en juegos de 64, salida de consola del juego en logcat.
- **Panorámica real para Cuphead** (Menú rápido → Imagen → Pantalla = **Panorámica**, por
  defecto): el juego dibuja más escenario a los lados, a resolución nativa, sin estirar ni
  recortar. HyperPlay parchea en memoria el límite 16:9 de la cámara del juego al cargarlo (los
  archivos del juego no se tocan). Probado en el teléfono: menús, mapa del mundo y un nivel de
  disparos llenan la pantalla 20:9 a 60 FPS. Detalle conocido: en algunos niveles una capa de
  bruma del cielo está dibujada para 16:9 y deja una costura suave cerca de los bordes antiguos.
- Menú rápido → Imagen → **Pantalla**: Original / Estirar / Ampliar, se guarda por juego. Llena
  la pantalla en juegos que dibujan en 16:9 con barras a los lados (Cuphead en un teléfono 20:9).
  Estirar ensancha la imagen; Ampliar mantiene la forma y recorta arriba y abajo. Probado en el
  teléfono con Cuphead.
- Cada juego corre ahora en su propio proceso. Forzar el cierre de un juego y volver a abrirlo
  (o abrir otro) ya no cierra la app. Quick Resume sigue funcionando (el juego suspendido se
  reanuda al instante); abrir otro juego cierra antes el suspendido.
- Launcher: ya no se pierde una pulsación de la cruceta cuando aparece la capa de RedMagic Game
  Space al abrir la app.
- Menú rápido: tocar ‹ ahora retrocede el ajuste y › lo avanza (antes cualquier toque avanzaba).
- Mando: las pulsaciones muy cortas se mantienen al menos 40 ms, así los juegos que leen el mando
  una vez por frame ya no las pierden.

## 1.0.0

### English — first public version (alpha)

**Game status in this version**

| Game | Status |
|---|---|
| **Hollow Knight** | ✅ **Stable**: menus, saves, gameplay, benches and physics; 60-120 FPS |
| Hollow Knight: Silksong | 🧪 Starts; first tests only, not verified as playable yet |
| Cuphead | 🚧 Being optimized: menus, saves, world map and a level already run at ~120 FPS; the goal is to make it playable and stable for the next version |
| DmC: Devil May Cry | ❌ Not yet (needs 32-bit and Direct3D 9 support) |
| Celeste | ❌ Not yet (needs the .NET Framework runtime) |

Only **Hollow Knight** is considered stable in 1.0.0.

**Features**

- **Direct engine, no Wine or Linux**: Windows x86/x64 `.exe` games run directly on Android. PE
  loader, Win32 API interception and an x86/x64 → ARM64 JIT with an on-disk cache, real x87 FPU,
  SSE up to 4.2 and FMA.
- **GPU graphics**: Direct3D 11 → Vulkan (DXBC → SPIR-V shaders) on the Adreno 830, SGSR
  upscaling, CAS sharpening and HyperFG frame generation.
- **Handheld-console interface**: library with official covers, logos and game info from Steam,
  game page on a single screen, custom loading screen, Quick Resume (the game stays paused in
  memory) and an in-game quick menu (L3 + R3).
- **Controllers**: Xbox, PlayStation, GameSir, 8BitDo and generic HID over Bluetooth or USB, with
  automatic mapping, analog triggers and vibration (or the phone vibrates if the pad can't); full
  gamepad navigation in the launcher; touch controls appear when you touch the screen.
- **Performance and heat**: FPS cap (60/72/120/unlimited), render resolution, thermal protection
  that keeps the battery under 40 °C, and a performance overlay.
- **Your language**: the app follows the system language (English or Spanish), including the
  Steam game info.
- **Your folder**: no default folders; you choose where your games are.
- **In-app updates** from GitHub Releases, installed over the current version without losing
  settings, saves or caches.
- **Error log on the phone** and optional reports (asked on first launch). Automatic upload to
  GitHub is not enabled in this build yet.

**Requirements**: Android 12 or later, ARM64. Tuned for Snapdragon 8 Elite (tested on a
RedMagic 10S Pro). HyperPlay does not include games.

### Español — primera versión pública (alfa)

**Estado de los juegos en esta versión**

| Juego | Estado |
|---|---|
| **Hollow Knight** | ✅ **Estable**: menús, partidas, gameplay, bancos y física; 60-120 FPS |
| Hollow Knight: Silksong | 🧪 Arranca; solo primeras pruebas, aún sin verificar que sea jugable |
| Cuphead | 🚧 En optimización: menús, partidas, mapa del mundo y un nivel ya van a ~120 FPS; el objetivo es dejarlo jugable y estable para la próxima versión |
| DmC: Devil May Cry | ❌ Todavía no (necesita soporte de 32 bits y Direct3D 9) |
| Celeste | ❌ Todavía no (necesita el runtime de .NET Framework) |

En la 1.0.0 solo **Hollow Knight** se considera estable.

**Funciones**

- **Motor directo, sin Wine ni Linux**: los juegos `.exe` de Windows x86/x64 se ejecutan
  directamente en Android. Cargador PE, interceptación de la API Win32 y JIT x86/x64 → ARM64 con
  caché en disco, FPU x87 real, SSE hasta 4.2 y FMA.
- **Gráficos en la GPU**: Direct3D 11 → Vulkan (shaders DXBC → SPIR-V) en la Adreno 830,
  reescalado SGSR, nitidez CAS y generación de fotogramas HyperFG.
- **Interfaz de consola portátil**: biblioteca con portadas, logos e información oficial de Steam,
  ficha del juego en una sola pantalla, pantalla de carga propia, Quick Resume (el juego queda en
  pausa en memoria) y menú rápido dentro del juego (L3 + R3).
- **Mandos**: Xbox, PlayStation, GameSir, 8BitDo y HID genéricos por Bluetooth o USB, con mapeo
  automático, gatillos analógicos y vibración (o vibra el teléfono si el mando no puede);
  navegación completa con mando en el launcher; el mando táctil aparece al tocar la pantalla.
- **Rendimiento y temperatura**: tope de FPS (60/72/120/sin límite), resolución de render,
  protección térmica que mantiene la batería por debajo de 40 °C e indicador de rendimiento.
- **Tu idioma**: la app usa el idioma del sistema (inglés o español), también en la ficha de Steam.
- **Tu carpeta**: sin carpetas por defecto; tú eliges dónde están tus juegos.
- **Actualizaciones dentro de la app** desde GitHub Releases, instaladas por encima sin perder
  ajustes, partidas ni cachés.
- **Registro de errores en el teléfono** e informes opcionales (se pregunta en el primer arranque).
  El envío automático a GitHub aún no está activado en esta versión.

**Requisitos**: Android 12 o superior, ARM64. Optimizado para Snapdragon 8 Elite (probado en un
RedMagic 10S Pro). HyperPlay no incluye juegos.
