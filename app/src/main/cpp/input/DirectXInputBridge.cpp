#include "DirectXInputBridge.h"

#include <android/log.h>

#include <atomic>
#include <chrono>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

namespace hyperplay {
namespace {

// Win32 XINPUT_STATE layout:
// struct XINPUT_STATE { DWORD dwPacketNumber; XINPUT_GAMEPAD Gamepad; };
struct Win32XInputState {
    uint32_t dwPacketNumber;
    XInputGamepadState Gamepad;
};

// Win32 XINPUT_VIBRATION layout:
// struct XINPUT_VIBRATION { WORD wLeftMotorSpeed; WORD wRightMotorSpeed; };
struct Win32XInputVibration {
    uint16_t wLeftMotorSpeed;
    uint16_t wRightMotorSpeed;
};

constexpr uint32_t ERROR_SUCCESS = 0;
constexpr uint32_t ERROR_DEVICE_NOT_CONNECTED = 1167;

std::atomic<uint32_t> s_packetNumber{1};
// Momento (ns, reloj monotónico) en que se pulsó cada botón; 0 = nunca.
std::atomic<int64_t> s_pressNs[16] = {};

int64_t nowNs() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

/// Botones pulsados hace menos de kMinPressMs (aunque ya se hayan soltado).
uint16_t recentPresses() {
    const int64_t now = nowNs();
    const int64_t window = DirectXInputBridge::kMinPressMs * 1000000;
    uint16_t bits = 0;
    for (int i = 0; i < 16; ++i) {
        const int64_t t = s_pressNs[i].load(std::memory_order_relaxed);
        if (t != 0 && now - t < window) bits |= static_cast<uint16_t>(1u << i);
    }
    return bits;
}
XInputGamepadState s_gamepadState{};
XInputVibrationState s_vibrationState{};

// Mando virtual de depuración (`setprop debug.hyperplay.pad "R A X"`): permite jugar por adb
// con varias entradas a la vez (`input` de Android solo inyecta un puntero). Se combina con el
// estado real. Tokens: L R U D (stick izquierdo), A B X Y LB RB LT RT START BACK; vacío/"0" = nada.
std::atomic<uint32_t> s_debugPad{0};  // bits 0-15 botones, 16-19 L/R/U/D, 20 LT, 21 RT

void parseDebugPad(const char* v) {
    uint32_t bits = 0;
    char tok[16];
    size_t n = 0;
    for (const char* p = v;; ++p) {
        if (*p == ' ' || *p == ',' || *p == '\0') {
            tok[n] = '\0';
            if (n > 0) {
                const std::string t = tok;
                if (t == "L") bits |= 1u << 16;
                else if (t == "R") bits |= 1u << 17;
                else if (t == "U") bits |= 1u << 18;
                else if (t == "D") bits |= 1u << 19;
                else if (t == "LT") bits |= 1u << 20;
                else if (t == "RT") bits |= 1u << 21;
                else if (t == "A") bits |= 0x1000;
                else if (t == "B") bits |= 0x2000;
                else if (t == "X") bits |= 0x4000;
                else if (t == "Y") bits |= 0x8000;
                else if (t == "LB") bits |= 0x0100;
                else if (t == "RB") bits |= 0x0200;
                else if (t == "START") bits |= 0x0010;
                else if (t == "BACK") bits |= 0x0020;
                else if (t == "DU") bits |= 0x0001;
                else if (t == "DD") bits |= 0x0002;
                else if (t == "DL") bits |= 0x0004;
                else if (t == "DR") bits |= 0x0008;
            }
            n = 0;
            if (*p == '\0') break;
        } else if (n + 1 < sizeof(tok)) {
            tok[n++] = *p;
        }
    }
    if (s_debugPad.exchange(bits) != bits) s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

void startDebugPadWatcher() {
#if defined(__ANDROID__)
    static std::once_flag once;
    std::call_once(once, [] {
        std::thread([] {
            const prop_info* pi = nullptr;
            while ((pi = __system_property_find("debug.hyperplay.pad")) == nullptr) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
            }
            uint32_t serial = 0;
            for (;;) {
                __system_property_read_callback(
                    pi, [](void*, const char*, const char* value, uint32_t) { parseDebugPad(value); }, nullptr);
                uint32_t next = 0;
                __system_property_wait(pi, serial, &next, nullptr);  // bloquea hasta el siguiente cambio
                serial = next;
            }
        }).detach();
    });
#endif
}

}  // namespace

void DirectXInputBridge::setButton(uint16_t buttonMask, bool pressed) {
    if (pressed) {
        s_gamepadState.wButtons |= buttonMask;
    } else {
        s_gamepadState.wButtons &= ~buttonMask;
    }
    s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

void DirectXInputBridge::setButtons(uint16_t buttons) {
    const uint16_t pressed = static_cast<uint16_t>(buttons & ~s_gamepadState.wButtons);
    if (pressed != 0) {
        const int64_t now = nowNs();
        for (int i = 0; i < 16; ++i) {
            if (pressed & (1u << i)) s_pressNs[i].store(now, std::memory_order_relaxed);
        }
    }
    s_gamepadState.wButtons = buttons;
    s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

void DirectXInputBridge::setTriggers(uint8_t left, uint8_t right) {
    s_gamepadState.bLeftTrigger = left;
    s_gamepadState.bRightTrigger = right;
    s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

void DirectXInputBridge::setThumbL(int16_t x, int16_t y) {
    s_gamepadState.sThumbLX = x;
    s_gamepadState.sThumbLY = y;
    s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

void DirectXInputBridge::setThumbR(int16_t x, int16_t y) {
    s_gamepadState.sThumbRX = x;
    s_gamepadState.sThumbRY = y;
    s_packetNumber.fetch_add(1, std::memory_order_relaxed);
}

uint32_t DirectXInputBridge::XInputGetState(uint32_t userIndex, void* pState) {
    if (userIndex != 0 || pState == nullptr) return ERROR_DEVICE_NOT_CONNECTED;

    startDebugPadWatcher();
    auto* winState = reinterpret_cast<Win32XInputState*>(pState);
    winState->dwPacketNumber = s_packetNumber.load(std::memory_order_relaxed);
    winState->Gamepad = s_gamepadState;
    winState->Gamepad.wButtons |= recentPresses();
    if (const uint32_t d = s_debugPad.load(std::memory_order_relaxed); d != 0) {
        winState->Gamepad.wButtons |= static_cast<uint16_t>(d & 0xFFFF);
        if (d & (1u << 16)) winState->Gamepad.sThumbLX = -32767;
        if (d & (1u << 17)) winState->Gamepad.sThumbLX = 32767;
        if (d & (1u << 18)) winState->Gamepad.sThumbLY = 32767;
        if (d & (1u << 19)) winState->Gamepad.sThumbLY = -32767;
        if (d & (1u << 20)) winState->Gamepad.bLeftTrigger = 255;
        if (d & (1u << 21)) winState->Gamepad.bRightTrigger = 255;
    }
    // Diagnóstico: cada cambio de botones que se entrega al juego (los 50 primeros).
    static std::atomic<uint32_t> s_lastLogged{0};
    static std::atomic<int> s_logs{0};
    const uint32_t key = winState->Gamepad.wButtons | (winState->Gamepad.sThumbLX != 0 ? 0x10000u : 0u);
    if (s_lastLogged.exchange(key) != key && s_logs.fetch_add(1) < 50) {
        __android_log_print(ANDROID_LOG_INFO, "HyperPlay.XInput", "XInputGetState: botones=0x%04x LX=%d paquete=%u",
                            winState->Gamepad.wButtons, winState->Gamepad.sThumbLX, winState->dwPacketNumber);
    }
    return ERROR_SUCCESS;
}

uint32_t DirectXInputBridge::XInputSetState(uint32_t userIndex, const void* pVibration) {
    if (userIndex != 0 || pVibration == nullptr) return ERROR_DEVICE_NOT_CONNECTED;

    const auto* vib = reinterpret_cast<const Win32XInputVibration*>(pVibration);
    s_vibrationState.wLeftMotorSpeed = vib->wLeftMotorSpeed;
    s_vibrationState.wRightMotorSpeed = vib->wRightMotorSpeed;
    return ERROR_SUCCESS;
}

// XINPUT_CAPABILITIES (20 bytes): Type, SubType, Flags, Gamepad (máximos admitidos), Vibration.
uint32_t DirectXInputBridge::XInputGetCapabilities(uint32_t userIndex, uint32_t, void* pCaps) {
    if (userIndex != 0 || pCaps == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    auto* c = static_cast<uint8_t*>(pCaps);
    std::memset(c, 0, 20);
    c[0] = 1;                      // XINPUT_DEVTYPE_GAMEPAD
    c[1] = 1;                      // XINPUT_DEVSUBTYPE_GAMEPAD
    const uint16_t buttons = 0xF3FF;  // todos los botones del mando
    std::memcpy(c + 4, &buttons, 2);
    c[6] = c[7] = 0xFF;            // gatillos
    const int16_t axis = static_cast<int16_t>(0xFFC0);
    for (int i = 0; i < 4; ++i) std::memcpy(c + 8 + 2 * i, &axis, 2);
    c[16] = c[17] = c[18] = c[19] = 0xFF;  // motores de vibración
    return ERROR_SUCCESS;
}

void DirectXInputBridge::XInputEnable(int32_t) {}

uint32_t DirectXInputBridge::XInputGetBatteryInformation(uint32_t userIndex, uint8_t, void* pBattery) {
    if (userIndex != 0 || pBattery == nullptr) return ERROR_DEVICE_NOT_CONNECTED;
    auto* b = static_cast<uint8_t*>(pBattery);
    b[0] = 1;  // BATTERY_TYPE_WIRED
    b[1] = 3;  // BATTERY_LEVEL_FULL
    return ERROR_SUCCESS;
}

uint32_t DirectXInputBridge::XInputGetKeystroke(uint32_t userIndex, uint32_t, void*) {
    if (userIndex != 0 && userIndex != 0xFF) return ERROR_DEVICE_NOT_CONNECTED;
    return 4306;  // ERROR_EMPTY: no hay pulsaciones en cola
}

XInputVibrationState DirectXInputBridge::getVibration(uint32_t userIndex) {
    if (userIndex != 0) return XInputVibrationState{};
    return s_vibrationState;
}

void DirectXInputBridge::reset() {
    s_gamepadState = XInputGamepadState{};
    s_vibrationState = XInputVibrationState{};
    s_packetNumber.store(1, std::memory_order_relaxed);
    for (auto& t : s_pressNs) t.store(0, std::memory_order_relaxed);
}

}  // namespace hyperplay
