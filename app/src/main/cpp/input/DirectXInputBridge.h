#pragma once

#include <cstdint>
#include <atomic>

namespace hyperplay {

struct XInputGamepadState {
    uint16_t wButtons = 0;
    uint8_t  bLeftTrigger = 0;
    uint8_t  bRightTrigger = 0;
    int16_t  sThumbLX = 0;
    int16_t  sThumbLY = 0;
    int16_t  sThumbRX = 0;
    int16_t  sThumbRY = 0;
};

struct XInputVibrationState {
    uint16_t wLeftMotorSpeed = 0;
    uint16_t wRightMotorSpeed = 0;
};

class DirectXInputBridge {
public:
    static void setButton(uint16_t buttonMask, bool pressed);
    /// Estado completo de los botones. Una pulsación más corta que kMinPressMs se sigue viendo
    /// pulsada hasta cumplir ese mínimo: los juegos sondean XInput una vez por frame y si no la
    /// perderían (toques muy rápidos, `input keyevent`).
    static void setButtons(uint16_t buttons);
    static constexpr int64_t kMinPressMs = 40;
    static void setTriggers(uint8_t left, uint8_t right);
    static void setThumbL(int16_t x, int16_t y);
    static void setThumbR(int16_t x, int16_t y);

    /// Llamado directamente por juegos que usan XInput (xinput1_3 / xinput1_4 / xinput9_1_0)
    static uint32_t XInputGetState(uint32_t userIndex, void* pState);
    static uint32_t XInputSetState(uint32_t userIndex, const void* pVibration);
    // Resto de la API que usan Rewired/Unity para detectar el mando.
    static uint32_t XInputGetCapabilities(uint32_t userIndex, uint32_t flags, void* pCaps);
    static void XInputEnable(int32_t enable);
    static uint32_t XInputGetBatteryInformation(uint32_t userIndex, uint8_t devType, void* pBattery);
    static uint32_t XInputGetKeystroke(uint32_t userIndex, uint32_t reserved, void* pKeystroke);

    static XInputVibrationState getVibration(uint32_t userIndex);

    static void reset();
};

}  // namespace hyperplay
