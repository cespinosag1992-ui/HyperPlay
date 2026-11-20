package com.murdoc820.hyperplay.input

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.Canvas
import android.graphics.Color
import android.graphics.Paint
import android.graphics.Path
import android.graphics.RectF
import android.view.KeyEvent
import android.view.MotionEvent
import android.view.View

/**
 * Mando virtual minimalista ([VirtualTouchLayout]): todo pegado a los bordes y a la mitad inferior
 * para no tapar el juego. Estilo "cristal oscuro": relleno negro translúcido (~25 % en reposo,
 * ~65 % al pulsar) con borde fino gris claro. Cruceta de 4 pétalos con hueco central; sticks
 * flotantes casi invisibles en reposo que solo se marcan mientras el dedo los arrastra
 * ([VirtualTouchInput]). Todo va al mismo [ControllerManager] que los mandos físicos
 * (`fromTouch = true`), así que llega al juego como XInput.
 */
class VirtualTouchOverlayView(context: Context, private val controller: ControllerManager) : View(context) {
    private val density = resources.displayMetrics.density
    private val input = VirtualTouchInput(
        layout = VirtualTouchLayout(1f, 1f, density),
        onButton = { key, pressed -> controller.handleButton(key, pressed, fromTouch = true) },
        onSticks = { lx, ly, rx, ry -> controller.handleAxes(lx, ly, rx, ry, 0f, 0f, 0f, 0f, fromTouch = true) },
    )

    private val fill = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.FILL }
    private val stroke = Paint(Paint.ANTI_ALIAS_FLAG).apply { style = Paint.Style.STROKE }
    private val glyph = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        style = Paint.Style.STROKE
        strokeCap = Paint.Cap.ROUND
        strokeJoin = Paint.Join.ROUND
    }
    private val text = Paint(Paint.ANTI_ALIAS_FLAG).apply {
        textAlign = Paint.Align.CENTER
        isFakeBoldText = true
    }
    private val rect = RectF()
    private val path = Path()

    init {
        isClickable = false
        isFocusable = false
    }

    override fun onSizeChanged(w: Int, h: Int, oldw: Int, oldh: Int) {
        input.cancelAll()
        input.layout = VirtualTouchLayout(w.toFloat(), h.toFloat(), density)
    }

    override fun onVisibilityChanged(changedView: View, visibility: Int) {
        super.onVisibilityChanged(changedView, visibility)
        if (visibility != VISIBLE) releaseAll()
    }

    /** Suelta todos los controles táctiles pulsados (p. ej. al abrir el Quick Menu). */
    fun releaseAll() {
        input.cancelAll()
        invalidate()
    }

    @SuppressLint("ClickableViewAccessibility")  // Mando de juego: no hay acción de "click" accesible.
    override fun onTouchEvent(event: MotionEvent): Boolean {
        var handled = false
        when (event.actionMasked) {
            MotionEvent.ACTION_DOWN, MotionEvent.ACTION_POINTER_DOWN -> {
                val i = event.actionIndex
                input.pointerDown(event.getPointerId(i), event.getX(i), event.getY(i))
                handled = true
            }
            MotionEvent.ACTION_MOVE -> {
                for (i in 0 until event.pointerCount) input.pointerMove(event.getPointerId(i), event.getX(i), event.getY(i))
                handled = true
            }
            MotionEvent.ACTION_POINTER_UP -> {
                input.pointerUp(event.getPointerId(event.actionIndex))
                handled = true
            }
            MotionEvent.ACTION_UP, MotionEvent.ACTION_CANCEL -> {
                input.cancelAll()
                handled = true
            }
        }
        if (handled) invalidate()
        return handled
    }

    override fun onDraw(canvas: Canvas) {
        val layout = input.layout
        val active = input.activeControls
        val h = layout.height
        stroke.strokeWidth = 1.3f * density

        // Gatillos y bumpers: rectángulos finos.
        text.textSize = h * 0.040f
        for ((control, b) in layout.shoulderButtons) {
            rect.set(b.left, b.top, b.right, b.bottom)
            val r = (b.bottom - b.top) * 0.45f
            glass(control in active)
            canvas.drawRoundRect(rect, r, r, fill)
            canvas.drawRoundRect(rect, r, r, stroke)
            label(canvas, control.label, b.cx, b.cy, control in active)
        }

        // L3 / R3 dedicados.
        text.textSize = h * 0.034f
        for ((control, c) in layout.thumbButtons) {
            glass(control in active)
            canvas.drawCircle(c.cx, c.cy, c.r, fill)
            canvas.drawCircle(c.cx, c.cy, c.r, stroke)
            label(canvas, control.label, c.cx, c.cy, control in active)
        }

        // View (⧉) y Menú (≡): pequeños, arriba.
        for ((control, c) in layout.centerButtons) {
            glass(control in active)
            canvas.drawCircle(c.cx, c.cy, c.r, fill)
            canvas.drawCircle(c.cx, c.cy, c.r, stroke)
            glyph.color = Color.argb(if (control in active) 240 else 170, 255, 255, 255)
            glyph.strokeWidth = c.r * 0.11f
            if (control == TouchControl.VIEW) drawViewIcon(canvas, c) else drawMenuIcon(canvas, c)
        }

        drawDpad(canvas, layout.dpad, TouchControl.DPAD in active, input.activeDpadKeys)

        // A/B/X/Y: cristal oscuro con la letra en el color Xbox (acento sutil).
        text.textSize = h * 0.058f
        for ((control, c) in layout.faceButtons) {
            val pressed = control in active
            val color = FACE_COLORS.getValue(control)
            fill.color = if (pressed) withAlpha(color, 150) else IDLE_FILL
            canvas.drawCircle(c.cx, c.cy, c.r, fill)
            stroke.color = if (pressed) PRESSED_STROKE else IDLE_STROKE
            canvas.drawCircle(c.cx, c.cy, c.r, stroke)
            text.color = if (pressed) Color.WHITE else withAlpha(color, 200)
            canvas.drawText(control.label, c.cx, c.cy - (text.ascent() + text.descent()) / 2f, text)
        }

        drawStick(canvas, TouchControl.LEFT_STICK, TouchControl.LEFT_STICK in active)
        drawStick(canvas, TouchControl.RIGHT_STICK, TouchControl.RIGHT_STICK in active)
    }

    /** Cruceta de 4 pétalos independientes con hueco central; el pétalo pulsado se ilumina. */
    private fun drawDpad(canvas: Canvas, d: Circle, pressed: Boolean, keys: Set<Int>) {
        val half = d.r * 0.30f      // medio ancho del pétalo
        val inner = d.r * 0.22f     // hueco central
        val outer = d.r * 0.98f
        petal(canvas, d.cx - half, d.cy - outer, d.cx + half, d.cy - inner, KeyEvent.KEYCODE_DPAD_UP in keys, pressed)
        petal(canvas, d.cx - half, d.cy + inner, d.cx + half, d.cy + outer, KeyEvent.KEYCODE_DPAD_DOWN in keys, pressed)
        petal(canvas, d.cx - outer, d.cy - half, d.cx - inner, d.cy + half, KeyEvent.KEYCODE_DPAD_LEFT in keys, pressed)
        petal(canvas, d.cx + inner, d.cy - half, d.cx + outer, d.cy + half, KeyEvent.KEYCODE_DPAD_RIGHT in keys, pressed)
        val a = half * 0.55f
        val at = (outer + inner) / 2f
        arrow(canvas, d.cx, d.cy - at, 0f, -1f, a, KeyEvent.KEYCODE_DPAD_UP in keys)
        arrow(canvas, d.cx, d.cy + at, 0f, 1f, a, KeyEvent.KEYCODE_DPAD_DOWN in keys)
        arrow(canvas, d.cx - at, d.cy, -1f, 0f, a, KeyEvent.KEYCODE_DPAD_LEFT in keys)
        arrow(canvas, d.cx + at, d.cy, 1f, 0f, a, KeyEvent.KEYCODE_DPAD_RIGHT in keys)
    }

    private fun petal(canvas: Canvas, l: Float, t: Float, r: Float, b: Float, lit: Boolean, dpadTouched: Boolean) {
        rect.set(l, t, r, b)
        val rr = (r - l).coerceAtMost(b - t) * 0.35f
        fill.color = if (lit) PRESSED_FILL else if (dpadTouched) TOUCHED_FILL else IDLE_FILL
        stroke.color = if (lit) PRESSED_STROKE else IDLE_STROKE
        canvas.drawRoundRect(rect, rr, rr, fill)
        canvas.drawRoundRect(rect, rr, rr, stroke)
    }

    /** Stick flotante: casi invisible en reposo; base y pomo visibles mientras se arrastra. */
    private fun drawStick(canvas: Canvas, stick: TouchControl, pressed: Boolean) {
        val base = input.stickBase(stick)
        val (sx, sy) = input.stickPosition(stick)
        fill.color = if (pressed) Color.argb(70, 0, 0, 0) else Color.argb(22, 0, 0, 0)
        canvas.drawCircle(base.cx, base.cy, base.r, fill)
        stroke.color = if (pressed) Color.argb(150, 255, 255, 255) else Color.argb(55, 255, 255, 255)
        canvas.drawCircle(base.cx, base.cy, base.r, stroke)
        val kr = base.r * 0.46f
        val kx = base.cx + sx * (base.r - kr * 0.5f)
        val ky = base.cy + sy * (base.r - kr * 0.5f)
        fill.color = if (pressed) Color.argb(165, 0, 0, 0) else Color.argb(40, 0, 0, 0)
        canvas.drawCircle(kx, ky, kr, fill)
        stroke.color = if (pressed) Color.argb(200, 255, 255, 255) else Color.argb(70, 255, 255, 255)
        canvas.drawCircle(kx, ky, kr, stroke)
    }

    /** Cristal oscuro: ~25 % en reposo, ~65 % al pulsar; borde fino gris claro. */
    private fun glass(pressed: Boolean) {
        fill.color = if (pressed) PRESSED_FILL else IDLE_FILL
        stroke.color = if (pressed) PRESSED_STROKE else IDLE_STROKE
    }

    private fun label(canvas: Canvas, s: String, cx: Float, cy: Float, pressed: Boolean) {
        text.color = Color.argb(if (pressed) 245 else 165, 255, 255, 255)
        canvas.drawText(s, cx, cy - (text.ascent() + text.descent()) / 2f, text)
    }

    private fun arrow(canvas: Canvas, cx: Float, cy: Float, dx: Float, dy: Float, size: Float, lit: Boolean) {
        path.reset()
        val px = -dy
        val py = dx
        path.moveTo(cx + dx * size, cy + dy * size)
        path.lineTo(cx - dx * size * 0.6f + px * size * 0.8f, cy - dy * size * 0.6f + py * size * 0.8f)
        path.lineTo(cx - dx * size * 0.6f - px * size * 0.8f, cy - dy * size * 0.6f - py * size * 0.8f)
        path.close()
        fill.color = Color.argb(if (lit) 240 else 120, 255, 255, 255)
        canvas.drawPath(path, fill)
    }

    private fun drawViewIcon(canvas: Canvas, c: Circle) {
        val s = c.r * 0.34f
        val o = c.r * 0.13f
        rect.set(c.cx - s - o, c.cy - s - o, c.cx + s - o, c.cy + s - o)
        canvas.drawRoundRect(rect, s * 0.25f, s * 0.25f, glyph)
        rect.set(c.cx - s + o, c.cy - s + o, c.cx + s + o, c.cy + s + o)
        canvas.drawRoundRect(rect, s * 0.25f, s * 0.25f, glyph)
    }

    private fun drawMenuIcon(canvas: Canvas, c: Circle) {
        val w = c.r * 0.44f
        val gap = c.r * 0.26f
        for (k in -1..1) canvas.drawLine(c.cx - w, c.cy + k * gap, c.cx + w, c.cy + k * gap, glyph)
    }

    private fun withAlpha(color: Int, a: Int) = Color.argb(a, Color.red(color), Color.green(color), Color.blue(color))

    private companion object {
        val IDLE_FILL = Color.argb(0x30, 0, 0, 0)          // ~19 % negro: casi transparente
        val TOUCHED_FILL = Color.argb(0x60, 0, 0, 0)
        val PRESSED_FILL = Color.argb(0xA6, 0, 0, 0)       // ~65 %
        val IDLE_STROKE = Color.argb(0x66, 255, 255, 255)  // #66FFFFFF
        val PRESSED_STROKE = Color.argb(0xE0, 255, 255, 255)
        val FACE_COLORS = mapOf(
            TouchControl.Y to Color.rgb(0xF5, 0xC5, 0x18),
            TouchControl.X to Color.rgb(0x2D, 0x8C, 0xFF),
            TouchControl.B to Color.rgb(0xF0, 0x46, 0x4B),
            TouchControl.A to Color.rgb(0x5C, 0xD6, 0x4C),
        )
    }
}
