package com.murdoc820.hyperplay.input

import android.view.KeyEvent
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.hypot

class VirtualTouchLayoutTest {
    // RedMagic 10 Pro en horizontal: 2688x1216 px.
    private val layout = VirtualTouchLayout(2688f, 1216f, 3f)

    @Test
    fun controlsAreInsideTheScreenAndDoNotOverlap() {
        for (l in listOf(layout, VirtualTouchLayout(1920f, 1080f, 2.6f), VirtualTouchLayout(800f, 360f, 2f))) {
            val b = l.bounds()
            assertTrue(b.left >= 0f && b.top >= 0f && b.right <= l.width && b.bottom <= l.height)
            val circles = l.allCircles()
            for (i in circles.indices) for (j in i + 1 until circles.size) {
                val a = circles[i]
                val c = circles[j]
                assertTrue("solapan $a y $c en ${l.width}x${l.height}", hypot(a.cx - c.cx, a.cy - c.cy) >= a.r + c.r)
            }
            val boxes = l.shoulderButtons.values.toList()
            for (i in boxes.indices) for (j in i + 1 until boxes.size) {
                val a = boxes[i]
                val c = boxes[j]
                assertFalse("solapan $a y $c", a.left < c.right && c.left < a.right && a.top < c.bottom && c.top < a.bottom)
            }
            for (b in boxes) for (c in circles) assertFalse("solapan $b y $c", circleHitsBox(c, b))
            // Centro libre para ver el juego y franja inferior libre para el HUD.
            for (c in circles) {
                assertFalse("$c invade el centro en ${l.width}x${l.height}", circleHitsBox(c, l.centerZone))
                assertFalse("$c invade el HUD en ${l.width}x${l.height}", circleHitsBox(c, l.hudBand))
            }
            for (b in boxes) assertFalse(boxesOverlap(b, l.centerZone))
        }
    }

    private fun boxesOverlap(a: Box, c: Box) = a.left < c.right && c.left < a.right && a.top < c.bottom && c.top < a.bottom

    private fun circleHitsBox(c: Circle, b: Box): Boolean {
        val nx = c.cx.coerceIn(b.left, b.right)
        val ny = c.cy.coerceIn(b.top, b.bottom)
        return hypot(c.cx - nx, c.cy - ny) < c.r
    }

    @Test
    fun ergonomicDistribution() {
        for (l in listOf(layout, VirtualTouchLayout(1920f, 1080f, 2.6f))) {
            val w = l.width
            val h = l.height
            // Izquierda: stick en la esquina inferior-izquierda y cruceta a su derecha, abajo.
            assertTrue(l.leftStickRest.cx < l.dpad.cx && l.dpad.cx < w / 2)
            assertTrue(l.leftStickRest.cy > h / 2 && l.dpad.cy > h / 2)
            // Derecha: rombo en la esquina inferior-derecha y stick derecho a su izquierda, abajo.
            assertTrue(l.faceButtons.values.all { it.cx > l.rightStickRest.cx && it.cy > h / 3 })
            assertTrue(l.rightStickRest.cx > w / 2 && l.rightStickRest.cy > h / 2)
            val fb = l.faceButtons
            assertTrue(fb.getValue(TouchControl.Y).cy < fb.getValue(TouchControl.A).cy)
            assertTrue(fb.getValue(TouchControl.X).cx < fb.getValue(TouchControl.B).cx)
            // Gatillos sobre bumpers, pegados a los bordes superiores; L3/R3 justo debajo.
            val sb = l.shoulderButtons
            assertTrue(sb.getValue(TouchControl.LT).cy < sb.getValue(TouchControl.LB).cy)
            assertTrue(sb.getValue(TouchControl.RT).cy < sb.getValue(TouchControl.RB).cy)
            assertTrue(sb.getValue(TouchControl.LT).left < h * 0.06f && sb.getValue(TouchControl.RT).right > w - h * 0.06f)
            val l3 = l.thumbButtons.getValue(TouchControl.L3)
            val r3 = l.thumbButtons.getValue(TouchControl.R3)
            assertTrue(l3.cy - l3.r > sb.getValue(TouchControl.LB).bottom && l3.cx < w * 0.1f)
            assertTrue(r3.cy - r3.r > sb.getValue(TouchControl.RB).bottom && r3.cx > w * 0.9f)
            // View/Menú pequeños y pegados al borde superior.
            val view = l.centerButtons.getValue(TouchControl.VIEW)
            val menu = l.centerButtons.getValue(TouchControl.MENU)
            assertTrue(view.cx < w / 2 && menu.cx > w / 2)
            assertEquals(0.07f * h, view.cy, 1f)
            assertTrue(view.r < h * 0.05f && menu.r < h * 0.05f)
            // Cruceta más compacta que la original (0.19 * alto).
            assertTrue(l.dpad.r <= h * 0.19f * 0.80f)
            // Rombo ergonómico: botones de >= ~9.5 mm (alto ~70 mm) y >= ~2 mm entre bordes.
            val y = fb.getValue(TouchControl.Y)
            val x = fb.getValue(TouchControl.X)
            assertTrue(y.r * 2f >= h * 0.135f)
            assertTrue(hypot(y.cx - x.cx, y.cy - x.cy) - y.r - x.r >= h * 0.03f)
        }
        // Keycodes que llegan a XInput.
        assertEquals(KeyEvent.KEYCODE_BUTTON_SELECT, TouchControl.VIEW.keyCode)
        assertEquals(KeyEvent.KEYCODE_BUTTON_START, TouchControl.MENU.keyCode)
        assertEquals(KeyEvent.KEYCODE_BUTTON_L2, TouchControl.LT.keyCode)
        assertEquals(KeyEvent.KEYCODE_BUTTON_R2, TouchControl.RT.keyCode)
        assertEquals(KeyEvent.KEYCODE_BUTTON_THUMBL, TouchControl.LEFT_STICK.keyCode)
    }

    @Test
    fun hitTestFindsEachFixedControlAtItsCenter() {
        assertEquals(TouchControl.DPAD, layout.hitTest(layout.dpad.cx, layout.dpad.cy))
        for ((control, c) in layout.faceButtons + layout.centerButtons + layout.thumbButtons) {
            assertEquals(control, layout.hitTest(c.cx, c.cy))
        }
        for ((control, box) in layout.shoulderButtons) assertEquals(control, layout.hitTest(box.cx, box.cy))
        // Sobre el stick en reposo no hay botón fijo: es zona libre (nace el stick flotante).
        assertNull(layout.hitTest(layout.leftStickRest.cx, layout.leftStickRest.cy))
        assertEquals(TouchControl.LEFT_STICK, layout.stickForFreeTouch(layout.leftStickRest.cx))
        assertEquals(TouchControl.RIGHT_STICK, layout.stickForFreeTouch(layout.rightStickRest.cx))
    }

    @Test
    fun dpadEightWaySectors() {
        val d = layout.dpad
        val r = d.r * 0.8f
        val up = KeyEvent.KEYCODE_DPAD_UP
        val down = KeyEvent.KEYCODE_DPAD_DOWN
        val left = KeyEvent.KEYCODE_DPAD_LEFT
        val right = KeyEvent.KEYCODE_DPAD_RIGHT
        assertEquals(setOf(right), layout.dpadKeys(d.cx + r, d.cy))
        assertEquals(setOf(left), layout.dpadKeys(d.cx - r, d.cy))
        assertEquals(setOf(up), layout.dpadKeys(d.cx, d.cy - r))
        assertEquals(setOf(down), layout.dpadKeys(d.cx, d.cy + r))
        assertEquals(setOf(up, right), layout.dpadKeys(d.cx + r, d.cy - r))
        assertEquals(setOf(down, left), layout.dpadKeys(d.cx - r, d.cy + r))
        assertEquals(emptySet<Int>(), layout.dpadKeys(d.cx + 1f, d.cy + 1f))
    }

    // ---------------------------------------------------------------- VirtualTouchInput

    private class Recorder {
        val events = mutableListOf<String>()
        var left = 0f to 0f
        var right = 0f to 0f
        var now = 1_000L
        fun input(l: VirtualTouchLayout) = VirtualTouchInput(
            l,
            onButton = { k, p -> events += "$k:${if (p) "down" else "up"}" },
            onSticks = { lx, ly, rx, ry ->
                left = lx to ly
                right = rx to ry
            },
            clock = { now },
        )
    }

    @Test
    fun floatingStickBornUnderTheFinger() {
        val rec = Recorder()
        val input = rec.input(layout)
        // Toque libre lejos del reposo (abajo a la izquierda): la base nace bajo el dedo.
        val x = layout.width * 0.10f
        val y = layout.height * 0.62f
        assertTrue(input.pointerDown(0, x, y))
        val base = input.stickBase(TouchControl.LEFT_STICK)
        assertEquals(x, base.cx, 1f)
        assertEquals(y, base.cy, 1f)
        assertEquals(0f to 0f, rec.left)  // justo en el centro: sin desplazamiento
        input.pointerMove(0, x + base.r * 3f, y)  // fuera del círculo: sigue siendo el stick, al máximo
        assertEquals(1f, rec.left.first, 1e-4f)
        assertEquals(0f, rec.left.second, 1e-4f)
        // Un segundo dedo en la misma mitad no roba el stick.
        assertFalse(input.pointerDown(1, x, y))
        input.pointerUp(0)
        assertEquals(0f to 0f, rec.left)
        assertEquals(layout.leftStickRest, input.stickBase(TouchControl.LEFT_STICK))  // vuelve al reposo
        assertTrue(rec.events.isEmpty())
    }

    @Test
    fun floatingBaseStaysOnScreenNearEdges() {
        val rec = Recorder()
        val input = rec.input(layout)
        input.pointerDown(0, 2f, layout.height - 2f)
        val base = input.stickBase(TouchControl.LEFT_STICK)
        assertTrue(base.cx - base.r >= 0f && base.cy + base.r <= layout.height)
    }

    @Test
    fun bothSticksAtOnce() {
        val rec = Recorder()
        val input = rec.input(layout)
        val l = layout.leftStickRest
        val r = layout.rightStickRest
        input.pointerDown(0, l.cx, l.cy)
        input.pointerDown(1, r.cx, r.cy)
        input.pointerMove(0, l.cx, l.cy - l.r)   // izquierdo arriba
        input.pointerMove(1, r.cx - r.r, r.cy)   // derecho izquierda
        assertEquals(-1f, rec.left.second, 1e-4f)
        assertEquals(-1f, rec.right.first, 1e-4f)
        input.pointerUp(0)
        assertEquals(0f to 0f, rec.left)
        assertEquals(-1f, rec.right.first, 1e-4f)
    }

    @Test
    fun doubleTapStickClicksThumb() {
        val rec = Recorder()
        val input = rec.input(layout)
        val l = layout.leftStickRest
        input.pointerDown(0, l.cx, l.cy)
        input.pointerUp(0)
        rec.now += 150
        input.pointerDown(0, l.cx, l.cy)  // doble toque: L3 pulsado mientras se mantiene
        val k = KeyEvent.KEYCODE_BUTTON_THUMBL
        assertEquals(listOf("$k:down"), rec.events)
        input.pointerUp(0)
        assertEquals(listOf("$k:down", "$k:up"), rec.events)
        // Un toque tardío no es doble toque.
        rec.now += 1_000
        input.pointerDown(0, l.cx, l.cy)
        input.pointerUp(0)
        assertEquals(2, rec.events.size)
    }

    @Test
    fun buttonPressAndReleaseWithSlideBetweenButtons() {
        val rec = Recorder()
        val input = rec.input(layout)
        val a = layout.faceButtons.getValue(TouchControl.A)
        val b = layout.faceButtons.getValue(TouchControl.B)
        assertTrue(input.pointerDown(0, a.cx, a.cy))
        input.pointerMove(0, b.cx, b.cy)
        input.pointerUp(0)
        val ka = KeyEvent.KEYCODE_BUTTON_A
        val kb = KeyEvent.KEYCODE_BUTTON_B
        assertEquals(listOf("$ka:down", "$ka:up", "$kb:down", "$kb:up"), rec.events)
        assertTrue(input.activeControls.isEmpty())
    }

    @Test
    fun triggersAndMenuButtons() {
        val rec = Recorder()
        val input = rec.input(layout)
        val lt = layout.shoulderButtons.getValue(TouchControl.LT)
        val menu = layout.centerButtons.getValue(TouchControl.MENU)
        input.pointerDown(0, lt.cx, lt.cy)
        input.pointerDown(1, menu.cx, menu.cy)
        input.cancelAll()
        val l2 = KeyEvent.KEYCODE_BUTTON_L2
        val start = KeyEvent.KEYCODE_BUTTON_START
        assertEquals(setOf("$l2:down", "$start:down", "$l2:up", "$start:up"), rec.events.toSet())
    }

    @Test
    fun dedicatedThumbButtonsSendL3AndR3() {
        assertEquals(KeyEvent.KEYCODE_BUTTON_THUMBL, TouchControl.L3.keyCode)
        assertEquals(KeyEvent.KEYCODE_BUTTON_THUMBR, TouchControl.R3.keyCode)
        val rec = Recorder()
        val input = rec.input(layout)
        val l3 = layout.thumbButtons.getValue(TouchControl.L3)
        val r3 = layout.thumbButtons.getValue(TouchControl.R3)
        assertTrue(input.pointerDown(0, l3.cx, l3.cy))
        assertTrue(input.pointerDown(1, r3.cx, r3.cy))
        assertEquals(setOf(TouchControl.L3, TouchControl.R3), input.activeControls)
        input.pointerUp(0)
        input.pointerUp(1)
        val kl = KeyEvent.KEYCODE_BUTTON_THUMBL
        val kr = KeyEvent.KEYCODE_BUTTON_THUMBR
        assertEquals(listOf("$kl:down", "$kr:down", "$kl:up", "$kr:up"), rec.events)
        // Pulsar L3 no crea un stick flotante.
        assertEquals(layout.leftStickRest, input.stickBase(TouchControl.LEFT_STICK))
    }

    @Test
    fun floatingSticksWorkOn16by9() {
        val l = VirtualTouchLayout(1920f, 1080f, 2.6f)
        val rec = Recorder()
        val input = rec.input(l)
        // Toques en zonas libres de cada mitad (centro de la pantalla, fuera de los botones).
        val lx = l.width * 0.30f
        val rx = l.width * 0.70f
        val y = l.height * 0.45f
        assertNull(l.hitTest(lx, y))
        assertNull(l.hitTest(rx, y))
        input.pointerDown(0, lx, y)
        input.pointerDown(1, rx, y)
        assertEquals(lx, input.stickBase(TouchControl.LEFT_STICK).cx, 1f)
        assertEquals(rx, input.stickBase(TouchControl.RIGHT_STICK).cx, 1f)
        input.pointerMove(0, lx + l.stickRadius, y)
        input.pointerMove(1, rx, y + l.stickRadius)
        assertEquals(1f, rec.left.first, 1e-4f)
        assertEquals(1f, rec.right.second, 1e-4f)
        input.cancelAll()
        assertEquals(l.leftStickRest, input.stickBase(TouchControl.LEFT_STICK))
        assertEquals(l.rightStickRest, input.stickBase(TouchControl.RIGHT_STICK))
    }

    @Test
    fun dpadSlideChangesDirectionsWithoutDuplicates() {
        val rec = Recorder()
        val input = rec.input(layout)
        val d = layout.dpad
        val r = d.r * 0.8f
        input.pointerDown(0, d.cx + r, d.cy)          // derecha
        input.pointerMove(0, d.cx + r, d.cy - r)      // derecha+arriba
        input.pointerMove(0, d.cx, d.cy - r)          // arriba
        input.pointerUp(0)
        val up = KeyEvent.KEYCODE_DPAD_UP
        val right = KeyEvent.KEYCODE_DPAD_RIGHT
        assertEquals(listOf("$right:down", "$up:down", "$right:up", "$up:up"), rec.events)
    }

    @Test
    fun touchInputKeepsControllerInTouchMode() {
        val modes = mutableListOf<InputMode>()
        val sink = object : InputSink {
            val buttons = mutableListOf<Pair<Int, Boolean>>()
            override fun button(code: Int, pressed: Boolean) { buttons += code to pressed }
            override fun axis(code: Int, value: Int) = Unit
            override fun sync() = Unit
        }
        val cm = ControllerManager(null, sink, object : ControllerManager.Listener {
            override fun onQuickMenuRequested() = Unit
            override fun onInputModeChanged(mode: InputMode) { modes += mode }
            override fun onControllersChanged(names: List<String>) = Unit
        })
        assertTrue(cm.handleButton(KeyEvent.KEYCODE_BUTTON_A, true, fromTouch = true))
        cm.handleAxes(1f, 0f, 0f, 0f, 0f, 0f, 0f, 0f, fromTouch = true)
        assertEquals(InputMode.TOUCH, cm.mode)
        assertTrue(modes.isEmpty())
        assertEquals(listOf(ControllerManager.BTN_SOUTH to true), sink.buttons)
        cm.handleButton(KeyEvent.KEYCODE_BUTTON_B, true)
        assertEquals(InputMode.GAMEPAD, cm.mode)
    }
}
