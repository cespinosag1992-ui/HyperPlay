#pragma once

// Juegos .NET (Celeste: .NET Framework 4 + XNA): el .exe solo trae IL y su entrada salta a
// mscoree!_CorExeMain. HyperPlay no tiene el CLR de Microsoft; en su lugar ejecuta Mono x64 para
// Windows (traducido por el JIT Oryon, igual que el Mono de Unity) con el juego como argumento:
//   "C:\HyperPlay\Mono\bin\mono.exe" "D:\Celeste.exe"
// XNA se sustituye por FNA (reenvíos de tipos Microsoft.Xna.Framework.* -> FNA.dll).
//
// Montajes de rutas Windows:
//   D:\                  -> carpeta del juego (como siempre)
//   C:\HyperPlay\Mono\   -> g_runtimeDir (bin/, lib/mono/4.5, etc/, fna/)

#include <strings.h>

#include <cstring>
#include <string>

namespace hyperplay::managed {

inline std::string g_runtimeDir;  // ruta del host del runtime Mono; vacía = modo normal
inline std::string g_gameDir;     // carpeta del juego .NET (D:\)
inline std::string g_gameExe;     // nombre del .exe del juego (Celeste.exe)

inline constexpr char kWinRoot[] = "C:\\HyperPlay\\Mono";

inline bool active() noexcept { return !g_runtimeDir.empty(); }

/// FMOD real del usuario (propietario: no se distribuye). Si deja aquí fmod64.dll y
/// fmodstudio64.dll de "FMOD Engine 1.10.x" para Windows, sustituyen a los shims mudos.
inline constexpr char kUserFmodDir[] = "/storage/emulated/0/HyperPlay/fmod";

/// DLL del usuario que sustituye a `lowerBase` (fmod.dll -> fmod64.dll...). Vacía si no hay.
inline std::string userOverride(const std::string& lowerBase) {
    if (!active()) return {};
    if (lowerBase == "fmod.dll" || lowerBase == "fmod" || lowerBase == "fmod64.dll")
        return std::string(kUserFmodDir) + "/fmod64.dll";
    if (lowerBase == "fmodstudio.dll" || lowerBase == "fmodstudio" || lowerBase == "fmodstudio64.dll")
        return std::string(kUserFmodDir) + "/fmodstudio64.dll";
    return {};
}

inline void reset() {
    g_runtimeDir.clear();
    g_gameDir.clear();
    g_gameExe.clear();
}

/// Ruta normalizada (con '/') bajo el montaje del runtime -> ruta del host. Resuelve "." y ".."
/// sin salir del runtime. Vacía si no es una ruta del montaje.
inline std::string resolveMount(const std::string& norm) {
    if (!active()) return {};
    static constexpr char kPrefix[] = "c:/hyperplay/mono";
    constexpr size_t n = sizeof(kPrefix) - 1;
    if (norm.size() < n || strncasecmp(norm.c_str(), kPrefix, n) != 0) return {};
    if (norm.size() > n && norm[n] != '/') return {};
    std::string out;
    size_t start = n;
    while (start < norm.size()) {
        while (start < norm.size() && norm[start] == '/') ++start;
        const size_t slash = norm.find('/', start);
        const std::string part = norm.substr(start, slash == std::string::npos ? std::string::npos : slash - start);
        if (part == "..") {
            const size_t cut = out.find_last_of('/');
            out.erase(cut == std::string::npos ? 0 : cut);
        } else if (!part.empty() && part != ".") {
            out += "/" + part;
        }
        if (slash == std::string::npos) break;
        start = slash + 1;
    }
    return g_runtimeDir + out;
}

/// Ruta del host dentro del runtime -> ruta Windows bajo C:\HyperPlay\Mono. Vacía si no lo es.
inline std::string toWinPath(const std::string& hostPath) {
    if (!active() || hostPath.compare(0, g_runtimeDir.size(), g_runtimeDir) != 0) return {};
    std::string rest = hostPath.substr(g_runtimeDir.size());
    for (char& c : rest) if (c == '/') c = '\\';
    return std::string(kWinRoot) + rest;
}

}  // namespace hyperplay::managed
