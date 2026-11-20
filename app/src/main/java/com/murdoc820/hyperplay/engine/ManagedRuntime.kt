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
