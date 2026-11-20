package com.murdoc820.hyperplay.session

import android.app.ActivityManager
import android.content.Context
import android.os.Process
import android.os.SystemClock
import android.util.Log
import java.io.File

/**
 * Cada juego corre en su propio proceso (`:game`, ver AndroidManifest): un juego de Windows
 * traducido tiene decenas de hilos que no se pueden parar uno a uno, y si el siguiente juego se
 * carga en el mismo proceso, esos hilos ejecutan código ya liberado (SIGSEGV al relanzar tras
 * Forzar cierre). Matar el proceso es la única parada completa; el launcher vive en el proceso
 * principal y no se entera.
 *
 * Quick Resume sigue funcionando: el juego suspendido vive en `:game` y el archivo
 * [HELD_FILE] le dice al launcher (otro proceso) qué juego hay en memoria.
 */
object GameProcess {
    private const val TAG = "HyperPlayProc"
    const val SUFFIX = ":game"
    private const val HELD_FILE = "quick_resume_held"

    /** Último valor leído por el launcher ([refresh]); null = ningún juego en memoria. */
    @Volatile var heldExe: String? = null
        private set

    fun isGameProcess(context: Context): Boolean = currentProcessName(context).endsWith(SUFFIX)

    private fun currentProcessName(context: Context): String =
        if (android.os.Build.VERSION.SDK_INT >= 28) android.app.Application.getProcessName()
        else runningProcesses(context).firstOrNull { it.pid == Process.myPid() }?.processName.orEmpty()

    private fun runningProcesses(context: Context): List<ActivityManager.RunningAppProcessInfo> =
        context.getSystemService(ActivityManager::class.java)?.runningAppProcesses.orEmpty()

    /** PID del proceso del juego, o null si no está vivo. */
    fun gamePid(context: Context): Int? {
        val name = context.packageName + SUFFIX
        return runningProcesses(context).firstOrNull { it.processName == name }?.pid
    }

    private fun heldFile(context: Context) = File(context.applicationContext.filesDir, HELD_FILE)

    /** Lo llama el proceso del juego al suspender (Quick Resume) o al dejar de hacerlo. */
    fun publishHeld(context: Context, exePath: String?) {
        val f = heldFile(context)
        runCatching { if (exePath == null) f.delete() else f.writeText(exePath) }
    }

    /** Launcher: relee qué juego hay en memoria (solo cuenta si el proceso del juego vive). */
    fun refresh(context: Context): String? {
        val exe = runCatching { heldFile(context).takeIf { it.isFile }?.readText()?.trim() }.getOrNull()
        heldExe = if (!exe.isNullOrEmpty() && gamePid(context) != null) exe else null
        return heldExe
    }

    /**
     * Launcher, antes de abrir [exePath]: si en `:game` hay otro juego (o restos de uno cerrado),
     * mata ese proceso y espera a que desaparezca. Si es el mismo juego suspendido, lo deja (se
     * reanuda). Bloqueante (hasta ~2 s): llamar fuera del hilo de UI.
     */
    fun prepareFor(context: Context, exePath: String) {
        val pid = gamePid(context) ?: return
        val held = refresh(context)
        if (held != null && QuickResumeLogic.sameExe(held, exePath)) return
        Log.i(TAG, "matando el proceso del juego anterior (pid $pid) antes de abrir $exePath")
        publishHeld(context, null)
        Process.killProcess(pid)
        val until = SystemClock.elapsedRealtime() + 2000
        while (gamePid(context) != null && SystemClock.elapsedRealtime() < until) SystemClock.sleep(30)
        heldExe = null
    }

    /** Proceso del juego: termina este proceso (el juego ya se cerró o se forzó su cierre). */
    fun exitGameProcess(context: Context) {
        if (!isGameProcess(context)) return
        publishHeld(context, null)
        Log.i(TAG, "fin del proceso del juego")
        Process.killProcess(Process.myPid())
    }
}
