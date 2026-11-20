package com.murdoc820.hyperplay.input

import com.murdoc820.hyperplay.engine.HyperPlayNative

/**
 * Destino del estado XInput que publica [XInputPadBridge].
 * Interfaz para poder probar el puente en la JVM sin la librería nativa.
 */
interface XInputPadOutput {
    /** Deja el mando a cero antes de lanzar el juego. */
    fun open(): Boolean
    fun close()
    fun publish(slot: Int, connected: Boolean, flags: Int, state: XInputState): Boolean
    /** `(izquierdo << 16) | derecho` (0..65535 cada motor). */
    fun readRumble(slot: Int): Int
    fun clearRumble(slot: Int)
    fun heartbeat()
}

/**
 * Producción: `DirectXInputBridge` de `libhyperplay.so`, que responde a `XInputGetState` /
 * `XInputSetState` del juego interceptados en la IAT. Solo expone el mando 0.
 */
object XInputPadNative : XInputPadOutput {
    private const val DIRECT_SLOT = 0

    override fun open(): Boolean {
        neutral()
        return true
    }

    override fun close() = neutral()

    override fun publish(slot: Int, connected: Boolean, flags: Int, state: XInputState): Boolean {
        if (slot != DIRECT_SLOT) return false
        val s = if (connected) state else XInputState.NEUTRAL
        HyperPlayNative.nativeDirectSetGamepadState(
            s.buttons, s.leftTrigger, s.rightTrigger, s.thumbLX, s.thumbLY, s.thumbRX, s.thumbRY,
        )
        return true
    }

    override fun readRumble(slot: Int): Int = if (slot == DIRECT_SLOT) HyperPlayNative.nativeDirectGetRumble() else 0

    // El rumble lo fija el juego con XInputSetState; no hay nada que limpiar desde Android.
    override fun clearRumble(slot: Int) = Unit

    override fun heartbeat() = Unit

    private fun neutral() {
        publish(DIRECT_SLOT, connected = false, flags = 0, state = XInputState.NEUTRAL)
    }
}
