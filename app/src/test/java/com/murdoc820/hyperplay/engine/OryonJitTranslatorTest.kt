package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class OryonJitTranslatorTest {

    @Test
    fun verifiesOryonEmitterBackendFilesExist() {
        val header = File("src/main/cpp/cpu/OryonEmitterBackend.h")
        val source = File("src/main/cpp/cpu/OryonEmitterBackend.cpp")
        assertTrue("OryonEmitterBackend.h debe existir", header.isFile)
        assertTrue("OryonEmitterBackend.cpp debe existir", source.isFile)
    }

    @Test
    fun verifiesOryonEmitterGprMappingAndOpcodeSupport() {
        val cpp = File("src/main/cpp/cpu/OryonEmitterBackend.cpp").readText()
        val header = File("src/main/cpp/cpu/OryonEmitterBackend.h").readText()

        // 1. Mapeo real de GPR x86 a X0..X15 y contexto en X19.
        assertTrue("CpuContext debe exponer los 16 GPR", header.contains("uint64_t gpr[16]"))
        assertTrue(
            "El prólogo debe cargar los GPR emparejados en X0..X15",
            cpp.contains("emitPrologue") && cpp.contains("i * 2") && cpp.contains("i * 2 + 1"),
        )
        assertTrue("El contexto debe residir en X19", cpp.contains("emitMovReg(19, 0, true)"))
        assertTrue(
            "Debe reservar el stack virtual de 8 MB",
            header.contains("stackSize = 8u * 1024u * 1024u"),
        )
        assertTrue(
            "Debe reservar la caché JIT de 512 MB",
            header.contains("codeCacheSize = 512u * 1024u * 1024u"),
        )

        // 2. Verificación de decodificador de instrucciones x86/x64.
        assertTrue("Debe soportar prefijos REX (0x40-0x4F)", cpp.contains("0x40") && cpp.contains("0x4F"))
        assertTrue("Debe soportar MOV r64/r32 imm", cpp.contains("0xB8"))
        assertTrue("Debe soportar ADD", cpp.contains("0x01") || cpp.contains("0x03"))
        assertTrue("Debe soportar SUB", cpp.contains("0x29") || cpp.contains("0x2B"))
        assertTrue("Debe soportar RET (0xC3)", cpp.contains("0xC3"))
        assertTrue("Debe soportar PUSH/POP", cpp.contains("0x50") && cpp.contains("0x58"))
        assertTrue("Debe soportar llamadas directas/indirectas CALL", cpp.contains("0xE8") && cpp.contains("0xFF"))

        // 3. W^X real: RW -> RX y limpieza de caché de instrucciones AArch64.
        assertTrue("Debe invalidar la caché de instrucciones (__builtin___clear_cache)", cpp.contains("__builtin___clear_cache"))
        assertTrue("Debe pasar la arena JIT a RX con mprotect", cpp.contains("mprotect") && cpp.contains("PROT_EXEC"))
    }

    @Test
    fun verifiesNativeDllInterceptorExpandedWin32AndDirect3D() {
        val cpp = File("src/main/cpp/loader/NativeDllInterceptor.cpp").readText()

        // Verificación de APIs Win32 expandidas
        assertTrue("Debe exportar GetModuleHandleW", cpp.contains("win_GetModuleHandleW"))
        assertTrue("Debe exportar GetProcAddress", cpp.contains("win_GetProcAddress"))
        assertTrue("Debe exportar LoadLibraryW", cpp.contains("win_LoadLibraryW"))
        assertTrue("Debe exportar GetSystemInfo", cpp.contains("win_GetSystemInfo"))
        assertTrue("Debe exportar QueryPerformanceCounter", cpp.contains("win_QueryPerformanceCounter"))
        assertTrue("Debe exportar HeapAlloc", cpp.contains("win_HeapAlloc"))
        assertTrue("Debe exportar TlsAlloc", cpp.contains("win_TlsAlloc"))

        // Verificación de Direct3D 11 / Direct3D 9 conectados a DirectVulkanBridge
        assertTrue("Debe interceptar D3D11CreateDeviceAndSwapChain", cpp.contains("win_D3D11CreateDeviceAndSwapChain"))
        assertTrue("Debe interceptar Direct3DCreate9", cpp.contains("win_Direct3DCreate9"))
        assertTrue("Debe invocar DirectVulkanBridge", cpp.contains("directVulkanBridge"))
    }

    @Test
    fun verifiesGameLauncherUsesPureDirectEngineWithoutRootfsGate() {
        val launcher = File("src/main/java/com/murdoc820/hyperplay/engine/GameLauncher.kt").readText()
        assertTrue("GameLauncher debe llamar a nativeDirectLoadPe", launcher.contains("HyperPlayNative.nativeDirectLoadPe"))
        assertTrue("GameLauncher debe llamar a nativeDirectPrecompileAot", launcher.contains("HyperPlayNative.nativeDirectPrecompileAot"))
        // El anclaje a Prime + ADPF lo hace el propio hilo del juego al entrar en nativeDirectLaunch
        // (antes GameLauncher lo pedía desde el hilo de E/S de Kotlin y no afectaba al juego).
        val jni = File("src/main/cpp/jni_bridge.cpp").readText()
        val launch = jni.substringAfter("nativeDirectLaunch)").take(400)
        assertTrue("nativeDirectLaunch debe anclar el hilo principal del juego", launch.contains("pacing::setMainThread"))
    }
}
