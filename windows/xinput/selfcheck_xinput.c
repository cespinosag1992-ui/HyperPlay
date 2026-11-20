/*
 * HyperPlay · selfcheck_xinput.exe: prueba automática de las DLL XInput en Windows real o Wine.
 *
 *   selfcheck_xinput.exe <ruta\xinput1_3.dll>
 *
 * Hace de "lado Android": crea un fichero de estado temporal con HyperPlayPadProtocol.h,
 * apunta HYPERPLAY_PAD_SHM a él, carga la DLL y comprueba GetState/GetStateEx (Guide),
 * GetCapabilities, SetState (rumble), Enable, GetKeystroke, desconexión y latido caducado.
 * Sale con 0 si todo pasa.
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>

#include "HyperPlayPadProtocol.h"

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger, bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} PAD;
typedef struct {
    DWORD dwPacketNumber;
    PAD Gamepad;
} STATE;
typedef struct {
    WORD l, r;
} VIB;
typedef struct {
    BYTE Type, SubType;
    WORD Flags;
    PAD Gamepad;
    VIB Vibration;
} CAPS;
typedef struct {
    WORD VirtualKey;
    WCHAR Unicode;
    WORD Flags;
    BYTE UserIndex, HidCode;
} KEYS;

typedef DWORD(WINAPI* GetStateFn)(DWORD, STATE*);
typedef DWORD(WINAPI* SetStateFn)(DWORD, VIB*);
typedef DWORD(WINAPI* GetCapsFn)(DWORD, DWORD, CAPS*);
typedef void(WINAPI* EnableFn)(BOOL);
typedef DWORD(WINAPI* GetKeystrokeFn)(DWORD, DWORD, KEYS*);

static int failures;
#define CHECK(c)                                                       \
    do {                                                               \
        if (!(c)) {                                                    \
            printf("FALLO línea %d: %s\n", __LINE__, #c);              \
            ++failures;                                                \
        }                                                              \
    } while (0)

static void publish(hp_pad_shared* m, int slot, uint32_t connected, uint16_t buttons, uint8_t lt, int16_t lx) {
    hp_pad_snapshot s;
    ZeroMemory(&s, sizeof(s));
    hp_pad_read(&m->pads[slot], &s, 4);
    s.connected = connected;
    s.packet += 1;
    s.buttons = connected ? buttons : 0;
    s.left_trigger = lt;
    s.thumb_lx = lx;
    s.thumb_ly = (int16_t)-lx;
    hp_pad_write(&m->pads[slot], &s);
    __atomic_fetch_add(&m->heartbeat, 1u, __ATOMIC_RELEASE);
}

int main(int argc, char** argv) {
    if (argc < 2) {
        printf("uso: selfcheck_xinput.exe <xinput.dll>\n");
        return 2;
    }
    WCHAR tmpDir[MAX_PATH], path[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    GetTempFileNameW(tmpDir, L"hpx", 0, path);
    HANDLE f = CreateFileW(path, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY, NULL);
    HANDLE sec = CreateFileMappingW(f, NULL, PAGE_READWRITE, 0, HP_PAD_FILE_SIZE, NULL);
    hp_pad_shared* m = (hp_pad_shared*)MapViewOfFile(sec, FILE_MAP_ALL_ACCESS, 0, 0, HP_PAD_FILE_SIZE);
    if (m == NULL) {
        printf("no se pudo crear el fichero compartido\n");
        return 3;
    }
    m->version = HP_PAD_VERSION;
    m->size = sizeof(hp_pad_shared);
    m->max_pads = HP_PAD_MAX_PADS;
    __atomic_store_n(&m->magic, HP_PAD_MAGIC, __ATOMIC_RELEASE);
    SetEnvironmentVariableW(L"HYPERPLAY_PAD_SHM", path);

    HMODULE h = LoadLibraryA(argv[1]);
    if (h == NULL) {
        printf("LoadLibrary(%s) falló: %lu\n", argv[1], (unsigned long)GetLastError());
        return 4;
    }
    GetStateFn getState = (GetStateFn)(void*)GetProcAddress(h, "XInputGetState");
    GetStateFn getStateEx = (GetStateFn)(void*)GetProcAddress(h, (LPCSTR)(ULONG_PTR)100);
    SetStateFn setState = (SetStateFn)(void*)GetProcAddress(h, "XInputSetState");
    GetCapsFn getCaps = (GetCapsFn)(void*)GetProcAddress(h, "XInputGetCapabilities");
    EnableFn enable = (EnableFn)(void*)GetProcAddress(h, "XInputEnable");
    GetKeystrokeFn getKeystroke = (GetKeystrokeFn)(void*)GetProcAddress(h, "XInputGetKeystroke");
    CHECK(getState && getStateEx && setState && getCaps && enable && getKeystroke);
    if (failures) return 5;

    STATE st;
    CAPS caps;
    VIB vib;
    KEYS ks;
    CHECK(getState(0, &st) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(getState(4, &st) == ERROR_BAD_ARGUMENTS);

    publish(m, 0, 1, HP_XI_A | HP_XI_GUIDE | HP_XI_DPAD_LEFT, 200, 12345);
    CHECK(getState(0, &st) == ERROR_SUCCESS);
    CHECK(st.dwPacketNumber == 1);
    CHECK(st.Gamepad.wButtons == (HP_XI_A | HP_XI_DPAD_LEFT)); /* sin Guide */
    CHECK(st.Gamepad.bLeftTrigger == 200 && st.Gamepad.sThumbLX == 12345 && st.Gamepad.sThumbLY == -12345);
    CHECK(getStateEx(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons == (HP_XI_A | HP_XI_GUIDE | HP_XI_DPAD_LEFT));
    CHECK(getState(1, &st) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(getCaps(0, 1, &caps) == ERROR_SUCCESS && caps.Type == 1 && caps.SubType == 1);
    CHECK(getCaps(2, 0, &caps) == ERROR_DEVICE_NOT_CONNECTED);

    vib.l = 0xFFFF;
    vib.r = 0x1234;
    CHECK(setState(0, &vib) == ERROR_SUCCESS);
    CHECK(hp_pad_get_rumble(&m->pads[0]) == 0xFFFF1234u);
    CHECK(m->pads[0].rumble_seq == 1);
    CHECK(setState(1, &vib) == ERROR_DEVICE_NOT_CONNECTED);

    enable(FALSE);
    CHECK(hp_pad_get_rumble(&m->pads[0]) == 0);
    CHECK(getState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons == 0 && st.Gamepad.bLeftTrigger == 0);
    enable(TRUE);
    CHECK(hp_pad_get_rumble(&m->pads[0]) == 0xFFFF1234u);

    /* Keystroke: A, LTRIGGER, DPAD_LEFT, LTHUMB_RIGHT (12345 < umbral 20000: no). */
    int downs = 0;
    while (getKeystroke(0, 0, &ks) == ERROR_SUCCESS) {
        CHECK(ks.Flags == 1 && ks.UserIndex == 0);
        ++downs;
    }
    CHECK(downs == 3);
    CHECK(getKeystroke(0, 0, &ks) == 4306 /* ERROR_EMPTY */);
    publish(m, 0, 1, 0, 0, 0);
    CHECK(getKeystroke(0xFF, 0, &ks) == ERROR_SUCCESS && ks.Flags == 2 && ks.VirtualKey == 0x5800);

    /* Desconexión. */
    publish(m, 0, 0, 0, 0, 0);
    CHECK(getState(0, &st) == ERROR_DEVICE_NOT_CONNECTED);
    CHECK(getKeystroke(0xFF, 0, &ks) == ERROR_DEVICE_NOT_CONNECTED);

    /* Latido caducado: la app dejó de escribir → desconectado. */
    publish(m, 0, 1, HP_XI_B, 0, 0);
    CHECK(getState(0, &st) == ERROR_SUCCESS && st.Gamepad.wButtons == HP_XI_B);
    printf("esperando %u ms para el latido caducado...\n", HP_PAD_STALE_MS + 300);
    Sleep(HP_PAD_STALE_MS + 300);
    CHECK(getState(0, &st) == ERROR_DEVICE_NOT_CONNECTED);
    __atomic_fetch_add(&m->heartbeat, 1u, __ATOMIC_RELEASE);
    CHECK(getState(0, &st) == ERROR_SUCCESS);

    /* Al descargar la DLL el rumble vuelve a 0. */
    FreeLibrary(h);
    CHECK(hp_pad_get_rumble(&m->pads[0]) == 0);

    UnmapViewOfFile(m);
    CloseHandle(sec);
    CloseHandle(f);
    DeleteFileW(path);
    printf("%s: %s\n", argv[1], failures == 0 ? "OK" : "FALLOS");
    return failures == 0 ? 0 : 1;
}
