package com.murdoc820.hyperplay.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

/** Valores por defecto del estado de sesión sin Compose (los mismos que usa SessionUiState). */
private object SessionUiStateDefaults {
    fun frameGen(): Boolean = FrameGenLogic.DEFAULT_ENABLED
}

class EfficiencyModeTest {

    // ---- Gobernador térmico -------------------------------------------------------------------

    @Test
    fun coolBatteryKeepsUserProfile() {
        val g = ThermalGovernor()
        for (cap in FpsCapOption.entries) {
            val d = g.update(36f, enabled = true, userCap = cap)
            assertEquals(cap.fps, d.effectiveFps)
            assertFalse(d.relaxPrime)
            assertFalse(d.throttled)
            assertFalse(d.hot)
        }
    }

    @Test
    fun throttlesAt39_5AndLowersCapAbove60To60() {
        val g = ThermalGovernor()
        assertFalse(g.update(39.4f, true, FpsCapOption.FPS_120).throttled)
        val d = g.update(39.5f, true, FpsCapOption.FPS_120)
        assertTrue(d.throttled)
        assertTrue(d.relaxPrime)
        assertEquals(60, d.effectiveFps)
        assertEquals(60, g.update(40f, true, FpsCapOption.FPS_90).effectiveFps)
        // Un tope de 60 ya es el de protección: no cambia (pero sí relaja Prime).
        val at60 = g.update(40f, true, FpsCapOption.FPS_60)
        assertEquals(60, at60.effectiveFps)
        assertTrue(at60.relaxPrime)
    }

    @Test
    fun hysteresisReturnsToNormalOnlyAt37_5() {
        val g = ThermalGovernor()
        g.update(40f, true, FpsCapOption.FPS_90)
        assertTrue(g.update(38.9f, true, FpsCapOption.FPS_90).throttled)  // zona intermedia: sigue
        assertTrue(g.update(37.6f, true, FpsCapOption.FPS_90).throttled)
        val back = g.update(37.5f, true, FpsCapOption.FPS_90)
        assertFalse(back.throttled)
        assertFalse(back.relaxPrime)
        assertEquals(90, back.effectiveFps)
        // Y no vuelve a activarse hasta 39,5.
        assertFalse(g.update(39.0f, true, FpsCapOption.FPS_90).throttled)
    }

    @Test
    fun hotIndicatorAt41_5WithHysteresis() {
        val g = ThermalGovernor()
        assertFalse(g.update(41.4f, true, FpsCapOption.FPS_60).hot)
        assertTrue(g.update(41.5f, true, FpsCapOption.FPS_60).hot)  // (y tope de emergencia: otra prueba)
        assertTrue(g.update(41.0f, true, FpsCapOption.FPS_60).hot)
        assertFalse(g.update(40.5f, true, FpsCapOption.FPS_60).hot)
    }

    @Test
    fun disabledProtectionNeverThrottlesButStillReportsHot() {
        val g = ThermalGovernor()
        val d = g.update(45f, enabled = false, userCap = FpsCapOption.FPS_120)
        assertFalse(d.throttled)
        assertFalse(d.relaxPrime)
        assertEquals(120, d.effectiveFps)
        assertTrue(d.hot)
        // Desactivar con la protección activa la quita al momento, incluso sin lectura nueva.
        g.update(40f, true, FpsCapOption.FPS_120)
        assertFalse(g.update(null, enabled = false, userCap = FpsCapOption.FPS_120).throttled)
    }

    @Test
    fun missingReadingKeepsState() {
        val g = ThermalGovernor()
        g.update(40f, true, FpsCapOption.FPS_90)
        assertTrue(g.update(null, true, FpsCapOption.FPS_90).throttled)
        assertFalse(ThermalGovernor().update(null, true, FpsCapOption.FPS_90).throttled)
    }

    // ---- Selectores ---------------------------------------------------------------------------

    @Test
    fun fpsCapIs60_90_120AndCycles() {
        assertEquals(listOf(60, 90, 120), FpsCapOption.entries.map { it.fps })
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.DEFAULT)
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.fromKey(null))
        assertEquals(FpsCapOption.FPS_90, FpsCapOption.fromKey("90"))
        // Claves antiguas (72 y sin límite) vuelven al valor por defecto.
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.fromKey("72"))
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.fromKey("off"))
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.fromKey("basura"))
        assertEquals(FpsCapOption.FPS_90, FpsCapOption.cycle(FpsCapOption.FPS_60))
        assertEquals(FpsCapOption.FPS_60, FpsCapOption.cycle(FpsCapOption.FPS_120))
        assertEquals(FpsCapOption.FPS_120, FpsCapOption.cycle(FpsCapOption.FPS_60, -1))
    }

    @Test
    fun panelRefreshIsAnIntegerMultipleOfTheCap() {
        assertEquals(120, FpsCapOption.FPS_60.panelHz)   // 2 vsyncs exactos
        assertEquals(90, FpsCapOption.FPS_90.panelHz)
        assertEquals(120, FpsCapOption.FPS_120.panelHz)
        for (cap in FpsCapOption.entries) assertEquals(0, cap.panelHz % cap.fps)
    }

    @Test
    fun displayIsAlwaysNativeAndUndistorted() {
        assertEquals(100, DisplayPolicy.RENDER_SCALE_PERCENT)
        assertEquals(3, DisplayPolicy.SCREEN_FIT_NATIVE_ID)  // panorámica real o aspecto original
    }

    @Test
    fun upscalerSelection() {
        assertEquals(UpscalerMode.AUTO, UpscalerMode.DEFAULT)
        assertEquals(UpscalerMode.SGSR, UpscalerMode.fromKey("sgsr"))
        assertEquals(UpscalerMode.AUTO, UpscalerMode.fromKey(null))
        // Auto: SGSR solo por debajo de la nativa.
        assertFalse(UpscalerMode.useSgsr(UpscalerMode.AUTO, 2688, 1216, 2688, 1216))
        assertTrue(UpscalerMode.useSgsr(UpscalerMode.AUTO, 2016, 912, 2688, 1216))
        assertTrue(UpscalerMode.useSgsr(UpscalerMode.SGSR, 2688, 1216, 2688, 1216))
        assertFalse(UpscalerMode.useSgsr(UpscalerMode.OFF, 2016, 912, 2688, 1216))
        // Los ids nativos son los que entiende el motor (0 auto, 1 sgsr, 2 off).
        assertEquals(listOf(0, 1, 2), UpscalerMode.entries.map { it.nativeId })
    }

    // ---- HyperFG ------------------------------------------------------------------------------

    @Test
    fun frameGenIsOffByDefault() {
        assertFalse(FrameGenLogic.DEFAULT_ENABLED)
        assertFalse(SessionUiStateDefaults.frameGen())
    }

    @Test
    fun frameGenCapsTheGameToHalfThePanel() {
        val g = ThermalGovernor()
        val at60 = g.update(36f, true, FpsCapOption.FPS_60)
        assertEquals(60, FrameGenLogic.gameFps(at60, frameGen = false))
        assertEquals(60, FrameGenLogic.gameFps(at60, frameGen = true))        // panel 120 Hz -> 60 -> 120
        assertEquals(120, FrameGenLogic.presentedFps(60, frameGen = true))
        val at90 = g.update(36f, true, FpsCapOption.FPS_90)
        assertEquals(60, FrameGenLogic.gameFps(at90, frameGen = true))        // tope bloqueado: 60 -> 120
        val at120 = g.update(36f, true, FpsCapOption.FPS_120)
        assertEquals(60, FrameGenLogic.gameFps(at120, frameGen = true))
        assertEquals(120, FrameGenLogic.gameFps(at120, frameGen = false))
        assertEquals(60, FrameGenLogic.presentedFps(60, frameGen = false))
    }

    @Test
    fun frameGenRespectsThermalProtection() {
        val g = ThermalGovernor()
        val hot = g.update(40f, true, FpsCapOption.FPS_90)
        assertEquals(60, hot.effectiveFps)
        // Con calor HyperFG sigue, limitado a 60 en pantalla: 30 reales + 30 sintéticos.
        assertEquals(30, FrameGenLogic.gameFps(hot, frameGen = true))
        assertEquals(60, FrameGenLogic.presentedFps(FrameGenLogic.gameFps(hot, true), true))
        assertEquals(120, FrameGenLogic.panelHz(hot, frameGen = true))
        val cool = ThermalGovernor().update(36f, true, FpsCapOption.FPS_60)
        assertEquals(60, FrameGenLogic.gameFps(cool, frameGen = true))
    }

    @Test
    fun frameGenLocksTheUserFpsCap() {
        assertFalse(FrameGenLogic.fpsCapSelectable(frameGen = true))
        assertTrue(FrameGenLogic.fpsCapSelectable(frameGen = false))
        // Con HyperFG el tope del usuario no cuenta: siempre 60 -> 120 a 120 Hz.
        for (cap in FpsCapOption.entries) {
            val d = ThermalGovernor().update(36f, true, cap)
            assertEquals(60, FrameGenLogic.gameFps(d, frameGen = true))
            assertEquals(120, FrameGenLogic.panelHz(d, frameGen = true))
            assertEquals(cap.panelHz, FrameGenLogic.panelHz(d, frameGen = false))
        }
    }

    @Test
    fun slowFrameShieldScalesWithGameFps() {
        assertEquals(22f, FrameGenLogic.slowFrameMs(60), 0.01f)
        assertEquals(44f, FrameGenLogic.slowFrameMs(30), 0.01f)
        assertTrue(FrameGenLogic.shouldGenerate(true, hasHistory = true, gameFps = 30, frameIntervalMs = 33.4f))
        assertFalse(FrameGenLogic.shouldGenerate(true, hasHistory = true, gameFps = 30, frameIntervalMs = 50f))
    }

    @Test
    fun emergencyCapAt41_5UntilBelow40_5() {
        val g = ThermalGovernor()
        assertEquals(60, g.update(40f, true, FpsCapOption.FPS_60).effectiveFps)
        val hot = g.update(41.5f, true, FpsCapOption.FPS_60)
        assertEquals(ThermalGovernor.HOT_FPS, hot.effectiveFps)
        assertEquals(30, FrameGenLogic.gameFps(hot, frameGen = true))  // HyperFG: 30 -> 60
        assertEquals(40, g.update(41.0f, true, FpsCapOption.FPS_120).effectiveFps)  // histéresis
        assertEquals(60, g.update(40.5f, true, FpsCapOption.FPS_60).effectiveFps)     // vuelve a 60
        assertEquals(60, g.update(38f, true, FpsCapOption.FPS_120).effectiveFps)      // sigue protegido
        assertEquals(120, g.update(37.5f, true, FpsCapOption.FPS_120).effectiveFps)   // normal
        // Sin protección no hay tope de emergencia.
        assertEquals(120, ThermalGovernor().update(45f, false, FpsCapOption.FPS_120).effectiveFps)
    }

    @Test
    fun syntheticFrameIsSkippedOnSlowFramesOrWithoutHistory() {
        assertTrue(FrameGenLogic.shouldGenerate(true, hasHistory = true, gameFps = 60, frameIntervalMs = 16.7f))
        assertTrue(FrameGenLogic.shouldGenerate(true, true, 60, 22f))
        assertFalse(FrameGenLogic.shouldGenerate(true, true, 60, 22.1f))       // escudo 4: micro-carga
        assertFalse(FrameGenLogic.shouldGenerate(true, hasHistory = false, gameFps = 60, frameIntervalMs = 16f))
        assertFalse(FrameGenLogic.shouldGenerate(false, true, 60, 16f))        // apagado
        assertFalse(FrameGenLogic.shouldGenerate(true, true, 0, 16f))          // sin tope de juego
    }

    @Test
    fun hotToggleOnlyChangesTheGameCapWhenNeeded() {
        val d = ThermalGovernor().update(36f, true, FpsCapOption.FPS_120)
        // Activar en caliente: 120 -> 60 (el panel sigue a 120 Hz: sin cambio de modo ni swapchain).
        assertEquals(120, FrameGenLogic.gameFps(d, false))
        assertEquals(60, FrameGenLogic.gameFps(d, true))
        assertEquals(FpsCapOption.FPS_120.panelHz, FpsCapOption.FPS_60.panelHz)
    }

    // ---- HUD: rangos y consumo ----------------------------------------------------------------

    @Test
    fun fpsLevelIsRelativeToTheActiveCap() {
        val L = PerfMonitorLogic
        assertEquals(PerfLevel.GOOD, L.fpsLevel(57f, 60))
        assertEquals(PerfLevel.WARN, L.fpsLevel(55f, 60))
        assertEquals(PerfLevel.BAD, L.fpsLevel(50f, 60))
        assertEquals(PerfLevel.GOOD, L.fpsLevel(70f, 72))
        assertEquals(PerfLevel.WARN, L.fpsLevel(65f, 72))
        assertEquals(PerfLevel.GOOD, L.fpsLevel(118f, 120))
        assertEquals(PerfLevel.BAD, L.fpsLevel(90f, 120))
        assertEquals(L.fpsLevel(50f), L.fpsLevel(50f, 0))  // sin tope: escala fija
        assertEquals(PerfLevel.NONE, L.fpsLevel(null, 60))
    }

    @Test
    fun batteryAndSocTemperatureRanges() {
        val L = PerfMonitorLogic
        assertEquals(PerfLevel.GOOD, L.batteryTempLevel(37.9f))
        assertEquals(PerfLevel.WARN, L.batteryTempLevel(39f))
        assertEquals(PerfLevel.BAD, L.batteryTempLevel(41.5f))
        assertEquals(PerfLevel.GOOD, L.tempLevel(64f))
        assertEquals(PerfLevel.WARN, L.tempLevel(75f))
        assertEquals(PerfLevel.BAD, L.tempLevel(81f))
    }

    @Test
    fun batteryPowerHandlesMicroAndMilliAmps() {
        val L = PerfMonitorLogic
        assertEquals(4.0f, L.batteryPowerW(-1_000_000L, 4000)!!, 0.001f)  // µA, signo negativo al descargar
        assertEquals(4.0f, L.batteryPowerW(1_000_000L, 4000)!!, 0.001f)
        assertEquals(4.0f, L.batteryPowerW(1000L, 4000)!!, 0.001f)         // mA
        assertNull(L.batteryPowerW(Long.MIN_VALUE, 4000))
        assertNull(L.batteryPowerW(500_000L, 0))
        assertNull(L.batteryPowerW(0L, 4000))
        assertEquals(PerfLevel.GOOD, L.powerLevel(3.5f))
        assertEquals(PerfLevel.WARN, L.powerLevel(6f))
        assertEquals(PerfLevel.BAD, L.powerLevel(8f))
    }
}
