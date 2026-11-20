package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ManagedRuntimeTest {
    /** Cabecera PE mínima: MZ, e_lfanew = 0x80, "PE\0\0", magic y directorio CLR (índice 14). */
    private fun pe(magic: Int, clrRva: Int, clrSize: Int): ByteArray {
        val b = ByteArray(1024)
        fun put16(o: Int, v: Int) { b[o] = v.toByte(); b[o + 1] = (v shr 8).toByte() }
        fun put32(o: Int, v: Int) { put16(o, v and 0xFFFF); put16(o + 2, (v ushr 16) and 0xFFFF) }
        put16(0, 0x5A4D)
        put32(0x3C, 0x80)
        put32(0x80, 0x00004550)
        put16(0x80 + 24, magic)
        val dataDirs = 0x80 + 24 + if (magic == 0x20B) 112 else 96
        put32(dataDirs + 14 * 8, clrRva)
        put32(dataDirs + 14 * 8 + 4, clrSize)
        return b
    }

    @Test
    fun detectsClrHeaderInPe32AndPe32Plus() {
        assertTrue(ManagedRuntime.isManagedPe(pe(0x10B, 0x2008, 0x48)))   // Celeste.exe (PE32, IL)
        assertTrue(ManagedRuntime.isManagedPe(pe(0x20B, 0x2008, 0x48)))
        assertFalse(ManagedRuntime.isManagedPe(pe(0x20B, 0, 0)))           // nativo (Hollow Knight)
        assertFalse(ManagedRuntime.isManagedPe(ByteArray(16)))
        assertFalse(ManagedRuntime.isManagedPe(ByteArray(1024)))           // sin "MZ"
    }
}
