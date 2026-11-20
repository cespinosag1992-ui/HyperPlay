package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertEquals
import org.junit.Test
import java.util.TimeZone

class GameLauncherTest {

    @Test
    fun logFileNameIsSanitizedAndTimestamped() {
        val utc = TimeZone.getTimeZone("UTC")
        // 2026-11-11 10:20:30 UTC
        val t = 1_794_392_430_000L
        assertEquals("Hollow_Knight-20261111-102030.log", GameLauncher.logFileName("Hollow Knight", t, utc))
        assertEquals("game-20261111-102030.log", GameLauncher.logFileName("../", t, utc))
        val long = GameLauncher.logFileName("x".repeat(200), t, utc)
        assertEquals(48 + "-20261111-102030.log".length, long.length)
    }
}
