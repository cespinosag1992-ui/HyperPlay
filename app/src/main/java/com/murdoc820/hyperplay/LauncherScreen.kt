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
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.key.KeyEventType
import androidx.compose.ui.input.key.onPreviewKeyEvent
import androidx.compose.ui.input.key.type
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.murdoc820.hyperplay.art.GameArt
import com.murdoc820.hyperplay.engine.HyperPlayNative
import com.murdoc820.hyperplay.model.PortableGame
import com.murdoc820.hyperplay.session.QuickResume
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
internal fun jitCacheLabel(game: PortableGame): String {
    val dir = File(game.mountFolder, "_HyperPlaySaves/hypercache")
    val idx = File(dir, "oryon_jit.idx").length()
    val dat = File(dir, "oryon_jit.dat").length()
    if (idx <= 0L) return "Se crea la primera vez que juegas"
    val blocks = idx / 16
    val mb = (dat + idx) / (1024 * 1024)
    val k = if (blocks >= 1000) "${blocks / 1000} mil" else "$blocks"
    return "$k bloques · $mb MB"
}

private fun isHeld(game: PortableGame): Boolean =
    QuickResume.isHolding() && QuickResume.heldExe?.let { QuickResumeLogic.sameExe(it, game.exePath) } == true

private enum class Page { HOME, GAME, SETTINGS }

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
) {
    val games = library.games
    var selected by remember { mutableIntStateOf(0) }
    LaunchedEffect(games.size) { if (selected > games.lastIndex) selected = games.lastIndex.coerceAtLeast(0) }
    var page by remember { mutableStateOf(Page.HOME) }
    var keyNavPending by remember { mutableStateOf(false) }
    var folderToRemove by remember { mutableStateOf<String?>(null) }

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
                .onPreviewKeyEvent { e ->
                    val code = e.nativeKeyEvent.keyCode
                    val up = e.type == KeyEventType.KeyUp
                    when {
                        code == AndroidKeyEvent.KEYCODE_BUTTON_B -> { if (up) goBack(); true }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_X && page == Page.HOME -> {
                            if (up) games.getOrNull(selected)?.let(onPlay)
                            true
                        }
                        code == AndroidKeyEvent.KEYCODE_BUTTON_Y && page != Page.GAME -> {
                            if (up) { sounds.confirm(); onScan() }
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
            AnimatedContent(page, transitionSpec = { fadeIn(tween(160)) togetherWith fadeOut(tween(120)) },
                label = "page") { p ->
                when (p) {
                    Page.HOME -> HomePage(
                        library, art, selected, folders,
                        onSelect = { i -> if (i != selected) { selected = i; focusMoved() } },
                        onOpen = { i -> selected = i; sounds.confirm(); page = Page.GAME },
                        onTab = { page = it },
                        onScan = { sounds.confirm(); onScan() },
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
        if (system.reportsEnabled == null) {
            AlertDialog(
                onDismissRequest = {},
                containerColor = Deck.Panel,
                shape = Deck.Card,
                title = { Text("¿Ayudas a mejorar HyperPlay?", color = Deck.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold) },
                text = {
                    Text(
                        "Si un juego o la app fallan, HyperPlay puede enviar un informe con el registro del error, " +
                            "el modelo del teléfono, la versión de Android y el juego. No incluye tus partidas, cuentas " +
                            "ni archivos personales. Los informes siempre se guardan también en tu teléfono y puedes " +
                            "cambiarlo en Ajustes.",
                        color = Deck.Sub, fontSize = 14.sp, lineHeight = 20.sp,
                    )
                },
                confirmButton = {
                    MacButton("Enviar informes", style = MacButtonStyle.PRIMARY, onClick = {
                        sounds.confirm(); actions.setReports(true)
                    })
                },
                dismissButton = { MacButton("No, gracias", onClick = { sounds.back(); actions.setReports(false) }) },
            )
        }
        folderToRemove?.let { folder ->
            AlertDialog(
                onDismissRequest = { folderToRemove = null },
                containerColor = Deck.Panel,
                shape = Deck.Card,
                title = { Text("¿Quitar esta carpeta?", color = Deck.Text, fontSize = 18.sp, fontWeight = FontWeight.Bold) },
                text = {
                    Text("${LauncherLogic.displayFolder(folder)} dejará de aparecer en la biblioteca. Los archivos no se borran.",
                        color = Deck.Sub, fontSize = 14.sp)
                },
                confirmButton = {
                    MacButton("Quitar", style = MacButtonStyle.PRIMARY, onClick = {
                        folderToRemove = null
                        sounds.confirm()
                        onRemoveFolder(folder)
                    })
                },
                dismissButton = { MacButton("Cancelar", onClick = { folderToRemove = null; sounds.back() }) },
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
        Box(Modifier.size(26.dp).background(Brush.linearGradient(listOf(Deck.Accent, Deck.Cyan)), RoundedCornerShape(7.dp)),
            contentAlignment = Alignment.Center) {
            Text("H", color = Color.White, fontSize = 15.sp, fontWeight = FontWeight.Black)
        }
        Spacer(Modifier.width(10.dp))
        Text("HYPERPLAY", color = Deck.Text, fontSize = 16.sp, fontWeight = FontWeight.Black, letterSpacing = 2.sp)
        Spacer(Modifier.width(36.dp))
        com.murdoc820.hyperplay.ui.ButtonGlyph("LB")
        Spacer(Modifier.width(12.dp))
        TabLabel("Inicio", current == Page.HOME) { onTab(Page.HOME) }
        Spacer(Modifier.width(22.dp))
        TabLabel("Ajustes", current == Page.SETTINGS) { onTab(Page.SETTINGS) }
        Spacer(Modifier.width(12.dp))
        com.murdoc820.hyperplay.ui.ButtonGlyph("RB")
        Spacer(Modifier.weight(1f))
        ClockAndBattery()
    }
}

@Composable
private fun TabLabel(text: String, active: Boolean, onClick: () -> Unit) {
    Column(horizontalAlignment = Alignment.CenterHorizontally,
        modifier = Modifier.clickable(remember { MutableInteractionSource() }, indication = null, onClick = onClick)) {
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
    update: UpdateState,
    onUpdateBanner: () -> Unit,
) {
    val games = library.games
    val listState = rememberLazyListState()
    val requester = remember { FocusRequester() }
    val scanRequester = remember { FocusRequester() }
    LaunchedEffect(games.isEmpty()) {
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
            Row(
                Modifier.padding(horizontal = 40.dp).background(Color(0xCC1D2438), Deck.Pill)
                    .clickable(remember { MutableInteractionSource() }, indication = null, onClick = onUpdateBanner)
                    .padding(horizontal = 16.dp, vertical = 8.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                Box(Modifier.size(8.dp).background(Deck.Cyan, RoundedCornerShape(50)))
                Spacer(Modifier.width(10.dp))
                Text("Nueva versión ${av.release.version} disponible · Ajustes (RB) para instalar",
                    color = Deck.Text, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
            }
        }
        val game = games.getOrNull(selected)
        Box(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 40.dp), contentAlignment = Alignment.BottomStart) {
            if (game != null) FocusedGameHeader(game, art[game.exePath])
            else EmptyLibrary(library, folders, scanRequester, onScan)
        }
        if (games.isNotEmpty()) {
            Row(Modifier.padding(start = 40.dp, top = 6.dp, bottom = 0.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("Biblioteca", color = Deck.Text, fontSize = 14.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.width(10.dp))
                Text("${games.size} ${if (games.size == 1) "juego" else "juegos"}", color = Deck.Dim, fontSize = 12.sp)
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
            if (games.isEmpty()) listOf("Y" to "Buscar juegos", "B" to "Salir")
            else listOf("A" to "Abrir", "X" to "Jugar", "Y" to "Buscar juegos", "B" to "Salir"),
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
            if (isHeld(game)) Chip("En pausa · vuelve al instante", Deck.Green)
            info?.genres?.take(3)?.forEach { Chip(it) }
            if (info == null) Chip(if (game.pe.is64Bit) "Windows · 64 bits" else "Windows · 32 bits")
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
private fun EmptyLibrary(library: LibraryState, folders: List<String>, scanRequester: FocusRequester, onScan: () -> Unit) {
    Column(Modifier.fillMaxWidth().padding(bottom = 40.dp)) {
        Text(if (library.scanning || !library.scannedOnce) "Buscando juegos…" else "Tu biblioteca está vacía",
            color = Deck.Text, fontSize = 30.sp, fontWeight = FontWeight.Black)
        Spacer(Modifier.height(10.dp))
        Text("Copia la carpeta de un juego de PC (con su .exe) en " +
            (folders.firstOrNull()?.let(LauncherLogic::displayFolder) ?: "/sdcard/PC games") + " y busca juegos.",
            color = Deck.Sub, fontSize = 15.sp, modifier = Modifier.widthIn(max = 560.dp))
        Spacer(Modifier.height(22.dp))
        MacButton(if (library.scanning) "Buscando…" else "Buscar juegos", onClick = onScan,
            style = MacButtonStyle.PRIMARY, large = true, modifier = Modifier.focusRequester(scanRequester))
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
    LaunchedEffect(game.exePath) {
        withFrameNanos { }
        runCatching { playRequester.requestFocus() }
    }
    val cache = remember(game.exePath) { jitCacheLabel(game) }
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
                            if (held) Chip("En pausa en memoria", Deck.Green)
                            info?.genres?.take(4)?.forEach { Chip(it) }
                        }
                    }
                    Spacer(Modifier.height(8.dp))
                    Text(info?.description?.takeIf { it.isNotBlank() }
                        ?: "Sin ficha oficial todavía: conéctate a internet y vuelve a abrir la biblioteca.",
                        color = Deck.Sub, fontSize = 13.sp, lineHeight = 18.sp, maxLines = 4, overflow = TextOverflow.Ellipsis)
                    Spacer(Modifier.height(14.dp))
                    Row(verticalAlignment = Alignment.CenterVertically) {
                        MacButton(if (held) "CONTINUAR" else "JUGAR", onClick = onPlay, style = MacButtonStyle.PRIMARY,
                            onFocused = onFocused, modifier = Modifier.focusRequester(playRequester).width(150.dp))
                        Spacer(Modifier.width(14.dp))
                        Text(if (held) "Vuelves justo donde lo dejaste" else "Caché: $cache",
                            color = Deck.Dim, fontSize = 12.sp, maxLines = 2)
                    }
                }
                Column(Modifier.weight(0.85f)) {
                    InfoCard {
                        InfoLine("Desarrollador", info?.developers?.joinToString(", ").orEmpty().ifBlank { "—" })
                        InfoLine("Editor", info?.publishers?.joinToString(", ").orEmpty().ifBlank { "—" })
                        InfoLine("Lanzamiento", info?.releaseDate.orEmpty().ifBlank { "—" })
                        InfoLine("Plataforma", (if (game.pe.is64Bit) "Windows x64" else "Windows x86") + " · " +
                            LauncherLogic.graphicsApiLabel(game.pe.graphicsApis, game.engineHints))
                        InfoLine("Ejecutable", File(game.exePath).name)
                        InfoLine("Partidas", "_HyperPlaySaves")
                    }
                }
            }
            HintBar(listOf("A" to if (held) "Continuar" else "Jugar", "B" to "Atrás"), Modifier.fillMaxWidth())
        }
    }
}

@Composable
private fun TopBarCompact(onBack: () -> Unit) {
    Row(Modifier.fillMaxWidth().padding(horizontal = 28.dp, vertical = 8.dp), verticalAlignment = Alignment.CenterVertically) {
        Text("‹  Biblioteca", color = Deck.Text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold,
            modifier = Modifier.clickable(remember { MutableInteractionSource() }, indication = null, onClick = onBack))
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
        Text(label, color = Deck.Dim, fontSize = 12.sp, modifier = Modifier.width(96.dp))
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
    LaunchedEffect(Unit) {
        withFrameNanos { }
        runCatching { first.requestFocus() }
    }
    Column(Modifier.fillMaxSize()) {
        TopBar(Page.SETTINGS, onTab)
        Row(Modifier.weight(1f).fillMaxWidth().padding(horizontal = 48.dp, vertical = 12.dp),
            horizontalArrangement = Arrangement.spacedBy(28.dp)) {
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).focusGroup()) {
                SectionTitle("Biblioteca")
                Row(horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                    MacButton(if (library.scanning) "Buscando…" else "Buscar juegos", onClick = onScan,
                        style = MacButtonStyle.PRIMARY, onFocused = onFocused, modifier = Modifier.focusRequester(first))
                    MacButton("Añadir carpeta", onClick = onAddFolder, onFocused = onFocused)
                }
                val status = library.error ?: library.progress
                if (status.isNotEmpty()) {
                    Spacer(Modifier.height(10.dp))
                    Text(status, color = if (library.error != null) Deck.Orange else Deck.Dim, fontSize = 13.sp)
                }
                Spacer(Modifier.height(22.dp))
                SectionTitle("Carpetas de juegos")
                if (folders.isEmpty()) {
                    Text("Ninguna carpeta", color = Deck.Dim, fontSize = 14.sp)
                }
                folders.forEach { f ->
                    FolderRow(f, onFocused, onClick = { onFolderClick(f) })
                    Spacer(Modifier.height(8.dp))
                }
            }
            Column(Modifier.weight(1f).verticalScroll(rememberScrollState()).focusGroup()) {
                SectionTitle("Actualizaciones")
                UpdateCard(system, actions, onFocused, sounds)
                Spacer(Modifier.height(22.dp))
                SectionTitle("Informes de errores")
                ReportsCard(system, actions, onFocused, sounds)
                Spacer(Modifier.height(22.dp))
                SectionTitle("Este dispositivo")
                InfoCard {
                    InfoLine("Procesador", "Snapdragon 8 Elite · 2 Prime + 6 Perf")
                    InfoLine("Gráficos", "Adreno 830 · Vulkan")
                    InfoLine("Juegos", "${library.games.size}")
                }
                Spacer(Modifier.height(22.dp))
                SectionTitle("Mandos")
                InfoCard {
                    InfoLine("Compatibles", "Xbox, PlayStation, GameSir, 8BitDo y HID")
                    InfoLine("Vibración", "Mando o, si no tiene, el teléfono")
                    InfoLine("Menú rápido", "L3 + R3 o Select + Start")
                }
            }
        }
        HintBar(listOf("A" to "Elegir", "Y" to "Buscar juegos", "B" to "Atrás"), Modifier.fillMaxWidth())
    }
}

@Composable
private fun UpdateCard(system: SystemUi, actions: SystemActions, onFocused: () -> Unit, sounds: UiSounds) {
    val u = system.update
    InfoCard {
        InfoLine("Versión instalada", system.currentVersion)
        val status = when (u) {
            UpdateState.Idle -> "Sin comprobar"
            UpdateState.Checking -> "Buscando…"
            UpdateState.UpToDate -> "Tienes la última versión"
            is UpdateState.Available -> "Nueva: ${u.release.version}"
            is UpdateState.Downloading -> "Descargando ${u.release.version} · ${(u.progress * 100).toInt()} %"
            is UpdateState.Installing -> "Instalando ${u.release.version}…"
            is UpdateState.Error -> u.message
        }
        InfoLine("Estado", status)
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
                MacButton("Descargar e instalar", style = MacButtonStyle.PRIMARY, onFocused = onFocused,
                    onClick = { sounds.confirm(); actions.installUpdate(release) })
            }
            if (u !is UpdateState.Downloading && u !is UpdateState.Installing) {
                MacButton("Buscar actualizaciones", onFocused = onFocused, onClick = { sounds.confirm(); actions.checkUpdate() })
            }
        }
    }
}

@Composable
private fun ReportsCard(system: SystemUi, actions: SystemActions, onFocused: () -> Unit, sounds: UiSounds) {
    InfoCard {
        InfoLine("Enviar informes", if (system.reportsEnabled == true) "Activado" else "Desactivado")
        InfoLine("Guardados en el teléfono", if (system.pendingReports == 0) "Ninguno pendiente" else "${system.pendingReports} pendientes")
        if (!system.reportsConfigured) InfoLine("Servidor", "No configurado en esta versión")
        Spacer(Modifier.height(8.dp))
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), modifier = Modifier.padding(bottom = 8.dp)) {
            MacButton(if (system.reportsEnabled == true) "Desactivar" else "Activar", onFocused = onFocused,
                onClick = { sounds.confirm(); actions.setReports(system.reportsEnabled != true) })
            if (system.pendingReports > 0 && system.reportsEnabled == true && system.reportsConfigured) {
                MacButton("Enviar ahora", style = MacButtonStyle.PRIMARY, onFocused = onFocused,
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
        Text("Quitar", color = if (focused) Deck.Red else Deck.Dim, fontSize = 14.sp, fontWeight = FontWeight.SemiBold)
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
                Box(Modifier.size(56.dp).background(Brush.linearGradient(listOf(Deck.Accent, Deck.Cyan)), RoundedCornerShape(14.dp)),
                    contentAlignment = Alignment.Center) {
                    Text("H", color = Color.White, fontSize = 30.sp, fontWeight = FontWeight.Black)
                }
                Spacer(Modifier.height(18.dp))
                Text("Acceso a tus juegos", color = Deck.Text, fontSize = 22.sp, fontWeight = FontWeight.Bold)
                Spacer(Modifier.height(10.dp))
                Text(
                    "HyperPlay abre los juegos directamente desde su carpeta (por ejemplo /sdcard/PC games) y guarda " +
                        "las partidas junto al juego. Para eso necesita el permiso \"Acceso a todos los archivos\". " +
                        "Actívalo en Ajustes y vuelve.",
                    color = Deck.Sub, fontSize = 15.sp, textAlign = TextAlign.Center, lineHeight = 22.sp,
                )
                Spacer(Modifier.height(24.dp))
                MacButton("Abrir Ajustes", onClick = { sounds.confirm(); onGrant() },
                    modifier = Modifier.focusRequester(requester), style = MacButtonStyle.PRIMARY, large = true)
            }
        }
    }
}

@Suppress("unused")
private val keepMacTokens = Mac.Window
