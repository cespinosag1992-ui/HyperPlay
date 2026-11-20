package com.murdoc820.hyperplay.engine

import java.io.BufferedInputStream
import java.io.File
import java.io.FileInputStream
import java.io.FileOutputStream
import java.io.FilterInputStream
import java.io.IOException
import java.io.InputStream
import java.io.InputStreamReader
import java.io.OutputStreamWriter
import java.net.HttpURLConnection
import java.net.URI
import java.net.URISyntaxException
import java.net.URL
import java.nio.file.Files
import java.nio.file.StandardCopyOption
import java.util.Properties
import java.util.UUID
import java.util.zip.ZipException
import java.util.zip.ZipInputStream

/** Resultado del análisis ELF de un `libvulkan_freedreno.so` (ver `gpu/TurnipDriverInspector.h`). */
data class DriverInspectionResult(
    val validElf: Boolean,
    val aarch64: Boolean,
    val bionic: Boolean,
    val hasIcdEntry: Boolean,
    val hasNegotiate: Boolean,
) {
    companion object {
        val INVALID = DriverInspectionResult(false, false, false, false, false)
    }
}

/** Verificación de un driver; en producción [NativeDriverInspector], en tests un falso. */
interface DriverInspector {
    fun inspect(lib: File): DriverInspectionResult

    /** null si `dlopen` + `dlsym(vk_icdGetInstanceProcAddr)` funcionan; si no, el error. */
    fun dlopenCheck(lib: File): String?
}

/**
 * Driver Turnip instalado en `driversDir/<id>/`.
 *
 * @param bionic true si se compiló contra bionic (cargable por `TurnipLoader` en el proceso de la app).
 * @param dlopenOk resultado de `dlopen` en el proceso de la app; null si no aplica (build no bionic).
 */
data class InstalledDriver(
    val id: String,
    val name: String,
    val version: String,
    val libFile: File,
    val bionic: Boolean,
    val dlopenOk: Boolean?,
    val sizeBytes: Long,
)

/** Paquete rechazado: zip inválido o malicioso, sin driver, o driver no utilizable. */
class DriverImportException(message: String, cause: Throwable? = null) : IOException(message, cause)

/**
 * Gestor de drivers Mesa Turnip para Adreno 830 (`a830` / Gen 8).
 *
 * Importa o descarga paquetes `.zip` / `.adpkg` (formato AdrenoTools: zip con `meta.json`),
 * extrae `libvulkan_freedreno.so` a `driversDir/<id>/`, lo verifica con [DriverInspector]
 * (ELF aarch64, bionic, `vk_icdGetInstanceProcAddr`, `dlopen`) y recuerda el driver activo.
 * Sin driver activo se usa el Vulkan del sistema.
 *
 * Todas las operaciones son bloqueantes (E/S de disco o red): llamar fuera del hilo principal.
 *
 * @param driversDir `filesDir/drivers`.
 */
class TurnipDriverManager(
    private val driversDir: File,
    private val inspector: DriverInspector,
    private val maxExtractedBytes: Long = MAX_EXTRACTED_BYTES,
) {
    private val activeFile: File get() = File(driversDir, ACTIVE_FILE)

    /**
     * Importa un paquete `.zip` / `.adpkg`. No cierra [input].
     *
     * @param displayName nombre del archivo original; su extensión decide el formato.
     * @throws IllegalArgumentException si la extensión no es `.zip` ni `.adpkg`.
     * @throws DriverImportException si el paquete es inválido o el driver no sirve.
     */
    fun importPackage(input: InputStream, displayName: String): InstalledDriver {
        val ext = displayName.substringAfterLast('.', "").lowercase()
        require(ext in PACKAGE_EXTENSIONS) {
            "Formato no soportado: '$displayName' (se espera .zip o .adpkg)"
        }
        ensureDir(driversDir)
        val staging = File(driversDir, ".staging-${UUID.randomUUID()}")
        return try {
            ensureDir(staging)
            val entries = extractZip(input, staging)
            if (entries.isEmpty()) {
                throw DriverImportException("'$displayName' está vacío o no es un zip válido")
            }
            val metaPath = entries
                .filter { it.substringAfterLast('/') == META_FILE }
                .minByOrNull { path -> path.count { it == '/' } }
            val meta = metaPath?.let { readMeta(File(staging, it)) }.orEmpty()
            val libPath = locateLibrary(entries, metaPath, meta)
                ?: throw DriverImportException(
                    "'$displayName' no contiene $LIB_NAME ni ninguna biblioteca vulkan.*.so",
                )

            val name = meta["name"]?.trim()?.takeIf { it.isNotEmpty() }
                ?: displayName.substringBeforeLast('.')
            val version = meta["driverVersion"]?.trim().orEmpty()
            val id = uniqueId(sanitizeId(if (version.isEmpty()) name else "$name-$version"))
            val targetDir = File(driversDir, id)
            installLibrary(File(staging, libPath), targetDir, id, name, version)
        } finally {
            staging.deleteRecursively()
        }
    }

    /** Mueve la biblioteca a su carpeta final, la verifica y guarda sus metadatos. */
    private fun installLibrary(
        source: File,
        targetDir: File,
        id: String,
        name: String,
        version: String,
    ): InstalledDriver {
        return try {
            ensureDir(targetDir)
            val lib = File(targetDir, LIB_NAME)
            Files.move(source.toPath(), lib.toPath(), StandardCopyOption.REPLACE_EXISTING)
            lib.setReadable(true, false)
            lib.setExecutable(true, false)

            val result = inspector.inspect(lib)
            val problem = rejectionReason(result)
            if (problem != null) {
                throw DriverImportException("'$name' no es un driver Vulkan utilizable: $problem")
            }
            val dlopenOk = if (result.bionic) inspector.dlopenCheck(lib) == null else null
            val driver = InstalledDriver(
                id = id,
                name = name,
                version = version,
                libFile = lib,
                bionic = result.bionic,
                dlopenOk = dlopenOk,
                sizeBytes = lib.length(),
            )
            writeProperties(targetDir, driver, result.hasNegotiate)
            driver
        } catch (e: Exception) {
            targetDir.deleteRecursively()
            throw e
        }
    }

    /**
     * Descarga un paquete por HTTPS (redirecciones incluidas, todas https) y lo importa.
     *
     * @param onProgress (bytes recibidos, total); total = -1 si el servidor no lo indica.
     */
    fun downloadPackage(url: String, onProgress: (Long, Long) -> Unit): InstalledDriver {
        var current = httpsUrl(url)
        ensureDir(driversDir)
        val tmp = File(driversDir, ".download-${UUID.randomUUID()}.tmp")
        try {
            var redirects = 0
            while (true) {
                val conn = current.openConnection() as HttpURLConnection
                try {
                    conn.instanceFollowRedirects = false
                    conn.connectTimeout = CONNECT_TIMEOUT_MS
                    conn.readTimeout = READ_TIMEOUT_MS
                    conn.setRequestProperty("User-Agent", USER_AGENT)
                    val code = conn.responseCode
                    if (code in REDIRECT_CODES) {
                        val location = conn.getHeaderField("Location")
                            ?: throw IOException("Redirección HTTP $code sin cabecera Location")
                        if (++redirects > MAX_REDIRECTS) throw IOException("Demasiadas redirecciones")
                        current = httpsUrl(resolve(current, location))
                        continue
                    }
                    if (code !in 200..299) throw IOException("HTTP $code al descargar desde ${current.host}")
                    val total = conn.contentLengthLong
                    if (total > maxExtractedBytes) {
                        throw DriverImportException("El paquete ($total bytes) supera el límite de $maxExtractedBytes")
                    }
                    onProgress(0L, total)
                    conn.inputStream.use { input ->
                        FileOutputStream(tmp).use { out ->
                            val buf = ByteArray(BUFFER_SIZE)
                            var done = 0L
                            while (true) {
                                val n = input.read(buf)
                                if (n < 0) break
                                done += n
                                if (done > maxExtractedBytes) {
                                    throw DriverImportException("La descarga supera el límite de $maxExtractedBytes bytes")
                                }
                                out.write(buf, 0, n)
                                onProgress(done, total)
                            }
                        }
                    }
                    break
                } finally {
                    conn.disconnect()
                }
            }
            return FileInputStream(tmp).use { importPackage(it, packageNameFor(current)) }
        } finally {
            tmp.delete()
        }
    }

    fun listDrivers(): List<InstalledDriver> =
        (driversDir.listFiles() ?: emptyArray())
            .filter { it.isDirectory && isValidId(it.name) }
            .mapNotNull { readDriver(it) }
            .sortedBy { it.name.lowercase() }

    fun activeDriver(): InstalledDriver? {
        val id = activeId() ?: return null
        return readDriver(File(driversDir, id))
    }

    /** Persiste el driver activo en `active.txt`; null = driver del sistema ([ACTIVE_SYSTEM]). */
    fun setActive(id: String?) {
        if (id != null) {
            requireValidId(id)
            require(readDriver(File(driversDir, id)) != null) { "No existe el driver '$id'" }
        }
        ensureDir(driversDir)
        val tmp = File(driversDir, "$ACTIVE_FILE.tmp")
        tmp.writeText(id ?: ACTIVE_SYSTEM)
        Files.move(tmp.toPath(), activeFile.toPath(), StandardCopyOption.REPLACE_EXISTING)
    }

    /** Borra el driver; si era el activo, vuelve al driver del sistema. */
    fun delete(id: String): Boolean {
        requireValidId(id)
        if (activeId() == id) setActive(null)
        val dir = File(driversDir, id)
        return dir.exists() && dir.deleteRecursively()
    }

    // ---------------------------------------------------------------------------------------

    private fun activeId(): String? {
        val file = activeFile
        if (!file.isFile) return null
        return file.readText().trim().takeIf { isValidId(it) }
    }

    private fun extractZip(input: InputStream, dest: File): List<String> = try {
        extractZipEntries(input, dest)
    } catch (e: ZipException) {
        // Android 14+ ya rechaza rutas con ".." en ZipInputStream con ZipException.
        throw DriverImportException("Paquete zip inválido o inseguro: ${e.message}", e)
    }

    private fun extractZipEntries(input: InputStream, dest: File): List<String> {
        val files = linkedSetOf<String>()
        val root = dest.canonicalFile
        var total = 0L
        var count = 0
        ZipInputStream(BufferedInputStream(NonClosingInputStream(input))).use { zip ->
            val buf = ByteArray(BUFFER_SIZE)
            while (true) {
                val entry = zip.nextEntry ?: break
                val path = safeEntryPath(entry.name)
                if (entry.isDirectory || path.isEmpty()) continue
                if (++count > MAX_ENTRIES) throw DriverImportException("Demasiadas entradas en el paquete")
                val out = File(dest, path)
                if (!out.canonicalPath.startsWith(root.path + File.separator)) {
                    throw DriverImportException("Entrada fuera del destino (zip-slip): ${entry.name}")
                }
                ensureDir(out.parentFile!!)
                FileOutputStream(out).use { os ->
                    while (true) {
                        val n = zip.read(buf)
                        if (n < 0) break
                        total += n
                        if (total > maxExtractedBytes) {
                            throw DriverImportException("El contenido supera el límite de $maxExtractedBytes bytes")
                        }
                        os.write(buf, 0, n)
                    }
                }
                files += path
            }
        }
        return files.toList()
    }

    private fun locateLibrary(entries: List<String>, metaPath: String?, meta: Map<String, String>): String? {
        val libraryName = meta["libraryName"]?.trim().orEmpty()
        if (libraryName.isNotEmpty()) {
            val base = metaPath?.substringBeforeLast('/', "").orEmpty()
            val candidate = safeEntryPath(if (base.isEmpty()) libraryName else "$base/$libraryName")
            if (candidate in entries) return candidate
            val fileName = candidate.substringAfterLast('/')
            return entries.sorted().firstOrNull { it.substringAfterLast('/') == fileName }
                ?: throw DriverImportException("meta.json declara '$libraryName' pero no está en el paquete")
        }
        val sorted = entries.sorted()
        return sorted.firstOrNull { it.substringAfterLast('/') == LIB_NAME }
            ?: sorted.firstOrNull { VULKAN_LIB.matches(it.substringAfterLast('/')) }
    }

    private fun readMeta(file: File): Map<String, String> {
        if (!file.isFile || file.length() > MAX_META_BYTES) return emptyMap()
        val root = try {
            MiniJson.parse(file.readText())
        } catch (e: IllegalArgumentException) {
            return emptyMap()  // meta.json corrupto: se recurre a la detección por nombre.
        }
        val obj = root as? Map<*, *> ?: return emptyMap()
        val out = mutableMapOf<String, String>()
        for ((k, v) in obj) {
            if (k is String && v is String) out[k] = v
        }
        return out
    }

    private fun readDriver(dir: File): InstalledDriver? {
        val lib = File(dir, LIB_NAME)
        val propsFile = File(dir, PROPS_FILE)
        if (!lib.isFile || !propsFile.isFile) return null
        val props = Properties()
        try {
            InputStreamReader(FileInputStream(propsFile), Charsets.UTF_8).use { props.load(it) }
        } catch (e: IOException) {
            return null
        } catch (e: IllegalArgumentException) {
            return null
        }
        return InstalledDriver(
            id = dir.name,
            name = props.getProperty(KEY_NAME) ?: dir.name,
            version = props.getProperty(KEY_VERSION).orEmpty(),
            libFile = lib,
            bionic = props.getProperty(KEY_BIONIC) == "true",
            dlopenOk = when (props.getProperty(KEY_DLOPEN_OK)) {
                "true" -> true
                "false" -> false
                else -> null
            },
            sizeBytes = lib.length(),
        )
    }

    private fun writeProperties(dir: File, driver: InstalledDriver, hasNegotiate: Boolean) {
        val props = Properties()
        props.setProperty(KEY_NAME, driver.name)
        props.setProperty(KEY_VERSION, driver.version)
        props.setProperty(KEY_BIONIC, driver.bionic.toString())
        props.setProperty(KEY_DLOPEN_OK, driver.dlopenOk?.toString().orEmpty())
        props.setProperty(KEY_NEGOTIATE, hasNegotiate.toString())
        OutputStreamWriter(FileOutputStream(File(dir, PROPS_FILE)), Charsets.UTF_8).use {
            props.store(it, "HyperPlay Turnip driver")
        }
    }

    private fun uniqueId(base: String): String {
        if (!File(driversDir, base).exists()) return base
        var n = 2
        while (File(driversDir, "$base-$n").exists()) n++
        return "$base-$n"
    }

    /** Envoltorio que impide que ZipInputStream cierre el stream del llamador. */
    private class NonClosingInputStream(input: InputStream) : FilterInputStream(input) {
        override fun close() = Unit
    }

    companion object {
        const val LIB_NAME = "libvulkan_freedreno.so"
        const val META_FILE = "meta.json"
        const val ACTIVE_FILE = "active.txt"

        /** Contenido de `active.txt` cuando el usuario eligió el driver del sistema (id inválido a propósito). */
        const val ACTIVE_SYSTEM = "-system"

        const val PROPS_FILE = "driver.properties"
        const val MAX_EXTRACTED_BYTES = 512L * 1024 * 1024

        private const val MAX_ENTRIES = 4096
        private const val MAX_META_BYTES = 1L * 1024 * 1024
        private const val BUFFER_SIZE = 64 * 1024
        private const val CONNECT_TIMEOUT_MS = 15_000
        private const val READ_TIMEOUT_MS = 30_000
        private const val MAX_REDIRECTS = 5
        private const val USER_AGENT = "HyperPlay"
        private val REDIRECT_CODES = setOf(301, 302, 303, 307, 308)
        private val PACKAGE_EXTENSIONS = setOf("zip", "adpkg")
        private val VULKAN_LIB = Regex("^vulkan\\..+\\.so$")
        private val ID_PATTERN = Regex("^[a-z0-9][a-z0-9._-]{0,63}$")
        private val DRIVE_LETTER = Regex("^[A-Za-z]:")

        private const val KEY_NAME = "name"
        private const val KEY_VERSION = "version"
        private const val KEY_BIONIC = "bionic"
        private const val KEY_DLOPEN_OK = "dlopenOk"
        private const val KEY_NEGOTIATE = "hasNegotiate"

        /** Motivo por el que un driver no sirve; null si es aceptable. */
        fun rejectionReason(r: DriverInspectionResult): String? = when {
            !r.validElf -> "no es una biblioteca ELF64 válida"
            !r.aarch64 -> "no está compilado para aarch64"
            !r.hasIcdEntry -> "no exporta vk_icdGetInstanceProcAddr"
            else -> null
        }

        /** Id de carpeta seguro: minúsculas, dígitos, `.`, `_` y `-`. */
        fun sanitizeId(raw: String): String {
            val id = raw.lowercase()
                .replace(Regex("[^a-z0-9._-]+"), "-")
                .replace(Regex("\\.{2,}"), ".")
                .trim('-', '.', '_')
                .take(48)
                .trimEnd('-', '.', '_')
            return id.ifEmpty { "driver" }
        }

        fun isValidId(id: String): Boolean = ID_PATTERN.matches(id) && !id.contains("..")

        /**
         * Normaliza el nombre de una entrada zip. Rechaza rutas absolutas, con letra de
         * unidad o con `..` (zip-slip).
         */
        fun safeEntryPath(raw: String): String {
            val name = raw.replace('\\', '/')
            if (name.startsWith("/") || DRIVE_LETTER.containsMatchIn(name)) {
                throw DriverImportException("Ruta absoluta en el paquete (zip-slip): $raw")
            }
            val parts = name.split('/').filter { it.isNotEmpty() && it != "." }
            if (parts.any { it == ".." }) {
                throw DriverImportException("Ruta con '..' en el paquete (zip-slip): $raw")
            }
            return parts.joinToString("/")
        }

        private fun requireValidId(id: String) {
            require(isValidId(id)) { "Id de driver inválido: '$id'" }
        }

        private fun ensureDir(dir: File) {
            if (!dir.isDirectory && !dir.mkdirs()) throw IOException("No se puede crear $dir")
        }

        private fun httpsUrl(url: String): URL {
            val uri = try {
                URI(url)
            } catch (e: URISyntaxException) {
                throw IllegalArgumentException("URL inválida: $url", e)
            }
            require(uri.scheme.equals("https", ignoreCase = true) && !uri.host.isNullOrEmpty()) {
                "Solo se admiten descargas https:// ($url)"
            }
            return uri.toURL()
        }

        private fun resolve(base: URL, location: String): String = try {
            base.toURI().resolve(location).toString()
        } catch (e: URISyntaxException) {
            throw IOException("Redirección inválida: $location", e)
        } catch (e: IllegalArgumentException) {
            throw IOException("Redirección inválida: $location", e)
        }

        /** Nombre con extensión de paquete para importPackage a partir de la URL final. */
        private fun packageNameFor(url: URL): String {
            val segment = url.path.substringAfterLast('/').ifEmpty { "driver" }
            val ext = segment.substringAfterLast('.', "").lowercase()
            return if (ext in PACKAGE_EXTENSIONS) segment else "$segment.zip"
        }
    }
}

/**
 * Parser JSON mínimo para `meta.json` (org.json no está disponible en tests JVM).
 * Objetos → Map, arrays → List, cadenas → String, números → su texto, true/false/null.
 */
internal object MiniJson {
    private const val MAX_DEPTH = 64

    fun parse(text: String): Any? {
        val p = Parser(text)
        val v = p.value(0)
        p.ws()
        if (!p.end()) p.fail()
        return v
    }

    private class Parser(private val s: String) {
        private var i = 0

        fun end() = i >= s.length

        fun fail(): Nothing = throw IllegalArgumentException("JSON inválido en la posición $i")

        fun ws() {
            while (i < s.length && s[i] in " \t\r\n\uFEFF") i++
        }

        private fun peek(): Char = if (end()) fail() else s[i]

        private fun expect(c: Char) {
            if (peek() != c) fail()
            i++
        }

        fun value(depth: Int): Any? {
            if (depth > MAX_DEPTH) fail()
            ws()
            return when (val c = peek()) {
                '{' -> obj(depth)
                '[' -> arr(depth)
                '"' -> str()
                't' -> literal("true", true)
                'f' -> literal("false", false)
                'n' -> literal("null", null)
                else -> if (c == '-' || c in '0'..'9') num() else fail()
            }
        }

        private fun literal(word: String, v: Any?): Any? {
            if (!s.startsWith(word, i)) fail()
            i += word.length
            return v
        }

        private fun num(): String {
            val start = i
            while (i < s.length && s[i] in "+-0123456789.eE") i++
            val raw = s.substring(start, i)
            if (raw.toDoubleOrNull() == null) fail()
            return raw
        }

        private fun str(): String {
            expect('"')
            val sb = StringBuilder()
            while (true) {
                val c = peek()
                i++
                when (c) {
                    '"' -> return sb.toString()
                    '\\' -> {
                        val e = peek()
                        i++
                        when (e) {
                            '"', '\\', '/' -> sb.append(e)
                            'b' -> sb.append('\b')
                            'f' -> sb.append('\u000C')
                            'n' -> sb.append('\n')
                            'r' -> sb.append('\r')
                            't' -> sb.append('\t')
                            'u' -> {
                                if (i + 4 > s.length) fail()
                                val hex = s.substring(i, i + 4)
                                if (!hex.all { it in '0'..'9' || it in 'a'..'f' || it in 'A'..'F' }) fail()
                                val code = hex.toInt(16)
                                sb.append(code.toChar())
                                i += 4
                            }
                            else -> fail()
                        }
                    }
                    else -> sb.append(c)
                }
            }
        }

        private fun arr(depth: Int): List<Any?> {
            expect('[')
            val list = mutableListOf<Any?>()
            ws()
            if (peek() == ']') {
                i++
                return list
            }
            while (true) {
                list += value(depth + 1)
                ws()
                when (peek()) {
                    ',' -> i++
                    ']' -> {
                        i++
                        return list
                    }
                    else -> fail()
                }
            }
        }

        private fun obj(depth: Int): Map<String, Any?> {
            expect('{')
            val map = LinkedHashMap<String, Any?>()
            ws()
            if (peek() == '}') {
                i++
                return map
            }
            while (true) {
                ws()
                val key = str()
                ws()
                expect(':')
                map[key] = value(depth + 1)
                ws()
                when (peek()) {
                    ',' -> i++
                    '}' -> {
                        i++
                        return map
                    }
                    else -> fail()
                }
            }
        }
    }
}
