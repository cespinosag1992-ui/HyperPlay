package com.murdoc820.hyperplay.engine

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertThrows
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder
import java.io.ByteArrayInputStream
import java.io.ByteArrayOutputStream
import java.io.File
import java.util.zip.ZipEntry
import java.util.zip.ZipOutputStream

class TurnipDriverManagerTest {

    @get:Rule
    val tmp = TemporaryFolder()

    private lateinit var driversDir: File

    /** Decide por el contenido del archivo: "GLIBC", "BIONIC" o cualquier otra cosa (inválido). */
    private class FakeInspector(private val dlopenError: String? = null) : DriverInspector {
        val dlopenCalls = mutableListOf<File>()

        override fun inspect(lib: File): DriverInspectionResult = when (lib.readText()) {
            GLIBC -> DriverInspectionResult(true, true, false, true, true)
            BIONIC -> DriverInspectionResult(true, true, true, true, true)
            X86 -> DriverInspectionResult(true, false, false, true, true)
            else -> DriverInspectionResult.INVALID
        }

        override fun dlopenCheck(lib: File): String? {
            dlopenCalls += lib
            return dlopenError
        }
    }

    @Before
    fun setUp() {
        driversDir = File(tmp.root, "files/drivers")
    }

    private fun manager(inspector: DriverInspector = FakeInspector(), maxBytes: Long = TurnipDriverManager.MAX_EXTRACTED_BYTES) =
        TurnipDriverManager(driversDir, inspector, maxBytes)

    private fun zipOf(vararg entries: Pair<String, String>): ByteArray {
        val bos = ByteArrayOutputStream()
        ZipOutputStream(bos).use { z ->
            for ((name, data) in entries) {
                z.putNextEntry(ZipEntry(name))
                z.write(data.toByteArray())
                z.closeEntry()
            }
        }
        return bos.toByteArray()
    }

    private fun TurnipDriverManager.import(zip: ByteArray, name: String) =
        importPackage(ByteArrayInputStream(zip), name)

    private fun leftovers(): List<String> =
        driversDir.listFiles()?.map { it.name }.orEmpty()

    @Test
    fun importUsesLibraryNameFromMetaJson() {
        val meta = """
            {"schemaVersion": 1, "name": "Turnip a8xx", "description": "Mesa \"Turnip\"",
             "author": "x", "packageVersion": "1", "vendor": "Mesa",
             "driverVersion": "Mesa 25.3.0", "minApi": 28, "libraryName": "libvulkan_freedreno_a8.so"}
        """.trimIndent()
        val zip = zipOf(
            "meta.json" to meta,
            "libvulkan_freedreno.so" to "DECOY",
            "libvulkan_freedreno_a8.so" to GLIBC,
        )
        val m = manager()
        val d = m.import(zip, "turnip.zip")

        assertEquals("Turnip a8xx", d.name)
        assertEquals("Mesa 25.3.0", d.version)
        assertEquals("turnip-a8xx-mesa-25.3.0", d.id)
        assertEquals(File(driversDir, "${d.id}/libvulkan_freedreno.so"), d.libFile)
        assertEquals(GLIBC, d.libFile.readText())
        assertFalse(d.bionic)
        assertNull(d.dlopenOk)
        assertEquals(GLIBC.length.toLong(), d.sizeBytes)
        assertEquals(listOf(d), m.listDrivers())
        assertEquals(listOf(d.id), leftovers())  // Staging eliminado.
    }

    @Test
    fun importWithoutMetaDetectsLibraryByName() {
        val inspector = FakeInspector()
        val m = manager(inspector)
        val d = m.import(zipOf("turnip/lib/libvulkan_freedreno.so" to BIONIC, "README.txt" to "hola"), "Turnip v25.zip")
        assertEquals("Turnip v25", d.name)
        assertEquals("turnip-v25", d.id)
        assertEquals("", d.version)
        assertTrue(d.bionic)
        assertEquals(true, d.dlopenOk)
        assertEquals(1, inspector.dlopenCalls.size)

        val d2 = m.import(zipOf("vulkan.ad08xx.so" to GLIBC), "otro.zip")
        assertEquals(GLIBC, d2.libFile.readText())
        assertEquals("libvulkan_freedreno.so", d2.libFile.name)
    }

    @Test
    fun bionicDlopenFailureIsRecorded() {
        val d = manager(FakeInspector(dlopenError = "library not found")).import(
            zipOf("libvulkan_freedreno.so" to BIONIC), "b.zip",
        )
        assertEquals(false, d.dlopenOk)
        assertEquals(false, manager().listDrivers().single().dlopenOk)
    }

    @Test
    fun duplicateImportsGetUniqueIds() {
        val m = manager()
        val zip = zipOf("libvulkan_freedreno.so" to GLIBC)
        val a = m.import(zip, "t.zip")
        val b = m.import(zip, "t.zip")
        assertEquals("t", a.id)
        assertEquals("t-2", b.id)
    }

    @Test
    fun zipSlipIsRejected() {
        val m = manager()
        for (evil in listOf("../evil.so", "a/../../evil.so", "/etc/evil.so", "..\\evil.so", "C:/evil.so")) {
            assertThrows(DriverImportException::class.java) {
                m.import(zipOf(evil to GLIBC, "libvulkan_freedreno.so" to GLIBC), "x.zip")
            }
        }
        assertFalse(File(driversDir.parentFile, "evil.so").exists())
        assertFalse(File(tmp.root, "evil.so").exists())
        assertTrue(leftovers().isEmpty())
    }

    @Test
    fun libraryNameWithTraversalIsRejected() {
        val zip = zipOf("meta.json" to """{"libraryName":"../../x.so"}""", "libvulkan_freedreno.so" to GLIBC)
        assertThrows(DriverImportException::class.java) { manager().import(zip, "x.zip") }
        assertTrue(leftovers().isEmpty())
    }

    @Test
    fun inspectorRejectionDeletesFiles() {
        val m = manager()
        val e = assertThrows(DriverImportException::class.java) {
            m.import(zipOf("libvulkan_freedreno.so" to "NOT AN ELF"), "bad.zip")
        }
        assertTrue(e.message!!.contains("ELF"))
        val x86 = assertThrows(DriverImportException::class.java) {
            m.import(zipOf("libvulkan_freedreno.so" to X86), "x86.zip")
        }
        assertTrue(x86.message!!.contains("aarch64"))
        assertTrue(leftovers().isEmpty())
        assertTrue(m.listDrivers().isEmpty())
    }

    @Test
    fun packageWithoutDriverIsRejected() {
        assertThrows(DriverImportException::class.java) {
            manager().import(zipOf("readme.txt" to "nada"), "x.zip")
        }
        assertThrows(DriverImportException::class.java) {
            manager().importPackage(ByteArrayInputStream("no es zip".toByteArray()), "x.zip")
        }
        assertTrue(leftovers().isEmpty())
    }

    @Test
    fun extractedSizeIsCapped() {
        val big = "A".repeat(4096)
        assertThrows(DriverImportException::class.java) {
            manager(maxBytes = 1024).import(zipOf("libvulkan_freedreno.so" to big), "big.zip")
        }
        assertTrue(leftovers().isEmpty())
    }

    @Test
    fun adpkgExtensionAcceptedAndOthersRejected() {
        val d = manager().import(zipOf("libvulkan_freedreno.so" to GLIBC), "Turnip.ADPKG")
        assertEquals("turnip", d.id)
        assertThrows(IllegalArgumentException::class.java) {
            manager().import(zipOf("libvulkan_freedreno.so" to GLIBC), "turnip.rar")
        }
    }

    @Test
    fun activeDriverPersistsAcrossInstances() {
        val m = manager()
        val d = m.import(zipOf("libvulkan_freedreno.so" to GLIBC), "t.zip")
        assertNull(m.activeDriver())
        m.setActive(d.id)
        assertEquals("t", File(driversDir, "active.txt").readText())
        assertEquals(d, manager().activeDriver())

        m.setActive(null)
        assertNull(manager().activeDriver())
        assertEquals(TurnipDriverManager.ACTIVE_SYSTEM, File(driversDir, "active.txt").readText())

        assertThrows(IllegalArgumentException::class.java) { m.setActive("no-existe") }
        assertThrows(IllegalArgumentException::class.java) { m.setActive("../t") }
    }

    @Test
    fun deletingActiveDriverFallsBackToSystem() {
        val m = manager()
        val d = m.import(zipOf("libvulkan_freedreno.so" to GLIBC), "t.zip")
        m.setActive(d.id)

        assertTrue(m.delete(d.id))
        assertNull(m.activeDriver())
        assertFalse(d.libFile.exists())
        assertFalse(m.delete(d.id))
    }

    @Test
    fun downloadRequiresHttps() {
        val m = manager()
        assertThrows(IllegalArgumentException::class.java) {
            m.downloadPackage("http://example.invalid/turnip.zip") { _, _ -> }
        }
        assertThrows(IllegalArgumentException::class.java) {
            m.downloadPackage("file:///sdcard/turnip.zip") { _, _ -> }
        }
    }

    @Test
    fun sanitizeIdAndEntryPaths() {
        assertEquals("turnip-a8xx-mesa-25.3.0", TurnipDriverManager.sanitizeId("Turnip a8xx-Mesa 25.3.0"))
        assertEquals("driver", TurnipDriverManager.sanitizeId("../.."))
        assertEquals("a/b.so", TurnipDriverManager.safeEntryPath("./a//b.so"))
        assertFalse(TurnipDriverManager.isValidId(".staging-x"))
        assertFalse(TurnipDriverManager.isValidId("a..b"))
    }

    @Test
    fun miniJsonParsesEscapesAndNesting() {
        val v = MiniJson.parse("""{"a":"x\"\u00e9\n","b":[1,2.5e3,{"c":null}],"d":true}""") as Map<*, *>
        assertEquals("x\"é\n", v["a"])
        assertEquals(listOf("1", "2.5e3", mapOf("c" to null)), v["b"])
        assertEquals(true, v["d"])
        assertThrows(IllegalArgumentException::class.java) { MiniJson.parse("{\"a\":}") }
        assertThrows(IllegalArgumentException::class.java) { MiniJson.parse("{} x") }
    }

    private companion object {
        const val GLIBC = "GLIBC"
        const val BIONIC = "BIONIC"
        const val X86 = "X86"
    }
}
