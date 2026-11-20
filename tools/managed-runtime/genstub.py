import re, sys
# genstub.py <exports.txt> <out.c> <kind: steam|fmod>
kind = sys.argv[3]
raw = [l.strip() for l in open(sys.argv[1]) if l.strip() and not l.startswith("?")]
names = []
for n in raw:
    m = re.match(r"^_?([A-Za-z0-9_]+?)(@\d+)?$", n)
    if m and m.group(1) not in names:
        names.append(m.group(1))

out = ["// Shim x64 generado para HyperPlay (juegos .NET en Mono x64). Ver docs en ManagedHost.h.",
       "typedef unsigned long long u64;",
       "static const char kEnglish[] = \"english\";",
       "static const char kPlayer[] = \"Player\";",
       "static char g_fake[4096];"]
special = {}
if kind == "steam":
    special = {
        "Init": "return 1;", "InitSafe": "return 1;", "IsSteamRunning": "return 1;",
        "RestartAppIfNecessary": "return 0;",
        "ISteamApps_GetCurrentGameLanguage": "return (u64)kEnglish;",
        "ISteamApps_GetAvailableGameLanguages": "return (u64)kEnglish;",
        "ISteamUtils_GetSteamUILanguage": "return (u64)kEnglish;",
        "ISteamFriends_GetPersonaName": "return (u64)kPlayer;",
        "ISteamApps_BIsSubscribed": "return 1;", "ISteamApps_BIsSubscribedApp": "return 1;",
        "ISteamApps_BIsDlcInstalled": "return 1;",
        "ISteamUser_BLoggedOn": "return 1;",
        "ISteamUser_GetSteamID": "return 76561197960287930ull;",
        "ISteamUserStats_RequestCurrentStats": "return 1;",
        "ISteamUserStats_StoreStats": "return 1;",
        "ISteamUtils_GetAppID": "return 504230;",
    }
lines = []
for n in names:
    # FMOD: getters de texto (h, char* buf, int size, int* retrieved): cadena vacía con su NUL.
    if kind == "fmod" and re.search(r"_Get(Path|Name|Info)$", n):
        lines.append(f"__declspec(dllexport) u64 {n}(void* h, char* buf, int size, int* retrieved) "
                     f"{{ (void)h; if (buf && size > 0) buf[0] = 0; if (retrieved) *retrieved = 1; return 0; }}")
        continue
    body = special.get(n)
    if body is None and kind == "steam":
        # Steamworks.NET (CSteamAPIContext.Init): pipe/usuario != 0, SteamClient y cada
        # ISteamClient_GetISteam* != NULL. Son punteros falsos: solo vuelven a este shim.
        if n in ("GetHSteamPipe_", "GetHSteamUser_", "Steam_GetHSteamUserCurrent_", "GameServer_GetHSteamPipe"):
            body = "return 1;"
        elif re.match(r"^(SteamClient_|SteamGameServerClient_|SteamInternal_CreateInterface_|ISteamClient_GetISteam\w+)$", n):
            body = "return (u64)(g_fake + 64);"
    if body is None:
        body = "return 0;"
    lines.append(f"__declspec(dllexport) u64 {n}(void) {{ {body} }}")
out += lines
out.append("int _DllMainCRTStartup(void* h, unsigned r, void* p) { (void)h; (void)r; (void)p; return 1; }")
open(sys.argv[2], "w").write("\n".join(out) + "\n")
print(len(names), "exports ->", sys.argv[2])
