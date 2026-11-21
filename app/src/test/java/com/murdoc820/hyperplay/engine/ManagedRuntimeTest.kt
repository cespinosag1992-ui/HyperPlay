package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File
import java.nio.file.Files

class ManagedRuntimeTest {
    /**
     * PE mínimo: MZ, e_lfanew = 0x80, "PE\0\0", magic, 16 directorios, directorio CLR (índice 14)
     * y una sección .text (VA 0x2000, datos en el archivo en 0x400). Si [clrFlags] no es null, el
     * archivo lleva la cabecera CLR en la RVA [clrRva] con esos Flags.
     */
    private fun pe(magic: Int, clrRva: Int, clrSize: Int, clrFlags: Int? = null): ByteArray {
        val b = ByteArray(0x1000)
        fun put16(o: Int, v: Int) { b[o] = v.toByte(); b[o + 1] = (v shr 8).toByte() }
        fun put32(o: Int, v: Int) { put16(o, v and 0xFFFF); put16(o + 2, (v ushr 16) and 0xFFFF) }
        val optSize = if (magic == 0x20B) 240 else 224
        put16(0, 0x5A4D)
        put32(0x3C, 0x80)
        put32(0x80, 0x00004550)
        put16(0x80 + 6, 1)                 // 1 sección
        put16(0x80 + 20, optSize)
        put16(0x80 + 24, magic)
        put32(0x80 + 24 + if (magic == 0x20B) 108 else 92, 16)
        val dataDirs = 0x80 + 24 + if (magic == 0x20B) 112 else 96
        put32(dataDirs + 14 * 8, clrRva)
        put32(dataDirs + 14 * 8 + 4, clrSize)
        val sec = 0x80 + 24 + optSize
        put32(sec + 8, 0x1000)             // VirtualSize
        put32(sec + 12, 0x2000)            // VirtualAddress
        put32(sec + 16, 0x800)             // SizeOfRawData
        put32(sec + 20, 0x400)             // PointerToRawData
        if (clrFlags != null) put32(0x400 + (clrRva - 0x2000) + 16, clrFlags)
        return b
    }

    private fun file(bytes: ByteArray): File =
        Files.createTempFile("pe", ".exe").toFile().apply { writeBytes(bytes); deleteOnExit() }

    @Test
    fun detectsClrHeaderInPe32AndPe32Plus() {
        assertTrue(ManagedRuntime.isManagedPe(pe(0x10B, 0x2008, 0x48)))   // Celeste.exe (PE32, IL)
        assertTrue(ManagedRuntime.isManagedPe(pe(0x20B, 0x2008, 0x48)))
        assertFalse(ManagedRuntime.isManagedPe(pe(0x20B, 0, 0)))           // nativo (Hollow Knight)
        assertFalse(ManagedRuntime.isManagedPe(ByteArray(16)))
        assertFalse(ManagedRuntime.isManagedPe(ByteArray(1024)))           // sin "MZ"
    }

    @Test
    fun mapsClrHeaderToFileOffset() {
        assertEquals(0x408L, ManagedRuntime.clrHeaderOffset(pe(0x10B, 0x2008, 0x48)))
        assertNull(ManagedRuntime.clrHeaderOffset(pe(0x10B, 0x9000, 0x48)))  // fuera de las secciones
    }

    @Test
    fun onlyIlOnlyAssembliesRunOnMono() {
        assertTrue(ManagedRuntime.isManagedExe(file(pe(0x10B, 0x2008, 0x48, clrFlags = 0x1))))   // Celeste
        assertTrue(ManagedRuntime.isManagedExe(file(pe(0x10B, 0x2008, 0x48, clrFlags = 0x3))))   // IL + 32BITREQUIRED
        assertFalse(ManagedRuntime.isManagedExe(file(pe(0x10B, 0x2008, 0x48, clrFlags = 0x10)))) // mixto C++/CLI (DmC)
        assertFalse(ManagedRuntime.isManagedExe(file(pe(0x20B, 0, 0))))                          // nativo
    }
}
