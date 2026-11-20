package com.murdoc820.hyperplay.art

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.util.Log
import android.util.LruCache
import com.murdoc820.hyperplay.model.PortableGame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import org.json.JSONObject
import java.io.File
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import java.security.MessageDigest

/** Ficha oficial del juego (tienda de Steam, en español si existe). */
data class GameInfo(
    val appId: Int,
    val name: String,
    val description: String,
    val developers: List<String>,
    val publishers: List<String>,
    val releaseDate: String,
    val genres: List<String>,
)

/** Arte local de un juego (archivos ya descargados / extraídos). */
data class GameArt(
    val info: GameInfo? = null,
    /** Cápsula vertical de la biblioteca (600×900). */
    val capsule: File? = null,
    /** Imagen panorámica de fondo (library_hero). */
    val hero: File? = null,
    /** Logo transparente del juego. */
    val logo: File? = null,
    /** Cabecera horizontal (460×215), respaldo de la cápsula. */
    val header: File? = null,
    /** Icono extraído del .exe (PNG). */
    val icon: File? = null,
)

/**
 * Arte e información de los juegos de la biblioteca. Para cada juego:
 *  1. el icono del `.exe` (recurso RT_GROUP_ICON, sin red);
 *  2. el appid de Steam: `steam_appid.txt` de la carpeta del juego o búsqueda en la tienda por título;
 *  3. la ficha (`appdetails`, español) y las imágenes oficiales de biblioteca del CDN de Steam.
 * Todo se guarda en `files/art/<hash del exe>/` y solo se vuelve a pedir si falta algo (o la ficha
 * tiene más de [REFRESH_MS]). Sin red, la biblioteca usa el icono del `.exe`.
 */
object GameArtRepository {
    private const val TAG = "HyperPlayArt"
    private const val REFRESH_MS = 30L * 24 * 3600 * 1000
    private const val CDN = "https://shared.akamai.steamstatic.com/store_item_assets/steam/apps"
    private const val CDN_OLD = "https://cdn.akamai.steamstatic.com/steam/apps"

    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val network = Semaphore(3)
    private val inFlight = HashSet<String>()
    private val _art = MutableStateFlow<Map<String, GameArt>>(emptyMap())

    /** exePath → arte disponible (se va rellenando). */
    val art: StateFlow<Map<String, GameArt>> = _art.asStateFlow()

    private lateinit var root: File

    fun init(context: Context) {
        if (!::root.isInitialized) root = File(context.applicationContext.filesDir, "art").apply { mkdirs() }
    }

    /** Pide (en segundo plano) el arte de todos los juegos que aún no lo tengan. */
    fun ensure(games: List<PortableGame>) {
        if (!::root.isInitialized) return
        for (g in games) {
            synchronized(inFlight) { if (!inFlight.add(g.exePath)) continue }
            scope.launch {
                runCatching { load(g) }.onFailure { Log.w(TAG, "arte de ${g.title}: ${it.message}") }
            }
        }
    }

    /** Arte en disco de un juego (sin red); para la pantalla de carga. */
    fun cached(context: Context, exePath: String): GameArt {
        init(context)
        _art.value[exePath]?.let { return it }
        val dir = dirFor(exePath)
        return localArt(dir, readInfo(dir))
    }

    private fun dirFor(exePath: String): File {
        val md = MessageDigest.getInstance("SHA-1").digest(exePath.toByteArray())
        return File(root, md.joinToString("") { "%02x".format(it) }.take(16)).apply { mkdirs() }
    }

    private fun publish(exePath: String, art: GameArt) = _art.update { it + (exePath to art) }

    private suspend fun load(game: PortableGame) {
        val dir = dirFor(game.exePath)
        // 1. Icono del exe (local, rápido): se publica ya.
        val iconFile = File(dir, "icon.png")
        if (!iconFile.isFile) ExeIconExtractor.extractPng(File(game.exePath), iconFile)
        var info = readInfo(dir)
        publish(game.exePath, localArt(dir, info))

        // 2-3. Ficha e imágenes oficiales.
        val metaFile = File(dir, "info.json")
        val lang = steamLanguage()
        val stale = !metaFile.isFile || System.currentTimeMillis() - metaFile.lastModified() > REFRESH_MS ||
            readLang(dir) != lang
        if (info == null || stale) {
            val appId = localAppId(game) ?: network.withPermit { searchAppId(game.title) } ?: return
            info = network.withPermit { fetchDetails(appId, lang) } ?: info ?: return
            metaFile.writeText(infoToJson(info).put("lang", lang).toString())
        }
        val id = info.appId
        network.withPermit {
            download(listOf("$CDN/$id/library_600x900_2x.jpg", "$CDN_OLD/$id/library_600x900_2x.jpg",
                "$CDN_OLD/$id/library_600x900.jpg"), File(dir, "capsule.jpg"))
            download(listOf("$CDN/$id/library_hero.jpg", "$CDN_OLD/$id/library_hero.jpg"), File(dir, "hero.jpg"))
            download(listOf("$CDN/$id/logo.png", "$CDN_OLD/$id/logo.png"), File(dir, "logo.png"))
            download(listOf("$CDN/$id/header.jpg", "$CDN_OLD/$id/header.jpg"), File(dir, "header.jpg"))
        }
        publish(game.exePath, localArt(dir, info))
    }

    private fun localArt(dir: File, info: GameInfo?): GameArt {
        fun f(name: String) = File(dir, name).takeIf { it.isFile && it.length() > 0 }
        return GameArt(info, f("capsule.jpg"), f("hero.jpg"), f("logo.png"), f("header.jpg"), f("icon.png"))
    }

    // ---------------------------------------------------------------- Steam

    /** `steam_appid.txt` junto al exe (lo traen muchos juegos con Steamworks). */
    private fun localAppId(game: PortableGame): Int? {
        val folder = File(game.mountFolder)
        val candidates = listOf(File(folder, "steam_appid.txt"), File(File(game.exePath).parentFile, "steam_appid.txt"))
        return candidates.firstNotNullOfOrNull { f ->
            if (f.isFile) f.readText().trim().takeWhile { it.isDigit() }.toIntOrNull() else null
        }
    }

    /** Título "limpio" para buscar: sin guiones bajos, versiones ni sufijos de repack. */
    internal fun searchTerm(title: String): String = title
        .replace('_', ' ').replace('.', ' ')
        .replace(Regex("(?i)\\b(v\\d[\\w.]*|goty|repack|portable|build \\d+)\\b"), " ")
        .replace(Regex("\\s+"), " ").trim()

    private fun normalized(s: String) = s.lowercase().filter { it.isLetterOrDigit() }

    /** Idioma de la tienda de Steam para el idioma del sistema (inglés si Steam no lo tiene). */
    internal fun steamLanguage(locale: java.util.Locale = java.util.Locale.getDefault()): String = when (locale.language) {
        "es" -> if (locale.country in setOf("", "ES")) "spanish" else "latam"
        "pt" -> if (locale.country == "BR") "brazilian" else "portuguese"
        "zh" -> if (locale.country in setOf("TW", "HK", "MO") || locale.script == "Hant") "tchinese" else "schinese"
        "fr" -> "french"; "de" -> "german"; "it" -> "italian"; "ja" -> "japanese"; "ko" -> "koreana"
        "ru" -> "russian"; "pl" -> "polish"; "tr" -> "turkish"; "uk" -> "ukrainian"; "nl" -> "dutch"
        "sv" -> "swedish"; "da" -> "danish"; "fi" -> "finnish"; "nb", "no" -> "norwegian"; "cs" -> "czech"
        "hu" -> "hungarian"; "ro" -> "romanian"; "el" -> "greek"; "th" -> "thai"; "vi" -> "vietnamese"
        "bg" -> "bulgarian"; "id", "in" -> "indonesian"; "ar" -> "arabic"
        else -> "english"
    }

    private fun readLang(dir: File): String? =
        runCatching { JSONObject(File(dir, "info.json").readText()).optString("lang") }.getOrNull()

    private fun searchAppId(title: String): Int? {
        val term = searchTerm(title)
        if (term.isEmpty()) return null
        val url = "https://store.steampowered.com/api/storesearch/?term=" +
            URLEncoder.encode(term, "UTF-8") + "&l=english&cc=US"
        val json = httpText(url)?.let { runCatching { JSONObject(it) }.getOrNull() } ?: return null
        val items = json.optJSONArray("items") ?: return null
        val want = normalized(term)
        var best: Int? = null
        for (i in 0 until items.length()) {
            val it = items.getJSONObject(i)
            val name = normalized(it.optString("name"))
            if (name == want) return it.optInt("id")
            if (best == null && (name.startsWith(want) || want.startsWith(name))) best = it.optInt("id")
        }
        return best ?: items.optJSONObject(0)?.optInt("id")?.takeIf { items.length() == 1 }
    }

    private fun fetchDetails(appId: Int, lang: String): GameInfo? {
        val text = httpText("https://store.steampowered.com/api/appdetails?appids=$appId&l=$lang") ?: return null
        val data = runCatching { JSONObject(text).getJSONObject(appId.toString()) }.getOrNull() ?: return null
        if (!data.optBoolean("success")) return null
        val d = data.getJSONObject("data")
        fun strings(key: String, field: String? = null): List<String> {
            val arr = d.optJSONArray(key) ?: return emptyList()
            return (0 until arr.length()).mapNotNull { i ->
                if (field == null) arr.optString(i) else arr.optJSONObject(i)?.optString(field)
            }.filter { it.isNotBlank() }
        }
        return GameInfo(
            appId = appId,
            name = d.optString("name"),
            description = htmlToText(d.optString("short_description")),
            developers = strings("developers"),
            publishers = strings("publishers"),
            releaseDate = d.optJSONObject("release_date")?.optString("date").orEmpty(),
            genres = strings("genres", "description"),
        )
    }

    private fun htmlToText(s: String) = s.replace(Regex("<[^>]+>"), "")
        .replace("&quot;", "\"").replace("&amp;", "&").replace("&#39;", "'").replace("&lt;", "<").replace("&gt;", ">")
        .trim()

    private fun infoToJson(i: GameInfo) = JSONObject().apply {
        put("appId", i.appId); put("name", i.name); put("description", i.description)
        put("developers", i.developers.joinToString("\u0001")); put("publishers", i.publishers.joinToString("\u0001"))
        put("releaseDate", i.releaseDate); put("genres", i.genres.joinToString("\u0001"))
    }

    private fun readInfo(dir: File): GameInfo? {
        val f = File(dir, "info.json")
        if (!f.isFile) return null
        return runCatching {
            val j = JSONObject(f.readText())
            fun list(k: String) = j.optString(k).split('\u0001').filter { it.isNotBlank() }
            GameInfo(j.getInt("appId"), j.optString("name"), j.optString("description"), list("developers"),
                list("publishers"), j.optString("releaseDate"), list("genres"))
        }.getOrNull()
    }

    private fun open(url: String): HttpURLConnection = (URL(url).openConnection() as HttpURLConnection).apply {
        connectTimeout = 8000
        readTimeout = 12000
        setRequestProperty("User-Agent", "HyperPlay/1.0 (Android)")
        instanceFollowRedirects = true
    }

    private fun httpText(url: String): String? = runCatching {
        val c = open(url)
        try {
            if (c.responseCode != 200) null else c.inputStream.bufferedReader().use { it.readText() }
        } finally {
            c.disconnect()
        }
    }.getOrNull()

    /** Descarga la primera URL que responda 200 a [target] (si aún no existe). */
    private fun download(urls: List<String>, target: File) {
        if (target.isFile && target.length() > 0) return
        for (u in urls) {
            val ok = runCatching {
                val c = open(u)
                try {
                    if (c.responseCode != 200) return@runCatching false
                    val tmp = File(target.parentFile, target.name + ".part")
                    c.inputStream.use { inp -> tmp.outputStream().use { inp.copyTo(it) } }
                    tmp.length() > 0 && tmp.renameTo(target)
                } finally {
                    c.disconnect()
                }
            }.getOrDefault(false)
            if (ok) return
        }
    }
}

/** Bitmaps decodificados a tamaño de pantalla, con caché en memoria (~48 MB). */
object ArtBitmaps {
    private val cache = object : LruCache<String, Bitmap>(48 * 1024 * 1024) {
        override fun sizeOf(key: String, value: Bitmap) = value.byteCount
    }

    fun get(file: File, maxWidthPx: Int): Bitmap? {
        val key = "${file.path}@${file.lastModified()}@$maxWidthPx"
        cache.get(key)?.let { return it }
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(file.path, bounds)
        if (bounds.outWidth <= 0) return null
        var sample = 1
        while (bounds.outWidth / (sample * 2) >= maxWidthPx) sample *= 2
        val bmp = BitmapFactory.decodeFile(file.path, BitmapFactory.Options().apply { inSampleSize = sample }) ?: return null
        cache.put(key, bmp)
        return bmp
    }
}
