package com.murdoc820.hyperplay.input

import android.view.KeyEvent
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class XInputPadBridgeTest {

    private class FakeMemory : XInputPadOutput {
        var opened = false
        val published = HashMap<Int, Triple<Boolean, Int, XInputState>>()
        val rumble = IntArray(4)
        val cleared = ArrayList<Int>()
        var heartbeats = 0
        override fun open(): Boolean { opened = true; return true }
        override fun close() { opened = false }
        override fun publish(slot: Int, connected: Boolean, flags: Int, state: XInputState): Boolean {
            published[slot] = Triple(connected, flags, state)
            return true
        }
        override fun readRumble(slot: Int): Int = rumble[slot]
        override fun clearRumble(slot: Int) { cleared += slot; rumble[slot] = 0 }
        override fun heartbeat() { heartbeats++ }
    }

    private class FakeRumble : RumbleOutput {
        val calls = ArrayList<Triple<Int, List<Int>, Int>>()
        var stopped = false
        override fun apply(slot: Int, deviceIds: IntArray, packed: Int) { calls += Triple(slot, deviceIds.toList(), packed) }
        override fun stopAll() { stopped = true }
    }

    private fun bridge(memory: FakeMemory = FakeMemory(), rumble: RumbleOutput? = null) =
        XInputPadBridge(null, memory, rumble).also {
            it.open()
            it.start()
        }

    // ---------------------------------------------------------------- mapeo

    @Test
    fun keyMapping_coversXboxLayout() {
        assertEquals(XInputButtons.A, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_A))
        assertEquals(XInputButtons.B, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_B))
        assertEquals(XInputButtons.X, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_X))
        assertEquals(XInputButtons.Y, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_Y))
        assertEquals(XInputButtons.LEFT_SHOULDER, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_L1))
        assertEquals(XInputButtons.RIGHT_SHOULDER, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_R1))
        assertEquals(XInputButtons.BACK, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_SELECT))
        assertEquals(XInputButtons.START, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_START))
        assertEquals(XInputButtons.GUIDE, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_MODE))
        assertEquals(XInputButtons.LEFT_THUMB, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_THUMBL))
        assertEquals(XInputButtons.RIGHT_THUMB, XInputButtons.forKeyCode(KeyEvent.KEYCODE_BUTTON_THUMBR))
        assertEquals(XInputButtons.DPAD_UP, XInputButtons.forKeyCode(KeyEvent.KEYCODE_DPAD_UP))
        assertEquals(XInputButtons.DPAD_RIGHT, XInputButtons.forKeyCode(KeyEvent.KEYCODE_DPAD_RIGHT))
        assertNull(XInputButtons.forKeyCode(KeyEvent.KEYCODE_A))
        assertNull(XInputButtons.forKeyCode(KeyEvent.KEYCODE_BACK))
    }

    @Test
    fun tracker_buttonsTriggersAndDigitalTriggers() {
        val t = PadInputTracker()
        assertTrue(t.onKey(KeyEvent.KEYCODE_BUTTON_A, true))
        assertTrue(t.onKey(KeyEvent.KEYCODE_BUTTON_L1, true))
        assertFalse(t.onKey(KeyEvent.KEYCODE_VOLUME_UP, true))
        assertEquals(XInputButtons.A or XInputButtons.LEFT_SHOULDER, t.state().buttons)
        t.onKey(KeyEvent.KEYCODE_BUTTON_A, false)
        assertEquals(XInputButtons.LEFT_SHOULDER, t.state().buttons)

        t.onAxes(0f, 0f, 0f, 0f, lt = 0.5f, rt = 1f, hatX = 0f, hatY = 0f)
        assertEquals(128, t.state().leftTrigger)
        assertEquals(255, t.state().rightTrigger)
        // L2 digital manda el gatillo al máximo aunque el analógico diga otra cosa.
        t.onKey(KeyEvent.KEYCODE_BUTTON_L2, true)
        assertEquals(255, t.state().leftTrigger)
        t.onKey(KeyEvent.KEYCODE_BUTTON_L2, false)
        assertEquals(128, t.state().leftTrigger)
    }

    @Test
    fun tracker_sticksInvertYAndSaturate() {
        val t = PadInputTracker()
        // Android: arriba = -1. XInput: arriba = +32767.
        t.onAxes(lx = 1f, ly = -1f, rx = -1f, ry = 1f, lt = 0f, rt = 0f, hatX = 0f, hatY = 0f)
        val s = t.state()
        assertEquals(32767, s.thumbLX)
        assertEquals(32767, s.thumbLY)
        assertEquals(-32768, s.thumbRX)
        assertEquals(-32768, s.thumbRY)
        t.onAxes(lx = 2f, ly = Float.NaN, rx = 0f, ry = 0.5f, lt = -1f, rt = 2f, hatX = 0f, hatY = 0f)
        assertEquals(32767, t.state().thumbLX)
        assertEquals(0, t.state().thumbLY)
        assertEquals(-16384, t.state().thumbRY)
        assertEquals(0, t.state().leftTrigger)
        assertEquals(255, t.state().rightTrigger)
    }

    @Test
    fun tracker_hatAndDpadKeysCombine() {
        val t = PadInputTracker()
        t.onAxes(0f, 0f, 0f, 0f, 0f, 0f, hatX = -1f, hatY = 1f)
        assertEquals(XInputButtons.DPAD_LEFT or XInputButtons.DPAD_DOWN, t.state().buttons)
        t.onKey(KeyEvent.KEYCODE_DPAD_UP, true)
        assertEquals(
            XInputButtons.DPAD_LEFT or XInputButtons.DPAD_DOWN or XInputButtons.DPAD_UP,
            t.state().buttons,
        )
        t.onAxes(0f, 0f, 0f, 0f, 0f, 0f, hatX = 0.2f, hatY = 0f)
        assertEquals(XInputButtons.DPAD_UP, t.state().buttons)
        t.reset()
        assertEquals(XInputState.NEUTRAL, t.state())
    }

    @Test
    fun rightStickAxesChoice() {
        assertEquals(RightStickAxes.Z_RZ, PadInputTracker.chooseRightStick(true, true, true, true))
        assertEquals(RightStickAxes.Z_RZ, PadInputTracker.chooseRightStick(true, true, false, false))
        assertEquals(RightStickAxes.RX_RY, PadInputTracker.chooseRightStick(false, false, true, true))
        assertEquals(RightStickAxes.Z_RZ, PadInputTracker.chooseRightStick(false, false, false, false))
    }

    @Test
    fun merge_orsButtonsAndKeepsLargerAxes() {
        val a = XInputState(buttons = XInputButtons.A, leftTrigger = 10, thumbLX = -20000, thumbLY = 100)
        val b = XInputState(buttons = XInputButtons.B, leftTrigger = 200, thumbLX = 5000, thumbLY = -30000)
        val m = a.merge(b)
        assertEquals(XInputButtons.A or XInputButtons.B, m.buttons)
        assertEquals(200, m.leftTrigger)
        assertEquals(-20000, m.thumbLX)
        assertEquals(-30000, m.thumbLY)
    }

    // ---------------------------------------------------------------- slots

    @Test
    fun allocator_lowestFreeStableAndFull() {
        val a = PadSlotAllocator()
        assertEquals(0, a.assign("p1"))
        assertEquals(1, a.assign("p2"))
        assertEquals(1, a.assign("p2"))  // idempotente
        assertEquals(2, a.assign("p3"))
        assertEquals(3, a.assign("p4"))
        assertNull(a.assign("p5"))
        assertEquals(1, a.release("p2"))
        assertNull(a.release("p2"))
        assertEquals(1, a.assign("p5"))  // el hueco más bajo
        assertEquals(0, a.release("p1"))
        assertEquals(1, a.release("p5"))
        // p2 vuelve y recupera su slot anterior (1), aunque el 0 esté libre.
        assertEquals(1, a.assign("p2"))
        assertEquals(0, a.assign("p1"))
        assertEquals("p2", a.owner(1))
    }

    @Test
    fun bridge_touchPadIsSlot0AndPhysicalPadMergesThere() {
        val mem = FakeMemory()
        val b = bridge(mem)
        // Sin mandos físicos: solo el táctil, conectado en el slot 0 y marcado virtual.
        assertEquals(true, mem.published[0]?.first)
        assertEquals(XInputPadBridge.FLAG_VIRTUAL, mem.published[0]?.second)
        assertEquals(false, mem.published[1]?.first)

        b.deviceConnected("padA", 7, eagerSlot = true, hasVibrator = true)
        assertEquals(0, b.slotOf("padA"))
        assertEquals(XInputPadBridge.FLAG_RUMBLE, mem.published[0]?.second)
        b.deviceConnected("padB", 9, eagerSlot = true, hasVibrator = false)
        assertEquals(1, b.slotOf("padB"))
        assertEquals(true, mem.published[1]?.first)
        assertEquals(0, mem.published[1]?.second)

        assertTrue(b.handleKey("padA", 7, KeyEvent.KEYCODE_BUTTON_A, true))
        b.setTouchState(XInputState(buttons = XInputButtons.B))
        assertEquals(XInputButtons.A or XInputButtons.B, mem.published[0]?.third?.buttons)
        assertTrue(b.handleKey("padB", 9, KeyEvent.KEYCODE_BUTTON_Y, true))
        assertEquals(XInputButtons.Y, mem.published[1]?.third?.buttons)

        // Desenchufar: slot desconectado y rumble a cero.
        b.deviceDisconnected(9)
        assertEquals(false, mem.published[1]?.first)
        assertEquals(listOf(1), mem.cleared)
        assertNull(b.slotOf("padB"))
    }

    @Test
    fun bridge_nonGamepadDeviceGetsSlotOnFirstPress() {
        val mem = FakeMemory()
        val b = bridge(mem)
        b.deviceConnected("phoneButtons", 3, eagerSlot = false, hasVibrator = false)
        assertNull(b.slotOf("phoneButtons"))
        b.deviceConnected("realPad", 4, eagerSlot = true, hasVibrator = true)
        assertEquals(0, b.slotOf("realPad"))
        // Soltar sin haber pulsado no da slot; pulsar sí.
        assertTrue(b.handleKey("phoneButtons", 3, KeyEvent.KEYCODE_BUTTON_A, false))
        assertNull(b.slotOf("phoneButtons"))
        assertTrue(b.handleKey("phoneButtons", 3, KeyEvent.KEYCODE_BUTTON_A, true))
        assertEquals(1, b.slotOf("phoneButtons"))
        assertEquals(XInputButtons.A, mem.published[1]?.third?.buttons)
    }

    @Test
    fun bridge_sameDescriptorTwoNodesShareSlotUntilBothGone() {
        val mem = FakeMemory()
        val b = bridge(mem)
        b.deviceConnected("ds5", 11, eagerSlot = true, hasVibrator = true)
        b.deviceConnected("ds5", 12, eagerSlot = false, hasVibrator = false)  // sensores
        assertEquals(0, b.slotOf("ds5"))
        b.deviceDisconnected(12)
        assertEquals(0, b.slotOf("ds5"))
        b.deviceDisconnected(11)
        assertNull(b.slotOf("ds5"))
    }

    @Test
    fun bridge_keyboardModeDisconnectsEverythingAndIgnoresInput() {
        val mem = FakeMemory()
        val b = bridge(mem)
        b.deviceConnected("padA", 1, eagerSlot = true, hasVibrator = true)
        b.handleKey("padA", 1, KeyEvent.KEYCODE_BUTTON_A, true)
        b.enabled = false
        for (slot in 0 until 4) assertEquals(false, mem.published[slot]?.first)
        assertFalse(b.handleKey("padA", 1, KeyEvent.KEYCODE_BUTTON_B, true))
        assertFalse(b.handleAxes("padA", 1, 1f, 0f, 0f, 0f, 0f, 0f, 0f, 0f))
        b.enabled = true
        // Al volver, el botón A pulsado antes ya no cuenta (estado limpio).
        assertEquals(true, mem.published[0]?.first)
        assertEquals(0, mem.published[0]?.third?.buttons)
    }

    @Test
    fun bridge_releaseAllAndSuspendNeutralizeState() {
        val mem = FakeMemory()
        val b = bridge(mem)
        b.deviceConnected("padA", 1, eagerSlot = true, hasVibrator = true)
        b.handleAxes("padA", 1, 1f, 1f, 0f, 0f, 1f, 0f, 0f, 0f)
        assertEquals(32767, mem.published[0]?.third?.thumbLX)
        b.releaseAll()
        assertEquals(XInputState.NEUTRAL, mem.published[0]?.third)
        b.handleKey("padA", 1, KeyEvent.KEYCODE_BUTTON_X, true)
        b.suspendOutput()
        assertEquals(XInputState.NEUTRAL, mem.published[0]?.third)
        assertEquals(true, mem.published[0]?.first)  // sigue conectado
    }

    @Test
    fun bridge_touchPadCanBeDisabled() {
        val mem = FakeMemory()
        val b = bridge(mem)
        b.touchPadEnabled = false
        assertEquals(false, mem.published[0]?.first)
        b.setTouchState(XInputState(buttons = XInputButtons.A))
        assertEquals(false, mem.published[0]?.first)
    }

    // ---------------------------------------------------------------- mando táctil (evdev)

    @Test
    fun evdevSink_mapsTouchControllerOutput() {
        var last: XInputState? = null
        val sink = EvdevXInputSink { last = it }
        val cm = ControllerManager(null, sink, object : ControllerManager.Listener {
            override fun onQuickMenuRequested() = Unit
            override fun onInputModeChanged(mode: InputMode) = Unit
            override fun onControllersChanged(names: List<String>) = Unit
        })
        cm.handleButton(KeyEvent.KEYCODE_BUTTON_A, true, fromTouch = true)
        assertEquals(XInputButtons.A, last?.buttons)
        cm.handleButton(KeyEvent.KEYCODE_DPAD_LEFT, true, fromTouch = true)
        assertEquals(XInputButtons.A or XInputButtons.DPAD_LEFT, last?.buttons)
        // Stick táctil hacia arriba (Android -1) → XInput +32767.
        cm.handleAxes(0.5f, -1f, 0f, 0f, 0f, 0f, 0f, 0f, fromTouch = true)
        assertEquals(16384, last?.thumbLX)
        assertEquals(32767, last?.thumbLY)
        cm.handleButton(KeyEvent.KEYCODE_BUTTON_R2, true, fromTouch = true)
        assertEquals(255, last?.rightTrigger)
        cm.releaseAll()
        assertEquals(XInputState.NEUTRAL, last)
    }

    @Test
    fun evdevSink_invertsYWithSaturation() {
        assertEquals(32767, XInputScale.invertEvdev(-32768))
        assertEquals(-32767, XInputScale.invertEvdev(32767))
        assertEquals(0, XInputScale.invertEvdev(0))
    }

    // ---------------------------------------------------------------- rumble

    @Test
    fun rumbleMixer_amplitudesAndChannels() {
        assertEquals(0, RumbleMixer.amplitude(0))
        assertEquals(1, RumbleMixer.amplitude(1))
        assertEquals(255, RumbleMixer.amplitude(0xFFFF))
        assertEquals(128, RumbleMixer.amplitude(0x8000))
        assertEquals(0, RumbleMixer.amplitude(0x1000, amplitudeControl = false))
        assertEquals(255, RumbleMixer.amplitude(0x2000, amplitudeControl = false))
        val packed = (0xFFFF shl 16) or 0x8000
        assertEquals(0xFFFF, RumbleMixer.left(packed))
        assertEquals(0x8000, RumbleMixer.right(packed))
        assertArrayEquals(intArrayOf(255, 128), RumbleMixer.mix(packed, 2))
        assertArrayEquals(intArrayOf(255, 128), RumbleMixer.mix(packed, 4))
        assertArrayEquals(intArrayOf(1), RumbleMixer.mix((0x10 shl 16) or 0x100, 1))  // el mayor (0x100)
        assertArrayEquals(intArrayOf(128), RumbleMixer.mix(0x8000, 1))  // solo derecho
        assertArrayEquals(IntArray(0), RumbleMixer.mix(packed, 0))
        assertEquals(255, RumbleMixer.phone(packed))
    }

    @Test
    fun pollRumble_appliesChangesRateLimitsAndRefreshes() {
        val mem = FakeMemory()
        val out = FakeRumble()
        val b = bridge(mem, out)
        b.deviceConnected("padA", 7, eagerSlot = true, hasVibrator = true)

        b.pollRumbleOnce(1000)
        assertTrue(out.calls.isEmpty())  // todo a cero, sin cambios
        assertEquals(1, mem.heartbeats)

        mem.rumble[0] = (0x4000 shl 16) or 0x1000
        b.pollRumbleOnce(1001)
        assertEquals(Triple(0, listOf(7), mem.rumble[0]), out.calls.last())

        // Cambio a no-cero antes de 16 ms: se aplaza.
        mem.rumble[0] = (0x5000 shl 16)
        b.pollRumbleOnce(1005)
        assertEquals(1, out.calls.size)
        b.pollRumbleOnce(1017)
        assertEquals(2, out.calls.size)

        // Rumble sostenido: se renueva cada segundo.
        b.pollRumbleOnce(1500)
        assertEquals(2, out.calls.size)
        b.pollRumbleOnce(2017)
        assertEquals(3, out.calls.size)

        // Parar es inmediato.
        mem.rumble[0] = 0
        b.pollRumbleOnce(2018)
        assertEquals(Triple(0, listOf(7), 0), out.calls.last())

        // Slot 0 sin mando físico (táctil): deviceIds vacío (el output decide el fallback).
        b.deviceDisconnected(7)
        mem.rumble[0] = 0x00010001
        b.pollRumbleOnce(3000)
        assertEquals(Triple(0, emptyList<Int>(), 0x00010001), out.calls.last())
    }

    @Test
    fun pollRumble_suspendedOrKeyboardModeStops() {
        val mem = FakeMemory()
        val out = FakeRumble()
        val b = bridge(mem, out)
        b.deviceConnected("padA", 7, eagerSlot = true, hasVibrator = true)
        mem.rumble[0] = 0x7FFF7FFF
        b.pollRumbleOnce(100)
        b.suspendOutput()
        b.pollRumbleOnce(101)
        assertEquals(0, out.calls.last().third)
        b.resumeOutput()
        b.pollRumbleOnce(200)
        assertEquals(0x7FFF7FFF, out.calls.last().third)
        b.enabled = false
        b.pollRumbleOnce(201)
        assertEquals(0, out.calls.last().third)
        b.close()
        assertTrue(out.stopped)
        assertFalse(mem.opened)
        val before = mem.heartbeats
        b.pollRumbleOnce(300)  // cerrado: nada
        assertEquals(before, mem.heartbeats)
    }
}
