package com.murdoc820.hyperplay.engine

import java.io.File
import java.io.RandomAccessFile

/**
 * Juegos .NET (Celeste: .NET Framework 4 + XNA). El .exe solo trae IL: se ejecuta con Mono x64
 * para Windows (traducido por el JIT, como el Mono de Unity) y XNA se sustituye por FNA.
 * Ver cpp/loader/ManagedHost.h.
 */
object ManagedRuntime {
    /** Runtime Mono (bin/mono.exe, lib/mono/4.5, etc/, fna/). Por ahora se copia a mano aquí. */
    const val DEFAULT_DIR = "/storage/emulated/0/HyperPlay/runtime/mono"

    fun runtimeDir(): File = File(DEFAULT_DIR)

    fun isInstalled(dir: File = runtimeDir()): Boolean =
        File(dir, "bin/mono.exe").isFile && File(dir, "bin/mono-2.0-sgen.dll").isFile &&
            File(dir, "lib/mono/4.5/mscorlib.dll").isFile

    /** ¿El PE tiene cabecera CLR (DataDirectory[14])? Es decir, ¿es un ensamblado .NET? */
    fun isManagedExe(file: File): Boolean = runCatching {
        RandomAccessFile(file, "r").use { f ->
            val b = ByteArray(1024)
            f.readFully(b, 0, minOf(1024L, f.length()).toInt())
            isManagedPe(b)
        }
    }.getOrDefault(false)

    /** Versión pura (probada en la JVM) sobre los primeros bytes del archivo. */
    fun isManagedPe(head: ByteArray): Boolean {
        fun u16(o: Int) = (head[o].toInt() and 0xFF) or ((head[o + 1].toInt() and 0xFF) shl 8)
        fun u32(o: Int) = u16(o) or (u16(o + 2) shl 16)
        if (head.size < 0x40 || u16(0) != 0x5A4D) return false
        val pe = u32(0x3C)
        if (pe < 0 || pe + 24 + 2 > head.size || u32(pe) != 0x00004550) return false
        val magic = u16(pe + 24)
        val dataDirs = pe + 24 + if (magic == 0x20B) 112 else 96
        val clr = dataDirs + 14 * 8
        if (clr + 8 > head.size) return false
        return u32(clr) != 0 && u32(clr + 4) != 0
    }
}
