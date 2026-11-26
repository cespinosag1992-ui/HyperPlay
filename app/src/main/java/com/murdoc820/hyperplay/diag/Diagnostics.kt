package com.murdoc820.hyperplay.diag

import android.content.Context
import android.content.SharedPreferences
import android.os.Build
import android.util.Log
import com.murdoc820.hyperplay.BuildConfig
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.File
import java.io.PrintWriter
import java.io.StringWriter
import java.net.HttpURLConnection
import java.net.URL
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.UUID

/**
 * Registro local y envío de informes de errores.
 *
 * En el teléfono (siempre, en `files/diag/`):
 *  - `hyperplay.log`: registro de la app (rotativo, 2 × 512 KB; se vacía al enviarse un informe);
 *  - `reports/` (un .json por informe): pendientes de enviar (crash de la app, cierre inesperado de una
 *    sesión de juego, juego que terminó con error); como mucho 20 y de menos de 30 días
 *    ([LogStorage]). Los logs de sesión de juego (`files/logs/`) guardan solo los 5 últimos.
 *
 * Envío: si el usuario lo aceptó y la app se compiló con [BuildConfig.REPORT_URL] (el Cloudflare
 * Worker de `server/report-worker`, que crea un Issue en GitHub), los informes se mandan al abrir
 * la app y se borran al confirmarse. Sin URL o sin red quedan guardados en el teléfono.
 */
object Diagnostics {
    private const val TAG = "HyperPlayDiag"
    private const val LOG_MAX = 512 * 1024
    private const val MAX_FIELD = 60_000
    private const val PREFS = "diagnostics"
    private const val KEY_ENABLED = "reports_enabled"
    private const val KEY_ASKED = "reports_asked"

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val lock = Any()
    private lateinit var dir: File
    private lateinit var prefs: SharedPreferences
    private val _pending = MutableStateFlow(0)

    /** Informes guardados en el teléfono que aún no se enviaron. */
    val pending: StateFlow<Int> = _pending.asStateFlow()

    private val logFile get() = File(dir, "hyperplay.log")
    private val reportsDir get() = File(dir, "reports").apply { mkdirs() }
    private val sessionMarker get() = File(dir, "session.json")

    fun init(context: Context) {
        if (::dir.isInitialized) return
        dir = File(context.applicationContext.filesDir, "diag").apply { mkdirs() }
        prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        val previous = Thread.getDefaultUncaughtExceptionHandler()
        Thread.setDefaultUncaughtExceptionHandler { thread, error ->
            runCatching { recordCrash(thread, error) }
            previous?.uncaughtException(thread, error)
        }
        log("I", "app", "inicio ${BuildConfig.VERSION_NAME} (${BuildConfig.VERSION_CODE}) en ${deviceLine()}")
        // Solo el proceso del juego (":game") abre sesiones: el launcher no debe tomar por cierre
        // inesperado la sesión de un juego que sigue vivo en el otro proceso.
        if (com.murdoc820.hyperplay.session.GameProcess.isGameProcess(context)) checkUncleanSession()
        refreshPending()
    }

    // ---------------------------------------------------------------- consentimiento

    /** null = aún no se preguntó al usuario. */
    val reportsEnabled: Boolean?
        get() = if (!::prefs.isInitialized || !prefs.getBoolean(KEY_ASKED, false)) null
        else prefs.getBoolean(KEY_ENABLED, false)

    fun setReportsEnabled(enabled: Boolean) {
        prefs.edit().putBoolean(KEY_ASKED, true).putBoolean(KEY_ENABLED, enabled).apply()
        log("I", "app", "informes de errores ${if (enabled) "activados" else "desactivados"}")
    }

    val uploadConfigured: Boolean get() = BuildConfig.REPORT_URL.isNotBlank()

    // ---------------------------------------------------------------- registro local

    fun log(level: String, tag: String, message: String) {
        if (!::dir.isInitialized) return
        val line = "${stamp()} $level/$tag: $message\n"
        synchronized(lock) {
            runCatching {
                val f = logFile
                if (f.length() > LOG_MAX) {
                    val old = File(dir, "hyperplay.1.log")
                    old.delete()
                    f.renameTo(old)
                }
                f.appendText(line)
            }
        }
    }

    fun i(tag: String, message: String) { Log.i(tag, message); log("I", tag, message) }
    fun w(tag: String, message: String) { Log.w(tag, message); log("W", tag, message) }
    fun e(tag: String, message: String, error: Throwable? = null) {
        Log.e(tag, message, error)
        log("E", tag, message + (error?.let { "\n" + stack(it) } ?: ""))
    }

    // ---------------------------------------------------------------- sesiones de juego

    /** Al empezar una sesión: si el proceso muere sin [sessionEnded], el próximo inicio lo informa. */
    fun sessionStarted(exePath: String, gameLog: File?) {
        if (!::dir.isInitialized) return
        val j = JSONObject().put("exe", exePath).put("log", gameLog?.absolutePath.orEmpty())
            .put("start", System.currentTimeMillis()).put("version", BuildConfig.VERSION_NAME)
        runCatching { sessionMarker.writeText(j.toString()) }
        log("I", "sesion", "inicio: $exePath")
    }

    /** Fin de una sesión. [exitCode] != 0 o [error] crean un informe con el log del juego. */
    fun sessionEnded(exePath: String, gameLog: File?, exitCode: Int, error: String?) {
        if (!::dir.isInitialized) return
        runCatching { sessionMarker.delete() }
        log("I", "sesion", "fin: $exePath código=$exitCode${error?.let { " error=$it" } ?: ""}")
        if (exitCode != 0 || error != null) {
            queueReport(
                kind = "juego",
                title = "${File(exePath).parentFile?.name ?: exePath}: exit code / código $exitCode",
                details = buildString {
                    appendLine("Juego: $exePath")
                    appendLine("Código de salida: $exitCode")
                    error?.let { appendLine("Error: $it") }
                },
                gameLog = gameLog,
            )
        }
    }

    private fun checkUncleanSession() {
        val marker = sessionMarker
        if (!marker.isFile) return
        val j = runCatching { JSONObject(marker.readText()) }.getOrNull()
        marker.delete()
        val exe = j?.optString("exe").orEmpty()
        val log = j?.optString("log")?.takeIf { it.isNotBlank() }?.let(::File)
        val minutes = j?.optLong("start")?.let { (System.currentTimeMillis() - it) / 60000 } ?: -1
        queueReport(
            kind = "cierre",
            title = "${File(exe).parentFile?.name ?: "game"}: unexpected session end / cierre inesperado",
            details = buildString {
                appendLine("La sesión de juego terminó sin cerrarse (crash nativo, falta de memoria o el sistema mató la app).")
                appendLine("Juego: $exe")
                appendLine("Versión de la app en la sesión: ${j?.optString("version")}")
                appendLine("Inicio de la sesión: hace ~$minutes min")
            },
            gameLog = log,
        )
    }

    // ---------------------------------------------------------------- informes

    private fun recordCrash(thread: Thread, error: Throwable) {
        log("E", "crash", "excepción no capturada en ${thread.name}\n${stack(error)}")
        queueReport(
            kind = "crash",
            title = "App crash / Crash de la app: ${error.javaClass.simpleName}: ${error.message.orEmpty().take(80)}",
            details = "Thread / Hilo: ${thread.name}\n\n${stack(error)}",
            gameLog = null,
            synchronous = true,
        )
    }

    /** Guarda un informe en el teléfono (y lo intenta enviar si está permitido). */
    fun queueReport(kind: String, title: String, details: String, gameLog: File?, synchronous: Boolean = false) {
        if (!::dir.isInitialized) return
        val work = {
            val id = UUID.randomUUID().toString()
            val json = JSONObject()
                .put("id", id)
                .put("kind", kind)
                .put("title", title.take(200))
                .put("appVersion", BuildConfig.VERSION_NAME)
                .put("appVersionCode", BuildConfig.VERSION_CODE)
                .put("device", deviceLine())
                .put("android", "Android ${Build.VERSION.RELEASE} (API ${Build.VERSION.SDK_INT})")
                .put("time", stamp())
                .put("details", details.take(MAX_FIELD))
                .put("gameLog", gameLog?.let { tail(it, MAX_FIELD) }.orEmpty())
                .put("appLog", tail(logFile, 20_000))
                .put("logcat", if (synchronous) "" else logcatTail())
            runCatching { File(reportsDir, "$kind-${System.currentTimeMillis()}-$id.json").writeText(json.toString()) }
            runCatching { LogStorage.pruneReports(reportsDir) }
            refreshPending()
        }
        if (synchronous) work() else scope.launch { work(); uploadPending() }
    }

    /** Envía los informes pendientes (si el usuario lo aceptó y hay servidor configurado). */
    fun uploadPending() {
        if (!::dir.isInitialized || reportsEnabled != true || !uploadConfigured) return
        scope.launch {
            val files = reportsDir.listFiles { f -> f.extension == "json" }?.sortedBy { it.name } ?: return@launch
            var sent = 0
            for (f in files.take(10)) {
                // Los informes acaban en GitHub: se quitan IPs, rutas de usuario, cuentas, etc.
                val ok = runCatching { post(ReportRedactor.redactReport(f.readText())) }.getOrDefault(false)
                if (ok) { f.delete(); sent++ } else break
            }
            // Lo enviado ya está en el servidor: el registro de la app empieza de nuevo.
            if (sent > 0) clearAppLog(sent)
            refreshPending()
        }
    }

    private fun post(body: String): Boolean {
        val c = (URL(BuildConfig.REPORT_URL).openConnection() as HttpURLConnection).apply {
            requestMethod = "POST"
            doOutput = true
            connectTimeout = 10_000
            readTimeout = 20_000
            setRequestProperty("Content-Type", "application/json; charset=utf-8")
            setRequestProperty("User-Agent", "HyperPlay/${BuildConfig.VERSION_NAME}")
        }
        return try {
            c.outputStream.use { it.write(body.toByteArray(Charsets.UTF_8)) }
            val code = c.responseCode
            if (code !in 200..299) Log.w(TAG, "informe rechazado: HTTP $code")
            code in 200..299
        } finally {
            c.disconnect()
        }
    }

    private fun clearAppLog(sent: Int) {
        synchronized(lock) {
            runCatching {
                File(dir, "hyperplay.1.log").delete()
                logFile.writeText("")
            }
        }
        log("I", "app", "$sent informe(s) enviados; registro reiniciado")
    }

    private fun refreshPending() {
        if (!::dir.isInitialized) return
        _pending.value = reportsDir.listFiles { f -> f.extension == "json" }?.size ?: 0
    }

    // ---------------------------------------------------------------- utilidades

    private fun stamp(): String = SimpleDateFormat("yyyy-MM-dd HH:mm:ss.SSS", Locale.US).format(Date())

    private fun deviceLine() = "${Build.MANUFACTURER} ${Build.MODEL} (${Build.SOC_MODEL})"

    private fun stack(t: Throwable): String = StringWriter().also { t.printStackTrace(PrintWriter(it)) }.toString()

    private fun tail(f: File, max: Int): String {
        if (!f.isFile) return ""
        return runCatching {
            val bytes = f.readBytes()
            val start = (bytes.size - max).coerceAtLeast(0)
            String(bytes, start, bytes.size - start, Charsets.UTF_8)
        }.getOrDefault("")
    }

    /** Últimas líneas del logcat de esta app (Android solo deja leer las del propio proceso/uid). */
    private fun logcatTail(): String = runCatching {
        val p = ProcessBuilder("logcat", "-d", "-t", "1500", "-v", "time").redirectErrorStream(true).start()
        val text = p.inputStream.bufferedReader().use { it.readText() }
        p.waitFor()
        text.takeLast(MAX_FIELD)
    }.getOrDefault("")
}
