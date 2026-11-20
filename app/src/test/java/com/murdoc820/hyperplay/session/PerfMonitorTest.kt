package com.murdoc820.hyperplay.session

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class PerfMonitorTest {
    @get:Rule val tmp = TemporaryFolder()

    private val L = PerfMonitorLogic

    @Test
    fun parsesStatTicksWithSpacesAndParensInName() {
        val stat = "1234 (wine (x) srv) S 1 1234 1234 0 -1 4194304 524 0 0 0 150 25 0 0 20 0 1 0 3522167 1 1"
        assertEquals(175L, L.parseStatTicks(stat))
        val real = "20776 (cat) R 20755 20755 20755 0 -1 4194304 524 0 0 0 7 3 0 0 20 0 1 0 3522167 11088744448 1256"
        assertEquals(10L, L.parseStatTicks(real))
    }

    @Test
    fun rejectsMalformedStat() {
        assertNull(L.parseStatTicks("garbage"))
        assertNull(L.parseStatTicks("1 (x) S 1 2"))
        assertNull(L.parseStatTicks("1 (x) S 1 1 1 0 -1 0 0 0 0 0 a b"))
    }

    @Test
    fun cpuPercentNormalizesByCoresAndIgnoresNewPids() {
        val prev = mapOf(1 to 100L, 2 to 50L)
        val cur = mapOf(1 to 300L, 2 to 150L, 3 to 9999L)  // 3 es nuevo: no cuenta
        // 300 ticks en 1 s con CLK_TCK 100 y 8 núcleos = 300 / 800 = 37,5 %.
        assertEquals(37.5f, L.cpuPercent(prev, cur, 1000, 100, 8)!!, 0.01f)
    }

    @Test
    fun cpuPercentEdgeCases() {
        assertNull(L.cpuPercent(emptyMap(), mapOf(1 to 1L), 1000, 100, 8))
        assertNull(L.cpuPercent(mapOf(1 to 1L), mapOf(1 to 2L), 0, 100, 8))
        // Contador que retrocede (pid reutilizado) no resta.
        assertEquals(0f, L.cpuPercent(mapOf(1 to 500L), mapOf(1 to 10L), 1000, 100, 8)!!, 0f)
        // Saturado a 100 %.
        assertEquals(100f, L.cpuPercent(mapOf(1 to 0L), mapOf(1 to 5000L), 1000, 100, 8)!!, 0f)
    }

    @Test
    fun gpuBusyParsingAndPercent() {
        assertEquals(86155L to 1008247L, L.parseGpuBusy("  86155 1008247\n"))
        assertNull(L.parseGpuBusy("nope"))
        assertNull(L.parseGpuBusy("12"))
        assertEquals(50f, L.gpuPercent(500, 1000)!!, 0f)
        assertNull(L.gpuPercent(0, 0))
    }

    @Test
    fun gpuTrackerTreatsRepeatedSampleAsIdle() {
        val t = GpuBusyTracker()
        assertEquals(10f, t.update(100L to 1000L)!!, 0.01f)
        assertEquals(10f, t.update(100L to 1000L)!!, 0.01f)
        assertEquals(10f, t.update(100L to 1000L)!!, 0.01f)
        assertEquals(0f, t.update(100L to 1000L)!!, 0f)
        assertEquals(20f, t.update(200L to 1000L)!!, 0.01f)
        assertNull(t.update(null))
    }

    @Test
    fun thermalTempParsing() {
        assertEquals(37f, L.parseThermalTemp("37000\n")!!, 0f)
        assertEquals(36f, L.parseThermalTemp("36")!!, 0f)
        assertNull(L.parseThermalTemp("-40960"))
        assertNull(L.parseThermalTemp("-273000"))
        assertNull(L.parseThermalTemp("x"))
    }

    @Test
    fun selectsCpuAndGpuZones() {
        val types = mapOf(
            "thermal_zone1" to "cpu-0-0-0", "thermal_zone22" to "cpuss-0-0", "thermal_zone25" to "cpu-1-0-0",
            "thermal_zone32" to "gpuss-0", "thermal_zone59" to "skin-msm-therm", "thermal_zone80" to "battery",
        )
        assertEquals(listOf("thermal_zone1", "thermal_zone25"), L.selectCpuZones(types))
        assertEquals(listOf("thermal_zone22"), L.selectCpuZones(mapOf("thermal_zone22" to "cpuss-0-0")))
        assertEquals(listOf("thermal_zone32"), L.selectGpuZones(types))
        assertEquals(emptyList<String>(), L.selectCpuZones(mapOf("a" to "battery")))
    }

    @Test
    fun cpuListsAndClusters() {
        assertEquals(listOf(0, 1, 2, 3, 4, 5), L.parseCpuList("0 1 2 3 4 5\n"))
        assertEquals(listOf(0, 1, 2, 3, 4, 5, 6, 7), L.parseCpuList("0-7"))
        assertEquals(listOf(0, 1, 4), L.parseCpuList("0-1,4"))
        val related = (0..5).associateWith { (0..5).toList() } + mapOf(6 to listOf(6, 7), 7 to listOf(6, 7))
        assertEquals(listOf((0..5).toList(), listOf(6, 7)), L.groupClusters(related))
        assertEquals(0.5f, ClusterFreq(listOf(0), 1_000_000, 2_000_000).load, 0f)
    }

    @Test
    fun fpsAndFrameTime() {
        assertEquals(60f, L.fps(0, 15, 250)!!, 0.01f)
        assertNull(L.fps(10, 5, 250))
        assertEquals(16.67f, L.frameTimeMs(15, 250)!!, 0.01f)
        assertNull(L.frameTimeMs(0, 250))
    }

    @Test
    fun batteryTemp() {
        assertEquals(31.5f, L.batteryTempC(315)!!, 0.001f)
        assertNull(L.batteryTempC(Int.MIN_VALUE))
        assertNull(L.batteryTempC(-4000))
    }

    @Test
    fun levels() {
        assertEquals(PerfLevel.GOOD, L.fpsLevel(60f))
        assertEquals(PerfLevel.GOOD, L.fpsLevel(58f))
        assertEquals(PerfLevel.WARN, L.fpsLevel(50f))
        assertEquals(PerfLevel.BAD, L.fpsLevel(30f))
        assertEquals(PerfLevel.NONE, L.fpsLevel(null))
        // SoC del Snapdragon 8 Elite: < 65 verde, ≤ 80 ámbar, > 80 rojo (antes 45/55: rojo en carga normal).
        assertEquals(PerfLevel.GOOD, L.tempLevel(40f))
        assertEquals(PerfLevel.WARN, L.tempLevel(70f))
        assertEquals(PerfLevel.BAD, L.tempLevel(85f))
        assertEquals(PerfLevel.GOOD, L.usageLevel(50f))
        assertEquals(PerfLevel.WARN, L.usageLevel(80f))
        assertEquals(PerfLevel.BAD, L.usageLevel(95f))
        assertEquals(PerfLevel.BAD, L.headroomLevel(1.0f))
    }

    @Test
    fun formatting() {
        assertEquals("—", L.formatOrDash(null))
        assertEquals("—", L.formatOrDash(Float.NaN))
        assertEquals("16.7", L.formatOrDash(16.666f, 1))
        assertEquals("60", L.formatOrDash(59.6f))
        assertEquals("3.5", L.formatGhz(3_532_800))
        assertEquals("—", L.formatGhz(0))
        assertEquals("1363", L.formatMhz(1_363_200))
        assertEquals("—", L.formatMhz(0))
    }

    @Test
    fun minFpsFromWorstFrameTime() {
        assertEquals(50f, L.minFps(listOf(16.7f, 20f, 16.6f))!!, 0.01f)
        assertNull(L.minFps(emptyList()))
        assertNull(L.minFps(listOf(0f, Float.NaN)))
    }

    @Test
    fun modeCycleAndKeys() {
        assertEquals(PerfOverlayMode.COMPACT, PerfOverlayMode.fromKey(null))
        // Solo dos opciones: activado (compacto) o desactivado; un "full" guardado pasa a compacto.
        assertEquals(PerfOverlayMode.COMPACT, PerfOverlayMode.fromKey("full"))
        assertEquals(PerfOverlayMode.OFF, PerfOverlayMode.cycle(PerfOverlayMode.COMPACT))
        assertEquals(PerfOverlayMode.COMPACT, PerfOverlayMode.cycle(PerfOverlayMode.OFF))
        assertEquals(PerfOverlayMode.COMPACT, PerfOverlayMode.cycle(PerfOverlayMode.OFF, -1))
        assertEquals(PerfOverlayMode.COMPACT, PerfOverlayMode.cycle(PerfOverlayMode.FULL))  // nunca queda en FULL
    }

    @Test
    fun historyKeepsLast120() {
        val h = FrameTimeHistory()
        repeat(130) { h.add(it.toFloat()) }
        assertEquals(120, h.size)
        assertEquals(10f, h.snapshot().first(), 0f)
        assertEquals(129f, h.snapshot().last(), 0f)
    }

    @Test
    fun sourcesReadFakeSysfs() {
        val root = tmp.root
        fun write(path: String, text: String) = File(root, path).apply { parentFile!!.mkdirs(); writeText(text) }
        write("proc/100/stat", "100 (proot) S 1 100 100 0 -1 0 0 0 0 0 40 10 0 0 20 0 1 0 1 1 1")
        write("proc/101/stat", "101 (wine64) S 100 100 100 0 -1 0 0 0 0 0 5 5 0 0 20 0 1 0 1 1 1")
        write("proc/self/stat", "ignored")
        write("cpu/possible", "0-2\n")
        write("cpu/cpu0/cpufreq/related_cpus", "0 1\n")
        write("cpu/cpu1/cpufreq/related_cpus", "0 1\n")
        write("cpu/cpu2/cpufreq/related_cpus", "2\n")
        write("cpu/cpu0/cpufreq/cpuinfo_max_freq", "3532800\n")
        write("cpu/cpu2/cpufreq/cpuinfo_max_freq", "4320000\n")
        write("cpu/cpu0/cpufreq/scaling_cur_freq", "1363200\n")
        write("cpu/cpu2/cpufreq/scaling_cur_freq", "1017600\n")
        write("thermal/thermal_zone1/type", "cpu-0-0-0\n")
        write("thermal/thermal_zone1/temp", "37000\n")
        write("thermal/thermal_zone2/type", "cpu-1-0-0\n")
        write("thermal/thermal_zone2/temp", "52500\n")
        write("thermal/thermal_zone3/type", "cpu-1-1-0\n")
        write("thermal/thermal_zone3/temp", "-40960\n")
        write("thermal/thermal_zone4/type", "gpuss-0\n")
        write("thermal/thermal_zone4/temp", "41000\n")
        write("kgsl/gpubusy", "  250 1000\n")

        val s = PerfSources(File(root, "proc"), File(root, "cpu"), File(root, "thermal"), File(root, "kgsl/gpubusy"))
        assertEquals(mapOf(100 to 50L, 101 to 10L), s.readProcessTicks())
        assertEquals(52.5f, s.readCpuTemp()!!, 0.001f)
        assertEquals(41f, s.readGpuTemp()!!, 0.001f)
        assertEquals(250L to 1000L, s.readGpuBusy())
        assertEquals(
            listOf(ClusterFreq(listOf(0, 1), 1_363_200, 3_532_800), ClusterFreq(listOf(2), 1_017_600, 4_320_000)),
            s.readClusters(),
        )
    }

    @Test
    fun sourcesReturnNothingWhenBlocked() {
        val missing = File(tmp.root, "none")
        val s = PerfSources(missing, missing, missing, File(missing, "gpubusy"))
        assertEquals(emptyMap<Int, Long>(), s.readProcessTicks())
        assertNull(s.readCpuTemp())
        assertNull(s.readGpuBusy())
        assertEquals(emptyList<ClusterFreq>(), s.readClusters())
    }
}
