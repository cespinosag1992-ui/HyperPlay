#include "NativeSteamStub.h"

#include <android/log.h>
#include <fcntl.h>
#include <unistd.h>

#include <cstdlib>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>

#define LOG_TAG "HyperPlay.SteamStub"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace {

uint32_t s_appId = 480;
uint64_t s_steamId = 0x0110000100000001ULL;

// Tablas de las interfaces Steam con el layout de vtable esperado por steam_api.
struct MockVTable {
    void* funcs[64];
};

uint64_t steamUser_GetSteamID() { return s_steamId; }
bool steamUser_BLoggedOn() { return true; }

uint32_t steamUtils_GetAppID() { return s_appId; }
uint32_t steamUtils_GetServerRealTime() { return 1731600000; }
const char* steamUtils_GetIPCountry() { return "US"; }

bool steamApps_BIsSubscribed() { return true; }
bool steamApps_BIsLowViolence() { return false; }
bool steamApps_BIsCybercafe() { return false; }
bool steamApps_BIsVACBanned() { return false; }
const char* steamApps_GetCurrentGameLanguage() { return "english"; }
const char* steamApps_GetAvailableGameLanguages() { return "english,spanish"; }
bool steamApps_BIsSubscribedApp(uint32_t) { return true; }
bool steamApps_BIsDlcInstalled(uint32_t) { return true; }

const char* steamFriends_GetPersonaName() { return "HyperPlayUser"; }

bool steamStats_RequestCurrentStats() { return true; }
bool steamStats_GetStatInt32(const char*, int32_t* val) { if (val) *val = 0; return true; }
bool steamStats_GetStatFloat(const char*, float* val) { if (val) *val = 0.0f; return true; }
bool steamStats_SetStatInt32(const char*, int32_t) { return true; }
bool steamStats_SetStatFloat(const char*, float) { return true; }
bool steamStats_StoreStats() { return true; }

MockVTable s_userVTable;
MockVTable s_utilsVTable;
MockVTable s_appsVTable;
MockVTable s_friendsVTable;
MockVTable s_statsVTable;

void* s_userPtr = nullptr;
void* s_utilsPtr = nullptr;
void* s_appsPtr = nullptr;
void* s_friendsPtr = nullptr;
void* s_statsPtr = nullptr;

bool s_initialized = false;

void initMockInterfaces() {
    if (s_initialized) return;

    std::memset(&s_userVTable, 0, sizeof(s_userVTable));
    s_userVTable.funcs[1] = reinterpret_cast<void*>(&steamUser_BLoggedOn);
    s_userVTable.funcs[2] = reinterpret_cast<void*>(&steamUser_GetSteamID);
    static void* userObj = &s_userVTable;
    s_userPtr = &userObj;

    std::memset(&s_utilsVTable, 0, sizeof(s_utilsVTable));
    s_utilsVTable.funcs[1] = reinterpret_cast<void*>(&steamUtils_GetServerRealTime);
    s_utilsVTable.funcs[2] = reinterpret_cast<void*>(&steamUtils_GetIPCountry);
    s_utilsVTable.funcs[9] = reinterpret_cast<void*>(&steamUtils_GetAppID);
    static void* utilsObj = &s_utilsVTable;
    s_utilsPtr = &utilsObj;

    std::memset(&s_appsVTable, 0, sizeof(s_appsVTable));
    s_appsVTable.funcs[0] = reinterpret_cast<void*>(&steamApps_BIsSubscribed);
    s_appsVTable.funcs[1] = reinterpret_cast<void*>(&steamApps_BIsLowViolence);
    s_appsVTable.funcs[2] = reinterpret_cast<void*>(&steamApps_BIsCybercafe);
    s_appsVTable.funcs[3] = reinterpret_cast<void*>(&steamApps_BIsVACBanned);
    s_appsVTable.funcs[4] = reinterpret_cast<void*>(&steamApps_GetCurrentGameLanguage);
    s_appsVTable.funcs[5] = reinterpret_cast<void*>(&steamApps_GetAvailableGameLanguages);
    s_appsVTable.funcs[6] = reinterpret_cast<void*>(&steamApps_BIsSubscribedApp);
    s_appsVTable.funcs[7] = reinterpret_cast<void*>(&steamApps_BIsDlcInstalled);
    static void* appsObj = &s_appsVTable;
    s_appsPtr = &appsObj;

    std::memset(&s_friendsVTable, 0, sizeof(s_friendsVTable));
    s_friendsVTable.funcs[0] = reinterpret_cast<void*>(&steamFriends_GetPersonaName);
    static void* friendsObj = &s_friendsVTable;
    s_friendsPtr = &friendsObj;

    std::memset(&s_statsVTable, 0, sizeof(s_statsVTable));
    s_statsVTable.funcs[0] = reinterpret_cast<void*>(&steamStats_RequestCurrentStats);
    s_statsVTable.funcs[1] = reinterpret_cast<void*>(&steamStats_GetStatInt32);
    s_statsVTable.funcs[2] = reinterpret_cast<void*>(&steamStats_GetStatFloat);
    s_statsVTable.funcs[3] = reinterpret_cast<void*>(&steamStats_SetStatInt32);
    s_statsVTable.funcs[4] = reinterpret_cast<void*>(&steamStats_SetStatFloat);
    s_statsVTable.funcs[5] = reinterpret_cast<void*>(&steamStats_StoreStats);
    static void* statsObj = &s_statsVTable;
    s_statsPtr = &statsObj;

    s_initialized = true;
}

}  // namespace

void NativeSteamStub::init(const std::string& gameDir) {
    initMockInterfaces();

    // Intentar leer steam_appid.txt en la carpeta del juego
    const std::string appidFile = gameDir + "/steam_appid.txt";
    std::ifstream f(appidFile);
    if (f.is_open()) {
        uint32_t id = 0;
        if (f >> id && id > 0) {
            s_appId = id;
            LOGI("SteamAppID leído desde %s: %u", appidFile.c_str(), s_appId);
        }
    } else {
        LOGI("steam_appid.txt no encontrado; usando AppID por defecto: %u", s_appId);
    }
}

void NativeSteamStub::shutdown() {
    // No-op
}

bool NativeSteamStub::SteamAPI_Init() {
    initMockInterfaces();
    LOGI("SteamAPI_Init(): Steam emulado sin cliente real");
    return true;
}

int32_t NativeSteamStub::SteamAPI_InitFlat(char* outErrMsg) {
    if (outErrMsg) outErrMsg[0] = '\0';
    return 0; // k_ESteamAPIInitResult_OK
}

void NativeSteamStub::SteamAPI_Shutdown() {
    LOGI("SteamAPI_Shutdown() llamado");
}

bool NativeSteamStub::SteamAPI_RestartAppIfNecessary(uint32_t) {
    // Retornar false indica que el juego NO necesita reiniciar mediante el cliente de Steam
    return false;
}

bool NativeSteamStub::SteamAPI_IsSteamRunning() {
    return true;
}

void* NativeSteamStub::SteamClient() {
    return nullptr;
}

void* NativeSteamStub::SteamUser() {
    initMockInterfaces();
    return s_userPtr;
}

void* NativeSteamStub::SteamFriends() {
    initMockInterfaces();
    return s_friendsPtr;
}

void* NativeSteamStub::SteamUtils() {
    initMockInterfaces();
    return s_utilsPtr;
}

void* NativeSteamStub::SteamUserStats() {
    initMockInterfaces();
    return s_statsPtr;
}

void* NativeSteamStub::SteamApps() {
    initMockInterfaces();
    return s_appsPtr;
}

void NativeSteamStub::SteamAPI_RunCallbacks() {
    // No-op en stub nativo
}

uint32_t NativeSteamStub::getAppId() {
    return s_appId;
}

void* NativeSteamStub::resolveSymbol(const std::string& name) {
    initMockInterfaces();

    if (name == "SteamAPI_Init") return reinterpret_cast<void*>(&SteamAPI_Init);
    if (name == "SteamAPI_InitFlat") return reinterpret_cast<void*>(&SteamAPI_InitFlat);
    if (name == "SteamAPI_Shutdown") return reinterpret_cast<void*>(&SteamAPI_Shutdown);
    if (name == "SteamAPI_RestartAppIfNecessary") return reinterpret_cast<void*>(&SteamAPI_RestartAppIfNecessary);
    if (name == "SteamAPI_IsSteamRunning") return reinterpret_cast<void*>(&SteamAPI_IsSteamRunning);
    if (name == "SteamClient") return reinterpret_cast<void*>(&SteamClient);
    if (name == "SteamUser") return reinterpret_cast<void*>(&SteamUser);
    if (name == "SteamFriends") return reinterpret_cast<void*>(&SteamFriends);
    if (name == "SteamUtils") return reinterpret_cast<void*>(&SteamUtils);
    if (name == "SteamUserStats") return reinterpret_cast<void*>(&SteamUserStats);
    if (name == "SteamApps") return reinterpret_cast<void*>(&SteamApps);
    if (name == "SteamAPI_RunCallbacks") return reinterpret_cast<void*>(&SteamAPI_RunCallbacks);

    return nullptr;
}

}  // namespace hyperplay
