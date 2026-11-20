package com.murdoc820.hyperplay

import com.murdoc820.hyperplay.engine.HyperPlayNative
import com.murdoc820.hyperplay.engine.PortableExeScanner
import com.murdoc820.hyperplay.model.PortableGame
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.launch
import java.io.File

data class LibraryState(
    val games: List<PortableGame> = emptyList(),
    val scanning: Boolean = false,
    /** Carpeta que se escanea ahora (null fuera del escaneo). La UI lo traduce al idioma del sistema. */
    val scan: ScanProgress? = null,
    val scannedOnce: Boolean = false,
    /** Detalle técnico del fallo del escaneo (la UI añade "Error al escanear"). */
    val error: String? = null,
)

/** Progreso del escaneo: carpeta [index] de [total] ([total] = 0 → preparando). */
data class ScanProgress(val index: Int, val total: Int, val folder: String, val found: Int)

/**
 * Caché en memoria (de proceso) de la biblioteca.
 * Sobrevive a recreaciones de la actividad; el escaneo corre en Dispatchers.IO.
 */
object LibraryRepository {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    private val _state = MutableStateFlow(LibraryState())
    val state: StateFlow<LibraryState> = _state.asStateFlow()
    private var scanJob: Job? = null

    @Synchronized
    fun scan(folders: List<String>) {
        if (scanJob?.isActive == true) return
        scanJob = scope.launch {
            _state.update { it.copy(scanning = true, error = null, scan = ScanProgress(0, 0, "", 0)) }
            try {
                val scanner = PortableExeScanner(HyperPlayNative::inspectPe, LauncherLogic.SCAN_MAX_DEPTH)
                val results = mutableListOf<List<PortableGame>>()
                folders.forEachIndexed { i, folder ->
                    val found = results.sumOf { it.size }
                    _state.update {
                        it.copy(scan = ScanProgress(i + 1, folders.size, folder, found))
                    }
                    results += scanner.scan(File(folder))
                }
                val games = LauncherLogic.mergeGames(results)
                _state.update {
                    it.copy(games = games, scanning = false, scannedOnce = true, scan = null)
                }
            } catch (t: Throwable) {
                _state.update {
                    it.copy(scanning = false, scannedOnce = true, scan = null, error = t.message ?: t.javaClass.simpleName)
                }
            }
        }
    }
}
