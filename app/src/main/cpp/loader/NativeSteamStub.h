#pragma once

#include <cstdint>
#include <string>

namespace hyperplay {

class NativeSteamStub {
public:
    static void init(const std::string& gameDir);
    static void shutdown();

    /// Resuelve cualquier función exportada de steam_api / steam_api64 a nuestro trampoline nativo.
    [[nodiscard]] static void* resolveSymbol(const std::string& functionName);

    // Métodos nativos expuestos
    static bool SteamAPI_Init();
    static int32_t SteamAPI_InitFlat(char* outErrMsg);
    static void SteamAPI_Shutdown();
    static bool SteamAPI_RestartAppIfNecessary(uint32_t ownAppId);
    static bool SteamAPI_IsSteamRunning();
    static void* SteamClient();
    static void* SteamUser();
    static void* SteamFriends();
    static void* SteamUtils();
    static void* SteamUserStats();
    static void* SteamApps();
    static void SteamAPI_RunCallbacks();

    static uint32_t getAppId();
};

}  // namespace hyperplay
