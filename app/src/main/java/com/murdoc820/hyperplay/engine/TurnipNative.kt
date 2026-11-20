package com.murdoc820.hyperplay.engine

import java.io.File

/** Puente JNI hacia `gpu/TurnipDriverInspector.cpp` (dentro de `libhyperplay.so`). */
object TurnipNative {
    // Índices del IntArray de nativeInspectDriver (ver gpu/turnip_jni.cpp).
    private const val VALID_ELF = 0
    private const val AARCH64 = 1
    private const val BIONIC = 2
    private const val HAS_ICD_ENTRY = 3
    private const val HAS_NEGOTIATE = 4
    private const val FIELDS = 5

    init {
        System.loadLibrary("hyperplay")
    }

    @JvmStatic external fun nativeInspectDriver(path: String): IntArray?

    /** null = dlopen + dlsym(vk_icdGetInstanceProcAddr) correctos; si no, el error de dlerror(). */
    @JvmStatic external fun nativeDlopenCheck(path: String): String?

    /** Modelo KGSL (p. ej. "Adreno830v2"); vacío si sysfs no es legible. */
    @JvmStatic external fun nativeAdrenoModel(): String

    /** true si `/dev/kgsl-3d0` es accesible en lectura/escritura para la app. */
    @JvmStatic external fun nativeKgslAccessible(): Boolean

    fun inspect(lib: File): DriverInspectionResult {
        val v = nativeInspectDriver(lib.absolutePath)
        if (v == null || v.size < FIELDS) return DriverInspectionResult.INVALID
        return DriverInspectionResult(
            validElf = v[VALID_ELF] != 0,
            aarch64 = v[AARCH64] != 0,
            bionic = v[BIONIC] != 0,
            hasIcdEntry = v[HAS_ICD_ENTRY] != 0,
            hasNegotiate = v[HAS_NEGOTIATE] != 0,
        )
    }
}

/** Implementación de producción de [DriverInspector]: delega en [TurnipNative]. */
object NativeDriverInspector : DriverInspector {
    override fun inspect(lib: File): DriverInspectionResult = TurnipNative.inspect(lib)
    override fun dlopenCheck(lib: File): String? = TurnipNative.nativeDlopenCheck(lib.absolutePath)
}
