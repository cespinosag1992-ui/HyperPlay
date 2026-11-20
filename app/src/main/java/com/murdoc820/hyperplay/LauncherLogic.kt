package com.murdoc820.hyperplay

import com.murdoc820.hyperplay.model.EngineHint
import com.murdoc820.hyperplay.model.ExeArch
import com.murdoc820.hyperplay.model.GraphicsApi
import com.murdoc820.hyperplay.model.PortableGame
import java.net.URLDecoder

/**
 * Lógica pura del launcher (sin Android): badges, etiquetas y carpetas.
 * Probada en `LauncherLogicTest`.
 */
object LauncherLogic {
    const val PRIMARY_ROOT = "/storage/emulated/0"
    const val SCAN_MAX_DEPTH = 4

    val DEFAULT_FOLDERS = listOf("$PRIMARY_ROOT/PC games", "$PRIMARY_ROOT/Games", "$PRIMARY_ROOT/Download")

    const val BADGE_PORTABLE = "[PORTABLE D:\\]"
    const val BADGE_LAA = "[LAA]"
    const val GRAPHICS_UNITY_UNREAL = "DirectX 11 (Unity/Unreal)"
    const val GRAPHICS_AUTO = "Auto (D3D/Vulkan directo)"
    const val GRAPHICS_DXGI_ONLY = "DXGI (DirectX 10/11/12)"

    private const val EXTERNAL_STORAGE_AUTHORITY = "com.android.externalstorage.documents"
    private const val DOWNLOADS_AUTHORITY = "com.android.providers.downloads.documents"
    private val VOLUME_UUID = Regex("^[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}$")
    private val PRIMARY_ALIASES = listOf("/sdcard", "/storage/self/primary", "/mnt/sdcard")

    private val MODERN_D3D = setOf(GraphicsApi.D3D10, GraphicsApi.D3D11, GraphicsApi.D3D12)

    // ---------- Badges / etiquetas ----------

    fun archBadge(arch: ExeArch): String = "[${arch.badge}]"

    /** Arquitectura, `[PORTABLE D:\]` y `[LAA]` si aplica. */
    fun badges(game: PortableGame): List<String> = buildList {
        add(archBadge(game.arch))
        add(BADGE_PORTABLE)
        if (game.pe.largeAddressAware) add(BADGE_LAA)
    }

    /** API gráfica a partir de los imports PE; si no hay, deduce por motor o deja "Auto". */
    fun graphicsApiLabel(apis: Set<GraphicsApi>, hints: Set<EngineHint>): String {
        val hasModern = apis.any { it in MODERN_D3D }
        val shown = GraphicsApi.entries.filter { it in apis && !(it == GraphicsApi.DXGI && hasModern) }
        return when {
            shown == listOf(GraphicsApi.DXGI) -> GRAPHICS_DXGI_ONLY
            shown.isNotEmpty() -> shown.joinToString(" + ") { it.label }
            EngineHint.UNITY in hints || EngineHint.UNREAL in hints -> GRAPHICS_UNITY_UNREAL
            else -> GRAPHICS_AUTO
        }
    }

    // ---------- Carpetas ----------

    /**
     * Convierte una URI de árbol SAF en ruta real cuando es almacenamiento accesible con
     * MANAGE_EXTERNAL_STORAGE (`primary:` → `/storage/emulated/0/…`, tarjeta SD `XXXX-XXXX:`,
     * `raw:` de Descargas). Devuelve null si no se puede mapear.
     */
    fun treeUriToPath(uri: String): String? {
        val afterScheme = uri.removePrefix("content://")
        if (afterScheme == uri) return null
        val authority = afterScheme.substringBefore('/')
        val treeId = afterScheme.substringAfter("/tree/", "").substringBefore('/')
        if (treeId.isEmpty()) return null
        val docId = runCatching { URLDecoder.decode(treeId, "UTF-8") }.getOrNull() ?: return null
        return when (authority) {
            EXTERNAL_STORAGE_AUTHORITY -> documentIdToPath(docId)
            DOWNLOADS_AUTHORITY -> when {
                docId.startsWith("raw:") -> normalizeFolder(docId.removePrefix("raw:"))
                docId == "downloads" -> "$PRIMARY_ROOT/Download"
                else -> null
            }
            else -> null
        }
    }

    /** Document ID de `ExternalStorageProvider` (`primary:PC games`) → ruta real. */
    fun documentIdToPath(docId: String): String? {
        val colon = docId.indexOf(':')
        if (colon < 0) return null
        val volume = docId.substring(0, colon)
        val rel = docId.substring(colon + 1).trim('/')
        if (rel.split('/').any { it == ".." }) return null
        val base = when {
            volume.equals("primary", ignoreCase = true) -> PRIMARY_ROOT
            volume.equals("home", ignoreCase = true) -> "$PRIMARY_ROOT/Documents"
            VOLUME_UUID.matches(volume) -> "/storage/${volume.uppercase()}"
            else -> return null
        }
        return if (rel.isEmpty()) base else "$base/$rel"
    }

    /** Unifica alias (`/sdcard`) y quita la barra final. */
    fun normalizeFolder(path: String): String {
        var p = path.trim().trimEnd('/')
        if (p.isEmpty()) return p
        for (alias in PRIMARY_ALIASES) {
            if (p == alias || p.startsWith("$alias/")) {
                p = PRIMARY_ROOT + p.removePrefix(alias)
                break
            }
        }
        return p
    }

    fun displayFolder(path: String): String =
        if (path == PRIMARY_ROOT || path.startsWith("$PRIMARY_ROOT/")) "/sdcard" + path.removePrefix(PRIMARY_ROOT) else path

    fun addFolder(folders: List<String>, path: String): List<String> {
        val n = normalizeFolder(path)
        return if (n.isEmpty()) folders else (folders + n).distinct()
    }

    fun removeFolder(folders: List<String>, path: String): List<String> {
        val n = normalizeFolder(path)
        return folders.filterNot { it == n }
    }

    fun encodeFolders(folders: List<String>): String = folders.joinToString("\n")

    /** null (nunca guardado) → carpetas por defecto. */
    fun decodeFolders(stored: String?): List<String> {
        if (stored == null) return DEFAULT_FOLDERS
        return stored.lineSequence().map(::normalizeFolder).filter { it.isNotEmpty() }.distinct().toList()
    }

    /** Une resultados de varias carpetas sin duplicar `.exe` (carpetas solapadas). */
    fun mergeGames(results: List<List<PortableGame>>): List<PortableGame> =
        results.flatten().distinctBy { it.exePath }.sortedBy { it.title.lowercase() }
}
