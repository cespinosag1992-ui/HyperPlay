# Changelog / Cambios

Each version is described in English first, then in Spanish. The release workflow copies the
section of the tagged version into the GitHub Release notes.

Cada versión se describe primero en inglés y luego en español. El workflow de release copia la
sección de la versión etiquetada en las notas de la Release de GitHub.

## Unreleased

### English

- **Engine: real Windows x64 exception handling (SEH).** C++ and .NET exceptions are now
  dispatched like on Windows: vectored handlers, then each frame's handler from the module's
  `.pdata` (and the JIT tables of Mono/CoreCLR), then the unhandled filter; `RtlUnwindEx`,
  `RtlRestoreContext` (C++ catch blocks, `longjmp`), `RtlVirtualUnwind`, `RtlLookupFunctionEntry`
  and `RtlCaptureContext` are implemented. Before, `RaiseException` did nothing.
- **Stardew Valley (work in progress):** its .NET 6 runtime (CoreCLR) now starts, loads the game
  and MonoGame and initializes SDL. Fixed on the way: forwarded exports of local DLLs, `SEC_IMAGE`
  file mappings, 64 KB-aligned `VirtualAlloc`, unaligned `VirtualProtect`, `GetCPInfo`, CET
  instructions (`RDSSP`), Windows version checks, file functions used by saves (`SetEndOfFile`,
  `SetFileInformationByHandle`, real `FlushFileBuffers`). Not yet: the game draws with OpenGL,
  which HyperPlay does not have yet.
- **OpenGL for Windows games (first version):** HyperPlay now has its own `opengl32.dll`: WGL
  contexts are EGL contexts and desktop OpenGL calls go to the phone's OpenGL ES 3.2 (float
  arguments read from XMM registers, desktop-only calls adapted, GLSL `#version` lines removed),
  plus the GDI that SDL needs (pixel formats, `SwapBuffers`, DCs, cursor bitmaps). Stardew Valley's
  SDL video now starts. Not verified yet: no OpenGL game draws on screen yet.
- Engine: `PUSHFQ`/`POPFQ`, `NtQuerySystemInformation` (process list), .NET settings now passed in
  the environment block that CoreCLR reads, SDL uses only XInput controllers in every game, safer
  exception dispatch over corrupt stacks.
- Stardew Valley still stops in its startup code (`Game1` static constructor) on a corrupt .NET
  object; under investigation.
- Tested: Celeste (60 FPS) and Hollow Knight (menu) still work.

### Español

- **Motor: manejo real de excepciones de Windows x64 (SEH).** Las excepciones de C++ y .NET se
  despachan como en Windows: manejadores vectoriales, después el manejador de cada marco según el
  `.pdata` del módulo (y las tablas del JIT de Mono/CoreCLR), y por último el filtro de no
  manejadas. Implementados `RtlUnwindEx`, `RtlRestoreContext` (bloques catch de C++, `longjmp`),
  `RtlVirtualUnwind`, `RtlLookupFunctionEntry` y `RtlCaptureContext`. Antes `RaiseException` no
  hacía nada.
- **Stardew Valley (en curso):** su runtime .NET 6 (CoreCLR) ya arranca, carga el juego y
  MonoGame e inicia SDL. Arreglado por el camino: exports reenviados de DLL locales, mapeos
  `SEC_IMAGE`, `VirtualAlloc` alineado a 64 KB, `VirtualProtect` no alineado, `GetCPInfo`,
  instrucciones CET (`RDSSP`), comprobaciones de versión de Windows y funciones de archivo que usan
  los guardados (`SetEndOfFile`, `SetFileInformationByHandle`, `FlushFileBuffers` real). Falta: el
  juego dibuja con OpenGL, que HyperPlay aún no tiene.
- **OpenGL para juegos de Windows (primera versión):** HyperPlay tiene su propio `opengl32.dll`:
  los contextos WGL son contextos EGL y las llamadas de OpenGL de escritorio van al OpenGL ES 3.2
  del teléfono (argumentos float leídos de los registros XMM, llamadas solo de escritorio
  adaptadas, líneas `#version` de GLSL quitadas), más el GDI que necesita SDL (formatos de píxel,
  `SwapBuffers`, DC, mapas de bits de cursor). El vídeo SDL de Stardew Valley ya arranca. Sin
  verificar aún: ningún juego OpenGL dibuja todavía en pantalla.
- Motor: `PUSHFQ`/`POPFQ`, `NtQuerySystemInformation` (lista de procesos), los ajustes de .NET van
  ahora en el bloque de entorno que lee CoreCLR, SDL usa solo mandos XInput en todos los juegos y
  el despacho de excepciones resiste pilas corruptas.
- Stardew Valley aún se para en su código de arranque (constructor estático de `Game1`) por un
  objeto .NET corrupto; en investigación.
- Probado: Celeste (60 FPS) y Hollow Knight (menú) siguen funcionando.

## 1.1.0

### English

- **Celeste at 120 FPS with HyperFG.** Games that draw with their own Vulkan (Celeste) now draw
  into HyperPlay images; HyperPlay copies each frame to the screen. With HyperFG on (Quick menu),
  HyperPlay generates one frame between each pair of game frames: Celeste runs at 60 and the
  screen shows 120. Tested on the phone: title, main menu and 3D mountain menu at 120 FPS, no
  crashes. Known: HyperFG uses more GPU at native resolution (about 60-80 %), and the thermal
  protection of HyperFG does not act on these games yet.
- The APK name now says the processor it is built for:
  `HyperPlay-vX.Y.Z-Snapdragon8Elite-arm64-v8a.apk`.

- **.NET games (Celeste)**: HyperPlay runs .NET Framework games with Mono x64 for Windows
  (translated) and FNA instead of XNA. The game's Vulkan renderer (FNA3D/SDL GPU) is passed to
  the phone's Vulkan almost as is (Win32 surface → Android surface, orientation handled). Tested
  on the phone: Celeste title, menus, file select and the prologue at 60 FPS with the controller.
  The .NET runtime (33 MB, release `runtime-mono-x64-1`) is downloaded by the app the first time
  a .NET game opens, checked with SHA-256 and installed in the app's own folder. Not yet: audio
  (FMOD is proprietary and the shim is silent; your own FMOD 1.10 x64 for Windows in
  `/sdcard/HyperPlay/fmod` replaces it).
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
- **Hollow Knight: Silksong starts without internet.** About 1 launch in 5 stayed on a black
  screen (with or without internet; it looked like a network problem). Cause:
  `TryEnterCriticalSection` waited like `EnterCriticalSection` and the Mono garbage collector,
  which uses it while it pauses threads, waited forever. Tested on the phone: 10 of 10 launches
  without internet reach the menu (~115 FPS), and a new game is playable.
- Fixed a crash when a game opened a file deep in its call stack (Silksong when starting a game):
  the Cuphead widescreen code used 64 KB of stack in every file open. The game thread and the
  threads games create now also get a larger native stack.
- A thread that runs game code from inside a native call (callbacks, exceptions) is marked as
  "in native code" again when it returns, so pausing it never waits forever.
- **Celeste has sound, without FMOD.** HyperPlay now has its own FMOD Studio (native code, no
  Firelight code): it reads the game's sound banks, decodes their Vorbis sounds with Android and
  mixes them at 48 kHz. Tested on the phone: title ambience (looping), menu music and interface
  sounds. Supported: events and timelines, random variations, nested events, loops and sustain
  points, bus/VCA volume and pause, long music decoded in blocks, a soft limiter. Tested in the
  prologue: footsteps, jump, landing, ambience and music. Also: game parameters (music layers fade
  in and out like on PC; defaults read from the bank), sounds chosen by a parameter, and stereo
  position (left/right, quieter off screen), the mixer buses and their faders, snapshots (the
  pause menu lowers the music and ambience; story moments lower the ambience) and music that
  changes section when the game says so. Not done: effects such as reverb. Celeste's saves were
  checked: "Save and Quit" writes the file and it loads
  after restarting.
- Loading screen: games that draw with their own Vulkan (Celeste) now leave it as soon as they
  show something (before it stayed up to 45 s).
- Fixed: Mono sometimes aborted (`mono-mmap-windows.c:132`) because `VirtualQuery` reported a
  just-reserved region as free; and a folder listing could come back empty when a handle number
  was reused (Celeste: "Missing Language Files").
- **Celeste saves again.** Changing an option showed "Save Failed": Mono creates the `Saves`
  folder with a Win32 long path (`\\?\D:\Saves`) and HyperPlay did not understand the `\\?\`
  prefix. Tested on the phone: an option changed in Celeste is still there after restarting.
- Loading screen: the technical engine text is gone; it now shows a friendly stage ("Getting the
  game ready…", "Almost there…") and a light-hearted line that changes every few seconds.
- .NET detection fixed: mixed C++/CLI executables (DmC: Devil May Cry) were started with Mono.
  Only IL-only assemblies use Mono now.
- 32-bit games: their DLLs are always mapped below 4 GB (before, some PhysX DLLs of DmC ended up
  above it), and the import table of a 32-bit DLL is written with 4-byte slots (DmC crashed while
  loading its wxWidgets DLLs). DmC still does not start (missing Windows functions).
- **Error reports reach the server.** With your permission, reports are sent to the HyperPlay
  report server, which opens an issue on GitHub (no token in the app). To use little space on
  the phone: the app log is cleared after reports are sent, at most 20 unsent reports (under 30
  days old) are kept, and only the last 5 game session logs.

### Español

- **Celeste a 120 FPS con HyperFG.** Los juegos que dibujan con su propio Vulkan (Celeste) dibujan
  ahora en imágenes de HyperPlay, y HyperPlay copia cada frame a la pantalla. Con HyperFG activo
  (Menú rápido), HyperPlay genera un frame entre cada par de frames del juego: Celeste va a 60 y
  la pantalla muestra 120. Probado en el teléfono: título, menú principal y menú 3D de la montaña
  a 120 FPS, sin cierres. Conocido: HyperFG usa más GPU a resolución nativa (60-80 %), y la
  protección térmica de HyperFG aún no actúa en estos juegos.
- El nombre del APK indica ahora el procesador para el que está hecho:
  `HyperPlay-vX.Y.Z-Snapdragon8Elite-arm64-v8a.apk`.
- **Juegos .NET (Celeste)**: HyperPlay ejecuta juegos de .NET Framework con Mono x64 para Windows
  (traducido) y FNA en lugar de XNA. El renderizador Vulkan del juego (FNA3D/SDL GPU) se pasa al
  Vulkan del teléfono casi tal cual (superficie Win32 → Android, orientación resuelta). Probado en
  el teléfono: título, menús, selección de partida y el prólogo de Celeste a 60 FPS con mando.
  La app descarga el runtime .NET (33 MB, release `runtime-mono-x64-1`) la primera vez que se abre
  un juego .NET, lo comprueba con SHA-256 y lo instala en su propia carpeta. Falta: sonido (FMOD
  es propietario y el shim es mudo; tu propio FMOD 1.10 x64 para Windows en
  `/sdcard/HyperPlay/fmod` lo sustituye).
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
- **Hollow Knight: Silksong arranca sin internet.** Alrededor de 1 de cada 5 arranques se quedaba
  en pantalla negra (con o sin internet; parecía un problema de red). Causa:
  `TryEnterCriticalSection` esperaba como `EnterCriticalSection` y el recolector de memoria de
  Mono, que la usa mientras pausa hilos, esperaba para siempre. Probado en el teléfono: 10 de 10
  arranques sin internet llegan al menú (~115 FPS) y una partida nueva se puede jugar.
- Arreglado un cierre cuando un juego abría un archivo con la pila de llamadas muy llena
  (Silksong al empezar partida): el código de panorámica de Cuphead usaba 64 KB de pila en cada
  apertura de archivo. El hilo del juego y los hilos que crean los juegos tienen ahora también
  más pila nativa.
- Un hilo que ejecuta código del juego desde dentro de una llamada nativa (callbacks,
  excepciones) vuelve a marcarse "en código nativo" al terminar, así pausarlo nunca espera para
  siempre.
- **Celeste tiene sonido, sin FMOD.** HyperPlay tiene ahora su propio FMOD Studio (código nativo,
  sin código de Firelight): lee los bancos de sonido del juego, decodifica sus sonidos Vorbis con
  Android y los mezcla a 48 kHz. Probado en el teléfono: ambiente del título (en bucle), música
  del menú y sonidos de la interfaz. Incluye: eventos y líneas de tiempo, variaciones aleatorias,
  eventos anidados, bucles y puntos de sostén, volumen y pausa de buses/VCA, música larga
  decodificada por bloques y un limitador suave. Probado en el prólogo: pasos, salto, aterrizaje,
  ambiente y música. Además: parámetros del juego (las capas de la música entran y salen como en
  PC; valores por defecto leídos del banco), sonidos elegidos por un parámetro y posición en
  estéreo (izquierda/derecha, más bajo fuera de la pantalla), los buses del mezclador y sus
  faders, snapshots (el menú de pausa baja la música y el ambiente; momentos de la historia bajan
  el ambiente) y música que cambia de sección cuando el juego lo indica. No hecho: efectos como la
  reverberación. Comprobado el guardado de Celeste: "Save and Quit" escribe la
  partida y se carga al reiniciar.
- Pantalla de carga: los juegos que dibujan con su propio Vulkan (Celeste) la quitan en cuanto
  muestran algo (antes podía durar hasta 45 s).
- Arreglado: Mono a veces abortaba (`mono-mmap-windows.c:132`) porque `VirtualQuery` daba por
  libre una región recién reservada; y el listado de una carpeta podía salir vacío al reutilizarse
  un número de handle (Celeste: "Missing Language Files").
- **Celeste vuelve a guardar.** Al cambiar una opción salía "Save Failed": Mono crea la carpeta
  `Saves` con una ruta larga de Win32 (`\\?\D:\Saves`) y HyperPlay no entendía el prefijo `\\?\`.
  Probado en el teléfono: una opción cambiada en Celeste sigue igual tras reiniciar el juego.
- Pantalla de carga: fuera el texto técnico del motor; ahora muestra una etapa amable
  ("Preparando el juego…", "Ya casi está…") y una frase divertida que cambia cada pocos segundos.
- Arreglada la detección de juegos .NET: los ejecutables mixtos C++/CLI (DmC: Devil May Cry) se
  arrancaban con Mono. Ahora solo usan Mono los ensamblados solo IL.
- Juegos de 32 bits: sus DLL se colocan siempre por debajo de 4 GB (antes algunas DLL de PhysX de
  DmC quedaban por encima) y la tabla de importaciones de una DLL de 32 bits se escribe con
  ranuras de 4 bytes (DmC se cerraba al cargar sus DLL de wxWidgets). DmC aún no arranca (faltan
  funciones de Windows).
- **Los informes de errores llegan al servidor.** Con tu permiso, los informes se envían al
  servidor de informes de HyperPlay, que abre un issue en GitHub (sin token en la app). Para
  ocupar poco en el teléfono: el registro de la app se vacía al enviar los informes, se guardan
  como mucho 20 informes sin enviar (de menos de 30 días) y solo los 5 últimos logs de sesión.

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
