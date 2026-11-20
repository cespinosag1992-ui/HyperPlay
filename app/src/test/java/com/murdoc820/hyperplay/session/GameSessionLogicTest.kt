package com.murdoc820.hyperplay.session

import com.murdoc820.hyperplay.engine.GameLauncher
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class GameSessionLogicTest {
    @get:Rule val tmp = TemporaryFolder()

    @Test
    fun refreshHzIsClamped() {
        assertEquals(60, GameSessionLogic.refreshHzFor(120f))
        assertEquals(60, GameSessionLogic.refreshHzFor(59.94f))
        assertEquals(30, GameSessionLogic.refreshHzFor(24f))
        assertEquals(60, GameSessionLogic.refreshHzFor(0f))
        assertEquals(60, GameSessionLogic.refreshHzFor(Float.NaN))
    }

    @Test
    fun menuFocusWrapsBothWays() {
        assertEquals(3, GameSessionLogic.moveFocus(0, -1, 4))
        assertEquals(0, GameSessionLogic.moveFocus(3, 1, 4))
        assertEquals(0, GameSessionLogic.moveFocus(2, 1, 0))
    }

    @Test
    fun tailLinesSkipsBlanksAndCarriageReturns() {
        val text = "a\r\nb\n\n  \nprogreso 10%\rprogreso 100%\nc\nd\ne\nf\n"
        assertEquals(listOf("b", "progreso 100%", "c", "d", "e", "f"), GameSessionLogic.tailLines(text))
        assertEquals(listOf("f"), GameSessionLogic.tailLines(text, 1))
        assertEquals(emptyList<String>(), GameSessionLogic.tailLines(text, 0))
        assertEquals(emptyList<String>(), GameSessionLogic.tailLines(""))
    }

    @Test
    fun readLogTailReadsOnlyTheEndAndDropsCutLine() {
        val f = tmp.newFile("game.log")
        f.writeText((1..2000).joinToString("\n") { "linea $it" } + "\n")
        assertEquals((1995..2000).map { "linea $it" }, GameSessionLogic.readLogTail(f))
        val text = GameSessionLogic.readLogText(f, maxBytes = 30)
        assertTrue(text.startsWith("linea "))
        assertTrue(text.length <= 30)
        assertEquals(emptyList<String>(), GameSessionLogic.readLogTail(null))
        assertEquals(emptyList<String>(), GameSessionLogic.readLogTail(tmp.root.resolve("no.log")))
    }

    @Test
    fun loadingStageFollowsLauncherMarkerAndNeverGoesBack() {
        assertEquals(LoadingStage.PE_LOAD, GameSessionLogic.loadingStage("[hyperplay] INICIANDO MOTOR"))
        assertEquals(
            LoadingStage.EXECUTING,
            GameSessionLogic.loadingStage("x\n" + GameLauncher.RUNNING_MARKER + " (refresh=60Hz).\n"),
        )
        assertEquals(LoadingStage.EXECUTING, GameSessionLogic.loadingStage("", LoadingStage.EXECUTING))
    }

    @Test
    fun graphicsStartedNeedsD3dOrVulkanCalls() {
        assertFalse(GameSessionLogic.graphicsStarted(null))
        assertFalse(GameSessionLogic.graphicsStarted(LongArray(2)))
        assertFalse(GameSessionLogic.graphicsStarted(longArrayOf(5, 1, 0, 0, 0, 0, 0, 0)))
        assertTrue(GameSessionLogic.graphicsStarted(longArrayOf(0, 0, 1, 0, 0, 0, 0, 0)))
        assertTrue(GameSessionLogic.graphicsStarted(longArrayOf(0, 0, 0, 3, 0, 0, 0, 0)))
    }

    @Test
    fun formattingHelpers() {
        assertEquals("0:00", GameSessionLogic.formatElapsed(-5))
        assertEquals("1:05", GameSessionLogic.formatElapsed(65_400))
        assertEquals("12:00", GameSessionLogic.formatElapsed(720_000))
        assertEquals("0 (normal)", GameSessionLogic.describeExitCode(0))
        assertEquals("137", GameSessionLogic.describeExitCode(137))
        assertEquals("1", GameSessionLogic.describeExitCode(1))
        assertEquals("desconocido", GameSessionLogic.describeExitCode(-1))
        assertEquals(30f, GameSessionLogic.ratePerSecond(10, 25, 500), 1e-4f)
        assertEquals(0f, GameSessionLogic.ratePerSecond(10, 5, 500), 1e-4f)
        assertEquals(0f, GameSessionLogic.ratePerSecond(10, 20, 0), 1e-4f)
    }

    @Test
    fun displayModeIsLowestIntegerMultipleOfTarget() {
        val m = { id: Int, hz: Float -> GameSessionLogic.DisplayModeOption(id, hz) }
        val redMagic = listOf(m(1, 144f), m(2, 120f), m(3, 90f), m(4, 60.000004f))
        assertEquals(4, GameSessionLogic.pickDisplayMode(redMagic, 60)?.id)
        // Sin 60 Hz: 120 (2 refrescos por frame), nunca 144 ni 90.
        assertEquals(2, GameSessionLogic.pickDisplayMode(listOf(m(1, 144f), m(2, 120f), m(3, 90f)), 60)?.id)
        assertEquals(null, GameSessionLogic.pickDisplayMode(listOf(m(1, 144f), m(3, 90f)), 60))
        assertEquals(4, GameSessionLogic.pickDisplayMode(redMagic, 30)?.id)
        assertEquals(2, GameSessionLogic.pickDisplayMode(redMagic, 120)?.id)
        assertEquals(null, GameSessionLogic.pickDisplayMode(redMagic, 0))
    }
}
