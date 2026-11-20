package com.murdoc820.hyperplay.audio

import android.content.Context
import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioTrack
import android.util.Log
import kotlin.math.PI
import kotlin.math.abs
import kotlin.math.min
import kotlin.math.roundToInt
import kotlin.math.sin

/**
 * Sonidos de interfaz estilo consola, sintetizados en memoria (sin ToneGenerator ni assets).
 * Cada sonido se pre-renderiza una vez a PCM 16-bit mono 48 kHz en un AudioTrack MODE_STATIC.
 */
class ConsoleUiSoundEngine(@Suppress("UNUSED_PARAMETER") context: Context) {

    private val tracks: Map<Sound, AudioTrack?> = Sound.entries.associateWith { createTrack(it.pcm) }
    @Volatile private var released = false

    fun navigate() = play(Sound.NAVIGATE)
    fun confirm() = play(Sound.CONFIRM)
    fun back() = play(Sound.BACK)
    fun error() = play(Sound.ERROR)

    fun release() {
        if (released) return
        released = true
        tracks.values.forEach { runCatching { it?.release() } }
    }

    private fun play(sound: Sound) {
        if (released) return
        val track = tracks[sound] ?: return
        runCatching {
            if (track.playState == AudioTrack.PLAYSTATE_PLAYING) track.stop()
            track.reloadStaticData()
            track.play()
        }.onFailure { Log.w(TAG, "No se pudo reproducir $sound", it) }
    }

    private fun createTrack(pcm: ShortArray): AudioTrack? = runCatching {
        val track = AudioTrack.Builder()
            .setAudioAttributes(
                AudioAttributes.Builder()
                    .setUsage(AudioAttributes.USAGE_GAME)
                    .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION)
                    .build(),
            )
            .setAudioFormat(
                AudioFormat.Builder()
                    .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                    .setSampleRate(SAMPLE_RATE)
                    .setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
                    .build(),
            )
            .setTransferMode(AudioTrack.MODE_STATIC)
            .setBufferSizeInBytes(pcm.size * 2)
            // Sin LOW_LATENCY: cada AudioTrack rápido ocupa una de las ~8 pistas del FastMixer y, con
            // launcher + sesión (8 sonidos), el stream AAudio del juego se quedaba sin ruta rápida
            // (burst de 20 ms en vez de 4). Para un clic de menú la ruta normal basta.
            .setPerformanceMode(AudioTrack.PERFORMANCE_MODE_NONE)
            .build()
        track.write(pcm, 0, pcm.size)
        track
    }.onFailure { Log.w(TAG, "AudioTrack no disponible", it) }.getOrNull()

    // Sonidos suaves estilo macOS: senoidales puras con caída exponencial (sin el "pitido" de las
    // ondas triangulares agudas de antes) y volumen bajo.
    private enum class Sound(segments: List<Segment>) {
        /** "Tic" breve y discreto al mover el foco. */
        NAVIGATE(listOf(Segment(1850.0, 1650.0, 22, 0.10, Wave.SINE, decayMs = 6.0))),
        /** "Pop" redondo al elegir. */
        CONFIRM(listOf(Segment(620.0, 920.0, 85, 0.22, Wave.SINE, decayMs = 26.0))),
        /** Descenso suave al volver. */
        BACK(listOf(Segment(760.0, 470.0, 90, 0.18, Wave.SINE, decayMs = 30.0))),
        /** Golpe grave único (tipo "Basso"), sin zumbido. */
        ERROR(listOf(Segment(240.0, 190.0, 170, 0.26, Wave.SINE, decayMs = 55.0))),
        ;

        val pcm: ShortArray = concat(
            segments.map { synthesize(it.startHz, it.endHz, it.durationMs, it.amplitude, it.wave, decayMs = it.decayMs) },
        )
    }

    private data class Segment(
        val startHz: Double,
        val endHz: Double,
        val durationMs: Int,
        val amplitude: Double,
        val wave: Wave,
        val decayMs: Double? = null,
    )

    enum class Wave { SINE, TRIANGLE }

    companion object {
        private const val TAG = "ConsoleUiSound"
        const val SAMPLE_RATE = 48_000

        /**
         * Genera un tono (con barrido lineal de frecuencia startHz→endHz) con envolvente
         * de ataque/caída corta. Función pura: mismo input → mismo output.
         * @param amplitude 0.0..1.0 relativo a Short.MAX_VALUE.
         */
        fun synthesize(
            startHz: Double,
            endHz: Double,
            durationMs: Int,
            amplitude: Double,
            wave: Wave = Wave.SINE,
            sampleRate: Int = SAMPLE_RATE,
            attackMs: Double = 2.0,
            releaseMs: Double = 6.0,
            /** Caída exponencial (constante de tiempo en ms); null = plano hasta el release. */
            decayMs: Double? = null,
        ): ShortArray {
            val n = (sampleRate.toLong() * durationMs / 1000).toInt().coerceAtLeast(0)
            val out = ShortArray(n)
            if (n == 0) return out
            val amp = amplitude.coerceIn(0.0, 1.0) * Short.MAX_VALUE
            val attack = min((attackMs * sampleRate / 1000).roundToInt().coerceAtLeast(1), n / 2 + 1)
            val release = min((releaseMs * sampleRate / 1000).roundToInt().coerceAtLeast(1), n / 2 + 1)
            var phase = 0.0 // en ciclos
            for (i in 0 until n) {
                val t = if (n > 1) i.toDouble() / (n - 1) else 0.0
                val freq = startHz + (endHz - startHz) * t
                val raw = when (wave) {
                    Wave.SINE -> sin(2.0 * PI * phase)
                    Wave.TRIANGLE -> {
                        val p = phase - kotlin.math.floor(phase)
                        // 0 en p=0, 1 en 0.25, -1 en 0.75
                        if (p < 0.25) 4 * p else if (p < 0.75) 2 - 4 * p else 4 * p - 4
                    }
                }
                val envA = if (i < attack) i.toDouble() / attack else 1.0
                val remaining = n - 1 - i
                val envR = if (remaining < release) remaining.toDouble() / release else 1.0
                val envD = if (decayMs != null) kotlin.math.exp(-(i * 1000.0 / sampleRate) / decayMs) else 1.0
                val v = raw * amp * min(envA, envR) * envD
                out[i] = v.roundToInt().coerceIn(-Short.MAX_VALUE.toInt(), Short.MAX_VALUE.toInt()).toShort()
                phase += freq / sampleRate
            }
            return out
        }

        internal fun concat(parts: List<ShortArray>): ShortArray {
            val out = ShortArray(parts.sumOf { it.size })
            var off = 0
            for (p in parts) {
                p.copyInto(out, off)
                off += p.size
            }
            return out
        }

        /** Pico absoluto de una señal PCM (útil para tests). */
        fun peak(pcm: ShortArray): Int = pcm.maxOfOrNull { abs(it.toInt()) } ?: 0
    }
}
