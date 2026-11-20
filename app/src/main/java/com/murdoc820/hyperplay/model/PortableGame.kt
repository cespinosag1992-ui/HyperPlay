package com.murdoc820.hyperplay.model

enum class ExeArch(val badge: String) {
    X86("32-BIT X86 -> ORYON 64 DIRECT"),
    X64("64-BIT X64 -> ORYON 64 DIRECT"),
    ARM64("ARM64 NATIVE"),
    UNKNOWN("UNKNOWN"),
}

enum class GraphicsApi(val bit: Int, val label: String) {
    DDRAW(1 shl 0, "DirectDraw"),
    D3D8(1 shl 1, "DirectX 8"),
    D3D9(1 shl 2, "DirectX 9"),
    D3D10(1 shl 3, "DirectX 10"),
    D3D11(1 shl 4, "DirectX 11"),
    D3D12(1 shl 5, "DirectX 12"),
    DXGI(1 shl 6, "DXGI"),
    VULKAN(1 shl 7, "Vulkan"),
    OPENGL(1 shl 8, "OpenGL");

    companion object {
        /** Decodifica la máscara que devuelve PeHeaderInspector (mismos bits que GraphicsApi en C++). */
        fun fromMask(mask: Int): Set<GraphicsApi> = entries.filterTo(linkedSetOf()) { mask and it.bit != 0 }
    }
}

enum class EngineHint(val label: String) {
    STEAM("Steam API"),
    UNITY("Unity"),
    UNREAL("Unreal Engine"),
    LOCAL_D3D9_WRAPPER("d3d9.dll local"),
}

/** Resultado del análisis de cabeceras PE de un `.exe`. */
data class PeInfo(
    val machine: Int,
    val characteristics: Int,
    val subsystem: Int,
    val is64Bit: Boolean,
    val largeAddressAware: Boolean,
    val graphicsApis: Set<GraphicsApi>,
) {
    val arch: ExeArch
        get() = when (machine) {
            MACHINE_I386 -> ExeArch.X86
            MACHINE_AMD64 -> ExeArch.X64
            MACHINE_ARM64 -> ExeArch.ARM64
            else -> ExeArch.UNKNOWN
        }

    companion object {
        const val MACHINE_I386 = 0x014c
        const val MACHINE_AMD64 = 0x8664
        const val MACHINE_ARM64 = 0xaa64
    }
}

data class PortableGame(
    val title: String,
    /** Ruta absoluta del `.exe` en el almacenamiento Android. */
    val exePath: String,
    /** Carpeta del `.exe`: se monta como `D:\` y es el working directory. */
    val mountFolder: String,
    val pe: PeInfo,
    val engineHints: Set<EngineHint>,
) {
    val arch: ExeArch get() = pe.arch
}
