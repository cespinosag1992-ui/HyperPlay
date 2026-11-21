package com.murdoc820.hyperplay.diag

import java.io.File

/**
 * Límites de espacio de los registros en el teléfono: los logs de sesión de juego y los informes
 * pendientes no crecen sin fin. Al enviarse un informe se borra y el registro de la app empieza de
 * nuevo (ver [Diagnostics.uploadPending]).
 */
object LogStorage {
    /** Logs de sesión de juego que se conservan (los más recientes). */
    const val KEEP_GAME_LOGS = 5
    /** Tamaño total máximo de los logs de sesión. */
    const val MAX_GAME_LOG_BYTES = 16L * 1024 * 1024
    /** Informes sin enviar que se conservan (los más recientes). */
    const val KEEP_REPORTS = 20
    /** Antigüedad máxima de un informe sin enviar. */
    const val MAX_REPORT_AGE_MS = 30L * 24 * 60 * 60 * 1000

    /**
     * Deja en [dir] como mucho [keep] ficheros (los más nuevos) y no más de [maxBytes] en total;
     * borra también los más antiguos que [maxAgeMs] (si es > 0). [exclude] nunca se borra (el log
     * de la sesión en curso). Devuelve los ficheros borrados.
     */
    fun prune(
        dir: File,
        extension: String,
        keep: Int,
        maxBytes: Long = Long.MAX_VALUE,
        maxAgeMs: Long = 0,
        now: Long = System.currentTimeMillis(),
        exclude: File? = null,
    ): List<File> {
        val files = dir.listFiles { f -> f.isFile && f.extension == extension }
            ?.sortedByDescending { it.lastModified() } ?: return emptyList()
        val removed = mutableListOf<File>()
        var total = 0L
        var kept = 0
        for (f in files) {
            val protected = exclude != null && f.absoluteFile == exclude.absoluteFile
            val tooOld = maxAgeMs > 0 && now - f.lastModified() > maxAgeMs
            val size = f.length()
            if (!protected && (kept >= keep || total + size > maxBytes || tooOld)) {
                if (f.delete()) removed += f
                continue
            }
            kept++
            total += size
        }
        return removed
    }

    fun pruneGameLogs(logsDir: File, current: File? = null): List<File> =
        prune(logsDir, "log", KEEP_GAME_LOGS, MAX_GAME_LOG_BYTES, exclude = current)

    fun pruneReports(reportsDir: File, now: Long = System.currentTimeMillis()): List<File> =
        prune(reportsDir, "json", KEEP_REPORTS, maxAgeMs = MAX_REPORT_AGE_MS, now = now)
}
