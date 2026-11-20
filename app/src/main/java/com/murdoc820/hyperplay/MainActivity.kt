package com.murdoc820.hyperplay

import android.content.ActivityNotFoundException
import android.content.Intent
import android.content.SharedPreferences
import android.net.Uri
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.widget.Toast
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.core.content.edit
import androidx.core.net.toUri
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import androidx.lifecycle.compose.collectAsStateWithLifecycle
import com.murdoc820.hyperplay.art.GameArtRepository
import com.murdoc820.hyperplay.audio.ConsoleUiSoundEngine
import com.murdoc820.hyperplay.diag.Diagnostics
import com.murdoc820.hyperplay.update.AppUpdater
import com.murdoc820.hyperplay.model.PortableGame
import com.murdoc820.hyperplay.session.GameProcess
import androidx.lifecycle.lifecycleScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import java.io.File

/**
 * Launcher de consola (LAUNCHER). Comprueba el permiso de almacenamiento, escanea las carpetas
 * de la biblioteca y lanza [GameSessionActivity].
 */
class MainActivity : ComponentActivity() {

    private lateinit var soundEngine: ConsoleUiSoundEngine
    private lateinit var prefs: SharedPreferences
    private lateinit var sounds: UiSounds

    private var hasStorage by mutableStateOf(true)
    private var reportsEnabled by mutableStateOf<Boolean?>(false)
    private var navKeyTick by mutableIntStateOf(0)

    /** Teclas de navegación del mando/teclado: si nada tiene foco, el launcher lo recoloca. */
    // Falso positivo de lint: androidx.core marca dispatchKeyEvent como restringido, pero es el
    // método público de Activity que se sobrescribe aquí.
    @android.annotation.SuppressLint("RestrictedApi")
    override fun dispatchKeyEvent(event: android.view.KeyEvent): Boolean {
        if (event.action == android.view.KeyEvent.ACTION_DOWN && event.repeatCount == 0) {
            when (event.keyCode) {
                android.view.KeyEvent.KEYCODE_DPAD_UP, android.view.KeyEvent.KEYCODE_DPAD_DOWN,
                android.view.KeyEvent.KEYCODE_DPAD_LEFT, android.view.KeyEvent.KEYCODE_DPAD_RIGHT,
                android.view.KeyEvent.KEYCODE_BUTTON_A, android.view.KeyEvent.KEYCODE_DPAD_CENTER,
                android.view.KeyEvent.KEYCODE_ENTER -> navKeyTick++
            }
        }
        return super.dispatchKeyEvent(event)
    }

    private val systemActions = SystemActions(
        checkUpdate = { AppUpdater.check(this) },
        installUpdate = { AppUpdater.downloadAndInstall(this, it) },
        setReports = { enabled ->
            Diagnostics.setReportsEnabled(enabled)
            reportsEnabled = Diagnostics.reportsEnabled
            if (enabled) Diagnostics.uploadPending()
        },
        sendReports = { Diagnostics.uploadPending() },
    )
    private var folders by mutableStateOf(emptyList<String>())

    private val pickFolder = registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
        if (uri != null) onFolderPicked(uri)
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        soundEngine = ConsoleUiSoundEngine(this)
        sounds = UiSounds(
            navigate = soundEngine::navigate,
            confirm = soundEngine::confirm,
            back = soundEngine::back,
            error = soundEngine::error,
        )
        prefs = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
        folders = LauncherLogic.decodeFolders(prefs.getString(KEY_FOLDERS, null))
        hideSystemBars()

        GameArtRepository.init(this)
        setContent {
            val library by LibraryRepository.state.collectAsStateWithLifecycle()
            val art by GameArtRepository.art.collectAsStateWithLifecycle()
            val update by AppUpdater.state.collectAsStateWithLifecycle()
            val pendingReports by Diagnostics.pending.collectAsStateWithLifecycle()
            LaunchedEffect(library.games) { GameArtRepository.ensure(library.games) }
            val system = SystemUi(
                update = update,
                currentVersion = AppUpdater.currentVersion,
                reportsEnabled = reportsEnabled,
                pendingReports = pendingReports,
                reportsConfigured = Diagnostics.uploadConfigured,
            )
            if (!hasStorage) {
                StoragePermissionScreen(sounds = sounds, onGrant = ::openStorageSettings, onExit = ::finish)
            } else {
                LauncherScreen(
                    library = library,
                    art = art,
                    folders = folders,
                    sounds = sounds,
                    onScan = { LibraryRepository.scan(folders) },
                    onAddFolder = ::launchFolderPicker,
                    onRemoveFolder = ::removeFolder,
                    onPlay = ::play,
                    onExit = ::finish,
                    system = system,
                    actions = systemActions,
                    navKeyTick = navKeyTick,
                )
            }
        }
        handleLaunchExtra(intent)
    }

    override fun onResume() {
        super.onResume()
        hideSystemBars()
        GameProcess.refresh(this)  // ¿hay un juego suspendido en memoria? (otro proceso)
        hasStorage = Environment.isExternalStorageManager()
        if (hasStorage && !LibraryRepository.state.value.scannedOnce) LibraryRepository.scan(folders)
        reportsEnabled = Diagnostics.reportsEnabled
        AppUpdater.checkIfDue(this)
        Diagnostics.uploadPending()
    }

    override fun onDestroy() {
        soundEngine.release()
        super.onDestroy()
    }

    private fun hideSystemBars() {
        WindowCompat.setDecorFitsSystemWindows(window, false)
        WindowCompat.getInsetsController(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }
    }

    private fun openStorageSettings() {
        val intent = Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, "package:$packageName".toUri())
        try {
            startActivity(intent)
        } catch (_: ActivityNotFoundException) {
            startActivity(Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION))
        }
    }

    private fun launchFolderPicker() {
        try {
            pickFolder.launch(null)
        } catch (_: ActivityNotFoundException) {
            sounds.error()
            Toast.makeText(this, getString(R.string.toast_no_picker), Toast.LENGTH_LONG).show()
        }
    }

    private fun onFolderPicked(uri: Uri) {
        val path = LauncherLogic.treeUriToPath(uri.toString())
        if (path == null) {
            sounds.error()
            Toast.makeText(this, getString(R.string.toast_pick_internal), Toast.LENGTH_LONG).show()
            return
        }
        saveFolders(LauncherLogic.addFolder(folders, path))
        LibraryRepository.scan(folders)
    }

    private fun removeFolder(path: String) {
        saveFolders(LauncherLogic.removeFolder(folders, path))
        LibraryRepository.scan(folders)
    }

    private fun saveFolders(list: List<String>) {
        folders = list
        prefs.edit { putString(KEY_FOLDERS, LauncherLogic.encodeFolders(list)) }
    }

    private fun play(game: PortableGame) {
        if (!File(game.exePath).isFile) {
            sounds.error()
            Toast.makeText(this, getString(R.string.toast_exe_missing, game.exePath), Toast.LENGTH_LONG).show()
            LibraryRepository.scan(folders)
            return
        }
        sounds.confirm()
        launchSession(game.exePath)
    }

    /** Abre [exePath] en el proceso del juego; antes cierra otro juego que siga en memoria. */
    private fun launchSession(exePath: String) {
        lifecycleScope.launch {
            withContext(Dispatchers.IO) { GameProcess.prepareFor(applicationContext, exePath) }
            GameSessionActivity.start(this@MainActivity, exePath)
        }
    }

    override fun onNewIntent(intent: Intent) {
        super.onNewIntent(intent)
        handleLaunchExtra(intent)
    }

    /** Una sesión abierta pidió otro juego: se abre aquí, tras cerrar su proceso. */
    private fun handleLaunchExtra(intent: Intent?) {
        val exe = intent?.getStringExtra(EXTRA_LAUNCH_EXE)?.takeIf { it.isNotEmpty() } ?: return
        intent.removeExtra(EXTRA_LAUNCH_EXE)
        launchSession(exe)
    }

    companion object {
        const val EXTRA_LAUNCH_EXE = "com.murdoc820.hyperplay.extra.LAUNCH_EXE"
        private const val PREFS_NAME = "launcher"
        private const val KEY_FOLDERS = "library_folders"
    }
}
