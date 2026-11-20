package com.murdoc820.hyperplay

import com.murdoc820.hyperplay.model.EngineHint
import com.murdoc820.hyperplay.model.GraphicsApi
import com.murdoc820.hyperplay.model.PeInfo
import com.murdoc820.hyperplay.model.PortableGame
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class LauncherLogicTest {

    private fun game(
        x64: Boolean = true,
        laa: Boolean = false,
        apis: Set<GraphicsApi> = emptySet(),
        hints: Set<EngineHint> = emptySet(),
        title: String = "Juego",
        exe: String = "/storage/emulated/0/PC games/Juego/juego.exe",
    ) = PortableGame(
        title = title,
        exePath = exe,
        mountFolder = exe.substringBeforeLast('/'),
        pe = PeInfo(
            machine = if (x64) PeInfo.MACHINE_AMD64 else PeInfo.MACHINE_I386,
            characteristics = 0,
            subsystem = 2,
            is64Bit = x64,
            largeAddressAware = laa,
            graphicsApis = apis,
        ),
        engineHints = hints,
    )

    @Test fun badgesX64Laa() {
        assertEquals(
            listOf("[64-BIT X64 -> ORYON 64 DIRECT]", "[PORTABLE D:\\]", "[LAA]"),
            LauncherLogic.badges(game(laa = true)),
        )
    }

    @Test fun badgesX86NoLaa() {
        assertEquals(listOf("[32-BIT X86 -> ORYON 64 DIRECT]", "[PORTABLE D:\\]"), LauncherLogic.badges(game(x64 = false)))
    }

    @Test fun graphicsFromImportsHidesDxgiWithModernD3d() {
        assertEquals("DirectX 11", LauncherLogic.graphicsApiLabel(setOf(GraphicsApi.D3D11, GraphicsApi.DXGI), emptySet()))
        assertEquals(
            "DirectX 9 + OpenGL",
            LauncherLogic.graphicsApiLabel(setOf(GraphicsApi.OPENGL, GraphicsApi.D3D9), emptySet()),
        )
    }

    @Test fun graphicsDxgiOnly() {
        assertEquals(LauncherLogic.GRAPHICS_DXGI_ONLY, LauncherLogic.graphicsApiLabel(setOf(GraphicsApi.DXGI), emptySet()))
    }

    @Test fun graphicsFallbacks() {
        assertEquals(LauncherLogic.GRAPHICS_UNITY_UNREAL, LauncherLogic.graphicsApiLabel(emptySet(), setOf(EngineHint.UNITY)))
        assertEquals(LauncherLogic.GRAPHICS_UNITY_UNREAL, LauncherLogic.graphicsApiLabel(emptySet(), setOf(EngineHint.UNREAL)))
        assertEquals(LauncherLogic.GRAPHICS_AUTO, LauncherLogic.graphicsApiLabel(emptySet(), setOf(EngineHint.STEAM)))
    }

    @Test fun treeUriPrimary() {
        assertEquals(
            "/storage/emulated/0/PC games",
            LauncherLogic.treeUriToPath("content://com.android.externalstorage.documents/tree/primary%3APC%20games"),
        )
        assertEquals(
            "/storage/emulated/0/Games/Sub",
            LauncherLogic.treeUriToPath("content://com.android.externalstorage.documents/tree/primary%3AGames%2FSub/document/primary%3AGames%2FSub"),
        )
        assertEquals("/storage/emulated/0", LauncherLogic.treeUriToPath("content://com.android.externalstorage.documents/tree/primary%3A"))
    }

    @Test fun treeUriSdCardAndDownloads() {
        assertEquals(
            "/storage/1A2B-3C4D/Juegos",
            LauncherLogic.treeUriToPath("content://com.android.externalstorage.documents/tree/1a2b-3c4d%3AJuegos"),
        )
        assertEquals(
            "/storage/emulated/0/Download/PC",
            LauncherLogic.treeUriToPath("content://com.android.providers.downloads.documents/tree/raw%3A%2Fstorage%2Femulated%2F0%2FDownload%2FPC"),
        )
        assertEquals("/storage/emulated/0/Download", LauncherLogic.treeUriToPath("content://com.android.providers.downloads.documents/tree/downloads"))
    }

    @Test fun treeUriRejectsUnknown() {
        assertNull(LauncherLogic.treeUriToPath("content://com.google.android.apps.docs.storage/tree/acc%3D1%3Bdoc%3D2"))
        assertNull(LauncherLogic.treeUriToPath("file:///sdcard/Games"))
        assertNull(LauncherLogic.treeUriToPath("content://com.android.externalstorage.documents/tree/primary%3A..%2Fx"))
        assertNull(LauncherLogic.documentIdToPath("nocolon"))
    }

    @Test fun folderNormalizationAndDisplay() {
        assertEquals("/storage/emulated/0/Games", LauncherLogic.normalizeFolder("/sdcard/Games/"))
        assertEquals("/storage/emulated/0/Games", LauncherLogic.normalizeFolder("/storage/self/primary/Games"))
        assertEquals("/sdcardX/a", LauncherLogic.normalizeFolder("/sdcardX/a"))
        assertEquals("/sdcard/PC games", LauncherLogic.displayFolder("/storage/emulated/0/PC games"))
        assertEquals("/storage/1A2B-3C4D/x", LauncherLogic.displayFolder("/storage/1A2B-3C4D/x"))
    }

    @Test fun folderListRoundTrip() {
        assertEquals(emptyList<String>(), LauncherLogic.decodeFolders(null))  // sin carpetas por defecto
        assertEquals(emptyList<String>(), LauncherLogic.decodeFolders(""))
        val start = listOf("/storage/emulated/0/PC games", "/storage/emulated/0/Games", "/storage/emulated/0/Download")
        val list = LauncherLogic.addFolder(start, "/sdcard/Juegos raros/")
        assertEquals(4, list.size)
        assertEquals(list, LauncherLogic.decodeFolders(LauncherLogic.encodeFolders(list)))
        // Duplicados (por alias) no se añaden.
        assertEquals(list, LauncherLogic.addFolder(list, "/sdcard/Games"))
        assertEquals(3, LauncherLogic.removeFolder(list, "/sdcard/Juegos raros").size)
        assertEquals(listOf("/storage/emulated/0/A"), LauncherLogic.decodeFolders("/sdcard/A\n\n /storage/emulated/0/A/ \n"))
    }

    @Test fun mergeGamesDedupesAndSorts() {
        val a = game(title = "zeta", exe = "/x/z.exe")
        val b = game(title = "Alfa", exe = "/x/a.exe")
        val merged = LauncherLogic.mergeGames(listOf(listOf(a, b), listOf(b.copy(title = "dup"))))
        assertEquals(listOf("Alfa", "zeta"), merged.map { it.title })
    }
}
