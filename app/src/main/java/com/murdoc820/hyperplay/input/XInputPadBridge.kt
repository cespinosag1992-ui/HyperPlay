package com.murdoc820.hyperplay.input

import android.content.Context
import android.hardware.input.InputManager
import android.os.CombinedVibration
import android.os.Handler
import android.os.Looper
import android.os.SystemClock
import android.os.VibrationEffect
import android.os.VibratorManager
import android.util.Log
import android.view.InputDevice
import android.view.KeyEvent
import android.view.MotionEvent

/** Salida de rumble (en producción [AndroidRumbleOutput]); se llama solo desde el hilo de rumble. */
interface RumbleOutput {
    /**
     * @param deviceIds InputDevice ids del mando del slot (vacío = solo mando táctil).
     * @param packed `(izquierdo << 16) | derecho`; 0 = parar.
     */
    fun apply(slot: Int, deviceIds: IntArray, packed: Int)
    fun stopAll()
}

/**
 * Puente XInput: mandos físicos de Android (hasta 4, slot estable por descriptor, hotplug) y el
 * mando táctil (fusionado en el slot 0) → estado XInput en [XInputPadOutput] (en producción
 * `DirectXInputBridge`, que el juego lee con `XInputGetState`). El rumble que escribe el juego se
 * lee en un hilo propio (~250 Hz) y se envía a los vibradores del mando ([RumbleOutput]).
 *
 * Entrada y estado: hilo principal. Con [enabled] = false todos los slots se publican
 * desconectados y el juego no ve mandos XInput.
 *
 * @param context null en tests: sin seguimiento de dispositivos ni hilo de rumble automático.
 */
class XInputPadBridge(
    context: Context?,
    private val memory: XInputPadOutput,
    private val rumbleOutput: RumbleOutput? = null,
) {
    private class Pad(val key: String) {
        val tracker = PadInputTracker()
        val deviceIds = LinkedHashSet<Int>()
        var rightStick = RightStickAxes.Z_RZ
        var hasVibrator = false
        /** Stick derecho en RX/RY y gatillos en Z/RZ (HID genérico); signed = rango -1..1. */
        var zTriggers = false
        var zTriggersSigned = false
    }

    private val appContext: Context? = context?.applicationContext
    private val allocator = PadSlotAllocator()
    private val pads = HashMap<String, Pad>()
    private val deviceKeys = HashMap<Int, String>()
    private var touchState = XInputState.NEUTRAL
    private var opened = false
    private var inputManager: InputManager? = null

    /** Modo XInput activo (si no, todo desconectado). */
    @Volatile var enabled: Boolean = true
        set(value) {
            if (field == value) return
            field = value
            if (!value) resetStates()
            publishAll()
        }

    /** Mando táctil en el slot 0 (fusionado con el mando físico de ese slot, si lo hay). */
    var touchPadEnabled: Boolean = true
        set(value) {
            if (field == value) return
            field = value
            if (!value) touchState = XInputState.NEUTRAL
            publishSlot(0)
        }

    /** Vibrar el teléfono si el mando no tiene vibrador (o solo hay mando táctil). */
    @Volatile var phoneRumbleFallback: Boolean = false

    // ---- Compartido con el hilo de rumble.
    @Volatile private var slotDevices: Array<IntArray> = Array(PadSlotAllocator.MAX_SLOTS) { IntArray(0) }
    @Volatile private var outputSuspended = false
    private val lastPacked = IntArray(PadSlotAllocator.MAX_SLOTS)
    private val lastApplyAt = LongArray(PadSlotAllocator.MAX_SLOTS)
    @Volatile private var rumbleThread: Thread? = null

    private val deviceListener = object : InputManager.InputDeviceListener {
        override fun onInputDeviceAdded(deviceId: Int) = refreshDevice(deviceId)
        override fun onInputDeviceRemoved(deviceId: Int) = deviceDisconnected(deviceId)
        override fun onInputDeviceChanged(deviceId: Int) = refreshDevice(deviceId)
    }

    // ------------------------------------------------------------------ ciclo de vida

    /** Deja el estado XInput a cero (cualquier hilo; antes de lanzar el juego). */
    fun open(): Boolean {
        val ok = memory.open()
        synchronized(this) { opened = ok }
        return ok
    }

    /** Publica el estado inicial, sigue los mandos y arranca el hilo de rumble (hilo principal). */
    fun start() {
        publishAll()
        val ctx = appContext ?: return
        if (inputManager == null) {
            val im = ctx.getSystemService(InputManager::class.java)
            if (im != null) {
                im.registerInputDeviceListener(deviceListener, Handler(Looper.getMainLooper()))
                inputManager = im
                InputDevice.getDeviceIds().forEach(::refreshDevice)
            }
        }
        if (rumbleThread == null && rumbleOutput != null) {
            rumbleThread = Thread(::rumbleLoop, "HyperPlayRumble").apply {
                isDaemon = true
                start()
            }
        }
    }

    /** Para el hilo, la vibración y desconecta todos los mandos (hilo principal). */
    fun close() {
        inputManager?.unregisterInputDeviceListener(deviceListener)
        inputManager = null
        rumbleThread?.let {
            rumbleThread = null
            it.interrupt()
            runCatching { it.join(RUMBLE_JOIN_MS) }
        }
        rumbleOutput?.stopAll()
        synchronized(this) {
            if (opened) memory.close()
            opened = false
        }
    }

    /** onPause / Quick Menu: suelta todo y para la vibración (el latido sigue). */
    fun suspendOutput() {
        outputSuspended = true
        resetStates()
        publishAll()
    }

    fun resumeOutput() {
        outputSuspended = false
    }

    /** Suelta botones y centra ejes de todos los mandos (p. ej. al abrir el Quick Menu). */
    fun releaseAll() {
        resetStates()
        publishAll()
    }

    // ------------------------------------------------------------------ entrada Android

    fun onKeyEvent(event: KeyEvent): Boolean {
        if (!isPadSource(event.source) || !PadInputTracker.isPadKey(event.keyCode)) return false
        if (event.action != KeyEvent.ACTION_DOWN && event.action != KeyEvent.ACTION_UP) return false
        if (event.action == KeyEvent.ACTION_DOWN && event.repeatCount > 0) return enabled
        val key = keyFor(event.deviceId, event.device) ?: return false
        return handleKey(key, event.deviceId, event.keyCode, event.action == KeyEvent.ACTION_DOWN)
    }

    fun onGenericMotionEvent(event: MotionEvent): Boolean {
        if (!event.isFromSource(InputDevice.SOURCE_JOYSTICK) || event.actionMasked != MotionEvent.ACTION_MOVE) {
            return false
        }
        val key = keyFor(event.deviceId, event.device) ?: return false
        val pad = pads[key]
        val rightZ = pad?.rightStick != RightStickAxes.RX_RY
        fun axis(a: Int) = event.getAxisValue(a)
        // Mandos HID genéricos: stick derecho en RX/RY y gatillos analógicos en Z/RZ.
        val zTrig = pad?.zTriggers == true
        val zSigned = pad?.zTriggersSigned == true
        fun zTrigger(a: Int): Float {
            if (!zTrig) return 0f
            val v = axis(a)
            return if (zSigned) (v + 1f) / 2f else v
        }
        return handleAxes(
            key, event.deviceId,
            lx = axis(MotionEvent.AXIS_X),
            ly = axis(MotionEvent.AXIS_Y),
            rx = axis(if (rightZ) MotionEvent.AXIS_Z else MotionEvent.AXIS_RX),
            ry = axis(if (rightZ) MotionEvent.AXIS_RZ else MotionEvent.AXIS_RY),
            lt = maxOf(axis(MotionEvent.AXIS_LTRIGGER), axis(MotionEvent.AXIS_BRAKE), zTrigger(MotionEvent.AXIS_Z)),
            rt = maxOf(axis(MotionEvent.AXIS_RTRIGGER), axis(MotionEvent.AXIS_GAS), zTrigger(MotionEvent.AXIS_RZ)),
            hatX = axis(MotionEvent.AXIS_HAT_X),
            hatY = axis(MotionEvent.AXIS_HAT_Y),
        )
    }

    // ------------------------------------------------------------------ lógica (testeable)

    /**
     * Alta de un dispositivo. Los mandos "de verdad" (sticks X/Y) reciben slot ya; el resto
     * (p. ej. botones integrados del teléfono) solo cuando envían su primera pulsación.
     */
    fun deviceConnected(key: String, deviceId: Int, eagerSlot: Boolean, hasVibrator: Boolean,
                        rightStick: RightStickAxes = RightStickAxes.Z_RZ) {
        deviceKeys[deviceId] = key
        val pad = pads.getOrPut(key) { Pad(key) }
        pad.deviceIds.add(deviceId)
        pad.hasVibrator = pad.hasVibrator || hasVibrator
        pad.rightStick = rightStick
        if (eagerSlot) assignSlot(pad)
    }

    fun deviceDisconnected(deviceId: Int) {
        val key = deviceKeys.remove(deviceId) ?: return
        val pad = pads[key] ?: return
        pad.deviceIds.remove(deviceId)
        if (pad.deviceIds.isNotEmpty()) return
        pads.remove(key)
        val slot = allocator.release(key) ?: return
        log("mando desconectado: slot $slot")
        updateSlotDevices()
        memory.clearRumble(slot)
        publishSlot(slot)
    }

    /** @return true si el evento se consumió como entrada XInput. */
    fun handleKey(key: String, deviceId: Int, keyCode: Int, pressed: Boolean): Boolean {
        if (!enabled) return false
        val pad = padFor(key, deviceId)
        if (!pad.tracker.onKey(keyCode, pressed)) return false
        val slot = allocator.slotOf(key)
        if (slot != null) publishSlot(slot) else if (pressed) assignSlot(pad)  // assignSlot publica
        return true
    }

    fun handleAxes(key: String, deviceId: Int, lx: Float, ly: Float, rx: Float, ry: Float,
                   lt: Float, rt: Float, hatX: Float, hatY: Float): Boolean {
        if (!enabled) return false
        val pad = padFor(key, deviceId)
        pad.tracker.onAxes(lx, ly, rx, ry, lt, rt, hatX, hatY)
        allocator.slotOf(key)?.let(::publishSlot)
        return true
    }

    /** Estado del mando táctil (desde [EvdevXInputSink]). */
    fun setTouchState(state: XInputState) {
        if (!touchPadEnabled || state == touchState) return
        touchState = state
        publishSlot(0)
    }

    fun slotOf(key: String): Int? = allocator.slotOf(key)

    /** Estado que se publica en [slot]; null = desconectado. */
    fun stateForSlot(slot: Int): XInputState? {
        if (!enabled) return null
        val pad = allocator.owner(slot)?.let { pads[it] }
        val touch = slot == 0 && touchPadEnabled
        return when {
            pad != null && touch -> pad.tracker.state().merge(touchState)
            pad != null -> pad.tracker.state()
            touch -> touchState
            else -> null
        }
    }

    fun flagsForSlot(slot: Int): Int {
        val pad = allocator.owner(slot)?.let { pads[it] }
        var flags = 0
        if (pad == null && slot == 0 && touchPadEnabled) flags = flags or FLAG_VIRTUAL
        if (pad?.hasVibrator == true || phoneRumbleFallback) flags = flags or FLAG_RUMBLE
        return flags
    }

    /**
     * Una vuelta del hilo de rumble: latido + rumble nuevo → [RumbleOutput]. Pública para tests.
     * Cambios a no-cero como mucho cada [RUMBLE_MIN_INTERVAL_MS]; parar es inmediato; un rumble
     * activo se renueva cada [RUMBLE_REFRESH_MS] (los efectos duran [RUMBLE_EFFECT_MS]).
     */
    fun pollRumbleOnce(nowMs: Long) {
        val out = rumbleOutput ?: return
        val devices = slotDevices
        synchronized(this) {
            if (!opened) return
            memory.heartbeat()
        }
        for (slot in 0 until PadSlotAllocator.MAX_SLOTS) {
            val packed = if (outputSuspended || !enabled) 0 else memory.readRumble(slot)
            val last = lastPacked[slot]
            val due = when {
                packed == last -> packed != 0 && nowMs - lastApplyAt[slot] >= RUMBLE_REFRESH_MS
                packed == 0 -> true
                else -> nowMs - lastApplyAt[slot] >= RUMBLE_MIN_INTERVAL_MS
            }
            if (!due) continue
            lastPacked[slot] = packed
            lastApplyAt[slot] = nowMs
            out.apply(slot, devices[slot], packed)
        }
    }

    // ------------------------------------------------------------------ internos

    private fun rumbleLoop() {
        try {
            while (!Thread.currentThread().isInterrupted) {
                pollRumbleOnce(SystemClock.elapsedRealtime())
                Thread.sleep(RUMBLE_POLL_MS)
            }
        } catch (_: InterruptedException) {
            // fin de sesión
        } catch (e: RuntimeException) {
            Log.w(TAG, "hilo de rumble terminado: ${e.message}")
        }
    }

    /** Sin contexto (tests JVM) no se usa android.util.Log. */
    private fun log(msg: String) {
        if (appContext != null) Log.i(TAG, msg)
    }

    private fun padFor(key: String, deviceId: Int): Pad {
        deviceKeys[deviceId] = key
        return pads.getOrPut(key) { Pad(key) }.also { it.deviceIds.add(deviceId) }
    }

    private fun assignSlot(pad: Pad) {
        if (allocator.slotOf(pad.key) != null) return
        val slot = allocator.assign(pad.key) ?: run {
            log("más de ${PadSlotAllocator.MAX_SLOTS} mandos: ${pad.key} sin slot XInput")
            return
        }
        log("mando conectado: slot $slot (vibrador=${pad.hasVibrator})")
        updateSlotDevices()
        publishSlot(slot)
    }

    private fun updateSlotDevices() {
        slotDevices = Array(PadSlotAllocator.MAX_SLOTS) { slot ->
            allocator.owner(slot)?.let { pads[it] }?.deviceIds?.toIntArray() ?: IntArray(0)
        }
    }

    private fun resetStates() {
        pads.values.forEach { it.tracker.reset() }
        touchState = XInputState.NEUTRAL
    }

    private fun publishAll() {
        for (slot in 0 until PadSlotAllocator.MAX_SLOTS) publishSlot(slot)
    }

    private fun publishSlot(slot: Int) {
        synchronized(this) { if (!opened) return }
        val state = stateForSlot(slot)
        memory.publish(slot, state != null, flagsForSlot(slot), state ?: XInputState.NEUTRAL)
    }

    private fun keyFor(deviceId: Int, device: InputDevice?): String? {
        deviceKeys[deviceId]?.let { return it }
        val dev = device ?: return null
        refreshDevice(dev.id)
        return deviceKeys[deviceId] ?: dev.descriptor
    }

    private fun refreshDevice(deviceId: Int) {
        val dev = InputDevice.getDevice(deviceId) ?: return
        if (dev.isVirtual || !(isPadSource(dev.sources))) return
        val hasStick = dev.getMotionRange(MotionEvent.AXIS_X) != null && dev.getMotionRange(MotionEvent.AXIS_Y) != null
        val hasVibrator = runCatching { dev.vibratorManager.vibratorIds.isNotEmpty() }.getOrDefault(false)
        val right = PadInputTracker.chooseRightStick(
            hasZ = dev.getMotionRange(MotionEvent.AXIS_Z) != null,
            hasRz = dev.getMotionRange(MotionEvent.AXIS_RZ) != null,
            hasRx = dev.getMotionRange(MotionEvent.AXIS_RX) != null,
            hasRy = dev.getMotionRange(MotionEvent.AXIS_RY) != null,
        )
        deviceConnected(dev.descriptor, deviceId, eagerSlot = hasStick, hasVibrator = hasVibrator, rightStick = right)
        val hasAnalogTriggers = dev.getMotionRange(MotionEvent.AXIS_LTRIGGER) != null ||
            dev.getMotionRange(MotionEvent.AXIS_BRAKE) != null
        pads[dev.descriptor]?.let { pad ->
            val zRange = dev.getMotionRange(MotionEvent.AXIS_Z)
            pad.zTriggers = right == RightStickAxes.RX_RY && !hasAnalogTriggers && zRange != null &&
                dev.getMotionRange(MotionEvent.AXIS_RZ) != null
            pad.zTriggersSigned = (zRange?.min ?: 0f) < -0.5f
        }
        log("mando ${dev.name}: stick derecho=$right gatillosZ=${pads[dev.descriptor]?.zTriggers} vibrador=$hasVibrator")
    }

    companion object {
        private const val TAG = "HyperPlayPad"
        const val FLAG_RUMBLE = 0x1
        const val FLAG_VIRTUAL = 0x2
        const val RUMBLE_POLL_MS = 4L
        const val RUMBLE_MIN_INTERVAL_MS = 16L
        const val RUMBLE_REFRESH_MS = 1000L
        const val RUMBLE_EFFECT_MS = 2000L
        private const val RUMBLE_JOIN_MS = 500L

        fun isPadSource(source: Int): Boolean =
            (source and InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
                (source and InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK
    }
}

/**
 * Rumble en los vibradores del mando (`InputDevice.getVibratorManager()`, API 31): con 2 vibradores
 * motor izquierdo/derecho por separado; con 1, el mayor. Si el mando no vibra y
 * [phoneFallback] lo permite, vibra el teléfono. Efectos de un disparo de
 * [XInputPadBridge.RUMBLE_EFFECT_MS] renovados por el puente mientras dure el rumble.
 */
class AndroidRumbleOutput(context: Context, private val phoneFallback: () -> Boolean) : RumbleOutput {
    private val phone = context.applicationContext.getSystemService(VibratorManager::class.java)?.defaultVibrator
    private val phoneAmps = IntArray(PadSlotAllocator.MAX_SLOTS)
    private var phoneActive = 0
    /** Slot → vibradores del mando que están vibrando (para pararlos al cerrar). */
    private val padActive = HashMap<Int, VibratorManager>()

    override fun apply(slot: Int, deviceIds: IntArray, packed: Int) {
        val vm = deviceIds.asSequence()
            .mapNotNull { InputDevice.getDevice(it)?.vibratorManager }
            .firstOrNull { it.vibratorIds.isNotEmpty() }
        if (vm != null) {
            phoneAmps[slot] = 0
            vibratePad(slot, vm, packed)
        } else {
            val amp = if (phoneFallback()) RumbleMixer.phone(packed, phone?.hasAmplitudeControl() ?: false) else 0
            phoneAmps[slot] = amp
        }
        updatePhone()
    }

    override fun stopAll() {
        for (vm in padActive.values) runCatching { vm.cancel() }
        padActive.clear()
        phoneAmps.fill(0)
        updatePhone()
    }

    private fun vibratePad(slot: Int, vm: VibratorManager, packed: Int) {
        val ids = vm.vibratorIds
        val amplitudeControl = ids.all { vm.getVibrator(it).hasAmplitudeControl() }
        val amps = RumbleMixer.mix(packed, ids.size, amplitudeControl)
        if (amps.all { it == 0 }) {
            runCatching { vm.cancel() }
            padActive.remove(slot)
            return
        }
        runCatching {
            val combined = if (ids.size >= 2) {
                val parallel = CombinedVibration.startParallel()
                for (i in 0 until 2) if (amps[i] > 0) parallel.addVibrator(ids[i], effect(amps[i]))
                parallel.combine()
            } else {
                CombinedVibration.createParallel(effect(amps[0]))
            }
            vm.vibrate(combined)
            padActive[slot] = vm
        }.onFailure { Log.w(TAG, "rumble del mando falló: ${it.message}") }
    }

    private fun updatePhone() {
        val v = phone ?: return
        val amp = phoneAmps.max()
        if (amp == phoneActive && amp == 0) return
        phoneActive = amp
        runCatching { if (amp == 0) v.cancel() else v.vibrate(effect(amp)) }
            .onFailure { Log.w(TAG, "vibración del teléfono falló: ${it.message}") }
    }

    private fun effect(amp: Int): VibrationEffect =
        VibrationEffect.createOneShot(XInputPadBridge.RUMBLE_EFFECT_MS, amp.coerceIn(1, 255))

    private companion object {
        const val TAG = "HyperPlayPad"
    }
}
