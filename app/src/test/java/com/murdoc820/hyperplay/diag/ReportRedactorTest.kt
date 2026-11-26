package com.murdoc820.hyperplay.diag

import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class ReportRedactorTest {
    private fun r(s: String) = ReportRedactor.redact(s)

    @Test
    fun ipv4() {
        assertEquals("connect to <ip>:443 failed", r("connect to 192.168.1.37:443 failed"))
        assertEquals("from <ip>", r("from 8.8.4.4"))
        // Loopback y 0.0.0.0 no identifican a nadie; versiones con más de 4 partes no son IPs.
        assertEquals("bind 127.0.0.1 0.0.0.0", r("bind 127.0.0.1 0.0.0.0"))
        assertEquals("lib 1.2.3.4.5", r("lib 1.2.3.4.5"))
    }

    @Test
    fun ipv6() {
        assertEquals("addr <ip> up", r("addr 2001:0db8:85a3:0000:0000:8a2e:0370:7334 up"))
        assertEquals("addr <ip>%wlan0", r("addr fe80::1c2d:3e4f%wlan0"))
        assertEquals("[<ip>]:80", r("[2001:db8::42]:80"))
        // Horas, código C++ y loopback quedan igual.
        assertEquals("11-26 12:34:56.789 I/x", r("11-26 12:34:56.789 I/x"))
        assertEquals("std::vector<int> Foo::bar() ::1", r("std::vector<int> Foo::bar() ::1"))
    }

    @Test
    fun email() {
        assertEquals("Account {name=<email>, type=com.google}", r("Account {name=juan.perez+x@gmail.com, type=com.google}"))
    }

    @Test
    fun androidStorageKeepsOnlyGameFolderAndFile() {
        assertEquals(
            "Juego: <storage>/…/Hollow Knight/hollow_knight.exe",
            r("Juego: /storage/emulated/0/Juan Pérez/Juegos/Hollow Knight/hollow_knight.exe"),
        )
        assertEquals("<storage>/…/Celeste/Celeste.exe", r("/sdcard/Download/Celeste/Celeste.exe"))
        assertEquals("<storage>/…/Ori/ori.exe", r("/storage/1A2B-3C4D/Mis cosas/Ori/ori.exe"))
        assertEquals("<storage>/Celeste", r("/storage/emulated/10/Celeste"))
        assertFalse(r("open /storage/emulated/0/Carlos/Games/Cuphead/Cuphead.exe").contains("Carlos"))
    }

    @Test
    fun desktopUserFolders() {
        assertEquals("C:\\Users\\<user>\\AppData\\Roaming", r("C:\\Users\\Murdoc\\AppData\\Roaming"))
        assertEquals("/home/<user>/x and /Users/<user>/y", r("/home/carlos/x and /Users/ana/y"))
    }

    @Test
    fun deviceIdentifiers() {
        assertEquals("wlan0 <mac> ok", r("wlan0 a4:5e:60:c1:22:9f ok"))
        assertEquals("eth <mac>", r("eth A4-5E-60-C1-22-9F"))
        assertEquals("android_id=<device-id> serial: <device-id>", r("android_id=9774d56d682e549c serial: R5CT12ABCDE"))
        assertEquals("ro.serialno=<device-id>", r("ro.serialno=ZY22H7K9LP"))
        assertEquals("imei <device-id>", r("imei 490154203237518"))   // cumple Luhn
        assertEquals("n=490154203237519", r("n=490154203237519"))      // no cumple Luhn
        assertEquals("steam <steam-id>", r("steam 76561198012345678"))
    }

    @Test
    fun accountsAndTokens() {
        assertEquals("username=<account> ok", r("username=carlos92 ok"))
        assertEquals("Authorization: Bearer <token>", r("Authorization: Bearer abcdefghijklmnop123"))
        assertEquals("t <token>", r("t ghp_abcdefghijklmnopqrstuvwxyz0123456789"))
        assertEquals("t <token>", r("t github_pat_11ABCDEFG0123456789_abcdefghijklmnop"))
        assertEquals("api_key=<token>&x=1", r("api_key=sk-12345678&x=1"))
        assertEquals("password: \"<token>\"", r("password: \"hunter2\""))
        assertEquals("jwt <token>", r("jwt eyJhbGciOiJIUzI1NiJ9.eyJzdWIiOiIxMjM0NTY3ODkwIn0.dozjgNryP4J3jVmNHl0w5N_XgL0n3I9PlFUP0THsR8U"))
    }

    @Test
    fun crashDetailsStayUseful() {
        val trace = """
            java.lang.IllegalStateException: boom
                at com.murdoc820.hyperplay.GameSessionActivity.onCreate(GameSessionActivity.kt:123)
            #00 pc 000000000004b1c0  /data/app/~~x/lib/arm64/libhyperplay.so (Translator::run+64)
            exit code 3221225477, 0xC0000005 at 0x7f3a2b1c00
        """.trimIndent()
        assertEquals(trace, r(trace))
    }

    @Test
    fun reportJsonRedactsFreeTextButKeepsMetadata() {
        val src = JSONObject()
            .put("id", "5a1c1f6e-3c9b-4e43-9f1e-1e5e3c2d9a10")
            .put("kind", "juego")
            .put("appVersion", "1.2.3.4")
            .put("appVersionCode", 42)
            .put("title", "Celeste: exit code / código 1")
            .put("details", "Juego: /storage/emulated/0/Pepe/Celeste/Celeste.exe")
            .put("logcat", "D/net: 10.0.0.5 me@example.org")
        val out = JSONObject(ReportRedactor.redactReport(src.toString()))
        assertEquals("5a1c1f6e-3c9b-4e43-9f1e-1e5e3c2d9a10", out.getString("id"))
        assertEquals("1.2.3.4", out.getString("appVersion"))
        assertEquals(42, out.getInt("appVersionCode"))
        assertEquals("Celeste: exit code / código 1", out.getString("title"))
        assertEquals("Juego: <storage>/Celeste/Celeste.exe", out.getString("details").replace("/…/", "/"))
        assertFalse(out.getString("details").contains("Pepe"))
        assertEquals("D/net: <ip> <email>", out.getString("logcat"))
        assertTrue(out.has("kind"))
    }
}
