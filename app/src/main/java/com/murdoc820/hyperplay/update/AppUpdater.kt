package com.murdoc820.hyperplay.update

import android.app.PendingIntent
import android.content.BroadcastReceiver
import android.content.Context
import com.murdoc820.hyperplay.R
import android.content.Intent
import android.content.pm.PackageInstaller
import android.net.Uri
import android.provider.Settings
import com.murdoc820.hyperplay.BuildConfig
import com.murdoc820.hyperplay.diag.Diagnostics
import com.murdoc820.hyperplay.session.GameProcess
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.security.MessageDigest

/** Una versión publicada en GitHub Releases. */
data class ReleaseInfo(
    val version: String,
    val notes: String,
    val apkUrl: String,
    val apkSize: Long,
    val sha256Url: String?,
    val pageUrl: String,
)

sealed interface UpdateState {
    data object Idle : UpdateState
    data object Checking : UpdateState
    data object UpToDate : UpdateState
    data class Available(val release: ReleaseInfo) : UpdateState
    data class Downloading(val release: ReleaseInfo, val progress: Float) : UpdateState
    data class Installing(val release: ReleaseInfo) : UpdateState
    data class Error(val message: String, val release: ReleaseInfo? = null) : UpdateState
}

/**
 * Actualizaciones desde GitHub Releases (repo público: sin tokens).
 *  1. `releases/latest` → versión (etiqueta vX.Y.Z) y el `.apk` adjunto (+ `.apk.sha256`).
 *  2. Descarga a `files/updates/` y comprueba el SHA-256 publicado.
 *  3. Instala con PackageInstaller (MODE_FULL_INSTALL sobre el mismo paquete y la misma firma):
 *     se conservan datos, ajustes, arte y partidas. La caché del traductor de cada juego (en su
 *     carpeta `_HyperPlaySaves/hypercache`) va ligada al build: con una versión nueva se rehace
 *     sola en el primer arranque, nunca se mezcla código de versiones distintas.
 * No instala mientras hay un juego en pausa en memoria (Quick Resume): pide cerrarlo antes.
 */
object AppUpdater {
    private const val TAG = "HyperPlayUpdate"
    private const val CHECK_INTERVAL_MS = 6L * 3600 * 1000
    private const val ACTION_STATUS = "com.murdoc820.hyperplay.UPDATE_STATUS"

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow<UpdateState>(UpdateState.Idle)
    val state: StateFlow<UpdateState> = _state.asStateFlow()
    private var lastCheck = 0L

    val currentVersion: String get() = BuildConfig.VERSION_NAME

    /** Comprueba al abrir el launcher (como mucho cada [CHECK_INTERVAL_MS]). */
    fun checkIfDue(context: Context) {
        val now = System.currentTimeMillis()
        if (now - lastCheck < CHECK_INTERVAL_MS) return
        check(context)
    }

    fun check(context: Context) {
        if (_state.value is UpdateState.Checking || _state.value is UpdateState.Downloading) return
        lastCheck = System.currentTimeMillis()
        _state.value = UpdateState.Checking
        scope.launch {
            _state.value = runCatching { fetchLatest() }.fold(
                onSuccess = { rel ->
                    when {
                        rel == null -> UpdateState.UpToDate
                        isNewer(rel.version, currentVersion) -> UpdateState.Available(rel)
                        else -> UpdateState.UpToDate
                    }
                },
                onFailure = { UpdateState.Error(context.getString(R.string.update_err_check, it.message ?: context.getString(R.string.update_err_offline))) },
            )
            Diagnostics.log("I", "update", "comprobación: ${_state.value}")
        }
    }

    /** Descarga, verifica e instala [release]. */
    fun downloadAndInstall(context: Context, release: ReleaseInfo) {
        val app = context.applicationContext
        if (GameProcess.refresh(app) != null) {
            _state.value = UpdateState.Error(context.getString(R.string.update_err_held), release)
            return
        }
        if (!app.packageManager.canRequestPackageInstalls()) {
            _state.value = UpdateState.Error(context.getString(R.string.update_err_permission), release)
            app.startActivity(Intent(Settings.ACTION_MANAGE_UNKNOWN_APP_SOURCES, Uri.parse("package:${app.packageName}"))
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK))
            return
        }
        _state.value = UpdateState.Downloading(release, 0f)
        scope.launch {
            try {
                val dir = File(app.filesDir, "updates").apply { mkdirs() }
                dir.listFiles()?.forEach { it.delete() }
                val apk = File(dir, "HyperPlay-${release.version}.apk")
                download(release.apkUrl, apk, release.apkSize) { p -> _state.value = UpdateState.Downloading(release, p) }
                release.sha256Url?.let { url ->
                    val expected = httpText(url)?.trim()?.split(Regex("\\s+"))?.firstOrNull()?.lowercase()
                    val actual = sha256(apk)
                    if (expected != null && expected.length == 64 && expected != actual) {
                        apk.delete()
                        error("el archivo descargado no coincide (SHA-256)")
                    }
                }
                _state.value = UpdateState.Installing(release)
                Diagnostics.log("I", "update", "instalando ${release.version} (${apk.length()} B)")
                install(app, apk)
            } catch (e: Exception) {
                Diagnostics.log("E", "update", "fallo: ${e.message}")
                _state.value = UpdateState.Error(context.getString(R.string.update_err_failed, e.message.orEmpty()), release)
            }
        }
    }

    /** Resultado de PackageInstaller (receptor del manifiesto). */
    class StatusReceiver : BroadcastReceiver() {
        override fun onReceive(context: Context, intent: Intent) {
            when (val status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE)) {
                PackageInstaller.STATUS_PENDING_USER_ACTION -> {
                    @Suppress("DEPRECATION")
                    val confirm = intent.getParcelableExtra<Intent>(Intent.EXTRA_INTENT)
                    confirm?.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)?.let(context::startActivity)
                }
                PackageInstaller.STATUS_SUCCESS -> Diagnostics.log("I", "update", "instalada")
                else -> {
                    val msg = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE) ?: context.getString(R.string.update_err_code, status)
                    Diagnostics.log("E", "update", "instalación fallida: $msg")
                    val rel = (_state.value as? UpdateState.Installing)?.release
                    _state.value = UpdateState.Error(context.getString(R.string.update_err_install, msg), rel)
                }
            }
        }
    }

    // ---------------------------------------------------------------- internos

    private fun fetchLatest(): ReleaseInfo? {
        val text = httpText("https://api.github.com/repos/${BuildConfig.GITHUB_REPO}/releases/latest") ?: return null
        val j = JSONObject(text)
        val tag = j.optString("tag_name").removePrefix("v").removePrefix("V")
        val assets = j.optJSONArray("assets") ?: return null
        var apk: JSONObject? = null
        var sha: String? = null
        for (i in 0 until assets.length()) {
            val a = assets.getJSONObject(i)
            val name = a.optString("name")
            if (name.endsWith(".apk")) apk = a
            if (name.endsWith(".apk.sha256")) sha = a.optString("browser_download_url")
        }
        val a = apk ?: return null
        return ReleaseInfo(
            version = tag,
            notes = j.optString("body").take(4000),
            apkUrl = a.optString("browser_download_url"),
            apkSize = a.optLong("size"),
            sha256Url = sha,
            pageUrl = j.optString("html_url"),
        )
    }

    /** "1.10.0" > "1.9.3"; ignora sufijos (-beta…) salvo para desempatar. */
    internal fun isNewer(candidate: String, current: String): Boolean {
        fun parts(v: String) = v.substringBefore('-').split('.').map { it.toIntOrNull() ?: 0 }
        val a = parts(candidate)
        val b = parts(current)
        for (i in 0 until maxOf(a.size, b.size)) {
            val x = a.getOrElse(i) { 0 }
            val y = b.getOrElse(i) { 0 }
            if (x != y) return x > y
        }
        return false
    }

    private fun open(url: String) = (URL(url).openConnection() as HttpURLConnection).apply {
        connectTimeout = 10_000
        readTimeout = 30_000
        instanceFollowRedirects = true
        setRequestProperty("Accept", "application/vnd.github+json")
        setRequestProperty("User-Agent", "HyperPlay/${BuildConfig.VERSION_NAME}")
    }

    private fun httpText(url: String): String? {
        val c = open(url)
        return try {
            if (c.responseCode != 200) null else c.inputStream.bufferedReader().use { it.readText() }
        } finally {
            c.disconnect()
        }
    }

    private fun download(url: String, target: File, expectedSize: Long, progress: (Float) -> Unit) {
        val c = open(url)
        try {
            check(c.responseCode == 200) { "HTTP ${c.responseCode}" }
            val total = if (c.contentLengthLong > 0) c.contentLengthLong else expectedSize
            val tmp = File(target.parentFile, target.name + ".part")
            c.inputStream.use { inp ->
                tmp.outputStream().use { out ->
                    val buf = ByteArray(256 * 1024)
                    var done = 0L
                    var lastPct = -1
                    while (true) {
                        val n = inp.read(buf)
                        if (n < 0) break
                        out.write(buf, 0, n)
                        done += n
                        if (total > 0) {
                            val pct = (done * 100 / total).toInt()
                            if (pct != lastPct) { lastPct = pct; progress(done.toFloat() / total) }
                        }
                    }
                }
            }
            check(expectedSize <= 0 || tmp.length() == expectedSize) { "descarga incompleta" }
            check(tmp.renameTo(target)) { "no se pudo guardar la descarga" }
        } finally {
            c.disconnect()
        }
    }

    private fun sha256(f: File): String {
        val md = MessageDigest.getInstance("SHA-256")
        f.inputStream().use { inp ->
            val buf = ByteArray(1 shl 16)
            while (true) {
                val n = inp.read(buf)
                if (n < 0) break
                md.update(buf, 0, n)
            }
        }
        return md.digest().joinToString("") { "%02x".format(it) }
    }

    private fun install(context: Context, apk: File) {
        val installer = context.packageManager.packageInstaller
        val params = PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL).apply {
            setAppPackageName(context.packageName)
            setSize(apk.length())
            setRequireUserAction(PackageInstaller.SessionParams.USER_ACTION_NOT_REQUIRED)
        }
        val id = installer.createSession(params)
        installer.openSession(id).use { session ->
            session.openWrite("HyperPlay.apk", 0, apk.length()).use { out ->
                apk.inputStream().use { it.copyTo(out, 1 shl 16) }
                session.fsync(out)
            }
            val intent = Intent(context, StatusReceiver::class.java).setAction(ACTION_STATUS)
            val pi = PendingIntent.getBroadcast(context, id, intent,
                PendingIntent.FLAG_UPDATE_CURRENT or PendingIntent.FLAG_MUTABLE)
            session.commit(pi.intentSender)
        }
    }
}
