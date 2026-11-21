package com.murdoc820.hyperplay.engine

import android.content.Context
import java.io.File
import java.io.RandomAccessFile
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest
import java.util.zip.ZipInputStream

/**
 * Juegos .NET (Celeste: .NET Framework 4 + XNA). El .exe solo trae IL: se ejecuta con Mono x64
 * para Windows (traducido por el JIT, como el Mono de Unity) y XNA se sustituye por FNA.
 * Ver cpp/loader/ManagedHost.h y tools/managed-runtime.
 *
 * El runtime (~33 MB) se descarga la primera vez que se abre un juego .NET desde la Release de
 * GitHub [RUNTIME_TAG], se comprueba su SHA-256 y se descomprime en la carpeta privada de la app.
 * Una copia manual en [LEGACY_DIR] (desarrollo) también vale. El FMOD real x64 del usuario
 * (fmod64.dll, fmodstudio64.dll en /sdcard/HyperPlay/fmod) sustituye al shim mudo.
 */
object ManagedRuntime {
    const val RUNTIME_VERSION = 1
    const val RUNTIME_TAG = "runtime-mono-x64-1"
    const val RUNTIME_ZIP = "hyperplay-runtime-mono-x64-1.zip"
    const val RUNTIME_URL = "https://github.com/cespinosag1992-ui/HyperPlay/releases/download/$RUNTIME_TAG/$RUNTIME_ZIP"
    const val RUNTIME_SHA256 = "84fa30600a8b12942ace289746b42397ddb38085cf3c63b8b8462fff02f027b2"

    /** Copia manual (desarrollo) y carpeta para DLL x64 opcionales del usuario (FMOD real). */
    const val LEGACY_DIR = "/storage/emulated/0/HyperPlay/runtime/mono"

    // Carpeta externa de la app (Android/data/<paquete>/files): como Windows, no distingue
    // mayúsculas (Mono pide "VCRUNTIME140.dll" y el archivo es vcruntime140.dll); la interna (ext4)
    // sí las distingue y las importaciones fallaban. Se borra al desinstalar la app.
    private fun privateDir(context: Context) =
        File(context.getExternalFilesDir(null) ?: context.filesDir, "runtime/mono")
    private fun marker(dir: File) = File(dir, ".hyperplay-runtime-$RUNTIME_VERSION")

    /** Carpeta del runtime que se usará: la privada instalada o, si no, la copia manual. */
    fun runtimeDir(context: Context): File {
        val priv = privateDir(context)
        if (isInstalled(priv) && marker(priv).isFile) return priv
        val legacy = File(LEGACY_DIR)
        return if (isInstalled(legacy)) legacy else priv
    }

    fun isInstalled(dir: File): Boolean =
        File(dir, "bin/mono.exe").isFile && File(dir, "bin/mono-2.0-sgen.dll").isFile &&
            File(dir, "lib/mono/4.5/mscorlib.dll").isFile

    /**
     * Devuelve el runtime listo, descargándolo si falta. Bloqueante (red y disco): llamar fuera
     * del hilo de UI. [progress] recibe 0..100 (descarga) y -1 al descomprimir.
     */
    fun ensureInstalled(context: Context, progress: (Int) -> Unit = {}): File {
        val current = runtimeDir(context)
        if (isInstalled(current) && (current.path == LEGACY_DIR || marker(current).isFile)) return current
        val dest = privateDir(context)
        val zip = File(context.cacheDir, RUNTIME_ZIP)
        download(RUNTIME_URL, zip, progress)
        val sha = sha256(zip)
        check(sha == RUNTIME_SHA256) {
            zip.delete()
            "Runtime .NET dañado (SHA-256 $sha) / .NET runtime corrupted"
        }
        progress(-1)
        val tmp = File(dest.parentFile, "mono.tmp")
        tmp.deleteRecursively()
        unzip(zip, tmp.parentFile!!, "mono/", tmp)
        dest.deleteRecursively()
        check(tmp.renameTo(dest)) { "No se pudo instalar el runtime en ${dest.absolutePath}" }
        marker(dest).writeText(RUNTIME_TAG)
        zip.delete()
        check(isInstalled(dest)) { "Runtime .NET incompleto tras descomprimir / .NET runtime incomplete" }
        return dest
    }

    private fun download(url: String, out: File, progress: (Int) -> Unit) {
        var conn = URL(url).openConnection() as HttpURLConnection
        var redirects = 0
        while (true) {
            conn.instanceFollowRedirects = false
            conn.connectTimeout = 15_000
            conn.readTimeout = 30_000
            conn.setRequestProperty("User-Agent", "HyperPlay")
            val code = conn.responseCode
            if (code in 301..308 && redirects++ < 5) {
                val next = conn.getHeaderField("Location") ?: break
                conn.disconnect()
                conn = URL(URL(url), next).openConnection() as HttpURLConnection
                continue
            }
            check(code == 200) { "Descarga del runtime .NET: HTTP $code" }
            break
        }
        val total = conn.contentLengthLong
        out.parentFile?.mkdirs()
        conn.inputStream.use { input ->
            out.outputStream().use { output ->
                val buf = ByteArray(1 shl 16)
                var done = 0L
                var last = -1
                while (true) {
                    val n = input.read(buf)
                    if (n < 0) break
                    output.write(buf, 0, n)
                    done += n
                    val pct = if (total > 0) (done * 100 / total).toInt() else 0
                    if (pct != last) { last = pct; progress(pct) }
                }
            }
        }
        conn.disconnect()
    }

    private fun sha256(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        f.inputStream().use { input ->
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = input.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    /** Descomprime las entradas bajo [prefix] en [target] (sin salir de él: nada de "../"). */
    private fun unzip(zip: File, base: File, prefix: String, target: File) {
        val root = target.canonicalFile
        ZipInputStream(zip.inputStream().buffered()).use { zin ->
            while (true) {
                val e = zin.nextEntry ?: break
                val name = e.name.replace('\\', '/')
                if (!name.startsWith(prefix)) continue
                val out = File(root, name.removePrefix(prefix)).canonicalFile
                check(out.path.startsWith(root.path)) { "Entrada de zip fuera del runtime: $name" }
                if (e.isDirectory) { out.mkdirs(); continue }
                out.parentFile?.mkdirs()
                out.outputStream().use { zin.copyTo(it) }
            }
        }
        check(base.isDirectory)
    }

    /**
     * ¿Es un ensamblado .NET puro (solo IL)? Hace falta cabecera CLR (DataDirectory[14]) y la marca
     * COMIMAGE_FLAGS_ILONLY: los ejecutables mixtos C++/CLI (DmC, Unreal Engine 3) también tienen
     * cabecera CLR pero son código nativo y van por el cargador PE normal.
     */
    fun isManagedExe(file: File): Boolean = runCatching {
        RandomAccessFile(file, "r").use { f ->
            val head = ByteArray(minOf(4096L, f.length()).toInt())
            f.readFully(head)
            val clrOffset = clrHeaderOffset(head) ?: return@use false
            if (clrOffset + 20 > f.length()) return@use false
            val clr = ByteArray(20)
            f.seek(clrOffset)
            f.readFully(clr)
            isIlOnlyClrHeader(clr)
        }
    }.getOrDefault(false)

    /** Versión pura sobre los primeros bytes del archivo: ¿tiene cabecera CLR? */
    fun isManagedPe(head: ByteArray): Boolean = clrDirectory(head) != null

    /** RVA y tamaño de DataDirectory[14] (cabecera CLR), o null. */
    private fun clrDirectory(head: ByteArray): Pair<Long, Long>? {
        fun u16(o: Int) = (head[o].toInt() and 0xFF) or ((head[o + 1].toInt() and 0xFF) shl 8)
        fun u32(o: Int) = (u16(o).toLong()) or (u16(o + 2).toLong() shl 16)
        if (head.size < 0x40 || u16(0) != 0x5A4D) return null
        val pe = u32(0x3C).toInt()
        if (pe < 0 || pe + 24 + 2 > head.size || u32(pe) != 0x00004550L) return null
        val magic = u16(pe + 24)
        val countOff = pe + 24 + if (magic == 0x20B) 108 else 92
        if (countOff + 4 > head.size || u32(countOff) <= 14) return null
        val clr = pe + 24 + (if (magic == 0x20B) 112 else 96) + 14 * 8
        if (clr + 8 > head.size) return null
        val rva = u32(clr)
        val size = u32(clr + 4)
        return if (rva != 0L && size != 0L) rva to size else null
    }

    /** Desplazamiento en el archivo de la cabecera CLR (RVA -> sección), o null. */
    fun clrHeaderOffset(head: ByteArray): Long? {
        val (rva, _) = clrDirectory(head) ?: return null
        fun u16(o: Int) = (head[o].toInt() and 0xFF) or ((head[o + 1].toInt() and 0xFF) shl 8)
        fun u32(o: Int) = (u16(o).toLong()) or (u16(o + 2).toLong() shl 16)
        val pe = u32(0x3C).toInt()
        val sections = u16(pe + 6)
        val optSize = u16(pe + 20)
        var s = pe + 24 + optSize
        repeat(sections) {
            if (s + 40 > head.size) return null
            val vsize = u32(s + 8)
            val va = u32(s + 12)
            val rawSize = u32(s + 16)
            val raw = u32(s + 20)
            if (rva >= va && rva < va + maxOf(vsize, rawSize)) return raw + (rva - va)
            s += 40
        }
        return null
    }

    /** Cabecera CLR (IMAGE_COR20_HEADER): Flags en +16; bit 0 = COMIMAGE_FLAGS_ILONLY. */
    fun isIlOnlyClrHeader(clr: ByteArray): Boolean {
        if (clr.size < 20) return false
        val flags = (clr[16].toInt() and 0xFF) or ((clr[17].toInt() and 0xFF) shl 8)
        return (flags and 0x1) != 0
    }
}
