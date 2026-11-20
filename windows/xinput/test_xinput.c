/*
 * HyperPlay · test_xinput.exe: imprime XInputGetState / XInputGetStateEx de los 4 usuarios.
 *
 *   test_xinput.exe [segundos] [dll]      (por defecto 10 s y xinput1_3.dll)
 *
 * Con A pulsado manda rumble (izquierdo = gatillo izquierdo, derecho = gatillo derecho) para
 * probar la vibración; al salir lo pone a cero. Carga la DLL con LoadLibrary para poder probar
 * cualquiera de los nombres (xinput1_4.dll, xinput9_1_0.dll, ...).
 */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    WORD wButtons;
    BYTE bLeftTrigger;
    BYTE bRightTrigger;
    SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY;
} PAD;
typedef struct {
    DWORD dwPacketNumber;
    PAD Gamepad;
} STATE;
typedef struct {
    WORD wLeftMotorSpeed, wRightMotorSpeed;
} VIB;
typedef struct {
    BYTE Type, SubType;
    WORD Flags;
    PAD Gamepad;
    VIB Vibration;
} CAPS;

typedef DWORD(WINAPI* GetStateFn)(DWORD, STATE*);
typedef DWORD(WINAPI* SetStateFn)(DWORD, VIB*);
typedef DWORD(WINAPI* GetCapsFn)(DWORD, DWORD, CAPS*);

int main(int argc, char** argv) {
    const int seconds = argc > 1 ? atoi(argv[1]) : 10;
    const char* dll = argc > 2 ? argv[2] : "xinput1_3.dll";
    HMODULE h = LoadLibraryA(dll);
    if (h == NULL) {
        printf("LoadLibrary(%s) falló: %lu\n", dll, (unsigned long)GetLastError());
        return 1;
    }
    char path[MAX_PATH] = {0};
    GetModuleFileNameA(h, path, MAX_PATH);
    GetStateFn getState = (GetStateFn)(void*)GetProcAddress(h, "XInputGetState");
    GetStateFn getStateEx = (GetStateFn)(void*)GetProcAddress(h, (LPCSTR)(ULONG_PTR)100);
    SetStateFn setState = (SetStateFn)(void*)GetProcAddress(h, "XInputSetState");
    GetCapsFn getCaps = (GetCapsFn)(void*)GetProcAddress(h, "XInputGetCapabilities");
    printf("%s -> %s (GetState=%p GetStateEx=%p SetState=%p GetCapabilities=%p)\n", dll, path, (void*)getState,
           (void*)getStateEx, (void*)setState, (void*)getCaps);
    if (getState == NULL || setState == NULL) return 2;

    for (DWORD u = 0; u < 4; ++u) {
        CAPS c;
        const DWORD r = getCaps ? getCaps(u, 0, &c) : ERROR_NOT_SUPPORTED;
        if (r == ERROR_SUCCESS) {
            printf("user %lu: conectado type=%u subtype=%u\n", (unsigned long)u, c.Type, c.SubType);
        } else {
            printf("user %lu: GetCapabilities=%lu\n", (unsigned long)u, (unsigned long)r);
        }
    }

    DWORD lastPacket[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};
    const DWORD end = GetTickCount() + (DWORD)seconds * 1000u;
    while ((LONG)(end - GetTickCount()) > 0) {
        for (DWORD u = 0; u < 4; ++u) {
            STATE s;
            const DWORD r = getStateEx ? getStateEx(u, &s) : getState(u, &s);
            if (r != ERROR_SUCCESS) continue;
            if (s.dwPacketNumber == lastPacket[u]) continue;
            lastPacket[u] = s.dwPacketNumber;
            printf("u%lu pkt=%lu btn=%04x lt=%3u rt=%3u L=(%6d,%6d) R=(%6d,%6d)\n", (unsigned long)u,
                   (unsigned long)s.dwPacketNumber, s.Gamepad.wButtons, s.Gamepad.bLeftTrigger,
                   s.Gamepad.bRightTrigger, s.Gamepad.sThumbLX, s.Gamepad.sThumbLY, s.Gamepad.sThumbRX,
                   s.Gamepad.sThumbRY);
            VIB v = {0, 0};
            if (s.Gamepad.wButtons & 0x1000) { /* A */
                v.wLeftMotorSpeed = (WORD)(s.Gamepad.bLeftTrigger * 257u);
                v.wRightMotorSpeed = (WORD)(s.Gamepad.bRightTrigger * 257u);
                if (v.wLeftMotorSpeed == 0 && v.wRightMotorSpeed == 0) v.wLeftMotorSpeed = 0x8000;
            }
            setState(u, &v);
            fflush(stdout);
        }
        Sleep(8);
    }
    for (DWORD u = 0; u < 4; ++u) {
        VIB v = {0, 0};
        setState(u, &v);
    }
    printf("fin\n");
    return 0;
}
