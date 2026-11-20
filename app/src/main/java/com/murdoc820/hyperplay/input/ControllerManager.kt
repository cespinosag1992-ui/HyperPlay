package com.murdoc820.hyperplay.input

import android.content.Context
import android.hardware.input.InputManager
import android.os.Handler
import android.os.Looper
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent
import kotlin.math.abs
import kotlin.math.roundToInt

/** Destino de los eventos con códigos evdev (en producción, [EvdevXInputSink] del mando táctil). */
interface InputSink {
    fun button(code: Int, pressed: Boolean)
    fun axis(code: Int, value: Int)
    fun sync()
}

/** Sin salida: mandos físicos, cuyo estado XInput publica [XInputPadBridge] (aquí solo combos y modo). */
object NoInputSink : InputSink {
    override fun button(code: Int, pressed: Boolean) = Unit
    override fun axis(code: Int, value: Int) = Unit
    override fun sync() = Unit
}

enum class InputMode { GAMEPAD, TOUCH }

/**
 * Traduce mandos físicos de Android (USB-C GameSir/Backbone, Bluetooth) a eventos evdev de Linux
 * con layout Xbox 360, detecta el combo del Quick Menu y conmuta entre modo mando y táctil.
 *
 * La lógica pura ([handleButton], [handleAxes], [onTouch]) no toca APIs de Android, por eso se
 * prueba en la JVM. Debe usarse desde un único hilo (el principal).
 *
 * @param context si es null no hay seguimiento de dispositivos ([start]/[stop] no hacen nada); útil en tests.
 */
class ControllerManager(
    context: Context?,
    private val sink: InputSink,
    private val listener: Listener,
) {

    interface Listener {
        fun onQuickMenuRequested()
        fun onInputModeChanged(mode: InputMode)
        fun onControllersChanged(names: List<String>)
    }

    private val appContext: Context? = context?.applicationContext

    var mode: InputMode = InputMode.TOUCH
        private set

    private val pressedButtons = HashSet<Int>()
    private var l3r3Armed = true
    private var selectStartArmed = true

    // Cruceta: se guardan las cuatro direcciones para que soltar una no anule la opuesta.
    private var dpadUp = false
    private var dpadDown = false
    private var dpadLeft = false
    private var dpadRight = false
    private var digitalL2 = false
    private var digitalR2 = false

    // Últimas fuentes analógicas (ya escaladas a evdev).
    private var analogLt = 0
    private var analogRt = 0
    private var analogHatX = 0
    private var analogHatY = 0

    // Último valor enviado por eje evdev; solo se reenvían cambios.
    private val sentAxes = HashMap<Int, Int>()

    private var inputManager: InputManager? = null
    private var controllers: List<String> = emptyList()

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = refreshControllers()
        override fun onInputDeviceRemoved(deviceId: Int) = refreshControllers()
        override fun onInputDeviceChanged(deviceId: Int) = refreshControllers()
    }

    // ---------------------------------------------------------------- lógica pura

    /**
     * Procesa un botón con keycode de Android. Devuelve false si el keycode no es de mando.
     *
     * @param fromTouch true si viene del mando virtual en pantalla: mantiene el modo táctil en vez
     *   de pasar a modo mando (los controles táctiles no deben ocultarse al usarlos).
     */
    fun handleButton(keyCode: Int, pressed: Boolean, fromTouch: Boolean = false): Boolean {
        val button = BUTTON_MAP[keyCode]
        val markInput = if (fromTouch) ::onTouch else ::markGamepadInput
        when {
            button != null -> {
                markInput()
                if (pressed) pressedButtons.add(button) else pressedButtons.remove(button)
                sink.button(button, pressed)
                sink.sync()
                updateCombos(button, pressed)
            }
            keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2 -> {
                markInput()
                if (keyCode == KeyEvent.KEYCODE_BUTTON_L2) digitalL2 = pressed else digitalR2 = pressed
                pushDerivedAxes()
            }
            keyCode in DPAD_KEYS -> {
                markInput()
                when (keyCode) {
                    KeyEvent.KEYCODE_DPAD_UP -> dpadUp = pressed
                    KeyEvent.KEYCODE_DPAD_DOWN -> dpadDown = pressed
                    KeyEvent.KEYCODE_DPAD_LEFT -> dpadLeft = pressed
                    KeyEvent.KEYCODE_DPAD_RIGHT -> dpadRight = pressed
                }
                pushDerivedAxes()
            }
            else -> return false
        }
        return true
    }

    /**
     * Sticks en -1..1, gatillos en 0..1, hat en -1..1 (valores crudos de Android).
     *
     * @param fromTouch true si viene del stick virtual en pantalla (no cambia a modo mando).
     */
    fun handleAxes(
        lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float, hatX: Float, hatY: Float,
        fromTouch: Boolean = false,
    ) {
        val sticks = intArrayOf(scaleStick(lx), scaleStick(ly), scaleStick(rx), scaleStick(ry))
        analogLt = scaleTrigger(lt)
        analogRt = scaleTrigger(rt)
        analogHatX = scaleHat(hatX)
        analogHatY = scaleHat(hatY)

        // Ruido en reposo o drift no debe sacar al usuario del modo táctil.
        val active = sticks.any { abs(it) > STICK_ACTIVITY } ||
            analogLt > TRIGGER_ACTIVITY || analogRt > TRIGGER_ACTIVITY ||
            analogHatX != 0 || analogHatY != 0
        if (fromTouch) onTouch() else if (active) markGamepadInput()

        var changed = setAxis(ABS_X, sticks[0])
        changed = setAxis(ABS_Y, sticks[1]) or changed
        changed = setAxis(ABS_RX, sticks[2]) or changed
        changed = setAxis(ABS_RY, sticks[3]) or changed
        changed = writeDerivedAxes() or changed
        if (changed) sink.sync()
    }

    /** El usuario tocó la pantalla: se vuelven a mostrar los controles táctiles. */
    fun onTouch() = setMode(InputMode.TOUCH)

    /** Suelta todos los botones y centra los ejes (p. ej. al desconectarse el mando). */
    fun releaseAll() {
        var changed = false
        for (button in pressedButtons.toList()) {
            sink.button(button, false)
            changed = true
        }
        pressedButtons.clear()
        l3r3Armed = true
        selectStartArmed = true
        dpadUp = false; dpadDown = false; dpadLeft = false; dpadRight = false
        digitalL2 = false; digitalR2 = false
        analogLt = 0; analogRt = 0; analogHatX = 0; analogHatY = 0
        for (code in intArrayOf(ABS_X, ABS_Y, ABS_RX, ABS_RY)) changed = setAxis(code, 0) or changed
        changed = writeDerivedAxes() or changed
        if (changed) sink.sync()
    }

    // ---------------------------------------------------------------- entrada Android

    fun onKeyEvent(event: KeyEvent): Boolean {
        val fromPad = event.isFromSource(InputDevice.SOURCE_GAMEPAD) ||
            event.isFromSource(InputDevice.SOURCE_JOYSTICK) ||
            event.isFromSource(InputDevice.SOURCE_DPAD)
        if (!fromPad) return false
        return when (event.action) {
            KeyEvent.ACTION_DOWN -> {
                // Las repeticiones se consumen para que Android no las procese, pero no se reenvían.
                if (event.repeatCount > 0) isMappedKey(event.keyCode) else handleButton(event.keyCode, true)
            }
            KeyEvent.ACTION_UP -> handleButton(event.keyCode, false)
            else -> false
        }
    }

    fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (!event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.actionMasked != MotionEvent.ACTION_MOVE) {
            return false
        }
        val lt = event.getAxisValue(MotionEvent.AXIS_LTRIGGER).takeIf { it != 0f }
            ?: event.getAxisValue(MotionEvent.AXIS_BRAKE)
        val rt = event.getAxisValue(MotionEvent.AXIS_RTRIGGER).takeIf { it != 0f }
            ?: event.getAxisValue(MotionEvent.AXIS_GAS)
        handleAxes(
            lx = event.getAxisValue(MotionEvent.AXIS_X),
            ly = event.getAxisValue(MotionEvent.AXIS_Y),
            rx = event.getAxisValue(MotionEvent.AXIS_Z),
            ry = event.getAxisValue(MotionEvent.AXIS_RZ),
            lt = lt,
            rt = rt,
            hatX = event.getAxisValue(MotionEvent.AXIS_HAT_X),
            hatY = event.getAxisValue(MotionEvent.AXIS_HAT_Y),
        )
        return true
    }

    // ---------------------------------------------------------------- dispositivos

    /** Empieza a seguir conexiones de mandos (llamar desde el hilo principal). */
    fun start() {
        val ctx = appContext ?: return
        if (inputManager != null) return
        val im = ctx.getSystemService(InputManager::class.java) ?: return
        im.registerInputDeviceListener(deviceListener, Handler(Looper.getMainLooper()))
        inputManager = im
        refreshControllers()
    }

    fun stop() {
        inputManager?.unregisterInputDeviceListener(deviceListener)
        inputManager = null
        releaseAll()
    }

    fun connectedControllers(): List<String> = controllers

    private fun refreshControllers() {
        val names = InputDevice.getDeviceIds().toList()
            .mapNotNull { InputDevice.getDevice(it) }
            .filter { dev ->
                !dev.isVirtual &&
                    (dev.supportsSource(InputDevice.SOURCE_GAMEPAD) || dev.supportsSource(InputDevice.SOURCE_JOYSTICK))
            }
            // Algunos mandos se exponen como varios nodos (botones + sensores) con el mismo descriptor.
            .distinctBy { it.descriptor }
            .map { it.name }
        if (names == controllers) return
        controllers = names
        if (names.isEmpty()) releaseAll()
        listener.onControllersChanged(names)
    }

    // ---------------------------------------------------------------- internos

    private fun markGamepadInput() = setMode(InputMode.GAMEPAD)

    private fun setMode(newMode: InputMode) {
        if (mode == newMode) return
        mode = newMode
        listener.onInputModeChanged(newMode)
    }

    private fun updateCombos(button: Int, pressed: Boolean) {
        if (button == BTN_THUMBL || button == BTN_THUMBR) {
            if (!pressed) {
                l3r3Armed = true
            } else if (l3r3Armed && BTN_THUMBL in pressedButtons && BTN_THUMBR in pressedButtons) {
                l3r3Armed = false
                listener.onQuickMenuRequested()
            }
        }
        if (button == BTN_SELECT || button == BTN_START) {
            if (!pressed) {
                selectStartArmed = true
            } else if (selectStartArmed && BTN_SELECT in pressedButtons && BTN_START in pressedButtons) {
                selectStartArmed = false
                listener.onQuickMenuRequested()
            }
        }
    }

    /** Recalcula gatillos y hat combinando fuentes digitales y analógicas; hace sync si algo cambió. */
    private fun pushDerivedAxes() {
        if (writeDerivedAxes()) sink.sync()
    }

    private fun writeDerivedAxes(): Boolean {
        val keyHatX = (if (dpadRight) 1 else 0) - (if (dpadLeft) 1 else 0)
        val keyHatY = (if (dpadDown) 1 else 0) - (if (dpadUp) 1 else 0)
        var changed = setAxis(ABS_Z, if (digitalL2) TRIGGER_MAX else analogLt)
        changed = setAxis(ABS_RZ, if (digitalR2) TRIGGER_MAX else analogRt) or changed
        changed = setAxis(ABS_HAT0X, if (analogHatX != 0) analogHatX else keyHatX) or changed
        changed = setAxis(ABS_HAT0Y, if (analogHatY != 0) analogHatY else keyHatY) or changed
        return changed
    }

    private fun setAxis(code: Int, value: Int): Boolean {
        if ((sentAxes[code] ?: 0) == value) return false
        sentAxes[code] = value
        sink.axis(code, value)
        return true
    }

    private fun isMappedKey(keyCode: Int): Boolean =
        keyCode in BUTTON_MAP || keyCode in DPAD_KEYS ||
            keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2

    companion object {
        // Códigos de linux/input-event-codes.h.
        const val BTN_SOUTH = 0x130
        const val BTN_EAST = 0x131
        const val BTN_NORTH = 0x133
        const val BTN_WEST = 0x134
        const val BTN_TL = 0x136
        const val BTN_TR = 0x137
        const val BTN_SELECT = 0x13a
        const val BTN_START = 0x13b
        const val BTN_MODE = 0x13c
        const val BTN_THUMBL = 0x13d
        const val BTN_THUMBR = 0x13e

        const val ABS_X = 0x00
        const val ABS_Y = 0x01
        const val ABS_Z = 0x02
        const val ABS_RX = 0x03
        const val ABS_RY = 0x04
        const val ABS_RZ = 0x05
        const val ABS_HAT0X = 0x10
        const val ABS_HAT0Y = 0x11

        const val STICK_MIN = -32768
        const val STICK_MAX = 32767
        const val TRIGGER_MAX = 255

        // Umbrales para considerar que el usuario está usando el mando (evita cambios de modo por drift).
        private const val STICK_ACTIVITY = 8192
        private const val TRIGGER_ACTIVITY = 64

        private val BUTTON_MAP: Map<Int, Int> = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to BTN_SOUTH,
            KeyEvent.KEYCODE_BUTTON_B to BTN_EAST,
            KeyEvent.KEYCODE_BUTTON_X to BTN_WEST,
            KeyEvent.KEYCODE_BUTTON_Y to BTN_NORTH,
            KeyEvent.KEYCODE_BUTTON_L1 to BTN_TL,
            KeyEvent.KEYCODE_BUTTON_R1 to BTN_TR,
            KeyEvent.KEYCODE_BUTTON_SELECT to BTN_SELECT,
            KeyEvent.KEYCODE_BUTTON_START to BTN_START,
            KeyEvent.KEYCODE_BUTTON_MODE to BTN_MODE,
            KeyEvent.KEYCODE_BUTTON_THUMBL to BTN_THUMBL,
            KeyEvent.KEYCODE_BUTTON_THUMBR to BTN_THUMBR,
        )

        private val DPAD_KEYS = setOf(
            KeyEvent.KEYCODE_DPAD_UP,
            KeyEvent.KEYCODE_DPAD_DOWN,
            KeyEvent.KEYCODE_DPAD_LEFT,
            KeyEvent.KEYCODE_DPAD_RIGHT,
        )

        /** -1..1 → -32768..32767 con saturación. */
        fun scaleStick(v: Float): Int {
            if (v.isNaN()) return 0
            val c = v.coerceIn(-1f, 1f)
            return (if (c < 0f) c * -STICK_MIN else c * STICK_MAX).roundToInt().coerceIn(STICK_MIN, STICK_MAX)
        }

        /** 0..1 → 0..255 con saturación. */
        fun scaleTrigger(v: Float): Int {
            if (v.isNaN()) return 0
            return (v.coerceIn(0f, 1f) * TRIGGER_MAX).roundToInt()
        }

        /** Hat analógico → -1/0/1. */
        fun scaleHat(v: Float): Int = when {
            v.isNaN() -> 0
            v <= -0.5f -> -1
            v >= 0.5f -> 1
            else -> 0
        }
    }
}
