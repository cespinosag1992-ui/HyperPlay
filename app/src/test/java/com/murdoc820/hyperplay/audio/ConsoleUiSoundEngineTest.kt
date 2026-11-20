package com.murdoc820.hyperplay.audio

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import kotlin.math.abs

class ConsoleUiSoundEngineTest {

    private fun check(pcm: ShortArray, amplitude: Double, expectedLen: Int) {
        assertEquals(expectedLen, pcm.size)
        val peak = ConsoleUiSoundEngine.peak(pcm)
        assertTrue("pico $peak", peak <= (amplitude * Short.MAX_VALUE).toInt() + 1)
        assertTrue("pico demasiado bajo $peak", peak > (amplitude * Short.MAX_VALUE * 0.5).toInt())
        assertTrue("empieza en ${pcm.first()}", abs(pcm.first().toInt()) <= 64)
        assertTrue("termina en ${pcm.last()}", abs(pcm.last().toInt()) <= 64)
    }

    @Test fun navigateTickSine() {
        check(ConsoleUiSoundEngine.synthesize(1400.0, 1400.0, 18, 0.35), 0.35, 48_000 * 18 / 1000)
    }

    @Test fun confirmChirpTriangle() {
        val pcm = ConsoleUiSoundEngine.synthesize(880.0, 1760.0, 60, 0.4, ConsoleUiSoundEngine.Wave.TRIANGLE)
        check(pcm, 0.4, 2880)
    }

    @Test fun backFallingSweep() {
        check(ConsoleUiSoundEngine.synthesize(1100.0, 550.0, 50, 0.38, ConsoleUiSoundEngine.Wave.TRIANGLE), 0.38, 2400)
    }

    @Test fun amplitudeClampedAndDeterministic() {
        val a = ConsoleUiSoundEngine.synthesize(200.0, 200.0, 30, 3.0)
        assertTrue(ConsoleUiSoundEngine.peak(a) <= Short.MAX_VALUE)
        assertTrue(a.contentEquals(ConsoleUiSoundEngine.synthesize(200.0, 200.0, 30, 3.0)))
    }

    @Test fun emptyAndSilent() {
        assertEquals(0, ConsoleUiSoundEngine.synthesize(1000.0, 1000.0, 0, 0.5).size)
        assertEquals(0, ConsoleUiSoundEngine.peak(ConsoleUiSoundEngine.synthesize(0.0, 0.0, 40, 0.0)))
    }

    @Test fun concatJoinsInOrder() {
        val out = ConsoleUiSoundEngine.concat(listOf(shortArrayOf(1, 2), shortArrayOf(), shortArrayOf(3)))
        assertTrue(out.contentEquals(shortArrayOf(1, 2, 3)))
    }
}
