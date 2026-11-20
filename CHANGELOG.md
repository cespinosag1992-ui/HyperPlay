# Changelog / Cambios

Each version is described in English first, then in Spanish. The release workflow copies the
section of the tagged version into the GitHub Release notes.

Cada versión se describe primero en inglés y luego en español. El workflow de release copia la
sección de la versión etiquetada en las notas de la Release de GitHub.

## 1.0.0

**English** — first public version

- Direct engine for Windows x86/x64 games on Android (PE loader, IAT, ARM64 JIT with cache).
- Real x87 FPU in the JIT (before, C# `Math.Sqrt` returned the square: endless bounces, odd physics).
- Handheld-console UI with official covers and game info; your own logo; full gamepad navigation.
- Custom loading screen until the game draws.
- The app follows the system language (English and Spanish), including the Steam game info.
- No default game folders: on first launch you choose the folder where your games are.
- Controllers: Z/RZ triggers for HID pads, phone vibration if the controller doesn't vibrate.
- In-app updates from GitHub Releases.
- Error log on the phone and optional reports to GitHub (via a Cloudflare Worker).

**Español** — primera versión pública

- Motor directo para juegos de Windows x86/x64 en Android (cargador PE, IAT, JIT a ARM64 con caché).
- FPU x87 real en el JIT (antes `Math.Sqrt` de C# devolvía el cuadrado: rebotes infinitos, física rara).
- Interfaz de consola portátil con portadas e información oficial de los juegos; logo propio;
  navegación completa con mando.
- Pantalla de carga propia hasta que el juego dibuja.
- La app usa el idioma del sistema (inglés y español), también en la ficha de Steam del juego.
- Sin carpetas de juegos por defecto: en el primer arranque eliges la carpeta donde están tus juegos.
- Mandos: gatillos en Z/RZ para mandos HID, vibración del teléfono si el mando no vibra.
- Actualizaciones dentro de la app desde GitHub Releases.
- Registro de errores en el teléfono e informes opcionales a GitHub (vía Cloudflare Worker).
