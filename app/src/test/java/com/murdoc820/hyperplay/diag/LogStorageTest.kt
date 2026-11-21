package com.murdoc820.hyperplay.diag

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

class LogStorageTest {
    private fun dirWith(count: Int, ext: String, size: Int = 10): Pair<File, List<File>> {
        val dir = Files.createTempDirectory("logs").toFile()
        val files = (0 until count).map { i ->
            File(dir, "f$i.$ext").apply {
                writeBytes(ByteArray(size))
                setLastModified(1_000_000L + i * 1000L)  // f0 el más viejo
            }
        }
        return dir to files
    }

    @Test
    fun keepsOnlyNewestGameLogs() {
        val (dir, files) = dirWith(8, "log")
        val removed = LogStorage.pruneGameLogs(dir)
        assertEquals(3, removed.size)
        assertFalse(files[0].exists())
        assertFalse(files[2].exists())
        assertTrue(files[3].exists())
        assertTrue(files[7].exists())
    }

    @Test
    fun neverDeletesCurrentSessionLog() {
        val (dir, files) = dirWith(8, "log")
        files[0].setLastModified(1L)  // el más viejo, pero es el actual
        LogStorage.pruneGameLogs(dir, current = files[0])
        assertTrue(files[0].exists())
    }

    @Test
    fun respectsTotalSizeLimit() {
        val (dir, files) = dirWith(4, "log", size = 100)
        LogStorage.prune(dir, "log", keep = 10, maxBytes = 250)
        assertTrue(files[3].exists())
        assertTrue(files[2].exists())
        assertFalse(files[1].exists())
        assertFalse(files[0].exists())
    }

    @Test
    fun dropsOldAndExtraReports() {
        val (dir, files) = dirWith(25, "json")
        val now = 1_000_000L + 24 * 1000L
        val removed = LogStorage.pruneReports(dir, now)
        files.forEach { if (it.exists()) it.setLastModified(it.lastModified() + 2 * LogStorage.MAX_REPORT_AGE_MS) }
        val later = now + 2 * LogStorage.MAX_REPORT_AGE_MS
        assertEquals(5, removed.size)
        val old = File(dir, "old.json").apply { writeText("{}"); setLastModified(later - LogStorage.MAX_REPORT_AGE_MS - 1) }
        LogStorage.prune(dir, "json", keep = 100, maxAgeMs = LogStorage.MAX_REPORT_AGE_MS, now = later)
        assertFalse(old.exists())
        assertTrue(files[24].exists())
    }

    @Test
    fun ignoresOtherExtensions() {
        val (dir, _) = dirWith(8, "txt")
        assertTrue(LogStorage.pruneGameLogs(dir).isEmpty())
    }
}
