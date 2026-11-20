# Cambios

## 1.0.0 — primera versión pública

- Motor directo para juegos de Windows x86/x64 en Android (cargador PE, IAT, JIT a ARM64 con caché).
- FPU x87 real en el JIT (antes `Math.Sqrt` de C# devolvía el cuadrado: rebotes infinitos, física rara).
- Interfaz de consola portátil con portadas e información oficial de los juegos.
- Pantalla de carga propia hasta que el juego dibuja.
- Mandos: gatillos en Z/RZ para mandos HID, vibración del teléfono si el mando no vibra.
- Actualizaciones dentro de la app desde GitHub Releases.
- Registro de errores en el teléfono e informes opcionales a GitHub (vía Cloudflare Worker).
