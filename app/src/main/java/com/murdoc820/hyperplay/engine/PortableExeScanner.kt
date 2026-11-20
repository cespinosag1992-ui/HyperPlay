package com.murdoc820.hyperplay.engine

import com.murdoc820.hyperplay.model.EngineHint
import com.murdoc820.hyperplay.model.ExeArch
import com.murdoc820.hyperplay.model.PeInfo
import com.murdoc820.hyperplay.model.PortableGame
import java.io.File

/**
 * Busca `.exe` portables jugables bajo una carpeta raíz.
 *
 * La raíz puede ser una carpeta de juego o una biblioteca con un juego por subcarpeta.
 * Descarta instaladores, redistribuibles y crash handlers, y añade pistas de motor.
 *
 * @param inspectPe lector de cabeceras PE (en producción, [HyperPlayNative.inspectPe]).
 */
class PortableExeScanner(
    private val inspectPe: (File) -> PeInfo?,
    private val maxDepth: Int = DEFAULT_MAX_DEPTH,
) {

    fun scan(root: File): List<PortableGame> {
        if (!root.isDirectory) return emptyList()
        val games = mutableListOf<PortableGame>()
        walk(root, root, 0, games)
        return games.sortedBy { it.title.lowercase() }
    }

    private fun walk(root: File, dir: File, depth: Int, out: MutableList<PortableGame>) {
        val children = dir.listFiles() ?: return
        for (child in children.sortedBy { it.name }) {
            when {
                child.isDirectory -> {
                    if (depth < maxDepth && !isIgnoredDir(child.name)) walk(root, child, depth + 1, out)
                }
                child.isFile && child.extension.equals("exe", ignoreCase = true) -> {
                    if (!isJunkExe(child.name)) toGame(root, child)?.let(out::add)
                }
            }
        }
    }

    private fun toGame(root: File, exe: File): PortableGame? {
        val pe = inspectPe(exe) ?: return null
        if (pe.arch != ExeArch.X86 && pe.arch != ExeArch.X64) return null
        if (pe.subsystem != SUBSYSTEM_WINDOWS_GUI) return null
        val gameRoot = gameRootOf(root, exe)
        return PortableGame(
            title = titleFor(root, gameRoot, exe),
            exePath = exe.absolutePath,
            mountFolder = exe.parentFile!!.absolutePath,
            pe = pe,
            engineHints = detectEngineHints(gameRoot, exe),
        )
    }

    /** Subcarpeta de primer nivel bajo la raíz que contiene el `.exe` (o la raíz misma). */
    private fun gameRootOf(root: File, exe: File): File {
        var current = exe.parentFile!!
        while (current.parentFile != null && current.parentFile != root && current != root) {
            current = current.parentFile!!
        }
        return current
    }

    private fun titleFor(root: File, gameRoot: File, exe: File): String {
        if (gameRoot != root) return gameRoot.name
        return exe.nameWithoutExtension.replace(SHIPPING_SUFFIX, "")
    }

    companion object {
        const val DEFAULT_MAX_DEPTH = 4

        /** IMAGE_SUBSYSTEM_WINDOWS_GUI: descarta herramientas de consola. */
        const val SUBSYSTEM_WINDOWS_GUI = 2

        private val SHIPPING_SUFFIX = Regex("-Win(32|64)-Shipping$", RegexOption.IGNORE_CASE)

        private val JUNK_EXE = listOf(
            Regex("^unins\\d*$"),
            Regex("^uninstall.*"),
            Regex("^(setup|install|installer)(\\b|_|-|\\d).*|^(setup|install|installer)$"),
            Regex("^vc_?redist.*"),
            Regex("^dxsetup$"),
            Regex("^dxwebsetup$"),
            Regex("^dotnetfx.*"),
            Regex("^ndp\\d+.*"),
            Regex("^oalinst$"),
            Regex("^physx.*"),
            Regex("^ue[45]?prereqsetup.*"),
            Regex("^unitycrashhandler(32|64)?$"),
            Regex("^crashreportclient$"),
            Regex("^crashpad_handler$"),
            Regex("^easyanticheat_setup$"),
        )

        private val IGNORED_DIRS = setOf(
            "_hyperplaysaves", "_commonredist", "redist", "redistributables",
            "directx", "vcredist", "dotnet", "__installer", "support",
        )

        fun isJunkExe(fileName: String): Boolean {
            val base = fileName.substringBeforeLast('.').lowercase()
            return JUNK_EXE.any { it.matches(base) }
        }

        fun isIgnoredDir(name: String): Boolean = name.lowercase() in IGNORED_DIRS

        fun detectEngineHints(gameRoot: File, exe: File): Set<EngineHint> {
            val hints = linkedSetOf<EngineHint>()
            val dirs = listOf(exe.parentFile!!, gameRoot).distinct()
            fun anyHas(name: String) = dirs.any { File(it, name).isFile }

            if (anyHas("steam_api.dll") || anyHas("steam_api64.dll")) hints += EngineHint.STEAM
            if (anyHas("UnityPlayer.dll")) hints += EngineHint.UNITY
            if (anyHas("d3d9.dll")) hints += EngineHint.LOCAL_D3D9_WRAPPER
            val unrealPath = exe.invariantSeparatorsPath.contains("/Binaries/Win64/", ignoreCase = true)
            if (unrealPath || File(gameRoot, "Engine/Binaries/Win64").isDirectory) hints += EngineHint.UNREAL
            return hints
        }
    }
}
