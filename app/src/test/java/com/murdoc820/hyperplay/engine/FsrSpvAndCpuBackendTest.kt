package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder

class FsrSpvAndCpuBackendTest {

    @Test
    fun verifiesFsrCasSpvHeaderAndSize() {
        val spvFile = File("src/main/cpp/gfx/shaders/fsr_cas.spv")
        assertTrue("fsr_cas.spv debe existir", spvFile.isFile)
        assertTrue("fsr_cas.spv debe tener tamaño mayor a 0", spvFile.length() > 0)
        assertEquals("El tamaño debe ser múltiplo de 4 bytes", 0L, spvFile.length() % 4)

        val bytes = spvFile.readBytes()
        val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
        val magic = buffer.int.toLong() and 0xFFFFFFFFL
        assertEquals("La palabra mágica de SPIR-V debe ser 0x07230203", 0x07230203L, magic)
    }

    @Test
    fun verifiesFsrCasIncSourceFile() {
        val incFile = File("src/main/cpp/gfx/shaders/fsr_cas.inc")
        assertTrue("fsr_cas.inc debe existir", incFile.isFile)
        val text = incFile.readText()
        assertTrue("fsr_cas.inc debe contener la cabecera mágica 0x07230203", text.contains("0x07230203"))
    }

    @Test
    fun driverManagerStartsWithoutDrivers() {
        val turnip = TurnipDriverManager(File("build/tmp/test_drivers"), NativeDriverInspector)
        assertTrue(turnip.listDrivers().isEmpty())
    }
}
