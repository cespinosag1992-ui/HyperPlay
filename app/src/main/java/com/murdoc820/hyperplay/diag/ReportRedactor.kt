package com.murdoc820.hyperplay.diag

import org.json.JSONObject

/**
 * Quita datos personales de un informe antes de enviarlo. Los informes terminan como Issues de
 * GitHub, así que todo lo que salga del teléfono debe poder ser público.
 *
 * Reemplaza: direcciones IPv4/IPv6 y MAC, correos, carpetas de usuario (Android, Windows, Linux,
 * macOS), números de serie / Android ID / IMEI, SteamID, cuentas y tokens. De las rutas de
 * almacenamiento conserva solo los dos últimos componentes (carpeta del juego y archivo), que hacen
 * falta para saber qué juego falló.
 *
 * Las mismas reglas están en `server/report-worker/src/redact.js` (segunda capa en el servidor):
 * si cambias una, cambia la otra.
 */
object ReportRedactor {
    /** Campos del JSON que se envían tal cual (no contienen texto libre). */
    private val PASS_THROUGH = setOf("id", "kind", "appVersion", "appVersionCode", "time")

    private const val HEX = "[0-9A-Fa-f]"

    private data class Rule(val regex: Regex, val replace: (MatchResult) -> String)

    private fun rule(pattern: String, replace: (MatchResult) -> String) =
        Rule(Regex(pattern, RegexOption.IGNORE_CASE), replace)

    private fun fixed(pattern: String, replacement: String) = rule(pattern) { replacement }

    /** Valor tras "clave=" / "clave: " hasta el siguiente separador. */
    private const val VALUE = """(\s*[:=]\s*["']?)[^\s"',;&)}\]]+"""

    private val rules: List<Rule> = listOf(
        // Tokens con formato conocido (antes que nada, para que no se partan).
        fixed("""\b(?:gh[pousr]_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})""", "<token>"),
        fixed("""\bxox[abprs]-[A-Za-z0-9-]{10,}""", "<token>"),
        fixed("""\bAKIA[0-9A-Z]{16}\b""", "<token>"),
        fixed("""\bAIza[0-9A-Za-z_-]{35}""", "<token>"),
        fixed("""\beyJ[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}\.[A-Za-z0-9_-]{8,}""", "<token>"),
        rule("""\b(Bearer|Basic)\s+[A-Za-z0-9._~+/=-]{8,}""") { "${it.groupValues[1]} <token>" },
        // clave=valor de secretos e identificadores.
        rule(
            """\b(access_?token|refresh_?token|auth_?token|token|password|passwd|pwd|secret|api_?key|""" +
                """access_?key|client_?secret|session_?id|cookie)$VALUE""",
        ) { "${it.groupValues[1]}${it.groupValues[2]}<token>" },
        rule(
            """\b(android_?id|ssaid|ro\.serialno|ro\.boot\.serialno|serial(?:_?no|_?number)?|imei|meid|""" +
                """device_?id|advertising_?id|gaid|adid)$VALUE""",
        ) { "${it.groupValues[1]}${it.groupValues[2]}<device-id>" },
        rule("""\b(account_?name|account|user_?name|username|login|steam_?user|persona_?name)$VALUE""") {
            "${it.groupValues[1]}${it.groupValues[2]}<account>"
        },
        // Correos.
        fixed("""\b[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}\b""", "<email>"),
        // Rutas de usuario. Almacenamiento de Android: se quedan los 2 últimos componentes.
        rule(
            """(?:/storage/emulated/\d+|/storage/self/primary|/storage/[0-9A-F]{4}-[0-9A-F]{4}|""" +
                """/mnt/user/\d+/[^/\s]+|/(?:mnt/)?sdcard)((?:/[^/\\\r\n"'<>|:;,()\[\]{}=]+)*)""",
        ) { m -> "<storage>" + keepTail(m.groupValues[1]) },
        rule("""\b([A-Z]:\\(?:Users|Documents and Settings)\\)[^\\/\r\n"'<>|:]+""") { "${it.groupValues[1]}<user>" },
        rule("""(?<![\w.])(/(?:home|Users))/[^/\s"'<>|:]+""") { "${it.groupValues[1]}/<user>" },
        // MAC antes que IPv6 (6 grupos de 2).
        fixed("""(?<![\w:-])(?:$HEX{2}[:-]){5}$HEX{2}(?![\w:-])""", "<mac>"),
        // IPv4 (deja 0.0.0.0 y 127.x, que no identifican a nadie).
        rule("""(?<![\w.])(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)(?:\.(?:25[0-5]|2[0-4]\d|1\d\d|[1-9]?\d)){3}(?![\w.])""") { m ->
            if (m.value == "0.0.0.0" || m.value.startsWith("127.")) m.value else "<ip>"
        },
        // IPv6 completa (8 grupos) y comprimida (con "::" y al menos una cifra).
        fixed("""(?<![\w:])(?:$HEX{1,4}:){7}$HEX{1,4}(?![\w:])""", "<ip>"),
        rule("""(?<![\w:])(?:$HEX{1,4}(?::$HEX{1,4}){0,6})?::(?:$HEX{1,4}(?::$HEX{1,4}){0,6})?(?![\w:])""") { m ->
            if (m.value == "::1" || m.value.none { it.isDigit() } || m.value.length < 3) m.value else "<ip>"
        },
        // SteamID64 y números tipo IMEI (15 cifras que cumplen Luhn).
        fixed("""(?<!\d)7656119\d{10}(?!\d)""", "<steam-id>"),
        rule("""(?<![\d.])\d{15}(?![\d.])""") { m -> if (luhn(m.value)) "<device-id>" else m.value },
    )

    /** Redacta un texto libre. */
    fun redact(text: String): String {
        if (text.isEmpty()) return text
        var out = text
        for (r in rules) out = r.regex.replace(out) { r.replace(it) }
        return out
    }

    /** Redacta todos los campos de texto libre de un informe JSON (ver [Diagnostics.queueReport]). */
    fun redactReport(json: String): String {
        val src = JSONObject(json)
        val out = JSONObject()
        for (key in src.keys()) {
            val v = src.get(key)
            out.put(key, if (key !in PASS_THROUGH && v is String) redact(v) else v)
        }
        return out.toString()
    }

    private fun keepTail(rest: String): String {
        val parts = rest.split('/').filter { it.isNotEmpty() }
        return when {
            parts.isEmpty() -> ""
            parts.size <= 2 -> "/" + parts.joinToString("/")
            else -> "/…/" + parts.takeLast(2).joinToString("/")
        }
    }

    private fun luhn(digits: String): Boolean {
        var sum = 0
        for ((i, c) in digits.reversed().withIndex()) {
            var d = c - '0'
            if (i % 2 == 1) { d *= 2; if (d > 9) d -= 9 }
            sum += d
        }
        return sum % 10 == 0
    }
}
