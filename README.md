# HyperPlay

**Juegos de PC (Windows `.exe`) directamente en Android**, sin Wine, sin Linux invitado, sin
contenedores. HyperPlay carga el ejecutable en memoria, traduce el código x86/x64 a ARM64 con un
JIT propio y conecta Direct3D, audio y mandos directamente con Vulkan, AAudio y el mando de Android.

> Estado: **alfa**. Optimizado para Snapdragon 8 Elite (Adreno 830). Juegos probados: Hollow
> Knight y Cuphead (Unity, Direct3D 11).

## Características

- **Motor directo** (`libhyperplay.so`): cargador PE32/PE32+, interceptación de la IAT de Windows
  (kernel32, ntdll, D3D11/DXGI, XInput, WASAPI, Steam), JIT x86/x64 → ARM64 con caché en disco,
  FPU x87, SSE hasta 4.2 y FMA.
- **Gráficos**: D3D11 → Vulkan en la GPU (DXBC → SPIR-V), reescalado SGSR y nitidez CAS,
  generación de fotogramas HyperFG.
- **Interfaz de consola portátil**: biblioteca con portadas e información oficial de cada juego,
  pantalla de carga propia, Quick Resume (el juego queda en pausa en memoria) y menú rápido.
- **Mandos**: Xbox, PlayStation, GameSir, 8BitDo y HID genéricos (Bluetooth o USB), con mapeo
  automático, gatillos analógicos y vibración; mando táctil que aparece al tocar la pantalla.
- **Protección térmica**, límite de FPS y overlay de rendimiento.
- **Actualizaciones dentro de la app** desde GitHub Releases, instaladas por encima sin perder datos.
- **Informes de errores** opcionales (registro siempre guardado en el teléfono).

## Instalar

1. Descarga el `HyperPlay-vX.Y.Z.apk` de la última [Release](../../releases/latest).
2. Instálalo (Android 12 o superior, ARM64). La primera vez Android pedirá permitir apps de
   origen desconocido.
3. Concede **Acceso a todos los archivos**: los juegos se abren desde su carpeta y las partidas se
   guardan junto al juego (`_HyperPlaySaves`).
4. Copia la carpeta de un juego de PC (con su `.exe`) a `/sdcard/PC games/` y pulsa **Buscar juegos**.

Las siguientes versiones se instalan desde **Ajustes → Actualizaciones** dentro de la app.

> HyperPlay **no incluye juegos**. Usa solo juegos que poseas legalmente.

## Compilar

Requisitos: Android Studio / SDK 35, **NDK 28.2.13676358**, CMake 3.22.1, JDK 17 o superior.

```bash
./gradlew testDebugUnitTest lintDebug assembleDebug
```

El APK firmado de release necesita `keystore.properties` en la raíz (no se versiona); ver
[docs/PUBLICAR.md](docs/PUBLICAR.md).

## Estructura

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

## Informes de errores

Si el usuario lo acepta, la app envía cada crash, cierre inesperado o juego que termina con error
a un Worker de Cloudflare que abre un Issue con la etiqueta `auto-report`. Ver
[server/report-worker](server/report-worker/README.md). Para avisar de un fallo a mano, abre un
[Issue](../../issues/new/choose).

## Licencia

[GPL-3.0](LICENSE). Los parches de Mesa de `third_party/mesa-patches` siguen la licencia MIT de Mesa.
