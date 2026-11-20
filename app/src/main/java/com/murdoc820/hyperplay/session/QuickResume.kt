package com.murdoc820.hyperplay.session

import com.murdoc820.hyperplay.engine.GameLauncher
import java.io.File

/** Qué hacer al abrir una sesión de juego según lo que haya suspendido en memoria. */
enum class QuickResumeAction {
    /** Nada suspendido (o ya terminó): arranque normal. */
    LAUNCH,
    /** El mismo juego sigue en memoria: se reanuda donde estaba, sin cargar nada. */
    RESUME,
    /** Hay otro juego suspendido: se cierra antes de lanzar el nuevo (el motor es único). */
    KILL_AND_LAUNCH,
}

/** Lógica pura de Quick Resume (probada en JVM). */
object QuickResumeLogic {
    fun action(heldExe: String?, heldRunning: Boolean, requestedExe: String): QuickResumeAction = when {
        heldExe == null || !heldRunning -> QuickResumeAction.LAUNCH
        sameExe(heldExe, requestedExe) -> QuickResumeAction.RESUME
        else -> QuickResumeAction.KILL_AND_LAUNCH
    }

    /** Las rutas llegan como /sdcard/... o /storage/emulated/0/...: se comparan canónicas. */
    fun sameExe(a: String, b: String): Boolean = canonical(a) == canonical(b)

    fun canonical(path: String): String {
        val p = if (path.startsWith("/sdcard/")) "/storage/emulated/0/" + path.removePrefix("/sdcard/") else path
        val f = File(p)
        return if (f.exists()) runCatching { f.canonicalPath }.getOrDefault(p) else p
    }
}

/**
 * Quick Resume en memoria (como una consola): al salir de la sesión el juego no se cierra, se
 * congela en su siguiente Present (cero CPU y GPU, audio parado) y el proceso sigue vivo gracias a
 * [com.murdoc820.hyperplay.QuickResumeService]. Al volver a abrir el mismo `.exe` se reanuda al
 * instante, sin volver a cargar el PE ni el JIT ni los assets.
 */
object QuickResume {
    class Held(val exePath: String, val launcher: GameLauncher, val logFile: File?, val startedAt: Long)

    @Volatile private var held: Held? = null

    val heldExe: String? get() = held?.exePath

    fun isHolding(): Boolean = held?.launcher?.isRunning() == true

    fun hold(entry: Held) {
        held = entry
    }

    /** Devuelve el juego suspendido si es [exePath] y sigue vivo (y deja de guardarlo). */
    fun take(exePath: String): Held? {
        val h = held ?: return null
        if (!h.launcher.isRunning() || !QuickResumeLogic.sameExe(h.exePath, exePath)) return null
        held = null
        return h
    }

    /** Cierra el juego suspendido (bloqueante: llamar fuera del hilo de UI). */
    fun killHeld() {
        val h = held ?: return
        held = null
        h.launcher.forceKill()
    }
}
