package com.murdoc820.hyperplay.input

import android.view.KeyEvent
import kotlin.math.atan2
import kotlin.math.hypot
import kotlin.math.max
import kotlin.math.min

/** Controles del mando virtual (distribución Xbox). [keyCode] es el keycode Android para [ControllerManager]. */
enum class TouchControl(val keyCode: Int, val label: String) {
    LEFT_STICK(KeyEvent.KEYCODE_BUTTON_THUMBL, ""),
    RIGHT_STICK(KeyEvent.KEYCODE_BUTTON_THUMBR, ""),
    DPAD(0, ""),
    A(KeyEvent.KEYCODE_BUTTON_A, "A"),
    B(KeyEvent.KEYCODE_BUTTON_B, "B"),
    X(KeyEvent.KEYCODE_BUTTON_X, "X"),
    Y(KeyEvent.KEYCODE_BUTTON_Y, "Y"),
    LB(KeyEvent.KEYCODE_BUTTON_L1, "LB"),
    RB(KeyEvent.KEYCODE_BUTTON_R1, "RB"),
    LT(KeyEvent.KEYCODE_BUTTON_L2, "LT"),
    RT(KeyEvent.KEYCODE_BUTTON_R2, "RT"),
    L3(KeyEvent.KEYCODE_BUTTON_THUMBL, "L3"),
    R3(KeyEvent.KEYCODE_BUTTON_THUMBR, "R3"),
    VIEW(KeyEvent.KEYCODE_BUTTON_SELECT, ""),
    MENU(KeyEvent.KEYCODE_BUTTON_START, "");

    val isStick: Boolean get() = this == LEFT_STICK || this == RIGHT_STICK
    val isButton: Boolean get() = !isStick && this != DPAD
}

data class Circle(val cx: Float, val cy: Float, val r: Float) {
    fun contains(x: Float, y: Float, slop: Float = 1f): Boolean = hypot(x - cx, y - cy) <= r * slop
}

data class Box(val left: Float, val top: Float, val right: Float, val bottom: Float) {
    val cx: Float get() = (left + right) / 2f
    val cy: Float get() = (top + bottom) / 2f
    fun contains(x: Float, y: Float): Boolean = x in left..right && y in top..bottom
}

/**
 * Geometría del mando virtual (en píxeles) para una vista de [width]x[height]. Pura: sin APIs
 * gráficas de Android, para poder probarla en la JVM.
 *
 * Distribución ergonómica que despeja el centro y la franja superior (el juego se ve entero):
 * - izquierda: LT sobre LB pegados al borde superior-izquierdo, L3 debajo; stick izquierdo en la
 *   esquina inferior-izquierda y cruceta compacta (4 pétalos) a su derecha, abajo;
 * - derecha (simétrica): RT sobre RB y R3 debajo; rombo A/B/X/Y en la esquina inferior-derecha y
 *   stick derecho a su izquierda, abajo;
 * - View (⧉) y Menú (≡) pequeños pegados al borde superior; franja inferior central libre para
 *   el HUD de rendimiento.
 * Medidas en unidades de la altura y ancladas a los bordes: escala igual en 2688x1216 (20:9) y
 * 16:9, y el ancho extra de 20:9 queda para el juego. Los sticks son flotantes (ver
 * [VirtualTouchInput]): estas son sus posiciones de reposo.
 */
class VirtualTouchLayout(val width: Float, val height: Float, @Suppress("UNUSED_PARAMETER") density: Float = 1f) {
    private fun u(f: Float) = f * height             // unidades de altura
    private fun fromRight(f: Float) = width - u(f)
    private fun fromBottom(f: Float) = height - u(f)

    val leftStickRest = Circle(u(0.22f), fromBottom(0.25f), u(STICK_R))
    val rightStickRest = Circle(fromRight(0.72f), fromBottom(0.25f), u(STICK_R))
    val dpad = Circle(u(0.62f), fromBottom(0.245f), u(DPAD_R))

    // Rombo en la zona natural del pulgar derecho (media-baja, algo hacia dentro).
    private val diamondX = fromRight(0.29f)
    private val diamondY = fromBottom(0.31f)
    val faceButtons: Map<TouchControl, Circle> = mapOf(
        TouchControl.Y to Circle(diamondX, diamondY - u(FACE_GAP), u(FACE_R)),
        TouchControl.X to Circle(diamondX - u(FACE_GAP), diamondY, u(FACE_R)),
        TouchControl.B to Circle(diamondX + u(FACE_GAP), diamondY, u(FACE_R)),
        TouchControl.A to Circle(diamondX, diamondY + u(FACE_GAP), u(FACE_R)),
    )

    /** View (⧉) y Menú (≡): pequeños, pegados al borde superior. */
    val centerButtons: Map<TouchControl, Circle> = mapOf(
        TouchControl.VIEW to Circle(width / 2f - u(0.16f), u(0.07f), u(CENTER_R)),
        TouchControl.MENU to Circle(width / 2f + u(0.16f), u(0.07f), u(CENTER_R)),
    )

    /** L3 y R3 dedicados: círculos pequeños bajo LB y RB, pegados a los bordes. */
    val thumbButtons: Map<TouchControl, Circle> = mapOf(
        TouchControl.L3 to Circle(u(0.105f), u(0.37f), u(THUMB_R)),
        TouchControl.R3 to Circle(fromRight(0.105f), u(0.37f), u(THUMB_R)),
    )

    /** Gatillos sobre bumpers, rectángulos finos en las esquinas superiores. */
    val shoulderButtons: Map<TouchControl, Box> = mapOf(
        TouchControl.LT to Box(u(0.04f), u(0.04f), u(0.25f), u(0.135f)),
        TouchControl.LB to Box(u(0.04f), u(0.165f), u(0.25f), u(0.26f)),
        TouchControl.RT to Box(fromRight(0.25f), u(0.04f), fromRight(0.04f), u(0.135f)),
        TouchControl.RB to Box(fromRight(0.25f), u(0.165f), fromRight(0.04f), u(0.26f)),
    )

    /** Franja inferior central reservada al HUD de rendimiento (ningún control la invade). */
    // Medido en el teléfono: el HUD ocupa ~±0.57 alto alrededor del centro y ~0.07 alto abajo.
    val hudBand = Box(width / 2f - u(0.58f), fromBottom(0.085f), width / 2f + u(0.58f), height)

    /** Zona central que debe quedar libre para ver el juego. */
    val centerZone = Box(width * 0.35f, height * 0.16f, width * 0.65f, height * 0.60f)

    /** Radio de la base de un stick flotante. */
    val stickRadius: Float get() = u(STICK_R)

    /**
     * Botón fijo bajo (x, y), o null (zona libre: ahí nacen los sticks flotantes). Los círculos
     * tienen margen ([HIT_SLOP]) porque el dedo tapa el control.
     */
    fun hitTest(x: Float, y: Float): TouchControl? {
        shoulderButtons.entries.firstOrNull { it.value.contains(x, y) }?.let { return it.key }
        (faceButtons + centerButtons + thumbButtons).entries
            .filter { it.value.contains(x, y, HIT_SLOP) }
            .minByOrNull { hypot(x - it.value.cx, y - it.value.cy) }
            ?.let { return it.key }
        if (dpad.contains(x, y, HIT_SLOP)) return TouchControl.DPAD
        return null
    }

    /** Stick flotante que nace con un toque en zona libre: izquierdo en la mitad izquierda. */
    fun stickForFreeTouch(x: Float): TouchControl = if (x < width / 2f) TouchControl.LEFT_STICK else TouchControl.RIGHT_STICK

    /** Centro de la base de un stick que nace en (x, y): la base entera queda dentro de la pantalla. */
    fun floatingCenter(x: Float, y: Float): Pair<Float, Float> {
        val r = stickRadius
        return x.coerceIn(r, max(r, width - r)) to y.coerceIn(r, max(r, height - r))
    }

    /** Vector del stick en -1..1 (y hacia abajo positivo, como AXIS_Y), limitado al círculo. */
    fun stickVector(base: Circle, x: Float, y: Float): Pair<Float, Float> {
        val dx = (x - base.cx) / base.r
        val dy = (y - base.cy) / base.r
        val len = hypot(dx, dy)
        if (len < STICK_DEADZONE) return 0f to 0f
        val k = if (len > 1f) 1f / len else 1f
        return dx * k to dy * k
    }

    /** Direcciones de la cruceta (keycodes DPAD_*) para un toque: 8 sectores de 45°, zona muerta central. */
    fun dpadKeys(x: Float, y: Float): Set<Int> {
        val dx = x - dpad.cx
        val dy = y - dpad.cy
        if (hypot(dx, dy) < dpad.r * DPAD_DEADZONE) return emptySet()
        val deg = (Math.toDegrees(atan2(dy.toDouble(), dx.toDouble())) + 360.0) % 360.0
        val sector = (((deg + 22.5) % 360.0) / 45.0).toInt()
        return DPAD_SECTORS[sector]
    }

    /** Círculos fijos (para comprobar solapes) y caja que engloba todo. */
    fun allCircles(): List<Circle> =
        faceButtons.values + centerButtons.values + thumbButtons.values + listOf(dpad, leftStickRest, rightStickRest)

    fun bounds(): Box {
        val circles = allCircles()
        val boxes = shoulderButtons.values
        return Box(
            left = min(circles.minOf { it.cx - it.r }, boxes.minOf { it.left }),
            top = min(circles.minOf { it.cy - it.r }, boxes.minOf { it.top }),
            right = max(circles.maxOf { it.cx + it.r }, boxes.maxOf { it.right }),
            bottom = max(circles.maxOf { it.cy + it.r }, boxes.maxOf { it.bottom }),
        )
    }

    companion object {
        // Tamaños en fracción de la altura de la pantalla. En el RedMagic 10 Pro el alto mide
        // ~70 mm: 0.01 = ~0.7 mm. Guías de objetivos táctiles: ~9-10 mm con >= 2 mm de separación;
        // pulgar adulto ~25 mm.
        const val STICK_R = 0.12f
        const val DPAD_R = 0.145f     // ~20 mm de diámetro
        const val FACE_R = 0.075f     // ~10.5 mm de diámetro
        const val FACE_GAP = 0.135f   // centro del rombo -> botón; borde a borde en diagonal ~2.9 mm
        const val CENTER_R = 0.042f
        const val THUMB_R = 0.052f

        const val HIT_SLOP = 1.25f
        const val STICK_DEADZONE = 0.10f
        const val DPAD_DEADZONE = 0.22f

        private val UP = KeyEvent.KEYCODE_DPAD_UP
        private val DOWN = KeyEvent.KEYCODE_DPAD_DOWN
        private val LEFT = KeyEvent.KEYCODE_DPAD_LEFT
        private val RIGHT = KeyEvent.KEYCODE_DPAD_RIGHT

        // Sector 0 = derecha, en sentido horario de 45 en 45 grados.
        private val DPAD_SECTORS: List<Set<Int>> = listOf(
            setOf(RIGHT), setOf(RIGHT, DOWN), setOf(DOWN), setOf(DOWN, LEFT),
            setOf(LEFT), setOf(LEFT, UP), setOf(UP), setOf(UP, RIGHT),
        )
    }
}

/**
 * Seguimiento multitáctil del mando virtual. Cada dedo se asigna al control donde empezó:
 * - botón fijo: se pulsa; el dedo puede deslizarse a otro botón (como en un mando real);
 * - cruceta: sigue al dedo (8 direcciones);
 * - zona libre: nace un **stick flotante** con la base bajo el dedo (mitad izquierda = stick
 *   izquierdo, derecha = stick derecho). Al soltar vuelve a su posición de reposo. Doble toque
 *   y mantener sobre la zona del stick = L3/R3.
 * Pura: emite por [onButton] (keycode, pulsado) y [onSticks] (lx, ly, rx, ry en -1..1).
 */
class VirtualTouchInput(
    var layout: VirtualTouchLayout,
    private val onButton: (keyCode: Int, pressed: Boolean) -> Unit,
    private val onSticks: (lx: Float, ly: Float, rx: Float, ry: Float) -> Unit,
    private val clock: () -> Long = { System.nanoTime() / 1_000_000L },
) {
    private val pointerControl = HashMap<Int, TouchControl>()
    private val dpadPointerKeys = HashMap<Int, Set<Int>>()
    private val stickPointer = HashMap<TouchControl, Int>()
    private val floatingBase = HashMap<TouchControl, Circle>()
    private val stickValue = HashMap<TouchControl, Pair<Float, Float>>()
    private val lastStickRelease = HashMap<TouchControl, Long>()
    private val thumbHeld = HashSet<TouchControl>()

    /** Controles pulsados ahora mismo (para dibujarlos resaltados). */
    val activeControls: Set<TouchControl> get() = pointerControl.values.toSet()

    /** Teclas de la cruceta pulsadas ahora mismo. */
    val activeDpadKeys: Set<Int> get() = dpadPointerKeys.values.flatten().toSet()

    /** Base actual de un stick: la flotante si hay dedo, la de reposo si no. */
    fun stickBase(stick: TouchControl): Circle =
        floatingBase[stick] ?: if (stick == TouchControl.LEFT_STICK) layout.leftStickRest else layout.rightStickRest

    fun stickPosition(stick: TouchControl): Pair<Float, Float> = stickValue[stick] ?: (0f to 0f)

    /** @return true si el toque se asignó a un control (siempre, salvo un stick ya ocupado). */
    fun pointerDown(id: Int, x: Float, y: Float): Boolean {
        val control = layout.hitTest(x, y) ?: layout.stickForFreeTouch(x)
        if (control.isStick) {
            if (stickPointer.containsKey(control)) return false
            stickPointer[control] = id
            pointerControl[id] = control
            val (cx, cy) = layout.floatingCenter(x, y)
            floatingBase[control] = Circle(cx, cy, layout.stickRadius)
            // Doble toque rápido sobre el stick = clic del stick (L3/R3) mientras se mantiene.
            val last = lastStickRelease[control]
            if (last != null && clock() - last <= DOUBLE_TAP_MS) {
                thumbHeld += control
                onButton(control.keyCode, true)
            }
            updateStick(control, x, y)
            return true
        }
        pointerControl[id] = control
        if (control == TouchControl.DPAD) updateDpad(id, layout.dpadKeys(x, y)) else pressIfFirst(control)
        return true
    }

    fun pointerMove(id: Int, x: Float, y: Float) {
        val control = pointerControl[id] ?: return
        when {
            control.isStick -> updateStick(control, x, y)
            control == TouchControl.DPAD -> updateDpad(id, layout.dpadKeys(x, y))
            else -> {
                val now = layout.hitTest(x, y)?.takeIf { it.isButton } ?: return
                if (now == control) return
                pointerControl[id] = now
                releaseIfLast(control)
                pressIfFirst(now)
            }
        }
    }

    fun pointerUp(id: Int) {
        val control = pointerControl.remove(id) ?: return
        when {
            control.isStick -> {
                stickPointer.remove(control)
                floatingBase.remove(control)
                stickValue.remove(control)
                lastStickRelease[control] = clock()
                if (thumbHeld.remove(control)) onButton(control.keyCode, false)
                emitSticks()
            }
            control == TouchControl.DPAD -> updateDpad(id, emptySet())
            else -> releaseIfLast(control)
        }
        dpadPointerKeys.remove(id)
    }

    /** Suelta todo (ACTION_CANCEL, ocultar el overlay, abrir el Quick Menu). */
    fun cancelAll() {
        for (id in pointerControl.keys.toList()) pointerUp(id)
        lastStickRelease.clear()
    }

    private fun updateStick(stick: TouchControl, x: Float, y: Float) {
        val v = layout.stickVector(stickBase(stick), x, y)
        if (stickValue[stick] == v) return
        stickValue[stick] = v
        emitSticks()
    }

    private fun emitSticks() {
        val l = stickPosition(TouchControl.LEFT_STICK)
        val r = stickPosition(TouchControl.RIGHT_STICK)
        onSticks(l.first, l.second, r.first, r.second)
    }

    private fun updateDpad(id: Int, keys: Set<Int>) {
        val before = activeDpadKeys
        if (keys.isEmpty()) dpadPointerKeys.remove(id) else dpadPointerKeys[id] = keys
        val after = activeDpadKeys
        for (k in before - after) onButton(k, false)
        for (k in after - before) onButton(k, true)
    }

    // Dos dedos sobre el mismo botón: se pulsa con el primero y se suelta con el último.
    private fun pressIfFirst(control: TouchControl) {
        if (pointerControl.values.count { it == control } == 1) onButton(control.keyCode, true)
    }

    private fun releaseIfLast(control: TouchControl) {
        if (pointerControl.values.none { it == control }) onButton(control.keyCode, false)
    }

    companion object {
        const val DOUBLE_TAP_MS = 280L
    }
}
