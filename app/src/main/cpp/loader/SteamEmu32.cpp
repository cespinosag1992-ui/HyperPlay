// Steam sin cliente para juegos de 32 bits (x86). Los de 64 bits siguen con NativeSteamStub, que no
// se toca. Aquí las interfaces (ISteamApps, ISteamUser...) son objetos por debajo de 4 GB cuya vtable
// apunta a funciones nativas con la aridad exacta de cada método. En x86 los métodos son thiscall:
// `this` va en ECX y el método saca sus argumentos de la pila (como stdcall), así que el número de
// ranuras de cada función tiene que ser exacto. El orden de los métodos es el del SDK público de
// Steamworks de 2012-2013 (comprobado con DmC: ISteamApps+0x18 = BIsSubscribedApp(220440)).
//
// Tipos en las firmas: CSteamID/CGameID/SteamAPICall_t/uint64 por valor = int64_t (2 ranuras).
// Las funciones que devuelven un CSteamID (clase de 8 bytes) reciben en MSVC un puntero oculto de
// retorno como primer argumento y lo devuelven. bool se devuelve como uint32_t (EAX completo).

#include "SteamEmu32.h"

#include <android/log.h>
#include <dirent.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "LowMem.h"
#include "NativeSteamStub.h"
#include "Win32Arity.h"

#define LOG_TAG "HyperPlay.Steam32"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace hyperplay {
namespace w32alias {
uintptr_t lowFor(void* host, const std::string& name);  // NativeDllInterceptor.cpp
}

namespace steamemu32 {
namespace {

using u32 = uint32_t;
using i64 = int64_t;
constexpr uint64_t kSteamId = 76561197960287930ull;  // cuenta local ficticia

std::string g_saveDir;  // <juego>/_HyperPlaySaves

// --- Memoria baja para objetos, vtables y cadenas devueltas al juego. ---
uint8_t* lowAlloc(size_t n) {
    static std::mutex m;
    static uint8_t* cur = nullptr;
    static size_t left = 0;
    std::lock_guard<std::mutex> lk(m);
    n = (n + 7) & ~size_t{7};
    if (cur == nullptr || left < n) {
        void* p = lowmem::mmapBelow4G(0x10000);
        if (p == MAP_FAILED) return nullptr;
        cur = static_cast<uint8_t*>(p);
        left = 0x10000;
    }
    uint8_t* r = cur;
    cur += n;
    left -= n;
    return r;
}
const char* lowStr(const std::string& s) {
    static std::mutex m;
    static std::unordered_map<std::string, const char*> cache;
    std::lock_guard<std::mutex> lk(m);
    if (const auto it = cache.find(s); it != cache.end()) return it->second;
    auto* p = reinterpret_cast<char*>(lowAlloc(s.size() + 1));
    if (p == nullptr) return "";
    std::memcpy(p, s.c_str(), s.size() + 1);
    cache[s] = p;
    return p;
}
u32 L(const char* s) { return static_cast<u32>(reinterpret_cast<uintptr_t>(lowStr(s))); }
void* retId(void* ret, uint64_t v) {
    if (ret != nullptr) std::memcpy(ret, &v, 8);
    return ret;
}

// --- Respuestas genéricas por aridad (devuelven 0). ---
u32 Z0() { return 0; }
u32 Z1(u32) { return 0; }
u32 Z2(u32, u32) { return 0; }
u32 Z3(u32, u32, u32) { return 0; }
u32 Z4(u32, u32, u32, u32) { return 0; }
u32 Z5(u32, u32, u32, u32, u32) { return 0; }
u32 T0() { return 1; }
u32 T1(u32) { return 1; }
u32 T2(u32, u32) { return 1; }
u32 T3(u32, u32, u32) { return 1; }
u32 Zs(i64) { return 0; }                   // (CSteamID)
u32 Zsi(i64, u32) { return 0; }             // (CSteamID, x)
u32 Zss(i64, i64) { return 0; }             // (CSteamID, CSteamID)
u32 Ess(i64) { return L(""); }              // const char* (CSteamID)
u32 Essi(i64, u32) { return L(""); }        // const char* (CSteamID, x)
i64 Q0() { return 0; }                      // SteamAPICall_t / uint64 sin argumentos
i64 Q1(u32) { return 0; }
i64 Q2(u32, u32) { return 0; }
i64 Q3(u32, u32, u32) { return 0; }
i64 Qs(i64) { return 0; }
void* Rid2(void* ret, u32) { return retId(ret, 0); }          // CSteamID f(int)
void* Rid3(void* ret, i64) { return retId(ret, 0); }          // CSteamID f(CSteamID)
void* Rid4(void* ret, i64, u32) { return retId(ret, 0); }     // CSteamID f(CSteamID, int)

// Método fuera de las tablas conocidas: se registra (sin argumentos: si se usa, hay que añadirlo).
template <int I, int S>
u32 Unknown() {
    static const char* const kNames[] = {"Apps", "User", "Friends", "Utils", "UserStats", "RemoteStorage", "MMServers"};
    LOGW("[Steam32] ISteam%s método %d sin implementar", kNames[I], S);
    return 0;
}

// --- ISteamApps ---
u32 Apps_BIsSubscribedApp(u32 app) { return app == NativeSteamStub::getAppId() ? 1u : 0u; }
u32 Apps_Lang() { return L("english"); }

// --- ISteamUser ---
void* User_GetSteamID(void* ret) { return retId(ret, kSteamId); }
u32 User_InitiateGameConnection(u32, u32, i64, u32, u32, u32) { return 0; }
u32 User_TrackAppUsageEvent(i64, u32, u32) { return 0; }
u32 User_GetUserDataFolder(char* buf, u32 size) {
    static const char kPath[] = "C:\\users\\default\\AppData\\Local\\Steam\\userdata";
    if (buf == nullptr || size < sizeof(kPath)) return 0;
    std::memcpy(buf, kPath, sizeof(kPath));
    return 1;
}
u32 User_VoiceNotRecording3(u32, u32, u32) { return 2; }  // k_EVoiceResultNotRecording
u32 User_GetVoice(u32, u32, u32, u32, u32, u32, u32, u32, u32) { return 2; }
u32 User_DecompressVoice(u32, u32, u32, u32, u32, u32) { return 2; }
u32 User_SampleRate() { return 11025; }
u32 User_BeginAuthSession(u32, u32, i64) { return 0; }  // k_EBeginAuthSessionResultOK
u32 User_UserHasLicense(i64, u32) { return 0; }         // k_EUserHasLicenseResultHasLicense
u32 User_AdvertiseGame(i64, u32, u32) { return 0; }

// --- ISteamFriends ---
u32 Friends_PersonaName() { return L("Player"); }
u32 Friends_PersonaState() { return 1; }  // online
u32 Friends_ClanActivity(i64, u32, u32, u32) { return 0; }
u32 Friends_GetClanChatMessage(i64, u32, u32, u32, u32, u32) { return 0; }
u32 Friends_GetFriendMessage(i64, u32, u32, u32, u32) { return 0; }

// --- ISteamUtils ---
u32 Utils_Universe() { return 1; }
u32 Utils_ServerTime() { return static_cast<u32>(::time(nullptr)); }
u32 Utils_Country() { return L("US"); }
u32 Utils_Battery() { return 255; }  // corriente alterna
u32 Utils_AppId() { return NativeSteamStub::getAppId(); }
u32 Utils_IsAPICallCompleted(i64, u32* failed) {
    if (failed != nullptr) *failed = 1;
    return 0;
}
u32 Utils_GetAPICallFailureReason(i64) { return 0xFFFFFFFFu; }
u32 Utils_GetAPICallResult(i64, u32, u32, u32, u32* failed) {
    if (failed != nullptr) *failed = 1;
    return 0;
}

// --- ISteamUserStats (estadísticas y logros solo en memoria) ---
std::mutex g_statsMutex;
std::unordered_map<std::string, u32> g_stats;
std::unordered_map<std::string, u32> g_ach;
std::string key(const char* n) { return n != nullptr ? n : ""; }
u32 Stats_GetStat(const char* n, u32* out) {
    std::lock_guard<std::mutex> lk(g_statsMutex);
    if (out != nullptr) *out = g_stats[key(n)];
    return 1;
}
u32 Stats_SetStat(const char* n, u32 v) {
    std::lock_guard<std::mutex> lk(g_statsMutex);
    g_stats[key(n)] = v;
    return 1;
}
u32 Stats_UpdateAvgRate(u32, u32, i64) { return 1; }
u32 Stats_GetAchievement(const char* n, u32* achieved) {
    std::lock_guard<std::mutex> lk(g_statsMutex);
    if (achieved != nullptr) *reinterpret_cast<uint8_t*>(achieved) = g_ach[key(n)] != 0 ? 1 : 0;
    return 1;
}
u32 Stats_SetAchievement(const char* n) {
    std::lock_guard<std::mutex> lk(g_statsMutex);
    g_ach[key(n)] = 1;
    LOGI("[Steam32] logro: %s", key(n).c_str());
    return 1;
}
u32 Stats_ClearAchievement(const char* n) {
    std::lock_guard<std::mutex> lk(g_statsMutex);
    g_ach[key(n)] = 0;
    return 1;
}
u32 Stats_GetAchievementAndUnlockTime(const char* n, u32* achieved, u32* t) {
    Stats_GetAchievement(n, achieved);
    if (t != nullptr) *t = 0;
    return 1;
}
u32 Stats_DisplayAttribute(u32, u32) { return L(""); }
u32 Stats_GetUserStat(i64, u32, u32* out) {
    if (out != nullptr) *out = 0;
    return 0;
}
u32 Stats_GetUserAchievementAndTime(i64, u32, u32, u32) { return 0; }
u32 Stats_LeaderboardName(i64) { return L(""); }
u32 Stats_Zq(i64) { return 0; }
i64 Stats_Download(i64, u32, u32, u32) { return 0; }
i64 Stats_DownloadForUsers(i64, u32, u32) { return 0; }
u32 Stats_GetDownloadedEntry(i64, u32, u32, u32, u32) { return 0; }
i64 Stats_Upload(i64, u32, u32, u32, u32) { return 0; }
i64 Stats_AttachUGC(i64, i64) { return 0; }
u32 Stats_MostAchieved(u32, u32, u32, u32) { return 0xFFFFFFFFu; }
u32 Stats_NextMostAchieved(u32, u32, u32, u32, u32) { return 0xFFFFFFFFu; }

// --- ISteamRemoteStorage: archivos reales en <guardado>/steam_cloud ---
std::string cloudPath(const char* name) {
    std::string n = key(name);
    for (char& c : n) if (c == '\\' || c == ':') c = '_';  // un solo nivel, sin rutas
    if (n.find("..") != std::string::npos) n = "_";
    const std::string dir = g_saveDir + "/steam_cloud";
    mkdir(dir.c_str(), 0755);
    return dir + "/" + n;
}
u32 RS_FileWrite(const char* name, const void* data, u32 len) {
    FILE* f = std::fopen(cloudPath(name).c_str(), "wb");
    if (f == nullptr) return 0;
    const size_t w = len > 0 && data != nullptr ? std::fwrite(data, 1, len, f) : 0;
    std::fclose(f);
    LOGI("[Steam32] nube: escrito %s (%u bytes)", key(name).c_str(), len);
    return w == len ? 1u : 0u;
}
u32 RS_FileRead(const char* name, void* data, u32 max) {
    FILE* f = std::fopen(cloudPath(name).c_str(), "rb");
    if (f == nullptr || data == nullptr) {
        if (f != nullptr) std::fclose(f);
        return 0;
    }
    const size_t r = std::fread(data, 1, max, f);
    std::fclose(f);
    return static_cast<u32>(r);
}
u32 RS_FileDelete(const char* name) { return unlink(cloudPath(name).c_str()) == 0 ? 1u : 0u; }
u32 RS_FileExists(const char* name) {
    struct stat st{};
    return stat(cloudPath(name).c_str(), &st) == 0 ? 1u : 0u;
}
u32 RS_GetFileSize(const char* name) {
    struct stat st{};
    return stat(cloudPath(name).c_str(), &st) == 0 ? static_cast<u32>(st.st_size) : 0u;
}
i64 RS_GetFileTimestamp(const char* name) {
    struct stat st{};
    return stat(cloudPath(name).c_str(), &st) == 0 ? static_cast<i64>(st.st_mtime) : 0;
}
std::vector<std::string> cloudFiles() {
    std::vector<std::string> v;
    if (DIR* d = opendir((g_saveDir + "/steam_cloud").c_str()); d != nullptr) {
        while (struct dirent* e = readdir(d)) {
            if (e->d_name[0] != '.') v.emplace_back(e->d_name);
        }
        closedir(d);
    }
    return v;
}
u32 RS_GetFileCount() { return static_cast<u32>(cloudFiles().size()); }
u32 RS_GetFileNameAndSize(u32 i, u32* size) {
    const auto v = cloudFiles();
    if (i >= v.size()) {
        if (size != nullptr) *size = 0;
        return L("");
    }
    if (size != nullptr) *size = RS_GetFileSize(v[i].c_str());
    return L(v[i].c_str());
}
u32 RS_GetQuota(u32* total, u32* avail) {
    if (total != nullptr) *total = 100u << 20;
    if (avail != nullptr) *avail = 100u << 20;
    return 1;
}
u32 RS_SyncPlatforms(u32) { return 0xFFFFFFFFu; }  // k_ERemoteStoragePlatformAll
u32 RS_StreamChunk(i64, u32, u32) { return 0; }

// --- Tablas ---
struct Iface {
    const char* name;
    std::vector<void*> methods;
    u32 obj = 0;
};

Iface makeApps() {
    return {"ISteamApps", {
        w32arity::hpFn(&T0),                     // 0 BIsSubscribed
        w32arity::hpFn(&Z0),                     // 1 BIsLowViolence
        w32arity::hpFn(&Z0),                     // 2 BIsCybercafe
        w32arity::hpFn(&Z0),                     // 3 BIsVACBanned
        w32arity::hpFn(&Apps_Lang),              // 4 GetCurrentGameLanguage
        w32arity::hpFn(&Apps_Lang),              // 5 GetAvailableGameLanguages
        w32arity::hpFn(&Apps_BIsSubscribedApp),  // 6 BIsSubscribedApp(AppId)
        w32arity::hpFn(&Z1),                     // 7 BIsDlcInstalled(AppId): sin DLC instalado
        w32arity::hpFn(&Z1),                     // 8 GetEarliestPurchaseUnixTime(AppId)
        w32arity::hpFn(&Z0),                     // 9 BIsSubscribedFromFreeWeekend
        w32arity::hpFn(&Z0),                     // 10 GetDLCCount
        w32arity::hpFn(&Z5),                     // 11 BGetDLCDataByIndex
        w32arity::hpFn(&Z1),                     // 12 InstallDLC
        w32arity::hpFn(&Z1),                     // 13 UninstallDLC
        w32arity::hpFn(&Z1),                     // 14 RequestAppProofOfPurchaseKey
        w32arity::hpFn(&Z2),                     // 15 GetCurrentBetaName
        w32arity::hpFn(&Z1),                     // 16 MarkContentCorrupt
        w32arity::hpFn(&Z2),                     // 17 GetInstalledDepots
        w32arity::hpFn(&Z3),                     // 18 GetAppInstallDir
        w32arity::hpFn(&T1),                     // 19 BIsAppInstalled
    }};
}
Iface makeUser() {
    return {"ISteamUser", {
        w32arity::hpFn(&T0),                           // 0 GetHSteamUser
        w32arity::hpFn(&T0),                           // 1 BLoggedOn
        w32arity::hpFn(&User_GetSteamID),              // 2 GetSteamID (retorno oculto)
        w32arity::hpFn(&User_InitiateGameConnection),  // 3
        w32arity::hpFn(&Z2),                           // 4 TerminateGameConnection
        w32arity::hpFn(&User_TrackAppUsageEvent),      // 5
        w32arity::hpFn(&User_GetUserDataFolder),       // 6
        w32arity::hpFn(&Z0),                           // 7 StartVoiceRecording
        w32arity::hpFn(&Z0),                           // 8 StopVoiceRecording
        w32arity::hpFn(&User_VoiceNotRecording3),      // 9 GetAvailableVoice
        w32arity::hpFn(&User_GetVoice),                // 10 GetVoice
        w32arity::hpFn(&User_DecompressVoice),         // 11 DecompressVoice
        w32arity::hpFn(&User_SampleRate),              // 12 GetVoiceOptimalSampleRate
        w32arity::hpFn(&Z3),                           // 13 GetAuthSessionTicket
        w32arity::hpFn(&User_BeginAuthSession),        // 14
        w32arity::hpFn(&Zs),                           // 15 EndAuthSession(CSteamID)
        w32arity::hpFn(&Z1),                           // 16 CancelAuthTicket
        w32arity::hpFn(&User_UserHasLicense),          // 17
        w32arity::hpFn(&Z0),                           // 18 BIsBehindNAT
        w32arity::hpFn(&User_AdvertiseGame),           // 19
        w32arity::hpFn(&Q2),                           // 20 RequestEncryptedAppTicket
        w32arity::hpFn(&Z3),                           // 21 GetEncryptedAppTicket
    }};
}
Iface makeFriends() {
    return {"ISteamFriends", {
        w32arity::hpFn(&Friends_PersonaName),   // 0 GetPersonaName
        w32arity::hpFn(&Q1),                    // 1 SetPersonaName -> SteamAPICall_t
        w32arity::hpFn(&Friends_PersonaState),  // 2 GetPersonaState
        w32arity::hpFn(&Z1),                    // 3 GetFriendCount(flags)
        w32arity::hpFn(&Rid4),                  // 4 GetFriendByIndex(int, flags) (ret oculto + 2)
        w32arity::hpFn(&Zs),                    // 5 GetFriendRelationship
        w32arity::hpFn(&Zs),                    // 6 GetFriendPersonaState
        w32arity::hpFn(&Ess),                   // 7 GetFriendPersonaName
        w32arity::hpFn(&Zsi),                   // 8 GetFriendGamePlayed
        w32arity::hpFn(&Essi),                  // 9 GetFriendPersonaNameHistory
        w32arity::hpFn(&Zsi),                   // 10 HasFriend
        w32arity::hpFn(&Z0),                    // 11 GetClanCount
        w32arity::hpFn(&Rid2),                  // 12 GetClanByIndex
        w32arity::hpFn(&Ess),                   // 13 GetClanName
        w32arity::hpFn(&Ess),                   // 14 GetClanTag
        w32arity::hpFn(&Friends_ClanActivity),  // 15 GetClanActivityCounts
        w32arity::hpFn(&Q2),                    // 16 DownloadClanActivityCounts
        w32arity::hpFn(&Zs),                    // 17 GetFriendCountFromSource
        w32arity::hpFn(&Rid4),                  // 18 GetFriendFromSourceByIndex
        w32arity::hpFn(&Zss),                   // 19 IsUserInSource
        w32arity::hpFn(&Zsi),                   // 20 SetInGameVoiceSpeaking
        w32arity::hpFn(&Z1),                    // 21 ActivateGameOverlay
        w32arity::hpFn(&Zsi),                   // 22 ActivateGameOverlayToUser(const char*, CSteamID)
        w32arity::hpFn(&Z1),                    // 23 ActivateGameOverlayToWebPage
        w32arity::hpFn(&Z2),                    // 24 ActivateGameOverlayToStore
        w32arity::hpFn(&Zs),                    // 25 SetPlayedWith
        w32arity::hpFn(&Zs),                    // 26 ActivateGameOverlayInviteDialog
        w32arity::hpFn(&Zs),                    // 27 GetSmallFriendAvatar
        w32arity::hpFn(&Zs),                    // 28 GetMediumFriendAvatar
        w32arity::hpFn(&Zs),                    // 29 GetLargeFriendAvatar
        w32arity::hpFn(&Zsi),                   // 30 RequestUserInformation
        w32arity::hpFn(&Qs),                    // 31 RequestClanOfficerList
        w32arity::hpFn(&Rid3),                  // 32 GetClanOwner
        w32arity::hpFn(&Zs),                    // 33 GetClanOfficerCount
        w32arity::hpFn(&Rid4),                  // 34 GetClanOfficerByIndex
        w32arity::hpFn(&Z0),                    // 35 GetUserRestrictions
        w32arity::hpFn(&T2),                    // 36 SetRichPresence
        w32arity::hpFn(&Z0),                    // 37 ClearRichPresence
        w32arity::hpFn(&Essi),                  // 38 GetFriendRichPresence
        w32arity::hpFn(&Zs),                    // 39 GetFriendRichPresenceKeyCount
        w32arity::hpFn(&Essi),                  // 40 GetFriendRichPresenceKeyByIndex
        w32arity::hpFn(&Zs),                    // 41 RequestFriendRichPresence
        w32arity::hpFn(&Zsi),                   // 42 InviteUserToGame
        w32arity::hpFn(&Z0),                    // 43 GetCoplayFriendCount
        w32arity::hpFn(&Rid2),                  // 44 GetCoplayFriend
        w32arity::hpFn(&Zs),                    // 45 GetFriendCoplayTime
        w32arity::hpFn(&Zs),                    // 46 GetFriendCoplayGame
        w32arity::hpFn(&Qs),                    // 47 JoinClanChatRoom
        w32arity::hpFn(&Zs),                    // 48 LeaveClanChatRoom
        w32arity::hpFn(&Zs),                    // 49 GetClanChatMemberCount
        w32arity::hpFn(&Rid4),                  // 50 GetChatMemberByIndex
        w32arity::hpFn(&Zsi),                   // 51 SendClanChatMessage
        w32arity::hpFn(&Friends_GetClanChatMessage),  // 52
        w32arity::hpFn(&Zss),                   // 53 IsClanChatAdmin
        w32arity::hpFn(&Zs),                    // 54 IsClanChatWindowOpenInSteam
        w32arity::hpFn(&Zs),                    // 55 OpenClanChatWindowInSteam
        w32arity::hpFn(&Zs),                    // 56 CloseClanChatWindowInSteam
        w32arity::hpFn(&Z1),                    // 57 SetListenForFriendsMessages
        w32arity::hpFn(&Zsi),                   // 58 ReplyToFriendMessage
        w32arity::hpFn(&Friends_GetFriendMessage),  // 59
        w32arity::hpFn(&Qs),                    // 60 GetFollowerCount
        w32arity::hpFn(&Qs),                    // 61 IsFollowing
        w32arity::hpFn(&Q1),                    // 62 EnumerateFollowingList
    }};
}
Iface makeUtils() {
    return {"ISteamUtils", {
        w32arity::hpFn(&Z0),                       // 0 GetSecondsSinceAppActive
        w32arity::hpFn(&Z0),                       // 1 GetSecondsSinceComputerActive
        w32arity::hpFn(&Utils_Universe),           // 2 GetConnectedUniverse
        w32arity::hpFn(&Utils_ServerTime),         // 3 GetServerRealTime
        w32arity::hpFn(&Utils_Country),            // 4 GetIPCountry
        w32arity::hpFn(&Z3),                       // 5 GetImageSize
        w32arity::hpFn(&Z3),                       // 6 GetImageRGBA
        w32arity::hpFn(&Z2),                       // 7 GetCSERIPPort
        w32arity::hpFn(&Utils_Battery),            // 8 GetCurrentBatteryPower
        w32arity::hpFn(&Utils_AppId),              // 9 GetAppID
        w32arity::hpFn(&Z1),                       // 10 SetOverlayNotificationPosition
        w32arity::hpFn(&Utils_IsAPICallCompleted), // 11
        w32arity::hpFn(&Utils_GetAPICallFailureReason),  // 12
        w32arity::hpFn(&Utils_GetAPICallResult),   // 13
        w32arity::hpFn(&Z0),                       // 14 RunFrame
        w32arity::hpFn(&Z0),                       // 15 GetIPCCallCount
        w32arity::hpFn(&Z1),                       // 16 SetWarningMessageHook
        w32arity::hpFn(&Z0),                       // 17 IsOverlayEnabled
        w32arity::hpFn(&Z0),                       // 18 BOverlayNeedsPresent
        w32arity::hpFn(&Q1),                       // 19 CheckFileSignature
        w32arity::hpFn(&Z5),                       // 20 ShowGamepadTextInput
        w32arity::hpFn(&Z0),                       // 21 GetEnteredGamepadTextLength
        w32arity::hpFn(&Z2),                       // 22 GetEnteredGamepadTextInput
    }};
}
Iface makeUserStats() {
    return {"ISteamUserStats", {
        w32arity::hpFn(&T0),                 // 0 RequestCurrentStats
        w32arity::hpFn(&Stats_GetStat),      // 1 GetStat(float*)  (MSVC: sobrecargas en orden inverso)
        w32arity::hpFn(&Stats_GetStat),      // 2 GetStat(int32*)
        w32arity::hpFn(&Stats_SetStat),      // 3 SetStat(float)
        w32arity::hpFn(&Stats_SetStat),      // 4 SetStat(int32)
        w32arity::hpFn(&Stats_UpdateAvgRate),  // 5 UpdateAvgRateStat
        w32arity::hpFn(&Stats_GetAchievement),  // 6
        w32arity::hpFn(&Stats_SetAchievement),  // 7
        w32arity::hpFn(&Stats_ClearAchievement),  // 8
        w32arity::hpFn(&Stats_GetAchievementAndUnlockTime),  // 9
        w32arity::hpFn(&T0),                 // 10 StoreStats
        w32arity::hpFn(&Z1),                 // 11 GetAchievementIcon
        w32arity::hpFn(&Stats_DisplayAttribute),  // 12 GetAchievementDisplayAttribute
        w32arity::hpFn(&T3),                 // 13 IndicateAchievementProgress
        w32arity::hpFn(&Qs),                 // 14 RequestUserStats(CSteamID)
        w32arity::hpFn(&Stats_GetUserStat),  // 15 GetUserStat(float*)
        w32arity::hpFn(&Stats_GetUserStat),  // 16 GetUserStat(int32*)
        w32arity::hpFn(&Stats_GetUserStat),  // 17 GetUserAchievement
        w32arity::hpFn(&Stats_GetUserAchievementAndTime),  // 18
        w32arity::hpFn(&T1),                 // 19 ResetAllStats
        w32arity::hpFn(&Q3),                 // 20 FindOrCreateLeaderboard
        w32arity::hpFn(&Q1),                 // 21 FindLeaderboard
        w32arity::hpFn(&Stats_LeaderboardName),  // 22 GetLeaderboardName
        w32arity::hpFn(&Stats_Zq),           // 23 GetLeaderboardEntryCount
        w32arity::hpFn(&Stats_Zq),           // 24 GetLeaderboardSortMethod
        w32arity::hpFn(&Stats_Zq),           // 25 GetLeaderboardDisplayType
        w32arity::hpFn(&Stats_Download),     // 26 DownloadLeaderboardEntries
        w32arity::hpFn(&Stats_DownloadForUsers),  // 27
        w32arity::hpFn(&Stats_GetDownloadedEntry),  // 28
        w32arity::hpFn(&Stats_Upload),       // 29 UploadLeaderboardScore
        w32arity::hpFn(&Stats_AttachUGC),    // 30 AttachLeaderboardUGC
        w32arity::hpFn(&Q0),                 // 31 GetNumberOfCurrentPlayers
        w32arity::hpFn(&Q0),                 // 32 RequestGlobalAchievementPercentages
        w32arity::hpFn(&Stats_MostAchieved),     // 33
        w32arity::hpFn(&Stats_NextMostAchieved), // 34
        w32arity::hpFn(&Z2),                 // 35 GetAchievementAchievedPercent
        w32arity::hpFn(&Q1),                 // 36 RequestGlobalStats
        w32arity::hpFn(&Z2),                 // 37 GetGlobalStat(double*)
        w32arity::hpFn(&Z2),                 // 38 GetGlobalStat(int64*)
        w32arity::hpFn(&Z3),                 // 39 GetGlobalStatHistory(double*)
        w32arity::hpFn(&Z3),                 // 40 GetGlobalStatHistory(int64*)
    }};
}
Iface makeRemoteStorage() {
    return {"ISteamRemoteStorage", {
        w32arity::hpFn(&RS_FileWrite),        // 0 FileWrite
        w32arity::hpFn(&RS_FileRead),         // 1 FileRead
        w32arity::hpFn(&T1),                  // 2 FileForget
        w32arity::hpFn(&RS_FileDelete),       // 3 FileDelete
        w32arity::hpFn(&Q1),                  // 4 FileShare
        w32arity::hpFn(&T2),                  // 5 SetSyncPlatforms
        w32arity::hpFn(&Q1),                  // 6 FileWriteStreamOpen
        w32arity::hpFn(&RS_StreamChunk),      // 7 FileWriteStreamWriteChunk
        w32arity::hpFn(&Stats_Zq),            // 8 FileWriteStreamClose
        w32arity::hpFn(&Stats_Zq),            // 9 FileWriteStreamCancel
        w32arity::hpFn(&RS_FileExists),       // 10 FileExists
        w32arity::hpFn(&RS_FileExists),       // 11 FilePersisted
        w32arity::hpFn(&RS_GetFileSize),      // 12 GetFileSize
        w32arity::hpFn(&RS_GetFileTimestamp), // 13 GetFileTimestamp
        w32arity::hpFn(&RS_SyncPlatforms),    // 14 GetSyncPlatforms
        w32arity::hpFn(&RS_GetFileCount),     // 15 GetFileCount
        w32arity::hpFn(&RS_GetFileNameAndSize),  // 16 GetFileNameAndSize
        w32arity::hpFn(&RS_GetQuota),         // 17 GetQuota
        w32arity::hpFn(&T0),                  // 18 IsCloudEnabledForAccount
        w32arity::hpFn(&T0),                  // 19 IsCloudEnabledForApp
        w32arity::hpFn(&Z1),                  // 20 SetCloudEnabledForApp
        w32arity::hpFn(&Qs),                  // 21 UGCDownload(UGCHandle_t)
    }};
}
Iface makeMMServers() {
    return {"ISteamMatchmakingServers", {
        w32arity::hpFn(&Z4),  // 0 RequestInternetServerList
        w32arity::hpFn(&Z2),  // 1 RequestLANServerList
        w32arity::hpFn(&Z4),  // 2 RequestFriendsServerList
        w32arity::hpFn(&Z4),  // 3 RequestFavoritesServerList
        w32arity::hpFn(&Z4),  // 4 RequestHistoryServerList
        w32arity::hpFn(&Z4),  // 5 RequestSpectatorServerList
        w32arity::hpFn(&Z1),  // 6 ReleaseRequest
        w32arity::hpFn(&Z2),  // 7 GetServerDetails
        w32arity::hpFn(&Z1),  // 8 CancelQuery
        w32arity::hpFn(&Z1),  // 9 RefreshQuery
        w32arity::hpFn(&Z1),  // 10 IsRefreshing
        w32arity::hpFn(&Z1),  // 11 GetServerCount
        w32arity::hpFn(&Z2),  // 12 RefreshServer
        w32arity::hpFn(&Z3),  // 13 PingServer
        w32arity::hpFn(&Z3),  // 14 PlayerDetails
        w32arity::hpFn(&Z3),  // 15 ServerRules
        w32arity::hpFn(&Z1),  // 16 CancelServerQuery
    }};
}

template <int I>
u32 build(Iface (*make)()) {
    static std::once_flag once;
    static u32 obj = 0;
    std::call_once(once, [&] {
        Iface f = make();
        // Ranuras de reserva: registran si el juego usa un método que no está en la tabla.
        f.methods.push_back(w32arity::hpFn(&Unknown<I, 100>));
        f.methods.push_back(w32arity::hpFn(&Unknown<I, 101>));
        f.methods.push_back(w32arity::hpFn(&Unknown<I, 102>));
        f.methods.push_back(w32arity::hpFn(&Unknown<I, 103>));
        const size_t n = f.methods.size();
        auto* mem = lowAlloc(8 + 4 * n);
        if (mem == nullptr) return;
        const auto base = static_cast<u32>(reinterpret_cast<uintptr_t>(mem));
        const u32 vt = base + 8;
        std::memcpy(mem, &vt, 4);
        for (size_t i = 0; i < n; ++i) {
            // Nombre sin "steam_api": así el puente los trata como stdcall/thiscall (el método
            // saca sus argumentos), no como cdecl.
            const auto a = static_cast<u32>(
                w32alias::lowFor(f.methods[i], std::string("steamclient.dll!") + f.name + "#" + std::to_string(i)));
            std::memcpy(mem + 8 + 4 * i, &a, 4);
        }
        obj = base;
        LOGI("[Steam32] %s en 0x%x (%zu métodos)", f.name, base, n);
    });
    return obj;
}

// --- Exportaciones planas de steam_api.dll (cdecl). ---
u32 Init() {
    LOGI("[Steam32] SteamAPI_Init: Steam sin cliente (AppID %u)", NativeSteamStub::getAppId());
    return 1;
}
void NoArgs() {}
void Cb2(u32, u32) {}             // RegisterCallback(CCallbackBase*, int)
void Cb1(u32) {}                  // UnregisterCallback(CCallbackBase*)
void CbRes(u32, i64) {}           // (Un)RegisterCallResult(CCallbackBase*, SteamAPICall_t)
u32 GsInit(u32, u32, u32, u32, u32, u32) { return 0; }  // SteamGameServer_Init: sin servidor
i64 GsSteamId() { return 0; }
u32 Apps() { return build<0>(&makeApps); }
u32 User() { return build<1>(&makeUser); }
u32 Friends() { return build<2>(&makeFriends); }
u32 Utils() { return build<3>(&makeUtils); }
u32 UserStats() { return build<4>(&makeUserStats); }
u32 RemoteStorage() { return build<5>(&makeRemoteStorage); }
u32 MMServers() { return build<6>(&makeMMServers); }
u32 Null0() { return 0; }  // SteamGameServer(), SteamGameServerStats(): sin servidor iniciado = NULL

}  // namespace

void* resolve(const std::string& name, const std::string& saveDir) {
    if (!saveDir.empty()) g_saveDir = saveDir;
    static const std::unordered_map<std::string, void*> kMap = {
        {"SteamAPI_Init", w32arity::hpFn(&Init)},
        {"SteamAPI_InitSafe", w32arity::hpFn(&Init)},
        {"SteamAPI_Shutdown", w32arity::hpFn(&NoArgs)},
        {"SteamAPI_RunCallbacks", w32arity::hpFn(&NoArgs)},
        {"SteamAPI_RestartAppIfNecessary", w32arity::hpFn(&Z1)},
        {"SteamAPI_RegisterCallback", w32arity::hpFn(&Cb2)},
        {"SteamAPI_UnregisterCallback", w32arity::hpFn(&Cb1)},
        {"SteamAPI_RegisterCallResult", w32arity::hpFn(&CbRes)},
        {"SteamAPI_UnregisterCallResult", w32arity::hpFn(&CbRes)},
        {"SteamApps", w32arity::hpFn(&Apps)},
        {"SteamUser", w32arity::hpFn(&User)},
        {"SteamFriends", w32arity::hpFn(&Friends)},
        {"SteamUtils", w32arity::hpFn(&Utils)},
        {"SteamUserStats", w32arity::hpFn(&UserStats)},
        {"SteamRemoteStorage", w32arity::hpFn(&RemoteStorage)},
        {"SteamMatchmakingServers", w32arity::hpFn(&MMServers)},
        {"SteamGameServer", w32arity::hpFn(&Null0)},
        {"SteamGameServerStats", w32arity::hpFn(&Null0)},
        {"SteamGameServer_Init", w32arity::hpFn(&GsInit)},
        {"SteamGameServer_Shutdown", w32arity::hpFn(&NoArgs)},
        {"SteamGameServer_RunCallbacks", w32arity::hpFn(&NoArgs)},
        {"SteamGameServer_BSecure", w32arity::hpFn(&Z0)},
        {"SteamGameServer_GetSteamID", w32arity::hpFn(&GsSteamId)},
    };
    const auto it = kMap.find(name);
    return it == kMap.end() ? nullptr : it->second;
}

}  // namespace steamemu32
}  // namespace hyperplay
