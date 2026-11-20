# .NET runtime for HyperPlay (Mono x64 + FNA) / Runtime .NET para HyperPlay (Mono x64 + FNA)

## English

.NET Framework games such as Celeste ship only IL code. HyperPlay runs them with **Mono x64 for
Windows**, translated by the Oryon JIT like Unity's Mono, and replaces XNA with **FNA**. The game
draws with FNA3D/SDL GPU over Vulkan, which HyperPlay passes to the phone's Vulkan
(`app/src/main/cpp/loader/VulkanPassthrough.cpp`). See `app/src/main/cpp/loader/ManagedHost.h`.

Runtime layout on the phone (`/storage/emulated/0/HyperPlay/runtime/mono`):

| Path | Content |
|---|---|
| `bin/` | `mono.exe`, `mono-2.0-sgen.dll` (Mono 6.12 x64), `ucrtbase.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` (Microsoft VC++ x64 redistributable) |
| `lib/mono/4.5/` | Mono class libraries (with `Facades/`) |
| `etc/mono/` | `config`, `4.5/machine.config` |
| `fna/` | `FNA.dll`, `FNA.NetStub.dll`, the `Microsoft.Xna.Framework*.dll` forwarders (FNA `abi/`), `SDL3.dll`, `FNA3D.dll`, `FAudio.dll`, `libtheorafile.dll` (fnalibs x64), and the x64 shims `CSteamworks.dll`, `fmod.dll`, `fmodstudio.dll` |

How it was built:

1. Mono 6.12.0.206 x64 MSI from download.mono-project.com, extracted with `msiexec /a` (no install).
2. FNA (github.com/FNA-XNA/FNA) and FNA.NetStub built with Mono's `mcs` (FNA `Makefile` source
   list); the `abi/*.cs` forwarders built against them.
3. fnalibs from the latest "CI" run of github.com/FNA-XNA/fnalibs-dailies (`x64/`).
4. Shims: `exports.py <game 32-bit dll> <list>` lists the exports; `genstub.py <list> <out.c> steam|fmod`
   writes a C stub; build it with the NDK clang:
   `clang --target=x86_64-pc-windows-msvc -O1 -c out.c` and
   `ld.lld -flavor link /dll /noentry /nodefaultlib /out:X.dll out.obj`.
   The Steam shim reports Steam as running (fake interfaces); the FMOD shim is silent for now.

Distribution: `pack-runtime.ps1` builds `hyperplay-runtime-mono-x64-N.zip` (with `THIRD_PARTY.txt`
and FNA's licenses), published as the prerelease `runtime-mono-x64-N`. The app downloads it the
first time a .NET game opens (`ManagedRuntime.kt`, SHA-256 checked) into
`Android/data/<package>/files/runtime/mono` (case-insensitive storage, like Windows).

Audio: FMOD is proprietary and is not distributed. If you put `fmod64.dll` and
`fmodstudio64.dll` from "FMOD Engine 1.10.x" for Windows (fmod.com, free account) in
`/sdcard/HyperPlay/fmod/`, HyperPlay uses them instead of the silent shims.

## Español

Los juegos de .NET Framework como Celeste solo traen código IL. HyperPlay los ejecuta con **Mono
x64 para Windows**, traducido por el JIT Oryon como el Mono de Unity, y sustituye XNA por **FNA**.
El juego dibuja con FNA3D/SDL GPU sobre Vulkan, que HyperPlay pasa al Vulkan del teléfono
(`app/src/main/cpp/loader/VulkanPassthrough.cpp`). Ver `app/src/main/cpp/loader/ManagedHost.h`.

Estructura del runtime en el teléfono (`/storage/emulated/0/HyperPlay/runtime/mono`):

| Ruta | Contenido |
|---|---|
| `bin/` | `mono.exe`, `mono-2.0-sgen.dll` (Mono 6.12 x64), `ucrtbase.dll`, `vcruntime140.dll`, `vcruntime140_1.dll` (redistribuible VC++ x64 de Microsoft) |
| `lib/mono/4.5/` | Bibliotecas de clases de Mono (con `Facades/`) |
| `etc/mono/` | `config`, `4.5/machine.config` |
| `fna/` | `FNA.dll`, `FNA.NetStub.dll`, los reenvíos `Microsoft.Xna.Framework*.dll` (`abi/` de FNA), `SDL3.dll`, `FNA3D.dll`, `FAudio.dll`, `libtheorafile.dll` (fnalibs x64) y los shims x64 `CSteamworks.dll`, `fmod.dll`, `fmodstudio.dll` |

Cómo se construyó:

1. MSI de Mono 6.12.0.206 x64 de download.mono-project.com, extraído con `msiexec /a` (sin instalar).
2. FNA (github.com/FNA-XNA/FNA) y FNA.NetStub compilados con `mcs` de Mono (lista de fuentes del
   `Makefile` de FNA); los reenvíos `abi/*.cs` compilados contra ellos.
3. fnalibs de la última ejecución "CI" de github.com/FNA-XNA/fnalibs-dailies (`x64/`).
4. Shims: `exports.py <dll de 32 bits del juego> <lista>` lista las exportaciones;
   `genstub.py <lista> <out.c> steam|fmod` escribe un stub en C; se compila con el clang del NDK:
   `clang --target=x86_64-pc-windows-msvc -O1 -c out.c` y
   `ld.lld -flavor link /dll /noentry /nodefaultlib /out:X.dll out.obj`.
   El shim de Steam dice que Steam está activo (interfaces falsas); el de FMOD es mudo por ahora.

Distribución: `pack-runtime.ps1` crea `hyperplay-runtime-mono-x64-N.zip` (con `THIRD_PARTY.txt` y
las licencias de FNA), publicado como prerelease `runtime-mono-x64-N`. La app lo descarga la
primera vez que se abre un juego .NET (`ManagedRuntime.kt`, con SHA-256) en
`Android/data/<paquete>/files/runtime/mono` (almacenamiento sin mayúsculas, como Windows).

Sonido: FMOD es propietario y no se distribuye. Si pones `fmod64.dll` y `fmodstudio64.dll` de
"FMOD Engine 1.10.x" para Windows (fmod.com, cuenta gratuita) en `/sdcard/HyperPlay/fmod/`,
HyperPlay los usa en lugar de los shims mudos.
