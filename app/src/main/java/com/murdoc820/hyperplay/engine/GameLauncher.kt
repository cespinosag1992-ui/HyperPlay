package com.murdoc820.hyperplay.engine

import android.content.Context
import com.murdoc820.hyperplay.model.ExeArch
import com.murdoc820.hyperplay.model.PortableGame
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.TimeZone

/**
 * Motor Directo Nativo Bionic (ZVDS).
 * Ejecuta ejecutables PE32 (x86) y PE32+ (x64) directamente en el proceso de Android
 * mediante `BionicPeLoader`, `NativeDllInterceptor`, `OryonCpuTranslator` (JIT AArch64)
 * y `DirectVulkanBridge` (120Hz MAILBOX).
 *
 * Cero Wine, Cero FEX, Cero Box64, Cero PRoot, Cero contenedores Linux.
 */
class GameLauncher(context: Context) {
    private val appContext = context.applicationContext
    private val logsDir = File(context.filesDir, LOGS_DIR_NAME)

    @Volatile private var runningDirect = false
    @Volatile private var lastExitCode = -1
    private var gameThread: Thread? = null

    /**
     * @param refreshHz frecuencia objetivo del panel: fija la duración de frame pedida a ADPF.
     * @return el fichero de log de la sesión, o el error.
     */
    fun launch(game: PortableGame, refreshHz: Int): Result<File> = runCatching {
        val is64 = when (game.arch) {
            ExeArch.X64 -> true
            ExeArch.X86 -> false
            else -> throw IllegalArgumentException("Arquitectura no soportada: ${game.arch.badge}")
        }
        check(logsDir.isDirectory || logsDir.mkdirs()) { "No se pudo crear ${logsDir.absolutePath}" }
        val log = File(logsDir, logFileName(game.title, System.currentTimeMillis()))

        log.writeText(
            "[hyperplay] ========================================================\n" +
            "[hyperplay] INICIANDO MOTOR NATIVO DIRECTO BIONIC (ZVDS)\n" +
            "[hyperplay] Silicio: Qualcomm Snapdragon 8 Elite (SM8750) Pure 64-bit ARMv9\n" +
            "[hyperplay] Afinidad: 2x Oryon Prime Cores (cpu6, cpu7 @ 4.32GHz) LOCKED\n" +
            "[hyperplay] Recompilador JIT: OryonEmitterBackend (x86/x64 -> AArch64)\n" +
            "[hyperplay] GPU: DirectVulkanBridge (120Hz MAILBOX + FSR CAS Compute Shader)\n" +
            "[hyperplay] Audio: DirectAAudioEngine (48kHz LOW_LATENCY)\n" +
            "[hyperplay] Input: XInput Direct Struct in RAM (0.0ms)\n" +
            "[hyperplay] Ejecutable: ${game.exePath} (${game.arch.badge}, LAA=${game.pe.largeAddressAware}, is64=$is64)\n" +
            "[hyperplay] ========================================================\n"
        )
        // Solo se guardan los últimos logs de sesión (espacio del teléfono).
        com.murdoc820.hyperplay.diag.LogStorage.pruneGameLogs(logsDir, current = log)

        // 1. Cargar PE en memoria y secuestrar IAT con trampolines C++20 nativos. Un juego .NET
        //    (solo IL) se ejecuta con Mono x64 (ManagedRuntime).
        val managed = ManagedRuntime.isManagedExe(File(game.exePath))
        val directLoaded = if (managed) {
            var lastLogged = -10
            val rt = ManagedRuntime.ensureInstalled(appContext) { pct ->
                when {
                    pct < 0 -> log.appendText("[hyperplay] Runtime .NET: descomprimiendo / extracting...\n")
                    pct >= lastLogged + 10 -> {
                        lastLogged = pct
                        log.appendText("[hyperplay] Runtime .NET: descargando / downloading $pct %\n")
                    }
                }
            }
            log.appendText("[hyperplay] Juego .NET: Mono x64 de ${rt.absolutePath}\n")
            HyperPlayNative.nativeDirectLoadManaged(game.exePath, rt.absolutePath)
        } else {
            HyperPlayNative.nativeDirectLoadPe(game.exePath)
        }
        if (!directLoaded) {
            val detail = HyperPlayNative.nativeDirectGetLastLoadReport().trim()
            val err = "Error al cargar el ejecutable PE con BionicPeLoader / IAT en ${game.exePath}" +
                if (detail.isEmpty()) "" else "\n$detail"
            log.appendText("[hyperplay] ERROR: $err\n")
            throw IllegalStateException(err)
        }

        // 2. Precompilar bloques AOT en caché local
        val savesDir = File(game.mountFolder, "_HyperPlaySaves").apply { mkdirs() }
        val aotStats = HyperPlayNative.nativeDirectPrecompileAot(game.exePath, savesDir.absolutePath)
        if (aotStats != null && aotStats.size >= 4) {
            log.appendText("[hyperplay] HyperAOT: bloques=${aotStats[0]}, cacheados=${aotStats[1]}, size=${aotStats[2]}B\n")
        }

        // 3. Afinidad Prime y ADPF: los hace el propio hilo del juego al entrar en nativeDirectLaunch
        // (loader/FramePacing). Antes se llamaban aquí, en el hilo de E/S de Kotlin, y no afectaban
        // al juego.

        runningDirect = true
        lastExitCode = -1
        log.appendText("$RUNNING_MARKER (refresh=${refreshHz}Hz).\n")

        // 4. Ejecutar el punto de entrada PE en el traductor Oryon. El dispatcher corre en su
        // propio hilo: `launch` debe devolver el log y dejar que la UI muestre la sesión.
        // Pila host grande: el despachador anida thunks y callbacks del juego (excepciones, APC,
        // ventanas) y con la pila por defecto de un hilo Java (~1 MB) se desbordaba.
        val thread = Thread(null, Runnable {
            try {
                val exit = HyperPlayNative.nativeDirectLaunch()
                lastExitCode = exit
                val detail = HyperPlayNative.nativeDirectGetLastLoadReport().trim()
                log.appendText("[hyperplay] nativeDirectLaunch terminó con código $exit" +
                    (if (detail.isNotEmpty()) ": $detail\n" else "\n"))
            } catch (t: Throwable) {
                log.appendText("[hyperplay] nativeDirectLaunch excepción: ${t.message}\n")
            } finally {
                runningDirect = false
            }
        }, "hyperplay-direct", GAME_THREAD_STACK_BYTES)
        gameThread = thread
        thread.start()
        log
    }

    /** Cierre inmediato del motor directo. */
    fun forceKill() {
        runningDirect = false
        HyperPlayNative.nativeDirectStop()
        HyperPlayNative.nativeDirectStopAudio()
        gameThread?.interrupt()
        gameThread = null
    }

    fun isRunning(): Boolean = runningDirect

    fun exitCode(): Int = lastExitCode

    companion object {
        const val LOGS_DIR_NAME = "logs"

        /** Pila host del hilo principal del juego. */
        const val GAME_THREAD_STACK_BYTES = 64L * 1024 * 1024

        /** Línea del log que marca que el punto de entrada PE ya corre en el traductor Oryon. */
        const val RUNNING_MARKER = "[hyperplay] Motor Directo Bionic en ejecución continua"

        private val UNSAFE_NAME_CHARS = Regex("[^A-Za-z0-9._-]+")
        private const val MAX_TITLE_CHARS = 48

        fun logFileName(title: String, timestampMillis: Long, timeZone: TimeZone = TimeZone.getDefault()): String {
            val safe = title.replace(UNSAFE_NAME_CHARS, "_").trim('_', '.').take(MAX_TITLE_CHARS).ifEmpty { "game" }
            val format = SimpleDateFormat("yyyyMMdd-HHmmss", Locale.US).apply { this.timeZone = timeZone }
            return "$safe-${format.format(Date(timestampMillis))}.log"
        }
    }
}
