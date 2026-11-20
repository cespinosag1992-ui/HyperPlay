package com.murdoc820.hyperplay.input

import android.view.KeyEvent
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_HAT0X
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_HAT0Y
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_RX
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_RY
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_RZ
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_X
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_Y
import com.murdoc820.hyperplay.input.ControllerManager.Companion.ABS_Z
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test

class ControllerManagerTest {

    private sealed class Ev {
        data class Btn(val code: Int, val pressed: Boolean) : Ev()
        data class Abs(val code: Int, val value: Int) : Ev()
        object Syn : Ev()
    }

    private class FakeSink : InputSink {
        val events = mutableListOf<Ev>()
        override fun button(code: Int, pressed: Boolean) { events += Ev.Btn(code, pressed) }
        override fun axis(code: Int, value: Int) { events += Ev.Abs(code, value) }
        override fun sync() { events += Ev.Syn }
        fun axes() = events.filterIsInstance<Ev.Abs>()
    }

    private class FakeListener : ControllerManager.Listener {
        var quickMenu = 0
        val modes = mutableListOf<InputMode>()
        override fun onQuickMenuRequested() { quickMenu++ }
        override fun onInputModeChanged(mode: InputMode) { modes += mode }
        override fun onControllersChanged(names: List<String>) = Unit
    }

    private lateinit var sink: FakeSink
    private lateinit var listener: FakeListener
    private lateinit var cm: ControllerManager

    @Before
    fun setUp() {
        sink = FakeSink()
        listener = FakeListener()
        cm = ControllerManager(null, sink, listener)
    }

    private fun press(key: Int) = assertTrue(cm.handleButton(key, true))
    private fun release(key: Int) = assertTrue(cm.handleButton(key, false))
    private fun axes(
        lx: Float = 0f, ly: Float = 0f, rx: Float = 0f, ry: Float = 0f,
        lt: Float = 0f, rt: Float = 0f, hx: Float = 0f, hy: Float = 0f,
    ) = cm.handleAxes(lx, ly, rx, ry, lt, rt, hx, hy)

    @Test
    fun buttonMapping() {
        val expected = mapOf(
            KeyEvent.KEYCODE_BUTTON_A to 0x130,
            KeyEvent.KEYCODE_BUTTON_B to 0x131,
            KeyEvent.KEYCODE_BUTTON_X to 0x134,
            KeyEvent.KEYCODE_BUTTON_Y to 0x133,
            KeyEvent.KEYCODE_BUTTON_L1 to 0x136,
            KeyEvent.KEYCODE_BUTTON_R1 to 0x137,
            KeyEvent.KEYCODE_BUTTON_SELECT to 0x13a,
            KeyEvent.KEYCODE_BUTTON_START to 0x13b,
            KeyEvent.KEYCODE_BUTTON_MODE to 0x13c,
            KeyEvent.KEYCODE_BUTTON_THUMBL to 0x13d,
            KeyEvent.KEYCODE_BUTTON_THUMBR to 0x13e,
        )
        for ((key, code) in expected) {
            sink.events.clear()
            press(key)
            assertEquals(listOf(Ev.Btn(code, true), Ev.Syn), sink.events)
            sink.events.clear()
            release(key)
            assertEquals(listOf(Ev.Btn(code, false), Ev.Syn), sink.events)
        }
    }

    @Test
    fun unknownKeycodeReturnsFalse() {
        assertFalse(cm.handleButton(KeyEvent.KEYCODE_VOLUME_UP, true))
        assertFalse(cm.handleButton(KeyEvent.KEYCODE_A, true))
        assertTrue(sink.events.isEmpty())
        assertTrue(listener.modes.isEmpty())
    }

    @Test
    fun stickScalingAndClamp() {
        axes(lx = 1f, ly = -1f, rx = 2f, ry = -5f)
        assertEquals(
            listOf(Ev.Abs(ABS_X, 32767), Ev.Abs(ABS_Y, -32768), Ev.Abs(ABS_RX, 32767), Ev.Abs(ABS_RY, -32768), Ev.Syn),
            sink.events,
        )
        assertEquals(16384, ControllerManager.scaleStick(0.5f))
        assertEquals(-16384, ControllerManager.scaleStick(-0.5f))
        assertEquals(0, ControllerManager.scaleStick(Float.NaN))
    }

    @Test
    fun analogTriggers() {
        axes(lt = 1f, rt = 0.5f)
        assertEquals(listOf(Ev.Abs(ABS_Z, 255), Ev.Abs(ABS_RZ, 128)), sink.axes())
        assertEquals(255, ControllerManager.scaleTrigger(3f))
        assertEquals(0, ControllerManager.scaleTrigger(-1f))
    }

    @Test
    fun digitalTriggers() {
        press(KeyEvent.KEYCODE_BUTTON_L2)
        press(KeyEvent.KEYCODE_BUTTON_R2)
        release(KeyEvent.KEYCODE_BUTTON_L2)
        assertEquals(
            listOf(Ev.Abs(ABS_Z, 255), Ev.Syn, Ev.Abs(ABS_RZ, 255), Ev.Syn, Ev.Abs(ABS_Z, 0), Ev.Syn),
            sink.events,
        )
    }

    @Test
    fun onlyChangedAxesAreSent() {
        axes(lx = 0.5f, rt = 1f)
        sink.events.clear()
        axes(lx = 0.5f, rt = 1f)
        assertTrue(sink.events.isEmpty())
        axes(lx = 0.5f, ly = 0.25f, rt = 1f)
        assertEquals(listOf(Ev.Abs(ABS_Y, 8192), Ev.Syn), sink.events)
    }

    @Test
    fun dpadOverlappingPresses() {
        press(KeyEvent.KEYCODE_DPAD_LEFT)
        press(KeyEvent.KEYCODE_DPAD_RIGHT)   // ambas: se cancelan
        release(KeyEvent.KEYCODE_DPAD_LEFT)  // queda derecha
        press(KeyEvent.KEYCODE_DPAD_UP)
        release(KeyEvent.KEYCODE_DPAD_RIGHT)
        release(KeyEvent.KEYCODE_DPAD_UP)
        assertEquals(
            listOf(
                Ev.Abs(ABS_HAT0X, -1), Ev.Abs(ABS_HAT0X, 0), Ev.Abs(ABS_HAT0X, 1),
                Ev.Abs(ABS_HAT0Y, -1), Ev.Abs(ABS_HAT0X, 0), Ev.Abs(ABS_HAT0Y, 0),
            ),
            sink.axes(),
        )
    }

    @Test
    fun dpadKeysSurviveNeutralAnalogHat() {
        press(KeyEvent.KEYCODE_DPAD_DOWN)
        sink.events.clear()
        axes(hx = 0f, hy = 0f)  // un mando que reporta cruceta como teclas no debe soltarla vía ejes
        assertTrue(sink.events.isEmpty())
        axes(hx = -1f)
        assertEquals(listOf(Ev.Abs(ABS_HAT0X, -1), Ev.Syn), sink.events)
    }

    @Test
    fun l3r3FiresOnceAndRearms() {
        press(KeyEvent.KEYCODE_BUTTON_THUMBL)
        press(KeyEvent.KEYCODE_BUTTON_THUMBR)
        assertEquals(1, listener.quickMenu)
        press(KeyEvent.KEYCODE_BUTTON_THUMBR)  // repetición sin soltar
        assertEquals(1, listener.quickMenu)
        release(KeyEvent.KEYCODE_BUTTON_THUMBL)
        press(KeyEvent.KEYCODE_BUTTON_THUMBL)
        assertEquals(2, listener.quickMenu)
        // Los botones del combo siguen llegando al contenedor.
        assertTrue(Ev.Btn(0x13d, true) in sink.events && Ev.Btn(0x13e, true) in sink.events)
    }

    @Test
    fun selectStartFiresOnceAndRearms() {
        press(KeyEvent.KEYCODE_BUTTON_START)
        press(KeyEvent.KEYCODE_BUTTON_SELECT)
        assertEquals(1, listener.quickMenu)
        release(KeyEvent.KEYCODE_BUTTON_START)
        release(KeyEvent.KEYCODE_BUTTON_SELECT)
        press(KeyEvent.KEYCODE_BUTTON_SELECT)
        assertEquals(1, listener.quickMenu)
        press(KeyEvent.KEYCODE_BUTTON_START)
        assertEquals(2, listener.quickMenu)
    }

    @Test
    fun singleComboButtonDoesNotFire() {
        press(KeyEvent.KEYCODE_BUTTON_THUMBL)
        press(KeyEvent.KEYCODE_BUTTON_START)
        assertEquals(0, listener.quickMenu)
    }

    @Test
    fun modeSwitching() {
        assertEquals(InputMode.TOUCH, cm.mode)
        cm.onTouch()
        assertTrue(listener.modes.isEmpty())
        press(KeyEvent.KEYCODE_BUTTON_A)
        assertEquals(InputMode.GAMEPAD, cm.mode)
        press(KeyEvent.KEYCODE_BUTTON_B)
        assertEquals(listOf(InputMode.GAMEPAD), listener.modes)
        cm.onTouch()
        assertEquals(listOf(InputMode.GAMEPAD, InputMode.TOUCH), listener.modes)
    }

    @Test
    fun stickNoiseDoesNotLeaveTouchModeButRealMovementDoes() {
        axes(lx = 0.05f, ry = -0.1f)
        assertEquals(InputMode.TOUCH, cm.mode)
        axes(lx = 0.9f)
        assertEquals(InputMode.GAMEPAD, cm.mode)
    }

    @Test
    fun releaseAllResetsState() {
        press(KeyEvent.KEYCODE_BUTTON_A)
        press(KeyEvent.KEYCODE_DPAD_UP)
        axes(lx = 1f, lt = 1f)
        sink.events.clear()
        cm.releaseAll()
        assertEquals(
            listOf(Ev.Btn(0x130, false), Ev.Abs(ABS_X, 0), Ev.Abs(ABS_Z, 0), Ev.Abs(ABS_HAT0Y, 0), Ev.Syn),
            sink.events,
        )
    }
}
