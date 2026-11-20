package com.murdoc820.hyperplay.engine

import com.murdoc820.hyperplay.model.EngineHint
import com.murdoc820.hyperplay.model.ExeArch
import com.murdoc820.hyperplay.model.PeInfo
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.File

class PortableExeScannerTest {

    @get:Rule
    val tmp = TemporaryFolder()

    // Inspector falso: el contenido del archivo indica la arquitectura ("x86", "x64", "console", otro = no PE).
    private val fakeInspector: (File) -> PeInfo? = { f ->
        when (f.readText()) {
            "x86" -> pe(PeInfo.MACHINE_I386, is64 = false)
            "x64" -> pe(PeInfo.MACHINE_AMD64, is64 = true)
            "console" -> pe(PeInfo.MACHINE_AMD64, is64 = true, subsystem = 3)
            else -> null
        }
    }

    private fun pe(machine: Int, is64: Boolean, subsystem: Int = 2) =
        PeInfo(machine, 0x0020, subsystem, is64, largeAddressAware = true, graphicsApis = emptySet())

    private fun file(path: String, content: String = ""): File =
        File(tmp.root, path).apply { parentFile!!.mkdirs(); writeText(content) }

    private val scanner = PortableExeScanner(fakeInspector)

    @Test
    fun filtersJunkExecutables() {
        listOf(
            "unins000.exe", "vc_redist.x64.exe", "vcredist_x86.exe", "DXSETUP.exe",
            "UnityCrashHandler64.exe", "UnityCrashHandler32.exe", "setup.exe", "CrashReportClient.exe",
            "UE4PrereqSetup_x64.exe",
        ).forEach { assertTrue(it, PortableExeScanner.isJunkExe(it)) }

        listOf("Game.exe", "HollowKnight.exe", "SetupWizardGame.exe", "Installation Saga.exe")
            .forEach { assertFalse(it, PortableExeScanner.isJunkExe(it)) }
    }

    @Test
    fun scansLibraryAndUsesFolderNameAsTitle() {
        file("Hollow Knight/hollow_knight.exe", "x64")
        file("Hollow Knight/unins000.exe", "x64")
        file("Hollow Knight/UnityPlayer.dll")
        file("Hollow Knight/UnityCrashHandler64.exe", "x64")
        file("Old Game/game.exe", "x86")
        file("Old Game/_CommonRedist/vcredist/helper.exe", "x86")
        file("Old Game/readme.txt")

        val games = scanner.scan(tmp.root)

        assertEquals(listOf("Hollow Knight", "Old Game"), games.map { it.title })
        val hk = games[0]
        assertEquals(ExeArch.X64, hk.arch)
        assertEquals(setOf(EngineHint.UNITY), hk.engineHints)
        assertEquals(File(tmp.root, "Hollow Knight").absolutePath, hk.mountFolder)
        assertEquals(ExeArch.X86, games[1].arch)
    }

    @Test
    fun skipsNonPeAndConsoleExecutables() {
        file("Game/notreal.exe", "garbage")
        file("Game/server.exe", "console")
        assertTrue(scanner.scan(tmp.root).isEmpty())
    }

    @Test
    fun detectsSteamUnrealAndLocalD3d9() {
        file("Shooter/Shooter.exe", "x64")
        file("Shooter/Engine/Binaries/Win64/placeholder.txt")
        file("Shooter/Shooter/Binaries/Win64/Shooter-Win64-Shipping.exe", "x64")
        file("Shooter/Shooter/Binaries/Win64/steam_api64.dll")
        file("Retro/retro.exe", "x86")
        file("Retro/d3d9.dll")
        file("Retro/steam_api.dll")

        val games = scanner.scan(tmp.root).associateBy { File(it.exePath).name }

        val shipping = games.getValue("Shooter-Win64-Shipping.exe")
        assertTrue(EngineHint.UNREAL in shipping.engineHints)
        assertTrue(EngineHint.STEAM in shipping.engineHints)
        assertTrue(EngineHint.UNREAL in games.getValue("Shooter.exe").engineHints)

        assertEquals(setOf(EngineHint.STEAM, EngineHint.LOCAL_D3D9_WRAPPER), games.getValue("retro.exe").engineHints)
    }

    @Test
    fun scanningAGameFolderDirectlyUsesExeName() {
        val root = File(tmp.root, "Lib/MyGame")
        file("Lib/MyGame/MyGame-Win64-Shipping.exe", "x64")

        val games = scanner.scan(root)

        assertEquals(listOf("MyGame"), games.map { it.title })
        assertEquals(root.absolutePath, games[0].mountFolder)
    }

    @Test
    fun respectsMaxDepth() {
        file("a/b/c/d/e/f/deep.exe", "x64")
        assertTrue(PortableExeScanner(fakeInspector, maxDepth = 2).scan(tmp.root).isEmpty())
    }

    @Test
    fun missingRootReturnsEmpty() {
        assertTrue(scanner.scan(File(tmp.root, "nope")).isEmpty())
    }
}
