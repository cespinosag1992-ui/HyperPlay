package com.murdoc820.hyperplay

import android.view.KeyEvent as AndroidKeyEvent
import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.clickable
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.CircularProgressIndicator
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.darkColorScheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.runtime.withFrameNanos
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.blur
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.scale
import androidx.compose.ui.draw.shadow
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.murdoc820.hyperplay.art.GameArt
import com.murdoc820.hyperplay.engine.HyperPlayNative
import com.murdoc820.hyperplay.model.PortableGame
import com.murdoc820.hyperplay.session.GameProcess
import com.murdoc820.hyperplay.session.QuickResumeLogic
import com.murdoc820.hyperplay.ui.ArtImage
import com.murdoc820.hyperplay.ui.ClockAndBattery
import com.murdoc820.hyperplay.ui.Deck
import com.murdoc820.hyperplay.ui.HintBar
import com.murdoc820.hyperplay.ui.Mac
import com.murdoc820.hyperplay.update.ReleaseInfo
import com.murdoc820.hyperplay.update.UpdateState
import java.io.File

private val HyperScheme = darkColorScheme(
    primary = Deck.Accent,
    background = Deck.Bg,
    surface = Deck.Panel,
    onBackground = Deck.Text,
    onSurface = Deck.Text,
)

/** Callbacks de sonido para que las pantallas no dependan del motor de audio. */
class UiSounds(
    val navigate: () -> Unit = {},
    val confirm: () -> Unit = {},
    val back: () -> Unit = {},
    val error: () -> Unit = {},
)

@Composable
fun HyperTheme(content: @Composable () -> Unit) {
    MaterialTheme(colorScheme = HyperScheme) {
        Box(Modifier.fillMaxSize().background(Deck.Bg)) { content() }
    }
}

/** Botón A del mando activa el elemento enfocado (Enter/D-pad centro los maneja `clickable`). */
private fun Modifier.gamepadActivate(onActivate: () -> Unit): Modifier = onPreviewKeyEvent { e ->
    if (e.nativeKeyEvent.keyCode == AndroidKeyEvent.KEYCODE_BUTTON_A) {
        if (e.type == KeyEventType.KeyUp) onActivate()
        true
    } else {
        false
    }
}

private val DPAD_KEYS = setOf(
    AndroidKeyEvent.KEYCODE_DPAD_UP, AndroidKeyEvent.KEYCODE_DPAD_DOWN,
    AndroidKeyEvent.KEYCODE_DPAD_LEFT, AndroidKeyEvent.KEYCODE_DPAD_RIGHT,
    AndroidKeyEvent.KEYCODE_TAB,
)

enum class MacButtonStyle { PRIMARY, SECONDARY, PLAIN, DESTRUCTIVE }

/** Botón del launcher: violeta relleno (principal), gris (secundario) o texto (plano). */
@Composable
fun MacButton(
    text: String,
    onClick: () -> Unit,
    modifier: Modifier = Modifier,
    style: MacButtonStyle = MacButtonStyle.SECONDARY,
    large: Boolean = false,
    onFocused: () -> Unit = {},
) {
    var focused by remember { mutableStateOf(false) }
    val shape = RoundedCornerShape(if (large) 8.dp else 6.dp)
    val bg = when (style) {
        MacButtonStyle.PRIMARY -> if (focused) Deck.AccentHi else Deck.Accent
        MacButtonStyle.SECONDARY -> if (focused) Deck.Line else Deck.Raised
        MacButtonStyle.PLAIN, MacButtonStyle.DESTRUCTIVE -> if (focused) Deck.Raised else Color.Transparent
    }
    val fg = when (style) {
        MacButtonStyle.PRIMARY, MacButtonStyle.SECONDARY -> Deck.Text
        MacButtonStyle.PLAIN -> Deck.AccentHi
        MacButtonStyle.DESTRUCTIVE -> Deck.Red
    }
    val scale by animateFloatAsState(if (focused) 1.04f else 1f, tween(120), label = "btn")
    Box(
        modifier
            .scale(scale)
            .onFocusChanged {
                if (it.isFocused && !focused) onFocused()
                focused = it.isFocused
            }
            .gamepadActivate(onClick)
            .then(if (focused) Modifier.border(2.dp, Deck.Text, shape) else Modifier)
            .background(bg, shape)
            .clickable(role = Role.Button, onClick = onClick)
            .heightIn(min = if (large) 52.dp else 36.dp)
            .padding(horizontal = if (large) 40.dp else 16.dp, vertical = if (large) 13.dp else 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Text(
            text, color = fg,
            fontSize = if (large) 18.sp else 14.sp,
            fontWeight = if (style == MacButtonStyle.PRIMARY) FontWeight.Bold else FontWeight.SemiBold,
            maxLines = 1, overflow = TextOverflow.Ellipsis,
        )
    }
}

/**
 * Caché persistente del JIT (`_HyperPlaySaves/hypercache/oryon_jit.idx`, 16 B por bloque): se llena
 * sola al jugar y hace más rápidos los arranques siguientes.
 */
internal fun jitCacheStats(game: PortableGame): Pair<Long, Long>? {
    val dir = File(game.mountFolder, "_HyperPlaySaves/hypercache")
    val idx = File(dir, "oryon_jit.idx").length()
    val dat = File(dir, "oryon_jit.dat").length()
    if (idx <= 0L) return null
    return idx / 16 to (dat + idx) / (1024 * 1024)
}

/** Texto de la caché en el idioma del sistema: bloques y MB, o "se crea la primera vez". */
@Composable
private fun jitCacheLabel(stats: Pair<Long, Long>?): String {
    if (stats == null) return stringResource(R.string.cache_first_time)
    val (blocks, mb) = stats
    val k = if (blocks >= 1000) stringResource(R.string.thousands, (blocks / 1000).toInt()) else "$blocks"
    return stringResource(R.string.cache_stats, k, mb.toInt())
}

private fun isHeld(game: PortableGame): Boolean =
    GameProcess.heldExe?.let { QuickResumeLogic.sameExe(it, game.exePath) } == true

private enum class Page { HOME, GAME, SETTINGS }

/** Cambia cada vez que hay que recolocar el foco (tecla del mando sin nada enfocado). */
private val LocalFocusTick = androidx.compose.runtime.compositionLocalOf { 0 }

/** Enfoca [requester] al aparecer la página y cada vez que [LocalFocusTick] cambia. */
@Composable
private fun FocusOnTick(requester: FocusRequester, vararg keys: Any?) {
    val tick = LocalFocusTick.current
    LaunchedEffect(tick, *keys) {
        withFrameNanos { }
        withFrameNanos { }
        runCatching { requester.requestFocus() }
    }
}

/** Estado de actualizaciones e informes de errores para la UI. */
data class SystemUi(
    val update: UpdateState = UpdateState.Idle,
    val currentVersion: String = "",
    /** null = aún no se preguntó si enviar informes. */
    val reportsEnabled: Boolean? = false,
    val pendingReports: Int = 0,
    val reportsConfigured: Boolean = false,
)

/** Acciones de actualizaciones / informes. */
class SystemActions(
    val checkUpdate: () -> Unit = {},
    val installUpdate: (ReleaseInfo) -> Unit = {},
    val setReports: (Boolean) -> Unit = {},
    val sendReports: () -> Unit = {},
)

/**
 * Launcher estilo consola portátil: Inicio (arte del juego enfocado de fondo + cápsulas), página
 * del juego (héroe, logo, Jugar, ficha oficial) y Ajustes (carpetas). LB/RB cambian de pestaña,
 * A abre / juega, X juega directo, Y busca juegos y B vuelve.
 */
@Composable
fun LauncherScreen(
    library: LibraryState,
    art: Map<String, GameArt>,
    folders: List<String>,
    sounds: UiSounds,
    onScan: () -> Unit,
    onAddFolder: () -> Unit,
    onRemoveFolder: (String) -> Unit,
    onPlay: (PortableGame) -> Unit,
    onExit: () -> Unit,
    system: SystemUi = SystemUi(),
    actions: SystemActions = SystemActions(),
    /** Sube con cada tecla de navegación del mando (MainActivity.dispatchKeyEvent). */
    navKeyTick: Int = 0,
) {
    val games = library.games
    var selected by remember { mutableIntStateOf(0) }
    LaunchedEffect(games.size) { if (selected > games.lastIndex) selected = games.lastIndex.coerceAtLeast(0) }
    var page by remember { mutableStateOf(Page.HOME) }
    var keyNavPending by remember { mutableStateOf(false) }
    var folderToRemove by remember { mutableStateOf<String?>(null) }
    // Android arranca en modo táctil (nada enfocable): la primera tecla del mando sin foco pide
    // a la página que enfoque su elemento principal (portada seleccionada, JUGAR, primer botón).
    var anyFocus by remember { mutableStateOf(false) }
    var focusTick by remember { mutableIntStateOf(0) }
    // Al volver de un juego (la ventana recupera el foco) Compose puede creer que algo sigue
    // enfocado sin que lo esté: la primera tecla del mando vuelve a colocar el foco.
    var staleFocus by remember { mutableStateOf(false) }
    // ¿Ya se usó el mando aquí? (algo tuvo el foco). Solo entonces se recoloca el foco solo.
    var usedKeys by remember { mutableStateOf(false) }
    LaunchedEffect(anyFocus) { if (anyFocus) usedKeys = true }
    val windowFocused = androidx.compose.ui.platform.LocalWindowInfo.current.isWindowFocused
    LaunchedEffect(windowFocused) {
        if (!windowFocused) {
            staleFocus = true
        } else if (staleFocus && (usedKeys || navKeyTick > 0)) {
            // (navKeyTick > 0: la pulsación llegó con la capa encima y no pudo enfocar nada)
            // La ventana vuelve (de un juego, de una capa de RedMagic/Game Space, de un aviso):
            // se recoloca el foco ya, sin gastar la siguiente pulsación del usuario en ello.
            withFrameNanos { }
            staleFocus = false
            focusTick++
        }
    }
    // Sin nada enfocado Compose no entrega las teclas a nadie: la actividad avisa con navKeyTick.
    LaunchedEffect(navKeyTick) {
        if (navKeyTick > 0 && (!anyFocus || staleFocus)) { staleFocus = false; focusTick++ }
    }

    val focusMoved: () -> Unit = {
        if (keyNavPending) sounds.navigate()
        keyNavPending = false
    }
    val goBack: () -> Unit = {
        sounds.back()
        if (page == Page.HOME) onExit() else page = Page.HOME
    }
    BackHandler(onBack = goBack)

    HyperTheme {
        Box(
            Modifier
                .fillMaxSize()
                .onFocusChanged { anyFocus = it.hasFocus }
                .focusGroup()
                .onPreviewKeyEvent { e ->
                    val code = e.nativeKeyEvent.keyCode
                    val up = e.type == KeyEventType.KeyUp
                    when {
                        (!anyFocus || staleFocus) && folderToRemove == null && (code in DPAD_KEYS ||
                            code == AndroidKeyEvent.KEYCODE_BUTTON_A ||
                            code == AndroidKeyEvent.KEYCODE_ENTER || code == AndroidKeyEvent.KEYCODE_DPAD_CENTER) -> {
                            if (!up) { staleFocus = false; focusTick++ }
                            true
                        }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_B -> { if (up) goBack(); true }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_X && page == Page.HOME -> {
                            if (up) games.getOrNull(selected)?.let(onPlay)
                            true
                        }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_Y && page != Page.GAME -> {
                            if (up) { sounds.confirm(); if (folders.isEmpty()) onAddFolder() else onScan() }
                            true
                        }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_L1 || code == AndroidKeyEvent.KEYCODE_BUTTON_R1 -> {
                            if (up && page != Page.GAME) {
                                sounds.navigate()
                                page = if (page == Page.HOME) Page.SETTINGS else Page.HOME
                            }
                            true
                        }
                        code in DPAD_KEYS && e.type == KeyEventType.KeyDown -> { keyNavPending = true; false }
                        else -> false
                    }
                },
        ) {
            val focusedGame = games.getOrNull(selected)
            Backdrop(focusedGame?.let { art[it.exePath] }, dimmed = page != Page.HOME)
            androidx.compose.runtime.CompositionLocalProvider(LocalFocusTick provides focusTick) {
            AnimatedContent(page, transitionSpec = { fadeIn(tween(160)) togetherWith fadeOut(tween(120)) },
                label = "page") { p ->
                when (p) {
                    Page.HOME -> HomePage(
                        library, art, selected, folders,
                        onSelect = { i -> if (i != selected) { selected = i; focusMoved() } },
                        onOpen = { i -> selected = i; sounds.confirm(); page = Page.GAME },
                        onTab = { page = it },
                        onScan = { sounds.confirm(); onScan() },
                        onAddFolder = { sounds.confirm(); onAddFolder() },
                        update = system.update,
                        onUpdateBanner = { sounds.confirm(); page = Page.SETTINGS },
                    )
                    Page.GAME -> focusedGame?.let { g ->
                        GamePage(g, art[g.exePath], onFocused = focusMoved, onPlay = { onPlay(g) },
                            onBack = goBack)
                    } ?: Box(Modifier.fillMaxSize())
                    Page.SETTINGS -> SettingsPage(
                        library, folders, onFocused = focusMoved,
                        onTab = { page = it },
                        onScan = { sounds.confirm(); onScan() },
                        onAddFolder = { sounds.confirm(); onAddFolder() },
                        onFolderClick = { sounds.confirm(); folderToRemove = it },
                        system = system,
                        actions = actions,
                        sounds = sounds,
                    )
                }
            }
            }
        }
        if (system.reportsEnabled == null) {
            AlertDialog(
                onDismissRequest = {},
                containerColor = Deck.Panel,
                shape = Deck.Card,
                title = { Text(stringResource(R.string.consent_title), color = Deck.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold) },
                text = {
                    Text(
                        stringResource(R.string.consent_body),
                        color = Deck.Sub, fontSize = 14.sp, lineHeight = 20.sp,
                    )
                },
                confirmButton = {
                    MacButton(stringResource(R.string.consent_send), style = MacButtonStyle.PRIMARY, onClick = {
                        sounds.confirm(); actions.setReports(true)
                    })
                },
                dismissButton = { MacButton(stringResource(R.string.consent_decline), onClick = { sounds.back(); actions.setReports(false) }) },
            )
        }
        folderToRemove?.let { folder ->
            AlertDialog(
                onDismissRequest = { folderToRemove = null },
                containerColor = Deck.Panel,
                shape = Deck.Card,
                title = { Text(stringResource(R.string.remove_folder_title), color = Deck.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold) },
                text = {
                    Text(stringResource(R.string.remove_folder_body, LauncherLogic.displayFolder(folder)),
                        color = Deck.Sub, fontSize = 14.sp)
                },
                confirmButton = {
                    MacButton(stringResource(R.string.remove), style = MacButtonStyle.PRIMARY, onClick = {
                        folderToRemove = null
                        sounds.confirm()
                        onRemoveFolder(folder)
                    })
                },
                dismissButton = { MacButton(stringResource(R.string.cancel), onClick = { folderToRemove = null; sounds.back() }) },
            )
        }
    }
}

/** Fondo: arte panorámico del juego enfocado, desenfocado y oscurecido (o el degradado de la marca). */
@Composable
private fun Backdrop(art: GameArt?, dimmed: Boolean) {
    Box(Modifier.fillMaxSize().background(Deck.BackdropBrush)) {
        val file = art?.hero ?: art?.header
        if (file != null) {
            ArtImage(file, Modifier.fillMaxSize().blur(if (dimmed) 0.dp else 18.dp))
        }
        // Brillo de marca arriba a la izquierda + velo para que el texto se lea siempre.
        Box(Modifier.fillMaxSize().background(
            Brush.radialGradient(listOf(Deck.AccentGlow, Color.Transparent), radius = 1400f,
                center = androidx.compose.ui.geometry.Offset(0f, 0f))))
        Box(Modifier.fillMaxSize().background(
            Brush.verticalGradient(listOf(Color(0x66060810), Color(0xCC060810), Deck.Bg))))
    }
}

@Composable
private fun TopBar(current: Page, onTab: (Page) -> Unit) {
    Row(
        Modifier.fillMaxWidth().windowInsetsPadding(WindowInsets.displayCutout)
            .padding(horizontal = 28.dp, vertical = 8.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        com.murdoc820.hyperplay.ui.HyperLogo(28.dp)
        Spacer(Modifier.width(10.dp))
        Text(stringResource(R.string.brand_upper), color = Deck.Text, fontSize = 16.sp, fontWeight = FontWeight.Black, letterSpacing = 2.sp)
        Spacer(Modifier.width(36.dp))
        com.murdoc820.hyperplay.ui.ButtonGlyph("LB")
        Spacer(Modifier.width(12.dp))
        TabLabel(stringResource(R.string.tab_home), current == Page.HOME) { onTab(Page.HOME) }
        Spacer(Modifier.width(22.dp))
        TabLabel(stringResource(R.string.tab_settings), current == Page.SETTINGS) { onTab(Page.SETTINGS) }
        Spacer(Modifier.width(12.dp))
        com.murdoc820.hyperplay.ui.ButtonGlyph("RB")
        Spacer(Modifier.weight(1f))
        ClockAndBattery()
    }
}

@Composable
private fun TabLabel(text: String, active: Boolean, onClick: () -> Unit) {
    // Solo táctil: con el mando las pestañas se cambian con LB/RB. Si la cruceta pudiera
    // enfocarlas, "arriba" desde las portadas dejaría el foco en un sitio que no se ve.
    Column(horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier
            .focusProperties { canFocus = false }
            .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onClick)) {
        Text(text, color = if (active) Deck.Text else Deck.Dim, fontSize = 16.sp,
            fontWeight = if (active) FontWeight.Bold else FontWeight.SemiBold)
        Spacer(Modifier.height(4.dp))
        Box(Modifier.size(width = 24.dp, height = 3.dp).background(if (active) Deck.Accent else Color.Transparent, Deck.Pill))
    }
}

// ------------------------------------------------------------------ Inicio

@Composable
private fun HomePage(
    library: LibraryState,
    art: Map<String, GameArt>,
    selected: Int,
    folders: List<String>,
    onSelect: (Int) -> Unit,
    onOpen: (Int) -> Unit,
    onTab: (Page) -> Unit,
    onScan: () -> Unit,
    onAddFolder: () -> Unit,
    update: UpdateState,
    onUpdateBanner: () -> Unit,
) {
    val games = library.games
    val listState = rememberLazyListState()
    val requester = remember { FocusRequester() }
    val scanRequester = remember { FocusRequester() }
    val tick = LocalFocusTick.current
    LaunchedEffect(games.isEmpty(), tick) {
        withFrameNanos { }
        if (games.isNotEmpty()) {
            listState.scrollToItem(selected.coerceIn(0, games.lastIndex))
            withFrameNanos { }
            runCatching { requester.requestFocus() }
        } else {
            runCatching { scanRequester.requestFocus() }
        }
    }
    Column(Modifier.fillMaxSize()) {
        TopBar(Page.HOME, onTab)
        (update as? UpdateState.Available)?.let { av ->
            var bannerFocused by remember { mutableStateOf(false) }
            Row(
                Modifier.padding(horizontal = 40.dp).background(Color(0xCC1D2438), Deck.Pill)
                    .border(2.dp, if (bannerFocused) Deck.Text else Color.Transparent, Deck.Pill)
                    .onFocusChanged { bannerFocused = it.isFocused }
                    .gamepadActivate(onUpdateBanner)
                    .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onUpdateBanner)
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(8.dp).background(Deck.Cyan, RoundedCornerShape(50)))
                Spacer(Modifier.width(10.dp))
                Text(stringResource(R.string.update_banner, av.release.version),
                    color = Deck.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            }
        }
        val game = games.getOrNull(selected)
        Box(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 40.dp), contentAlignment = Alignment.BottomStart) {
            if (game != null) FocusedGameHeader(game, art[game.exePath])
            else EmptyLibrary(library, folders, scanRequester, onScan, onAddFolder)
        }
        if (games.isNotEmpty()) {
            Row(Modifier.padding(start = 40.dp, top = 6.dp, bottom = 0.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(R.string.library), color = Deck.Text, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.width(10.dp))
                Text(pluralStringResource(R.plurals.games_count, games.size, games.size), color = Deck.Dim, fontSize = 12.sp)
                if (library.scanning) {
                    Spacer(Modifier.width(12.dp))
                    CircularProgressIndicator(Modifier.size(14.dp), color = Deck.Accent, strokeWidth = 2.dp)
                }
            }
            LazyRow(
                state = listState,
                contentPadding = PaddingValues(horizontal = 40.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
                modifier = Modifier.fillMaxWidth().height(166.dp).focusGroup(),
            ) {
                itemsIndexed(games, key = { _, g -> g.exePath }) { i, g ->
                    GameCapsule(
                        game = g, art = art[g.exePath], isSelected = i == selected,
                        modifier = if (i == selected) Modifier.focusRequester(requester) else Modifier,
                        onFocused = { onSelect(i) },
                        onOpen = { onOpen(i) },
                    )
                }
            }
        }
        HintBar(
            if (games.isEmpty()) {
                listOf("Y" to stringResource(if (folders.isEmpty()) R.string.choose_folder else R.string.hint_find_games),
                    "B" to stringResource(R.string.hint_exit))
            } else {
                listOf("A" to stringResource(R.string.hint_open), "X" to stringResource(R.string.hint_play),
                    "Y" to stringResource(R.string.hint_find_games), "B" to stringResource(R.string.hint_exit))
            },
            Modifier.fillMaxWidth(),
        )
    }
}

/** Título (logo oficial si lo hay), estado y ficha breve del juego enfocado. */
@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun FocusedGameHeader(game: PortableGame, art: GameArt?) {
    val info = art?.info
    Column(Modifier.widthIn(max = 620.dp).padding(bottom = 2.dp)) {
        GameLogo(game, art, Modifier.heightIn(max = 62.dp).widthIn(max = 300.dp), compact = true)
        Spacer(Modifier.height(6.dp))
        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            if (isHeld(game)) Chip(stringResource(R.string.chip_held_home), Deck.Green)
            info?.genres?.take(3)?.forEach { Chip(it) }
            if (info == null) Chip(stringResource(if (game.pe.is64Bit) R.string.chip_windows_64 else R.string.chip_windows_32))
        }
        info?.description?.takeIf { it.isNotBlank() }?.let {
            Spacer(Modifier.height(4.dp))
            Text(it, color = Deck.Sub, fontSize = 12.sp, maxLines = 2, overflow = TextOverflow.Ellipsis, lineHeight = 16.sp)
        }
    }
}

@Composable
private fun GameLogo(game: PortableGame, art: GameArt?, modifier: Modifier, compact: Boolean = false) {
    val title = art?.info?.name?.takeIf { it.isNotBlank() } ?: game.title
    val logo = art?.logo
    if (logo != null) {
        ArtImage(logo, modifier.aspectRatio(2.6f), contentScale = ContentScale.Fit, alignment = Alignment.CenterStart,
            fallback = { BigTitle(title, compact) })
    } else {
        BigTitle(title, compact)
    }
}

@Composable
private fun BigTitle(title: String, compact: Boolean = false) {
    Text(title, color = Deck.Text, fontSize = if (compact) 26.sp else 30.sp, fontWeight = FontWeight.Black, maxLines = 2,
        overflow = TextOverflow.Ellipsis, lineHeight = if (compact) 30.sp else 34.sp)
}

@Composable
private fun Chip(text: String, color: Color = Deck.Sub) {
    Text(text, color = color, fontSize = 12.sp, fontWeight = FontWeight.SemiBold,
        modifier = Modifier.background(Color(0x991D2438), Deck.Pill).padding(horizontal = 10.dp, vertical = 4.dp))
}

/** Cápsula vertical (arte 600×900 oficial; si no, icono del exe sobre un degradado). */
@Composable
private fun GameCapsule(
    game: PortableGame,
    art: GameArt?,
    isSelected: Boolean,
    modifier: Modifier,
    onFocused: () -> Unit,
    onOpen: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    val scale by animateFloatAsState(if (focused) 1.08f else 1f, tween(140), label = "capsule")
    Box(
        modifier
            .width(100.dp)
            .fillMaxHeight()
            .padding(vertical = 8.dp),
        contentAlignment = Alignment.Center,
    ) {
        Box(
            Modifier
                .scale(scale)
                .fillMaxSize()
                .then(if (focused) Modifier.shadow(18.dp, Deck.Capsule, ambientColor = Deck.Accent, spotColor = Deck.Accent) else Modifier)
                .clip(Deck.Capsule)
                .background(Deck.Raised)
                .then(if (focused) Modifier.border(3.dp, Deck.Text, Deck.Capsule) else Modifier)
                .onFocusChanged {
                    if (it.isFocused && !focused) onFocused()
                    focused = it.isFocused
                }
                .gamepadActivate(onOpen)
                .clickable(role = Role.Button, onClick = onOpen),
        ) {
            val capsule = art?.capsule
            if (capsule != null) {
                ArtImage(capsule, Modifier.fillMaxSize(), fallback = { IconCapsule(game, art) })
            } else {
                IconCapsule(game, art)
            }
            if (isHeld(game)) {
                Box(Modifier.align(Alignment.TopEnd).padding(8.dp).size(10.dp).background(Deck.Green, RoundedCornerShape(50)))
            }
        }
    }
}

/** Sin arte oficial: icono del exe (o la cabecera horizontal) y el título. */
@Composable
private fun IconCapsule(game: PortableGame, art: GameArt?) {
    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color(0xFF262E48), Color(0xFF151A2B))))) {
        Column(Modifier.fillMaxSize().padding(12.dp), horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.Center) {
            val header = art?.header
            val icon = art?.icon
            when {
                header != null -> ArtImage(header, Modifier.fillMaxWidth().aspectRatio(460f / 215f).clip(RoundedCornerShape(4.dp)))
                icon != null -> ArtImage(icon, Modifier.size(48.dp), contentScale = ContentScale.Fit)
                else -> Box(Modifier.size(48.dp).background(Brush.linearGradient(listOf(Deck.Accent, Deck.Cyan)), RoundedCornerShape(16.dp)))
            }
            Spacer(Modifier.height(14.dp))
            Text(art?.info?.name?.takeIf { it.isNotBlank() } ?: game.title, color = Deck.Text, fontSize = 11.sp,
                fontWeight = FontWeight.Bold, textAlign = TextAlign.Center, maxLines = 3, overflow = TextOverflow.Ellipsis)
        }
    }
}

@Composable
private fun EmptyLibrary(
    library: LibraryState,
    folders: List<String>,
    scanRequester: FocusRequester,
    onScan: () -> Unit,
    onAddFolder: () -> Unit,
) {
    Column(Modifier.fillMaxWidth().padding(bottom = 40.dp)) {
        if (folders.isEmpty()) {
            // Primer arranque: no hay carpetas por defecto, el usuario elige dónde tiene sus juegos.
            Text(stringResource(R.string.library_pick_title), color = Deck.Text, fontSize = 30.sp, fontWeight = FontWeight.Black)
            Spacer(Modifier.height(10.dp))
            Text(stringResource(R.string.library_pick_body), color = Deck.Sub, fontSize = 15.sp,
                modifier = Modifier.widthIn(max = 560.dp))
            Spacer(Modifier.height(22.dp))
            MacButton(stringResource(R.string.choose_folder), onClick = onAddFolder,
                style = MacButtonStyle.PRIMARY, large = true, modifier = Modifier.focusRequester(scanRequester))
            return@Column
        }
        Text(stringResource(if (library.scanning || !library.scannedOnce) R.string.library_searching else R.string.library_empty),
            color = Deck.Text, fontSize = 30.sp, fontWeight = FontWeight.Black)
        Spacer(Modifier.height(10.dp))
        Text(stringResource(R.string.library_empty_hint, folders.joinToString(", ", transform = LauncherLogic::displayFolder)),
            color = Deck.Sub, fontSize = 15.sp, modifier = Modifier.widthIn(max = 560.dp))
        Spacer(Modifier.height(22.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            MacButton(stringResource(if (library.scanning) R.string.searching else R.string.hint_find_games), onClick = onScan,
                style = MacButtonStyle.PRIMARY, large = true, modifier = Modifier.focusRequester(scanRequester))
            MacButton(stringResource(R.string.add_folder), onClick = onAddFolder, large = true)
        }
    }
}

// ------------------------------------------------------------------ Página del juego

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun GamePage(
    game: PortableGame,
    art: GameArt?,
    onFocused: () -> Unit,
    onPlay: () -> Unit,
    onBack: () -> Unit,
) {
    val info = art?.info
    val held = isHeld(game)
    val playRequester = remember { FocusRequester() }
    FocusOnTick(playRequester, game.exePath)
    val cache = jitCacheLabel(remember(game.exePath) { jitCacheStats(game) })
    // Todo en una pantalla (en horizontal el teléfono tiene ~370 dp de alto): el arte del juego es
    // el fondo, a la izquierda logo + descripción + JUGAR y a la derecha la ficha compacta.
    Box(Modifier.fillMaxSize()) {
        val hero = art?.hero ?: art?.header
        if (hero != null) ArtImage(hero, Modifier.fillMaxSize(), alignment = Alignment.TopEnd)
        Box(Modifier.fillMaxSize().background(Brush.horizontalGradient(
            0f to Color(0xF20A0D16), 0.45f to Color(0xCC0A0D16), 1f to Color(0x400A0D16))))
        Box(Modifier.fillMaxSize().background(Brush.verticalGradient(
            0.55f to Color.Transparent, 1f to Color(0xE60A0D16))))
        Column(Modifier.fillMaxSize()) {
            Box(Modifier.fillMaxWidth().windowInsetsPadding(WindowInsets.displayCutout)) { TopBarCompact(onBack) }
            Row(
                Modifier.weight(1f).fillMaxWidth().windowInsetsPadding(WindowInsets.displayCutout)
                    .padding(start = 36.dp, end = 28.dp, bottom = 4.dp),
                horizontalArrangement = Arrangement.spacedBy(28.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Column(Modifier.weight(1.15f)) {
                    GameLogo(game, art, Modifier.heightIn(max = 72.dp).widthIn(max = 340.dp), compact = true)
                    if (!info?.genres.isNullOrEmpty() || held) {
                        Spacer(Modifier.height(8.dp))
                        FlowRow(horizontalArrangement = Arrangement.spacedBy(6.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                            if (held) Chip(stringResource(R.string.chip_held), Deck.Green)
                            info?.genres?.take(4)?.forEach { Chip(it) }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(info?.description?.takeIf { it.isNotBlank() }
                        ?: stringResource(R.string.no_game_info),
                        color = Deck.Sub, fontSize = 13.sp, lineHeight = 18.sp, maxLines = 4, overflow = TextOverflow.Ellipsis)
                    Spacer(Modifier.height(14.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        MacButton(stringResource(if (held) R.string.continue_button else R.string.play_button), onClick = onPlay, style = MacButtonStyle.PRIMARY,
                            onFocused = onFocused, modifier = Modifier.focusRequester(playRequester).width(150.dp))
                        Spacer(Modifier.width(14.dp))
                        Text(if (held) stringResource(R.string.held_hint) else stringResource(R.string.cache_label, cache),
                            color = Deck.Dim, fontSize = 12.sp, maxLines = 2)
                    }
                }
                Column(Modifier.weight(0.85f)) {
                    InfoCard {
                        InfoLine(stringResource(R.string.info_developer), info?.developers?.joinToString(", ").orEmpty().ifBlank { "—" })
                        InfoLine(stringResource(R.string.info_publisher), info?.publishers?.joinToString(", ").orEmpty().ifBlank { "—" })
                        InfoLine(stringResource(R.string.info_release), info?.releaseDate.orEmpty().ifBlank { "—" })
                        InfoLine(stringResource(R.string.info_platform), (if (game.pe.is64Bit) "Windows x64" else "Windows x86") + " · " +
                            LauncherLogic.graphicsApiLabel(game.pe.graphicsApis, game.engineHints))
                        InfoLine(stringResource(R.string.info_executable), File(game.exePath).name)
                        InfoLine(stringResource(R.string.info_saves), "_HyperPlaySaves")
                    }
                }
            }
            HintBar(listOf("A" to stringResource(if (held) R.string.hint_continue else R.string.hint_play),
                "B" to stringResource(R.string.hint_back)), Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun TopBarCompact(onBack: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 28.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(stringResource(R.string.back_to_library_short), color = Deck.Text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold,
            // Solo táctil (con el mando, B): enfocable con la cruceta sería un foco invisible.
            modifier = Modifier.focusProperties { canFocus = false }
                .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onBack))
        Spacer(Modifier.weight(1f))
        ClockAndBattery()
    }
}

@Composable
private fun SectionTitle(text: String) {
    Text(text.uppercase(), color = Deck.Dim, fontSize = 12.sp, fontWeight = FontWeight.Bold, letterSpacing = 1.5.sp,
        modifier = Modifier.padding(bottom = 10.dp))
}

@Composable
private fun InfoCard(content: @Composable () -> Unit) {
    Column(Modifier.fillMaxWidth().background(Color(0xCC141A2B), Deck.Card).padding(horizontal = 14.dp, vertical = 6.dp)) {
        content()
    }
}

@Composable
private fun InfoLine(label: String, value: String) {
    Row(Modifier.fillMaxWidth().padding(vertical = 4.dp), verticalAlignment = Alignment.CenterVertically) {
        Text(label, color = Deck.Dim, fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.width(124.dp).padding(end = 8.dp))
        Text(value, color = Deck.Text, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

// ------------------------------------------------------------------ Ajustes

@Composable
private fun SettingsPage(
    library: LibraryState,
    folders: List<String>,
    onFocused: () -> Unit,
    onTab: (Page) -> Unit,
    onScan: () -> Unit,
    onAddFolder: () -> Unit,
    onFolderClick: (String) -> Unit,
    system: SystemUi,
    actions: SystemActions,
    sounds: UiSounds,
) {
    val first = remember { FocusRequester() }
    FocusOnTick(first)
    Column(Modifier.fillMaxSize()) {
        TopBar(Page.SETTINGS, onTab)
        Row(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 48.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(28.dp)) {
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).focusGroup()) {
                SectionTitle(stringResource(R.string.library))
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    MacButton(stringResource(if (library.scanning) R.string.searching else R.string.hint_find_games), onClick = onScan,
                        style = MacButtonStyle.PRIMARY, onFocused = onFocused, modifier = Modifier.focusRequester(first))
                    MacButton(stringResource(R.string.add_folder), onClick = onAddFolder, onFocused = onFocused)
                }
                val scan = library.scan
                val status = when {
                    library.error != null -> stringResource(R.string.scan_error, library.error)
                    scan != null && scan.total == 0 -> stringResource(R.string.scan_preparing)
                    scan != null -> stringResource(R.string.scan_progress, scan.index, scan.total,
                        LauncherLogic.displayFolder(scan.folder), scan.found)
                    library.scannedOnce -> pluralStringResource(R.plurals.games_count, library.games.size, library.games.size)
                    else -> ""
                }
                if (status.isNotEmpty()) {
                    Spacer(Modifier.height(10.dp))
                    Text(status, color = if (library.error != null) Deck.Orange else Deck.Dim, fontSize = 13.sp)
                }
                Spacer(Modifier.height(22.dp))
                SectionTitle(stringResource(R.string.section_folders))
                if (folders.isEmpty()) {
                    Text(stringResource(R.string.no_folders), color = Deck.Dim, fontSize = 14.sp)
                }
                folders.forEach { f ->
                    FolderRow(f, onFocused, onClick = { onFolderClick(f) })
                    Spacer(Modifier.height(8.dp))
                }
            }
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).focusGroup()) {
                SectionTitle(stringResource(R.string.section_updates))
                UpdateCard(system, actions, onFocused, sounds)
                Spacer(Modifier.height(22.dp))
                SectionTitle(stringResource(R.string.section_reports))
                ReportsCard(system, actions, onFocused, sounds)
                Spacer(Modifier.height(22.dp))
                SectionTitle(stringResource(R.string.section_device))
                InfoCard {
                    InfoLine(stringResource(R.string.info_cpu), "Snapdragon 8 Elite · 2 Prime + 6 Perf")
                    InfoLine(stringResource(R.string.info_gpu), "Adreno 830 · Vulkan")
                    InfoLine(stringResource(R.string.info_games), "${library.games.size}")
                }
                Spacer(Modifier.height(22.dp))
                SectionTitle(stringResource(R.string.section_controllers))
                InfoCard {
                    InfoLine(stringResource(R.string.info_supported), stringResource(R.string.supported_value))
                    InfoLine(stringResource(R.string.info_rumble), stringResource(R.string.rumble_value))
                    InfoLine(stringResource(R.string.info_quick_menu), stringResource(R.string.quick_menu_value))
                }
            }
        }
        HintBar(listOf("A" to stringResource(R.string.hint_select), "Y" to stringResource(R.string.hint_find_games),
            "B" to stringResource(R.string.hint_back)), Modifier.fillMaxWidth())
    }
}

@Composable
private fun UpdateCard(system: SystemUi, actions: SystemActions, onFocused: () -> Unit, sounds: UiSounds) {
    val u = system.update
    InfoCard {
        InfoLine(stringResource(R.string.update_installed), system.currentVersion)
        val status = when (u) {
            UpdateState.Idle -> stringResource(R.string.update_idle)
            UpdateState.Checking -> stringResource(R.string.searching)
            UpdateState.UpToDate -> stringResource(R.string.update_up_to_date)
            is UpdateState.Available -> stringResource(R.string.update_new, u.release.version)
            is UpdateState.Downloading -> stringResource(R.string.update_downloading, u.release.version, (u.progress * 100).toInt())
            is UpdateState.Installing -> stringResource(R.string.update_installing, u.release.version)
            is UpdateState.Error -> u.message
        }
        InfoLine(stringResource(R.string.update_status), status)
        if (u is UpdateState.Downloading) {
            androidx.compose.material3.LinearProgressIndicator(
                progress = { u.progress },
                modifier = Modifier.fillMaxWidth().padding(vertical = 6.dp).height(4.dp).clip(RoundedCornerShape(50)),
                color = Deck.Accent, trackColor = Deck.Line,
            )
        }
        val release = (u as? UpdateState.Available)?.release ?: (u as? UpdateState.Error)?.release
        if (release != null && release.notes.isNotBlank()) {
            Text(release.notes.take(600), color = Deck.Sub, fontSize = 13.sp, lineHeight = 18.sp,
                modifier = Modifier.padding(vertical = 6.dp), maxLines = 8, overflow = TextOverflow.Ellipsis)
        }
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(bottom = 8.dp)) {
            if (release != null) {
                MacButton(stringResource(R.string.update_install), style = MacButtonStyle.PRIMARY, onFocused = onFocused,
                    onClick = { sounds.confirm(); actions.installUpdate(release) })
            }
            if (u !is UpdateState.Downloading && u !is UpdateState.Installing) {
                MacButton(stringResource(R.string.update_check), onFocused = onFocused, onClick = { sounds.confirm(); actions.checkUpdate() })
            }
        }
    }
}

@Composable
private fun ReportsCard(system: SystemUi, actions: SystemActions, onFocused: () -> Unit, sounds: UiSounds) {
    InfoCard {
        InfoLine(stringResource(R.string.consent_send),
            stringResource(if (system.reportsEnabled == true) R.string.state_on else R.string.state_off))
        InfoLine(stringResource(R.string.reports_saved),
            if (system.pendingReports == 0) stringResource(R.string.reports_none)
            else pluralStringResource(R.plurals.reports_pending, system.pendingReports, system.pendingReports))
        if (!system.reportsConfigured) InfoLine(stringResource(R.string.reports_server), stringResource(R.string.reports_server_unset))
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(bottom = 8.dp)) {
            MacButton(stringResource(if (system.reportsEnabled == true) R.string.turn_off else R.string.turn_on), onFocused = onFocused,
                onClick = { sounds.confirm(); actions.setReports(system.reportsEnabled != true) })
            if (system.pendingReports > 0 && system.reportsEnabled == true && system.reportsConfigured) {
                MacButton(stringResource(R.string.send_now), style = MacButtonStyle.PRIMARY, onFocused = onFocused,
                    onClick = { sounds.confirm(); actions.sendReports() })
            }
        }
    }
}

@Composable
private fun FolderRow(path: String, onFocused: () -> Unit, onClick: () -> Unit) {
    var focused by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth()
            .onFocusChanged {
                if (it.isFocused && !focused) onFocused()
                focused = it.isFocused
            }
            .gamepadActivate(onClick)
            .then(if (focused) Modifier.border(2.dp, Deck.Text, Deck.Card) else Modifier)
            .background(if (focused) Deck.Raised else Color(0xB3141A2B), Deck.Card)
            .clickable(role = Role.Button, onClick = onClick)
            .padding(horizontal = 18.dp, vertical = 14.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Text(LauncherLogic.displayFolder(path), color = Deck.Text, fontSize = 15.sp, modifier = Modifier.weight(1f),
            maxLines = 1, overflow = TextOverflow.Ellipsis)
        Text(stringResource(R.string.remove), color = if (focused) Deck.Red else Deck.Dim, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
    }
}

/** Pantalla de permiso "Acceso a todos los archivos" (MANAGE_EXTERNAL_STORAGE). */
@Composable
fun StoragePermissionScreen(sounds: UiSounds, onGrant: () -> Unit, onExit: () -> Unit) {
    val requester = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        withFrameNanos { }
        runCatching { requester.requestFocus() }
    }
    BackHandler { sounds.back(); onExit() }
    HyperTheme {
        Box(
            Modifier.fillMaxSize().background(Deck.BackdropBrush)
                .onPreviewKeyEvent { e ->
                    if (e.nativeKeyEvent.keyCode == AndroidKeyEvent.KEYCODE_BUTTON_B) {
                        if (e.type == KeyEventType.KeyUp) { sounds.back(); onExit() }
                        true
                    } else {
                        false
                    }
                },
            contentAlignment = Alignment.Center,
        ) {
            Column(Modifier.widthIn(max = 560.dp).background(Deck.Panel, Deck.Card).padding(32.dp),
                horizontalAlignment = Alignment.CenterHorizontally) {
                com.murdoc820.hyperplay.ui.HyperLogo(64.dp)
                Spacer(Modifier.height(18.dp))
                Text(stringResource(R.string.perm_title), color = Deck.Text, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(10.dp))
                Text(
                    stringResource(R.string.perm_body),
                    color = Deck.Sub, fontSize = 15.sp, textAlign = TextAlign.Center, lineHeight = 22.sp,
                )
                Spacer(Modifier.height(24.dp))
                MacButton(stringResource(R.string.perm_open), onClick = { sounds.confirm(); onGrant() },
                    modifier = Modifier.focusRequester(requester), style = MacButtonStyle.PRIMARY, large = true)
            }
        }
    }
}

@Suppress("unused")
private val keepMacTokens = Mac.Window
