package com.murdoc820.hyperplay.session

import androidx.annotation.StringRes
import com.murdoc820.hyperplay.R
import com.murdoc820.hyperplay.engine.GameLauncher
import java.io.File
import java.io.RandomAccessFile
import kotlin.math.roundToInt

/** Etapas del overlay de carga (según el log de [GameLauncher]). */
enum class LoadingStage(@StringRes val label: Int) {
    PE_LOAD(R.string.stage_pe_load),
    EXECUTING(R.string.stage_executing),
}

/** Lógica pura de [com.murdoc820.hyperplay.GameSessionActivity] (probada en la JVM). */
object GameSessionLogic {
    /** Rango de Hz que se usa como objetivo de frame para ADPF. */
    const val MAX_REFRESH_HZ = 60
    const val MIN_REFRESH_HZ = 30

    const val LOG_TAIL_LINES = 6
    private const val LOG_TAIL_MAX_BYTES = 16 * 1024

    // Índices de nativeDirectGetTelemetry (ver jni_bridge.cpp).
    private const val TELEMETRY_D3D = 2
    private const val TELEMETRY_VULKAN = 3

    /** FPS objetivo del juego (frecuencia pedida al panel). */
    // 120 FPS: con la ruta GPU (Vulkan) los juegos superan 120; el panel va a 120 Hz y el
    // swapchain en FIFO entrega un frame por refresco (sin tearing, sin aceleraciones).
    const val TARGET_FPS = 120

    /** Modo de pantalla (id de Display.Mode) y su frecuencia. */
    data class DisplayModeOption(val id: Int, val refreshHz: Float)

    /**
     * Modo cuya frecuencia es un múltiplo entero de [targetFps] (±0,5 Hz), el más bajo: con 60 FPS
     * el panel va a 60 Hz (cada frame del juego dura exactamente un refresco, sin judder), nunca a
     * 144/90. null si ninguno encaja (se deja el modo del sistema).
     */
    fun pickDisplayMode(modes: List<DisplayModeOption>, targetFps: Int): DisplayModeOption? {
        if (targetFps <= 0) return null
        return modes.filter { m ->
            val ratio = m.refreshHz / targetFps
            val k = ratio.roundToInt()
            k >= 1 && kotlin.math.abs(m.refreshHz - k * targetFps) <= 0.5f
        }.minByOrNull { it.refreshHz }
    }

    /** Hz objetivo: la frecuencia de la pantalla, acotada a [MIN_REFRESH_HZ]..[MAX_REFRESH_HZ]. */
    fun refreshHzFor(displayHz: Float): Int {
        if (displayHz.isNaN() || displayHz <= 0f) return MAX_REFRESH_HZ
        return displayHz.roundToInt().coerceIn(MIN_REFRESH_HZ, MAX_REFRESH_HZ)
    }

    /** Foco del menú tras moverse [delta] (cíclico). */
    fun moveFocus(index: Int, delta: Int, size: Int): Int = if (size <= 0) 0 else (index + delta).mod(size)

    /** Últimas [n] líneas no vacías de [text] (admite \r\n y \r de barras de progreso). */
    fun tailLines(text: String, n: Int = LOG_TAIL_LINES): List<String> {
        if (n <= 0) return emptyList()
        return text.split('\n')
            .map { it.substringAfterLast('\r').trimEnd() }
            .filter { it.isNotBlank() }
            .takeLast(n)
    }

    /** Últimos [maxBytes] del log como texto (sin la primera línea si quedó cortada); "" si no se lee. */
    fun readLogText(file: File?, maxBytes: Int = LOG_TAIL_MAX_BYTES): String {
        if (file == null || !file.isFile) return ""
        return runCatching {
            RandomAccessFile(file, "r").use { raf ->
                val len = raf.length()
                val start = (len - maxBytes).coerceAtLeast(0)
                val buf = ByteArray((len - start).toInt())
                raf.seek(start)
                raf.readFully(buf)
                val text = String(buf, Charsets.UTF_8)
                if (start > 0) text.substringAfter('\n', "") else text
            }
        }.getOrDefault("")
    }

    /** Últimas [n] líneas del log (lee como mucho [maxBytes] del final). */
    fun readLogTail(file: File?, n: Int = LOG_TAIL_LINES, maxBytes: Int = LOG_TAIL_MAX_BYTES): List<String> =
        tailLines(readLogText(file, maxBytes), n)

    /**
     * Etapa de arranque según lo que ya escribió [GameLauncher] en el log. Nunca retrocede respecto
     * a [previous] (el marcador puede salir de la ventana leída del log).
     */
    fun loadingStage(logText: String, previous: LoadingStage = LoadingStage.PE_LOAD): LoadingStage {
        val now = if (logText.contains(GameLauncher.RUNNING_MARKER)) LoadingStage.EXECUTING else LoadingStage.PE_LOAD
        return if (now.ordinal >= previous.ordinal) now else previous
    }

    /** true cuando el juego ya llamó a D3D o Vulkan interceptados (telemetría de la IAT). */
    fun graphicsStarted(telemetry: LongArray?): Boolean {
        if (telemetry == null || telemetry.size <= TELEMETRY_VULKAN) return false
        return telemetry[TELEMETRY_D3D] > 0 || telemetry[TELEMETRY_VULKAN] > 0
    }

    /** Draws acumulados a partir de los cuales el juego ya muestra contenido (logo, menú). */
    const val CONTENT_DRAWS = 40L
    /** Si el juego nunca dibuja por D3D11 (otra API, ruta CPU), quitar la carga tras esto. */
    const val CONTENT_TIMEOUT_MS = 45_000L

    /**
     * true cuando hay que quitar la pantalla de carga: gráficos ya iniciados y, o bien el juego
     * lleva [CONTENT_DRAWS] draws reales, o bien pasó [CONTENT_TIMEOUT_MS] (red de seguridad).
     */
    fun contentStarted(telemetry: LongArray?, draws: Long, elapsedMs: Long): Boolean =
        graphicsStarted(telemetry) && (draws >= CONTENT_DRAWS || elapsedMs >= CONTENT_TIMEOUT_MS)

    /** "m:ss". */
    fun formatElapsed(millis: Long): String {
        val total = (millis.coerceAtLeast(0) / 1000)
        return "%d:%02d".format(total / 60, total % 60)
    }

    /** Texto del código que devolvió el punto de entrada PE (negativo = sin código). */
    fun describeExitCode(code: Int): String = when {
        code < 0 -> "—"
        code == 0 -> "0 (normal)"
        else -> code.toString()
    }

    /** Frames por segundo entre dos lecturas del contador de frames presentados. */
    fun ratePerSecond(prevCount: Long, count: Long, elapsedMillis: Long): Float {
        if (elapsedMillis <= 0 || count < prevCount) return 0f
        return (count - prevCount) * 1000f / elapsedMillis
    }
}
