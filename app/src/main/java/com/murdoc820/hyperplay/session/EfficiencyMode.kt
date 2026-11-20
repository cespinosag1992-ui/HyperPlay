package com.murdoc820.hyperplay.session

import androidx.annotation.StringRes
import com.murdoc820.hyperplay.R

/**
 * Modo eficiente y protección térmica (Quick Menu). Lógica pura, probada en la JVM; la actividad
 * solo lee sensores y aplica las decisiones al motor (tope de FPS en caliente y anclaje Prime).
 */

/** Tope de FPS del limitador preciso del motor. [fps] = 0 → sin límite. */
enum class FpsCapOption(val key: String, @StringRes val label: Int, val fps: Int) {
    FPS_60("60", R.string.fps_cap_60, 60),
    FPS_72("72", R.string.fps_cap_72, 72),
    FPS_120("120", R.string.fps_cap_120, 120),
    UNLIMITED("off", R.string.fps_cap_unlimited, 0);

    /** Refresco que se pide al panel para que cada frame dure un número entero de vsyncs. */
    val panelHz: Int get() = if (this == FPS_72) 144 else 120

    companion object {
        val DEFAULT = FPS_60
        const val PREFS_KEY = "fps_cap"

        fun fromKey(key: String?): FpsCapOption = entries.firstOrNull { it.key == key } ?: DEFAULT

        fun cycle(current: FpsCapOption, step: Int = 1): FpsCapOption = entries[(current.ordinal + step).mod(entries.size)]
    }
}

/** Escala de render (se aplica al iniciar la sesión). 100 % = nativa (por defecto). */
enum class RenderScaleOption(val key: String, @StringRes val label: Int, val percent: Int) {
    NATIVE("100", R.string.render_native, 100),
    P85("85", R.string.render_85, 85),
    P75("75", R.string.render_75, 75);

    companion object {
        val DEFAULT = NATIVE
        const val PREFS_KEY = "render_scale"

        fun fromKey(key: String?): RenderScaleOption = entries.firstOrNull { it.key == key } ?: DEFAULT

        fun cycle(current: RenderScaleOption, step: Int = 1): RenderScaleOption =
            entries[(current.ordinal + step).mod(entries.size)]

        /** Dimensión escalada (como la aplica el motor: entero truncado). */
        fun scaled(dim: Int, percent: Int): Int = dim * percent / 100
    }
}

/** Reescalado del pase de presentación. */
enum class UpscalerMode(val key: String, @StringRes val label: Int, val nativeId: Int) {
    AUTO("auto", R.string.upscaler_auto, 0),
    SGSR("sgsr", R.string.upscaler_sgsr, 1),
    OFF("off", R.string.upscaler_off, 2);

    companion object {
        val DEFAULT = AUTO
        const val PREFS_KEY = "upscaler"

        fun fromKey(key: String?): UpscalerMode = entries.firstOrNull { it.key == key } ?: DEFAULT

        fun cycle(current: UpscalerMode, step: Int = 1): UpscalerMode = entries[(current.ordinal + step).mod(entries.size)]

        /** ¿SGSR en este frame? Auto: solo si el juego renderiza por debajo del swapchain. */
        fun useSgsr(mode: UpscalerMode, srcW: Int, srcH: Int, dstW: Int, dstH: Int): Boolean = when (mode) {
            SGSR -> true
            OFF -> false
            AUTO -> srcW in 1 until dstW || srcH in 1 until dstH
        }
    }
}

/**
 * HyperFG (generación de fotogramas 2x). Espejo en Kotlin de las reglas del motor
 * (gfx/HyperFrameGen): se prueba en la JVM y decide el tope del juego.
 */
object FrameGenLogic {
    const val PREFS_KEY = "frame_gen"
    const val DEFAULT_ENABLED = false
    /** Escudo 4: un frame real de más de 22 ms no lleva sintético (no encadena tirones). */
    const val SLOW_FRAME_MS = 22f

    /** Con HyperFG el panel va siempre a 120 Hz: el tope del usuario no se usa (queda bloqueado). */
    const val PANEL_HZ = 120
    /** Juego con HyperFG: 60 reales + 60 sintéticos = 120 en pantalla. */
    const val GAME_FPS = PANEL_HZ / 2
    /** Con la protección térmica activa: 30 reales + 30 sintéticos = 60 en pantalla. */
    const val THROTTLED_GAME_FPS = 30

    /**
     * Tope que se pide al motor: sin HyperFG, el de la decisión térmica (tope del usuario o el de
     * protección); con HyperFG, [GAME_FPS] o, con la protección activa, [THROTTLED_GAME_FPS]
     * (HyperFG sigue, limitado a 60 en pantalla hasta que la batería se enfríe).
     */
    fun gameFps(decision: ThermalDecision, frameGen: Boolean): Int = when {
        !frameGen -> decision.effectiveFps
        decision.throttled -> THROTTLED_GAME_FPS
        else -> GAME_FPS
    }

    /** Refresco del panel: con HyperFG 120 Hz; si no, el que pide el tope en vigor. */
    fun panelHz(decision: ThermalDecision, frameGen: Boolean): Int =
        if (frameGen) PANEL_HZ else FpsCapOption.entries.firstOrNull { it.fps == decision.effectiveFps }?.panelHz ?: PANEL_HZ

    /** ¿Se puede cambiar el tope de FPS? Solo con HyperFG apagado. */
    fun fpsCapSelectable(frameGen: Boolean): Boolean = !frameGen

    /** Frame "lento" (sin sintético): más de 22 ms a 60 FPS; a menos FPS, más de 1,32 periodos (44 ms a 30). */
    fun slowFrameMs(gameFps: Int): Float = if (gameFps > 0) maxOf(SLOW_FRAME_MS, 1320f / gameFps) else SLOW_FRAME_MS

    /** FPS que verá el panel (y el HUD) con HyperFG activo y generando. */
    fun presentedFps(gameFps: Int, frameGen: Boolean): Int = if (frameGen && gameFps > 0) gameFps * 2 else gameFps

    /**
     * ¿Se genera un sintético en este ciclo? Solo con HyperFG activo, con un frame anterior en el
     * historial, con tope de juego y si el frame real no fue lento. (El corte de escena lo decide
     * la GPU dentro de la interpolación: el sintético sale igual al frame real.)
     */
    fun shouldGenerate(enabled: Boolean, hasHistory: Boolean, gameFps: Int, frameIntervalMs: Float): Boolean =
        enabled && hasHistory && gameFps > 0 && frameIntervalMs <= slowFrameMs(gameFps)
}

/** Decisión del gobernador para el motor y el HUD. */
data class ThermalDecision(
    /** Tope que debe aplicar el motor (0 = sin límite). */
    val effectiveFps: Int,
    /** Relajar el anclaje del hilo principal a Prime (repartir calor). */
    val relaxPrime: Boolean,
    /** Protección activa (bajó el tope o relajó Prime). */
    val throttled: Boolean,
    /** Batería ≥ [ThermalGovernor.HOT_ON_C]: indicador ámbar en el HUD. */
    val hot: Boolean,
)

/**
 * Gobernador térmico por temperatura de batería (objetivo < 40 °C), con histéresis para no
 * oscilar:
 * - < [THROTTLE_ON_C]: respeta el perfil del usuario (si estaba activo, solo vuelve a normal en
 *   ≤ [THROTTLE_OFF_C]).
 * - ≥ [THROTTLE_ON_C]: si el tope está por encima de 60 (o sin límite) lo baja a 60, relaja
 *   Prime y (en el motor) apaga la repetición de frames, así el sistema vuelve a ahorrar. Con
 *   HyperFG, este sigue pero limitado a 60 en pantalla (30 reales, ver [FrameGenLogic.gameFps]).
 * - ≥ [HOT_ON_C] con la protección activa: tope de emergencia [HOT_FPS] (3 vsyncs exactos a 120 Hz,
 *   sin judder) hasta bajar de [HOT_OFF_C]; con 60 ya no bastaba (batería a 41 °C con HyperFG).
 *   Indicador en el HUD.
 * Sin lectura de temperatura (null) se mantiene el estado. Con la protección desactivada nunca
 * limita (el indicador sigue informando).
 */
class ThermalGovernor {
    var throttled = false
        private set
    var hot = false
        private set

    fun update(batteryC: Float?, enabled: Boolean, userCap: FpsCapOption): ThermalDecision {
        if (batteryC != null) {
            throttled = enabled && if (throttled) batteryC > THROTTLE_OFF_C else batteryC >= THROTTLE_ON_C
            hot = if (hot) batteryC > HOT_OFF_C else batteryC >= HOT_ON_C
        } else if (!enabled) {
            throttled = false
        }
        var fps = if (throttled && (userCap.fps == 0 || userCap.fps > THROTTLED_FPS)) THROTTLED_FPS else userCap.fps
        if (throttled && hot && (fps == 0 || fps > HOT_FPS)) fps = HOT_FPS
        return ThermalDecision(effectiveFps = fps, relaxPrime = throttled, throttled = throttled, hot = hot)
    }

    companion object {
        const val THROTTLE_ON_C = 39.5f
        const val THROTTLE_OFF_C = 37.5f
        const val HOT_ON_C = 41.5f
        const val HOT_OFF_C = 40.5f
        const val THROTTLED_FPS = 60
        /** Tope de emergencia con la batería ≥ [HOT_ON_C] (120 Hz / 3). */
        const val HOT_FPS = 40
        const val PREFS_KEY = "thermal_protection"
        const val DEFAULT_ENABLED = true
    }
}
