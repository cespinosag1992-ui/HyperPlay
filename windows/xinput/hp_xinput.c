/*
 * HyperPlay · xinput*.dll (PE x86 / x64) para Wine dentro del contenedor.
 *
 * Lee el estado de los mandos que publica la app Android en /tmp/hyperplay_pad.shm
 * (protocolo: app/src/main/cpp/input/HyperPlayPadProtocol.h) y escribe allí el rumble.
 * La misma fuente se compila como xinput1_1/1_2/1_3/1_4 y xinput9_1_0 (ver hp_xinput.def y
 * scripts/build_xinput_dlls.sh). Sin dependencias aparte de kernel32 (+ CRT de mingw).
 *
 * Ruta del fichero (primera que abra): %HYPERPLAY_PAD_SHM%, Z:\tmp\hyperplay_pad.shm,
 * \\?\unix\tmp\hyperplay_pad.shm.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>

#include "HyperPlayPadProtocol.h"

/* ---- Tipos XInput (definidos aquí: los de xinput.h cambian según _WIN32_WINNT) ---------- */

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX;
    SHORT sThumbLY;
    SHORT sThumbRX;
    SHORT sThumbRY;
} HP_XINPUT_GAMEPAD;

typedef struct {
    DWORD dwPacketNumber;
    HP_XINPUT_GAMEPAD Gamepad;
} HP_XINPUT_STATE;

typedef struct {
    WORD wLeftMotorSpeed;
    WORD wRightMotorSpeed;
} HP_XINPUT_VIBRATION;

typedef struct {
    BYTE Type;
    BYTE SubType;
    WORD Flags;
    HP_XINPUT_GAMEPAD Gamepad;
    HP_XINPUT_VIBRATION Vibration;
} HP_XINPUT_CAPABILITIES;

typedef struct {
    HP_XINPUT_CAPABILITIES Capabilities;
    WORD VendorId;
    WORD ProductId;
    WORD ProductVersion;
    WORD unk1;
    DWORD unk2;
} HP_XINPUT_CAPABILITIES_EX;

typedef struct {
    BYTE BatteryType;
    BYTE BatteryLevel;
} HP_XINPUT_BATTERY_INFORMATION;

typedef struct {
    WORD VirtualKey;
    WCHAR Unicode;
    WORD Flags;
    BYTE UserIndex;
    BYTE HidCode;
} HP_XINPUT_KEYSTROKE;

_Static_assert(sizeof(HP_XINPUT_STATE) == 16, "XINPUT_STATE");
_Static_assert(sizeof(HP_XINPUT_CAPABILITIES) == 20, "XINPUT_CAPABILITIES");
_Static_assert(sizeof(HP_XINPUT_KEYSTROKE) == 8, "XINPUT_KEYSTROKE");

#define XUSER_MAX_COUNT 4
#define XUSER_INDEX_ANY 0x000000FF
#define XINPUT_DEVTYPE_GAMEPAD 0x01
#define XINPUT_DEVSUBTYPE_GAMEPAD 0x01
#define XINPUT_FLAG_GAMEPAD 0x00000001
#define BATTERY_DEVTYPE_GAMEPAD 0x00
#define BATTERY_TYPE_DISCONNECTED 0x00
#define BATTERY_TYPE_WIRED 0x01
#define BATTERY_LEVEL_EMPTY 0x00
#define BATTERY_LEVEL_FULL 0x03
#define XINPUT_KEYSTROKE_KEYDOWN 0x0001
#define XINPUT_KEYSTROKE_KEYUP 0x0002
#define HP_TRIGGER_THRESHOLD 30
#define HP_THUMB_THRESHOLD 20000
#ifndef ERROR_EMPTY
#define ERROR_EMPTY 4306L
#endif

/* ---- Mapa compartido ------------------------------------------------------------------- */

static hp_pad_shared* volatile g_map;
static HANDLE g_file = INVALID_HANDLE_VALUE;
static HANDLE g_section;
static volatile LONG g_open_lock;
static volatile LONG g_last_attempt; /* GetTickCount del último intento fallido */
static volatile LONG g_have_attempted;
static volatile LONG g_enabled = 1;
static volatile LONG g_hb_value;
static volatile LONG g_hb_tick;
/* Último rumble pedido por el juego (para XInputEnable y para pararlo al salir). */
static volatile LONG g_vibration[XUSER_MAX_COUNT];
/* XInputGetKeystroke: teclas virtuales "pulsadas" ya notificadas, por usuario. */
static volatile LONG g_key_state[XUSER_MAX_COUNT];

/* ---- Diagnóstico opcional: HYPERPLAY_PAD_DEBUG=1 → Z:\tmp\hp_xinput.log ------------------- */

static volatile LONG g_debug = -1; /* -1 = sin leer */
static volatile LONG g_logged_calls;

static void dbg(const char* fmt, ...) {
    if (g_debug < 0) {
        char v[8];
        const DWORD n = GetEnvironmentVariableA("HYPERPLAY_PAD_DEBUG", v, sizeof(v));
        InterlockedExchange(&g_debug, (n > 0 && n < sizeof(v) && v[0] != '0') ? 1 : 0);
    }
    if (g_debug != 1) return;
    char buf[512];
    char mod[MAX_PATH] = {0};
    GetModuleFileNameA(NULL, mod, MAX_PATH);
    int len = wsprintfA(buf, "[%lu %lu %s] ", (unsigned long)GetCurrentProcessId(), (unsigned long)GetTickCount(), mod);
    va_list ap;
    va_start(ap, fmt);
    len += wvsprintfA(buf + len, fmt, ap);
    va_end(ap);
    buf[len++] = '\n';
    HANDLE f = CreateFileA("Z:\\tmp\\hp_xinput.log", FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) return;
    DWORD w;
    WriteFile(f, buf, (DWORD)len, &w, NULL);
    CloseHandle(f);
}

/* Registra las primeras llamadas de la API (para ver qué usa cada juego). */
#define HP_TRACE(name, user) \
    do { if (g_logged_calls < 64) { InterlockedIncrement(&g_logged_calls); dbg("%s(%lu)", name, (unsigned long)(user)); } } while (0)

static int try_open_path(const WCHAR* path) {
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (f == INVALID_HANDLE_VALUE) {
        dbg("CreateFileW %ls: error %lu", path, (unsigned long)GetLastError());
        return 0;
    }
    HANDLE s = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, HP_PAD_FILE_SIZE, NULL);
    if (s == NULL) {
        dbg("CreateFileMappingW %ls: error %lu", path, (unsigned long)GetLastError());
        CloseHandle(f);
        return 0;
    }
    hp_pad_shared* m = (hp_pad_shared*)MapViewOfFile(s, FILE_MAP_READ | FILE_MAP_WRITE, 0, 0, HP_PAD_FILE_SIZE);
    if (m == NULL) {
        dbg("MapViewOfFile %ls: error %lu", path, (unsigned long)GetLastError());
        CloseHandle(s);
        CloseHandle(f);
        return 0;
    }
    dbg("mapa abierto: %ls (cabecera %s)", path, hp_pad_header_valid(m) ? "válida" : "INVÁLIDA");
    g_file = f;
    g_section = s;
    InterlockedExchange(&g_hb_value, (LONG)__atomic_load_n(&m->heartbeat, __ATOMIC_ACQUIRE));
    InterlockedExchange(&g_hb_tick, (LONG)GetTickCount());
    __atomic_store_n(&g_map, m, __ATOMIC_RELEASE);
    return 1;
}

/* Abre el mapa la primera vez (y reintenta como mucho una vez por segundo si falla). */
static hp_pad_shared* get_map(void) {
    hp_pad_shared* m = __atomic_load_n(&g_map, __ATOMIC_ACQUIRE);
    if (m != NULL) return m;
    const DWORD now = GetTickCount();
    if (g_have_attempted && now - (DWORD)g_last_attempt < 1000) return NULL;
    if (InterlockedCompareExchange(&g_open_lock, 1, 0) != 0) return NULL;
    m = __atomic_load_n(&g_map, __ATOMIC_ACQUIRE);
    if (m == NULL) {
        WCHAR env[MAX_PATH];
        const DWORD n = GetEnvironmentVariableW(L"HYPERPLAY_PAD_SHM", env, MAX_PATH);
        int ok = (n > 0 && n < MAX_PATH) && try_open_path(env);
        if (!ok) ok = try_open_path(L"Z:\\tmp\\hyperplay_pad.shm");
        if (!ok) ok = try_open_path(L"\\\\?\\unix\\tmp\\hyperplay_pad.shm");
        if (!ok) {
            InterlockedExchange(&g_last_attempt, (LONG)now);
            InterlockedExchange(&g_have_attempted, 1);
        }
        m = __atomic_load_n(&g_map, __ATOMIC_ACQUIRE);
    }
    InterlockedExchange(&g_open_lock, 0);
    return m;
}

/* Si la app Android deja de latir, todos los mandos cuentan como desconectados. */
static int writer_alive(const hp_pad_shared* m) {
    if (!hp_pad_header_valid(m)) return 0;
    const LONG hb = (LONG)__atomic_load_n(&m->heartbeat, __ATOMIC_ACQUIRE);
    const DWORD now = GetTickCount();
    if (hb != g_hb_value) {
        InterlockedExchange(&g_hb_value, hb);
        InterlockedExchange(&g_hb_tick, (LONG)now);
        return 1;
    }
    return now - (DWORD)g_hb_tick <= HP_PAD_STALE_MS;
}

/* Lee el mando `user`. Devuelve el mapa si está conectado (y rellena `out`), NULL si no. */
static hp_pad_shared* read_pad(DWORD user, hp_pad_snapshot* out) {
    hp_pad_shared* m = get_map();
    if (m == NULL || !writer_alive(m)) return NULL;
    if (!hp_pad_read(&m->pads[user], out, 1000)) return NULL;
    return out->connected ? m : NULL;
}

static void fill_gamepad(const hp_pad_snapshot* s, HP_XINPUT_GAMEPAD* g, BOOL with_guide) {
    if (!g_enabled) {
        ZeroMemory(g, sizeof(*g));
        return;
    }
    g->wButtons = with_guide ? s->buttons : (WORD)(s->buttons & ~HP_XI_GUIDE);
    g->bLeftTrigger = s->left_trigger;
    g->bRightTrigger = s->right_trigger;
    g->sThumbLX = s->thumb_lx;
    g->sThumbLY = s->thumb_ly;
    g->sThumbRX = s->thumb_rx;
    g->sThumbRY = s->thumb_ry;
}

static DWORD get_state(DWORD user, HP_XINPUT_STATE* state, BOOL with_guide) {
    HP_TRACE(with_guide ? "XInputGetStateEx" : "XInputGetState", user);
    if (user >= XUSER_MAX_COUNT || state == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    state->dwPacketNumber = s.packet;
    fill_gamepad(&s, &state->Gamepad, with_guide);
    return ERROR_SUCCESS;
}

static void apply_rumble(hp_pad_shared* m, DWORD user, LONG packed) {
    const uint16_t left = (uint16_t)(((DWORD)packed >> 16) & 0xFFFF);
    const uint16_t right = (uint16_t)((DWORD)packed & 0xFFFF);
    hp_pad_set_rumble(&m->pads[user], left, right);
}

/* ---- API exportada ---------------------------------------------------------------------- */

DWORD WINAPI XInputGetState(DWORD user, HP_XINPUT_STATE* state) { return get_state(user, state, FALSE); }

/* Ordinal 100: igual que XInputGetState pero con el botón Guide (0x0400). */
DWORD WINAPI XInputGetStateEx(DWORD user, HP_XINPUT_STATE* state) { return get_state(user, state, TRUE); }

DWORD WINAPI XInputSetState(DWORD user, HP_XINPUT_VIBRATION* vibration) {
    HP_TRACE("XInputSetState", user);
    if (user >= XUSER_MAX_COUNT || vibration == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    hp_pad_shared* m = read_pad(user, &s);
    if (m == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    const LONG packed = (LONG)hp_pad_pack_rumble(vibration->wLeftMotorSpeed, vibration->wRightMotorSpeed);
    InterlockedExchange(&g_vibration[user], packed);
    if (g_enabled) apply_rumble(m, user, packed);
    return ERROR_SUCCESS;
}

static void fill_caps(HP_XINPUT_CAPABILITIES* c) {
    ZeroMemory(c, sizeof(*c));
    c->Type = XINPUT_DEVTYPE_GAMEPAD;
    c->SubType = XINPUT_DEVSUBTYPE_GAMEPAD;
    c->Flags = 0;
    c->Gamepad.wButtons = 0xF3FF;
    c->Gamepad.bLeftTrigger = 0xFF;
    c->Gamepad.bRightTrigger = 0xFF;
    c->Gamepad.sThumbLX = (SHORT)0xFFC0;
    c->Gamepad.sThumbLY = (SHORT)0xFFC0;
    c->Gamepad.sThumbRX = (SHORT)0xFFC0;
    c->Gamepad.sThumbRY = (SHORT)0xFFC0;
    c->Vibration.wLeftMotorSpeed = 0xFF;
    c->Vibration.wRightMotorSpeed = 0xFF;
}

DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, HP_XINPUT_CAPABILITIES* caps) {
    HP_TRACE("XInputGetCapabilities", user);
    (void)flags; /* Solo hay mandos tipo gamepad: XINPUT_FLAG_GAMEPAD no filtra nada. */
    if (user >= XUSER_MAX_COUNT || caps == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    fill_caps(caps);
    return ERROR_SUCCESS;
}

/* Ordinal 108 (xinput1_4): VID/PID de un mando Xbox 360 con cable. */
DWORD WINAPI XInputGetCapabilitiesEx(DWORD unk, DWORD user, DWORD flags, HP_XINPUT_CAPABILITIES_EX* caps) {
    (void)flags;
    if (unk != 1 || user >= XUSER_MAX_COUNT || caps == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    ZeroMemory(caps, sizeof(*caps));
    fill_caps(&caps->Capabilities);
    caps->VendorId = 0x045E;
    caps->ProductId = 0x028E;
    caps->ProductVersion = 0x0114;
    return ERROR_SUCCESS;
}

void WINAPI XInputEnable(BOOL enable) {
    HP_TRACE("XInputEnable", enable);
    InterlockedExchange(&g_enabled, enable ? 1 : 0);
    hp_pad_shared* m = get_map();
    if (m == NULL) return;
    for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
        apply_rumble(m, i, enable ? g_vibration[i] : 0);
    }
}

DWORD WINAPI XInputGetBatteryInformation(DWORD user, BYTE dev_type, HP_XINPUT_BATTERY_INFORMATION* info) {
    if (user >= XUSER_MAX_COUNT || info == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    if (dev_type == BATTERY_DEVTYPE_GAMEPAD) {
        info->BatteryType = BATTERY_TYPE_WIRED;
        info->BatteryLevel = BATTERY_LEVEL_FULL;
    } else {
        info->BatteryType = BATTERY_TYPE_DISCONNECTED;
        info->BatteryLevel = BATTERY_LEVEL_EMPTY;
    }
    return ERROR_SUCCESS;
}

/* Teclas virtuales VK_PAD_* en el orden de los bits de g_key_state. */
static const WORD kPadKeys[] = {
    0x5800, 0x5801, 0x5802, 0x5803, /* A B X Y */
    0x5804, 0x5805, 0x5806, 0x5807, /* RSHOULDER LSHOULDER LTRIGGER RTRIGGER */
    0x5810, 0x5811, 0x5812, 0x5813, /* DPAD UP DOWN LEFT RIGHT */
    0x5814, 0x5815, 0x5816, 0x5817, /* START BACK LTHUMB_PRESS RTHUMB_PRESS */
    0x5820, 0x5821, 0x5822, 0x5823, /* LTHUMB UP DOWN RIGHT LEFT */
    0x5830, 0x5831, 0x5832, 0x5833, /* RTHUMB UP DOWN RIGHT LEFT */
};
#define HP_PAD_KEY_COUNT ((int)(sizeof(kPadKeys) / sizeof(kPadKeys[0])))

static LONG pressed_keys(const hp_pad_snapshot* s) {
    static const WORD kButtonBits[16] = {
        HP_XI_A, HP_XI_B, HP_XI_X, HP_XI_Y, HP_XI_RIGHT_SHOULDER, HP_XI_LEFT_SHOULDER, 0, 0,
        HP_XI_DPAD_UP, HP_XI_DPAD_DOWN, HP_XI_DPAD_LEFT, HP_XI_DPAD_RIGHT,
        HP_XI_START, HP_XI_BACK, HP_XI_LEFT_THUMB, HP_XI_RIGHT_THUMB,
    };
    LONG k = 0;
    for (int i = 0; i < 16; ++i) {
        if (kButtonBits[i] != 0 && (s->buttons & kButtonBits[i])) k |= 1L << i;
    }
    if (s->left_trigger > HP_TRIGGER_THRESHOLD) k |= 1L << 6;
    if (s->right_trigger > HP_TRIGGER_THRESHOLD) k |= 1L << 7;
    if (s->thumb_ly > HP_THUMB_THRESHOLD) k |= 1L << 16;
    if (s->thumb_ly < -HP_THUMB_THRESHOLD) k |= 1L << 17;
    if (s->thumb_lx > HP_THUMB_THRESHOLD) k |= 1L << 18;
    if (s->thumb_lx < -HP_THUMB_THRESHOLD) k |= 1L << 19;
    if (s->thumb_ry > HP_THUMB_THRESHOLD) k |= 1L << 20;
    if (s->thumb_ry < -HP_THUMB_THRESHOLD) k |= 1L << 21;
    if (s->thumb_rx > HP_THUMB_THRESHOLD) k |= 1L << 22;
    if (s->thumb_rx < -HP_THUMB_THRESHOLD) k |= 1L << 23;
    return k;
}

/* Devuelve ERROR_SUCCESS con un evento, ERROR_EMPTY sin cambios o ERROR_DEVICE_NOT_CONNECTED. */
static DWORD keystroke_for(DWORD user, HP_XINPUT_KEYSTROKE* ks) {
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) {
        InterlockedExchange(&g_key_state[user], 0);
        return ERROR_DEVICE_NOT_CONNECTED;
    }
    const LONG now = g_enabled ? pressed_keys(&s) : 0;
    const LONG prev = g_key_state[user];
    const LONG diff = now ^ prev;
    if (diff == 0) return ERROR_EMPTY;
    for (int i = 0; i < HP_PAD_KEY_COUNT; ++i) {
        const LONG bit = 1L << i;
        if (!(diff & bit)) continue;
        InterlockedExchange(&g_key_state[user], prev ^ bit);
        ZeroMemory(ks, sizeof(*ks));
        ks->VirtualKey = kPadKeys[i];
        ks->Flags = (now & bit) ? XINPUT_KEYSTROKE_KEYDOWN : XINPUT_KEYSTROKE_KEYUP;
        ks->UserIndex = (BYTE)user;
        return ERROR_SUCCESS;
    }
    return ERROR_EMPTY;
}

DWORD WINAPI XInputGetKeystroke(DWORD user, DWORD reserved, HP_XINPUT_KEYSTROKE* ks) {
    (void)reserved;
    HP_TRACE("XInputGetKeystroke", user);
    if (ks == NULL) return ERROR_BAD_ARGUMENTS;
    if (user == XUSER_INDEX_ANY) {
        int any_connected = 0;
        for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
            const DWORD r = keystroke_for(i, ks);
            if (r == ERROR_SUCCESS) return r;
            if (r == ERROR_EMPTY) any_connected = 1;
        }
        return any_connected ? ERROR_EMPTY : ERROR_DEVICE_NOT_CONNECTED;
    }
    if (user >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    return keystroke_for(user, ks);
}

DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture) {
    if (user >= XUSER_MAX_COUNT || render == NULL || capture == NULL) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    ZeroMemory(render, sizeof(*render)); /* GUID_NULL: sin auriculares */
    ZeroMemory(capture, sizeof(*capture));
    return ERROR_SUCCESS;
}

DWORD WINAPI XInputGetAudioDeviceIds(DWORD user, LPWSTR render, UINT* render_count, LPWSTR capture,
                                     UINT* capture_count) {
    if (user >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    if (read_pad(user, &s) == NULL) return ERROR_DEVICE_NOT_CONNECTED;
    if (render != NULL && render_count != NULL && *render_count > 0) render[0] = 0;
    if (capture != NULL && capture_count != NULL && *capture_count > 0) capture[0] = 0;
    if (render_count != NULL) *render_count = 0;
    if (capture_count != NULL) *capture_count = 0;
    return ERROR_SUCCESS;
}

static DWORD connected_or_error(DWORD user) {
    if (user >= XUSER_MAX_COUNT) return ERROR_BAD_ARGUMENTS;
    hp_pad_snapshot s;
    return read_pad(user, &s) == NULL ? ERROR_DEVICE_NOT_CONNECTED : ERROR_SUCCESS;
}

/* Ordinales 101-104: sin equivalente real; se comportan como en Wine. */
DWORD WINAPI XInputWaitForGuideButton(DWORD user, DWORD flags, void* data) {
    (void)flags;
    (void)data;
    return connected_or_error(user);
}

DWORD WINAPI XInputCancelGuideButtonWait(DWORD user) { return connected_or_error(user); }

DWORD WINAPI XInputPowerOffController(DWORD user) { return connected_or_error(user); }

DWORD WINAPI XInputGetBaseBusInformation(DWORD user, void* data) {
    (void)user;
    (void)data;
    return ERROR_NOT_SUPPORTED;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(instance);
        char self[MAX_PATH] = {0};
        GetModuleFileNameA(instance, self, MAX_PATH);
        dbg("cargada %s", self);
    } else if (reason == DLL_PROCESS_DETACH) {
        hp_pad_shared* m = __atomic_load_n(&g_map, __ATOMIC_ACQUIRE);
        if (m != NULL) {
            /* El juego sale: que no se quede vibrando el mando. */
            for (DWORD i = 0; i < XUSER_MAX_COUNT; ++i) {
                if (g_vibration[i] != 0) apply_rumble(m, i, 0);
            }
            if (reserved == NULL) { /* FreeLibrary: liberar; al terminar el proceso no hace falta */
                __atomic_store_n(&g_map, NULL, __ATOMIC_RELEASE);
                UnmapViewOfFile(m);
                CloseHandle(g_section);
                CloseHandle(g_file);
                g_file = INVALID_HANDLE_VALUE;
                g_section = NULL;
            }
        }
    }
    return TRUE;
}
