package com.murdoc820.hyperplay.input

import android.view.KeyEvent
import kotlin.math.abs
import kotlin.math.max

/**
 * Lógica pura del puente XInput (sin APIs de Android salvo constantes): estado XInput, mapeo
 * Android → XInput por mando, reparto estable de slots, mando táctil (evdev → XInput) y mezcla de
 * los dos motores de rumble. Probada en `XInputPadBridgeTest`.
 */

/** Estado XInput de un mando, ya en los rangos de XINPUT_GAMEPAD. */
data class XInputState(
    val buttons: Int = 0,
    val leftTrigger: Int = 0,
    val rightTrigger: Int = 0,
    val thumbLX: Int = 0,
    val thumbLY: Int = 0,
    val thumbRX: Int = 0,
    val thumbRY: Int = 0,
) {
    /** Combina dos fuentes (mando físico + táctil en el slot 0): OR de botones, el mayor de cada eje. */
    fun merge(other: XInputState): XInputState = XInputState(
        buttons = buttons or other.buttons,
        leftTrigger = max(leftTrigger, other.leftTrigger),
        rightTrigger = max(rightTrigger, other.rightTrigger),
        thumbLX = larger(thumbLX, other.thumbLX),
        thumbLY = larger(thumbLY, other.thumbLY),
        thumbRX = larger(thumbRX, other.thumbRX),
        thumbRY = larger(thumbRY, other.thumbRY),
    )

    companion object {
        val NEUTRAL = XInputState()
        private fun larger(a: Int, b: Int) = if (abs(b) > abs(a)) b else a
    }
}

/** Bits de wButtons (XINPUT_GAMEPAD_*); GUIDE solo lo ve XInputGetStateEx (ordinal 100). */
object XInputButtons {
    const val DPAD_UP = 0x0001
    const val DPAD_DOWN = 0x0002
    const val DPAD_LEFT = 0x0004
    const val DPAD_RIGHT = 0x0008
    const val START = 0x0010
    const val BACK = 0x0020
    const val LEFT_THUMB = 0x0040
    const val RIGHT_THUMB = 0x0080
    const val LEFT_SHOULDER = 0x0100
    const val RIGHT_SHOULDER = 0x0200
    const val GUIDE = 0x0400
    const val A = 0x1000
    const val B = 0x2000
    const val X = 0x4000
    const val Y = 0x8000

    const val DPAD_MASK = DPAD_UP or DPAD_DOWN or DPAD_LEFT or DPAD_RIGHT

    /** Keycode de Android → bit XInput (cruceta incluida); null si no es un botón de mando. */
    fun forKeyCode(keyCode: Int): Int? = KEY_MAP[keyCode]

    private val KEY_MAP: Map<Int, Int> = mapOf(
        KeyEvent.KEYCODE_BUTTON_A to A,
        KeyEvent.KEYCODE_BUTTON_B to B,
        KeyEvent.KEYCODE_BUTTON_X to X,
        KeyEvent.KEYCODE_BUTTON_Y to Y,
        KeyEvent.KEYCODE_BUTTON_L1 to LEFT_SHOULDER,
        KeyEvent.KEYCODE_BUTTON_R1 to RIGHT_SHOULDER,
        KeyEvent.KEYCODE_BUTTON_SELECT to BACK,
        KeyEvent.KEYCODE_BUTTON_START to START,
        KeyEvent.KEYCODE_BUTTON_MODE to GUIDE,
        KeyEvent.KEYCODE_BUTTON_THUMBL to LEFT_THUMB,
        KeyEvent.KEYCODE_BUTTON_THUMBR to RIGHT_THUMB,
        KeyEvent.KEYCODE_DPAD_UP to DPAD_UP,
        KeyEvent.KEYCODE_DPAD_DOWN to DPAD_DOWN,
        KeyEvent.KEYCODE_DPAD_LEFT to DPAD_LEFT,
        KeyEvent.KEYCODE_DPAD_RIGHT to DPAD_RIGHT,
    )
}

object XInputScale {
    const val THUMB_MIN = -32768
    const val THUMB_MAX = 32767
    const val TRIGGER_MAX = 255

    /** Eje Android -1..1 → -32768..32767 (sin zona muerta: la aplica el juego). */
    fun stick(v: Float): Int = ControllerManager.scaleStick(v)

    /** Eje Y de Android (abajo = +) → XInput (arriba = +). */
    fun stickY(v: Float): Int = if (v.isNaN()) 0 else ControllerManager.scaleStick(-v)

    fun trigger(v: Float): Int = ControllerManager.scaleTrigger(v)

    /** Valor evdev (abajo = +) → XInput (arriba = +), con saturación. */
    fun invertEvdev(v: Int): Int = (-v).coerceIn(THUMB_MIN, THUMB_MAX)

    /** Hat -1..1 → bits de cruceta. */
    fun hatBits(hatX: Float, hatY: Float): Int {
        var bits = 0
        when (ControllerManager.scaleHat(hatX)) {
            -1 -> bits = bits or XInputButtons.DPAD_LEFT
            1 -> bits = bits or XInputButtons.DPAD_RIGHT
        }
        when (ControllerManager.scaleHat(hatY)) {
            -1 -> bits = bits or XInputButtons.DPAD_UP
            1 -> bits = bits or XInputButtons.DPAD_DOWN
        }
        return bits
    }
}

/** Ejes del stick derecho: casi todos los mandos usan Z/RZ; algunos (HID genérico) RX/RY. */
enum class RightStickAxes { Z_RZ, RX_RY }

/**
 * Estado de un mando físico a partir de sus KeyEvent/MotionEvent (valores crudos de Android).
 * No es seguro entre hilos: usar desde el hilo principal.
 */
class PadInputTracker {
    private var keyButtons = 0
    private var hatBits = 0
    private var digitalLt = false
    private var digitalRt = false
    private var analogLt = 0
    private var analogRt = 0
    private var lx = 0
    private var ly = 0
    private var rx = 0
    private var ry = 0

    /** @return false si el keycode no es de mando (no cambia nada). */
    fun onKey(keyCode: Int, pressed: Boolean): Boolean {
        when (keyCode) {
            KeyEvent.KEYCODE_BUTTON_L2 -> digitalLt = pressed
            KeyEvent.KEYCODE_BUTTON_R2 -> digitalRt = pressed
            else -> {
                val bit = XInputButtons.forKeyCode(keyCode) ?: return false
                keyButtons = if (pressed) keyButtons or bit else keyButtons and bit.inv()
            }
        }
        return true
    }

    /** Sticks -1..1 (Y abajo = +), gatillos 0..1, hat -1..1. */
    fun onAxes(lx: Float, ly: Float, rx: Float, ry: Float, lt: Float, rt: Float, hatX: Float, hatY: Float) {
        this.lx = XInputScale.stick(lx)
        this.ly = XInputScale.stickY(ly)
        this.rx = XInputScale.stick(rx)
        this.ry = XInputScale.stickY(ry)
        analogLt = XInputScale.trigger(lt)
        analogRt = XInputScale.trigger(rt)
        hatBits = XInputScale.hatBits(hatX, hatY)
    }

    fun state(): XInputState = XInputState(
        buttons = keyButtons or hatBits,
        leftTrigger = if (digitalLt) XInputScale.TRIGGER_MAX else analogLt,
        rightTrigger = if (digitalRt) XInputScale.TRIGGER_MAX else analogRt,
        thumbLX = lx,
        thumbLY = ly,
        thumbRX = rx,
        thumbRY = ry,
    )

    fun reset() {
        keyButtons = 0; hatBits = 0
        digitalLt = false; digitalRt = false
        analogLt = 0; analogRt = 0
        lx = 0; ly = 0; rx = 0; ry = 0
    }

    companion object {
        fun isPadKey(keyCode: Int): Boolean =
            keyCode == KeyEvent.KEYCODE_BUTTON_L2 || keyCode == KeyEvent.KEYCODE_BUTTON_R2 ||
                XInputButtons.forKeyCode(keyCode) != null

        fun chooseRightStick(hasZ: Boolean, hasRz: Boolean, hasRx: Boolean, hasRy: Boolean): RightStickAxes =
            if (!(hasZ && hasRz) && hasRx && hasRy) RightStickAxes.RX_RY else RightStickAxes.Z_RZ
    }
}

/**
 * Reparto estable de slots XInput (0..maxSlots-1) por mando (clave = descriptor de InputDevice).
 * Se da el slot libre más bajo; un mando que se reconecta recupera su slot anterior si sigue libre.
 */
class PadSlotAllocator(private val maxSlots: Int = MAX_SLOTS) {
    private val owners = arrayOfNulls<String>(maxSlots)
    private val lastSlot = HashMap<String, Int>()

    fun slotOf(key: String): Int? = owners.indexOf(key).takeIf { it >= 0 }

    /** @return el slot del mando, o null si los 4 están ocupados. */
    fun assign(key: String): Int? {
        slotOf(key)?.let { return it }
        val preferred = lastSlot[key]
        val slot = if (preferred != null && owners[preferred] == null) {
            preferred
        } else {
            owners.indexOfFirst { it == null }.takeIf { it >= 0 } ?: return null
        }
        owners[slot] = key
        lastSlot[key] = slot
        return slot
    }

    /** @return el slot que quedó libre, o null si el mando no tenía. */
    fun release(key: String): Int? {
        val slot = slotOf(key) ?: return null
        owners[slot] = null
        return slot
    }

    fun owner(slot: Int): String? = owners.getOrNull(slot)

    companion object {
        const val MAX_SLOTS = 4
    }
}

/**
 * Sink evdev del [ControllerManager] del mando táctil → estado XInput (slot 0 del puente).
 * Recibe códigos con layout Xbox 360 de linux/input-event-codes.h (los de [ControllerManager]).
 */
class EvdevXInputSink(private val onState: (XInputState) -> Unit) : InputSink {
    private var buttons = 0
    private val axes = HashMap<Int, Int>()

    override fun button(code: Int, pressed: Boolean) {
        val bit = EVDEV_BUTTONS[code] ?: return
        buttons = if (pressed) buttons or bit else buttons and bit.inv()
    }

    override fun axis(code: Int, value: Int) {
        axes[code] = value
    }

    override fun sync() = onState(state())

    fun state(): XInputState {
        var b = buttons
        when (axes[ControllerManager.ABS_HAT0X] ?: 0) {
            -1 -> b = b or XInputButtons.DPAD_LEFT
            1 -> b = b or XInputButtons.DPAD_RIGHT
        }
        when (axes[ControllerManager.ABS_HAT0Y] ?: 0) {
            -1 -> b = b or XInputButtons.DPAD_UP
            1 -> b = b or XInputButtons.DPAD_DOWN
        }
        return XInputState(
            buttons = b,
            leftTrigger = (axes[ControllerManager.ABS_Z] ?: 0).coerceIn(0, XInputScale.TRIGGER_MAX),
            rightTrigger = (axes[ControllerManager.ABS_RZ] ?: 0).coerceIn(0, XInputScale.TRIGGER_MAX),
            thumbLX = (axes[ControllerManager.ABS_X] ?: 0).coerceIn(XInputScale.THUMB_MIN, XInputScale.THUMB_MAX),
            thumbLY = XInputScale.invertEvdev(axes[ControllerManager.ABS_Y] ?: 0),
            thumbRX = (axes[ControllerManager.ABS_RX] ?: 0).coerceIn(XInputScale.THUMB_MIN, XInputScale.THUMB_MAX),
            thumbRY = XInputScale.invertEvdev(axes[ControllerManager.ABS_RY] ?: 0),
        )
    }

    private companion object {
        val EVDEV_BUTTONS: Map<Int, Int> = mapOf(
            ControllerManager.BTN_SOUTH to XInputButtons.A,
            ControllerManager.BTN_EAST to XInputButtons.B,
            ControllerManager.BTN_WEST to XInputButtons.X,
            ControllerManager.BTN_NORTH to XInputButtons.Y,
            ControllerManager.BTN_TL to XInputButtons.LEFT_SHOULDER,
            ControllerManager.BTN_TR to XInputButtons.RIGHT_SHOULDER,
            ControllerManager.BTN_SELECT to XInputButtons.BACK,
            ControllerManager.BTN_START to XInputButtons.START,
            ControllerManager.BTN_MODE to XInputButtons.GUIDE,
            ControllerManager.BTN_THUMBL to XInputButtons.LEFT_THUMB,
            ControllerManager.BTN_THUMBR to XInputButtons.RIGHT_THUMB,
        )
    }
}

/** Velocidades XInput (0..65535) → amplitudes de VibrationEffect (0 = parado, 1..255). */
object RumbleMixer {
    /** Por debajo de esto, un vibrador sin control de amplitud no se enciende. */
    const val ON_OFF_THRESHOLD = 0x2000

    fun left(packed: Int): Int = (packed ushr 16) and 0xFFFF
    fun right(packed: Int): Int = packed and 0xFFFF

    fun amplitude(speed: Int, amplitudeControl: Boolean = true): Int {
        val s = speed.coerceIn(0, 0xFFFF)
        if (s == 0) return 0
        if (!amplitudeControl) return if (s >= ON_OFF_THRESHOLD) 255 else 0
        return ((s * 255L + 0xFFFE) / 0xFFFF).toInt().coerceIn(1, 255)
    }

    /**
     * Amplitud por vibrador. Con 2 o más: [0] = motor izquierdo (baja frecuencia, "strong"),
     * [1] = derecho ("weak"), como el orden de canales FF_RUMBLE de Android. Con 1: el mayor.
     */
    fun mix(packed: Int, vibratorCount: Int, amplitudeControl: Boolean = true): IntArray = when {
        vibratorCount <= 0 -> IntArray(0)
        vibratorCount == 1 -> intArrayOf(amplitude(max(left(packed), right(packed)), amplitudeControl))
        else -> intArrayOf(amplitude(left(packed), amplitudeControl), amplitude(right(packed), amplitudeControl))
    }

    /** Teléfono (un solo motor): el mayor de los dos. */
    fun phone(packed: Int, amplitudeControl: Boolean = true): Int =
        amplitude(max(left(packed), right(packed)), amplitudeControl)
}
