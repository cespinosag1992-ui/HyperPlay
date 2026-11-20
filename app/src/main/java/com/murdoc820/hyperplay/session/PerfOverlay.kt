package com.murdoc820.hyperplay.session

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.offset
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.displayCutout
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.layout.windowInsetsPadding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.SpanStyle
import com.murdoc820.hyperplay.R
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.buildAnnotatedString
import androidx.compose.ui.text.font.FontFamily
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.withStyle
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

private val HudPanel = Color(0x99000000)
private val HudLabel = Color(0xFFB0B6BC)
private val HudUnit = Color(0xFF80868B)
private val HudSep = Color(0x66FFFFFF)
private val HudGood = Color(0xFF3DDC84)
private val HudWarn = Color(0xFFFFC94D)
private val HudBad = Color(0xFFFF4D5E)
private val HudNone = Color(0xFF80868B)

private val HudText = TextStyle(
    fontFamily = FontFamily.Monospace,
    fontFeatureSettings = "tnum",
    fontSize = 12.sp,
    lineHeight = 15.sp,
    color = Color.White,
)

private fun PerfLevel.color(): Color = when (this) {
    PerfLevel.GOOD -> HudGood
    PerfLevel.WARN -> HudWarn
    PerfLevel.BAD -> HudBad
    PerfLevel.NONE -> HudNone
}

private val HudPill = Color(0xB3000000)

/** Estado del motor que el HUD muestra además de las métricas. */
data class HudContext(
    /** Tope de FPS en vigor (0 = sin límite): los colores de FPS se miden contra él. */
    val targetFps: Int = 0,
    val thermal: ThermalDecision? = null,
    /** Generación de fotogramas activa: el FPS mostrado es el presentado (×2). */
    val frameGen: Boolean = false,
)

/**
 * OSD de rendimiento. Compacto: arriba, en el hueco libre entre Menú (≡) y RT del mando táctil
 * (pastilla de dos líneas al estilo del overlay de RedMagic). Completo: abajo al centro.
 * Sin modificadores de puntero ni foco: los toques llegan al mando táctil y al juego de debajo.
 */
@Composable
fun PerfOverlay(mode: PerfOverlayMode, snap: PerfSnapshot, modifier: Modifier = Modifier, hud: HudContext = HudContext()) {
    if (mode == PerfOverlayMode.OFF) return
    if (mode == PerfOverlayMode.COMPACT) {
        BoxWithConstraints(modifier.fillMaxSize()) {
            val h = maxHeight
            // Geometría de VirtualTouchLayout (unidades de alto): Menú en w/2 + 0.16h (r 0.042h),
            // RT desde w - 0.25h. Hueco libre: [w/2 + 0.21h, w - 0.26h], franja 0.03h..0.13h.
            val start = maxWidth / 2 + h * 0.21f
            val end = maxWidth - h * 0.26f
            Box(
                Modifier.offset(x = start, y = h * 0.03f).width((end - start).coerceAtLeast(0.dp)),
                contentAlignment = Alignment.TopCenter,
            ) {
                Box(Modifier.background(HudPill, RoundedCornerShape(8.dp)).padding(horizontal = 8.dp, vertical = 3.dp)) {
                    CompactOsd(snap, hud)
                }
            }
        }
        return
    }
    Box(
        modifier.fillMaxSize().windowInsetsPadding(WindowInsets.displayCutout).padding(6.dp),
        contentAlignment = Alignment.BottomCenter,
    ) {
        Box(Modifier.background(HudPanel, RoundedCornerShape(6.dp)).padding(horizontal = 8.dp, vertical = 4.dp)) {
            FullOsd(snap, hud)
        }
    }
}

private val HudCompactText = HudText.copy(fontSize = 10.sp, lineHeight = 12.sp)

/**
 * `FPS 60 · 16.7ms │ CPU 26% 64°`
 * `GPU 79% 58° │ BAT 37° │ 4.2W`
 */
@Composable
private fun CompactOsd(s: PerfSnapshot, hud: HudContext) {
    val L = PerfMonitorLogic
    val fpsLv = L.fpsLevel(s.fps, hud.targetFps)
    fun line(block: androidx.compose.ui.text.AnnotatedString.Builder.() -> Unit) = buildAnnotatedString(block)
    fun androidx.compose.ui.text.AnnotatedString.Builder.label(t: String) = withStyle(SpanStyle(color = HudLabel)) { append(t) }
    fun androidx.compose.ui.text.AnnotatedString.Builder.value(t: String, level: PerfLevel) =
        withStyle(SpanStyle(color = level.color(), fontWeight = FontWeight.Bold)) { append(t) }
    fun androidx.compose.ui.text.AnnotatedString.Builder.unit(t: String) = withStyle(SpanStyle(color = HudUnit)) { append(t) }
    fun androidx.compose.ui.text.AnnotatedString.Builder.sep() = withStyle(SpanStyle(color = HudSep)) { append(" │ ") }

    val top = line {
        label("FPS "); value(L.formatOrDash(s.fps), fpsLv)
        if (hud.frameGen) unit(" FG")
        if (hud.thermal?.throttled == true) withStyle(SpanStyle(color = HudWarn, fontWeight = FontWeight.Bold)) { append("▼") }
        unit(" · "); value(L.formatOrDash(s.frameTimeMs, 1), fpsLv); unit("ms")
        sep(); label("CPU "); value(L.formatOrDash(s.cpuPercent), L.usageLevel(s.cpuPercent)); unit("% ")
        if (s.cpuTempC == null && s.thermalHeadroom != null) {
            value(L.formatOrDash(s.thermalHeadroom * 100f), L.headroomLevel(s.thermalHeadroom)); unit("%t")
        } else {
            value(L.formatOrDash(s.cpuTempC), L.tempLevel(s.cpuTempC)); unit("°")
        }
    }
    val bottom = line {
        label("GPU "); value(L.formatOrDash(s.gpuPercent), L.usageLevel(s.gpuPercent)); unit("% ")
        value(L.formatOrDash(s.gpuTempC), L.tempLevel(s.gpuTempC)); unit("°")
        sep(); label("BAT ")
        val batLv = if (hud.thermal?.hot == true) PerfLevel.BAD else L.batteryTempLevel(s.batteryTempC)
        value(L.formatOrDash(s.batteryTempC), batLv); unit("°")
        if (hud.thermal?.hot == true) value("!", PerfLevel.WARN)
        sep(); value(L.formatOrDash(s.batteryPowerW, 1), L.powerLevel(s.batteryPowerW)); unit("W")
        if (s.charging) unit("⚡")
    }
    Column(horizontalAlignment = Alignment.CenterHorizontally) {
        Text(top, style = HudCompactText, maxLines = 1, softWrap = false)
        Text(bottom, style = HudCompactText, maxLines = 1, softWrap = false)
    }
}

/** Una métrica por fila: etiqueta │ valor alineado a la derecha │ unidad. */
@Composable
private fun FullOsd(s: PerfSnapshot, hud: HudContext) {
    val L = PerfMonitorLogic
    Column {
        OsdRow(if (hud.frameGen) "FPS (FG)" else "FPS", L.formatOrDash(s.fps), "", L.fpsLevel(s.fps, hud.targetFps))
        val low = L.minFps(s.frameTimesMs)
        OsdRow(stringResource(R.string.osd_fps_min), L.formatOrDash(low), "", L.fpsLevel(low, hud.targetFps))
        OsdRow("Frametime", L.formatOrDash(s.frameTimeMs, 1), "ms", L.fpsLevel(s.fps, hud.targetFps))
        OsdRow("CPU", L.formatOrDash(s.cpuPercent), "%", L.usageLevel(s.cpuPercent))
        if (s.clusters.isEmpty()) {
            OsdRow("CPU MHz", "—", "", PerfLevel.NONE)
        } else {
            for (c in s.clusters) {
                val name = if (c.cpus.size == 1) "CPU${c.cpus.first()}" else "CPU${c.cpus.first()}-${c.cpus.last()}"
                OsdRow("$name MHz", L.formatMhz(c.curKhz), "", L.usageLevel(c.load * 100f))
            }
        }
        if (s.cpuTempC == null && s.thermalHeadroom != null) {
            OsdRow(stringResource(R.string.osd_thermal_headroom), L.formatOrDash(s.thermalHeadroom * 100f), "%", L.headroomLevel(s.thermalHeadroom))
        } else {
            OsdRow("CPU", L.formatOrDash(s.cpuTempC), "°C", L.tempLevel(s.cpuTempC))
        }
        OsdRow("GPU", L.formatOrDash(s.gpuPercent), "%", L.usageLevel(s.gpuPercent))
        OsdRow("GPU", L.formatOrDash(s.gpuTempC), "°C", L.tempLevel(s.gpuTempC))
        OsdRow("BAT", L.formatOrDash(s.batteryTempC), "°C",
            if (hud.thermal?.hot == true) PerfLevel.BAD else L.batteryTempLevel(s.batteryTempC))
        OsdRow(stringResource(if (s.charging) R.string.osd_charging else R.string.osd_draw), L.formatOrDash(s.batteryPowerW, 1), "W", L.powerLevel(s.batteryPowerW))
        if (hud.thermal?.throttled == true) OsdRow(stringResource(R.string.osd_thermal), "60", "▼", PerfLevel.WARN)
    }
}

@Composable
private fun OsdRow(label: String, value: String, unit: String, level: PerfLevel) {
    Row(verticalAlignment = Alignment.CenterVertically) {
        Text(label, style = HudText.copy(color = HudLabel), modifier = Modifier.width(84.dp), maxLines = 1)
        Text(
            value,
            style = HudText.copy(color = level.color(), fontWeight = FontWeight.Bold, textAlign = TextAlign.End),
            modifier = Modifier.width(40.dp), maxLines = 1,
        )
        Text(unit, style = HudText.copy(color = HudUnit), modifier = Modifier.width(24.dp).padding(start = 3.dp), maxLines = 1)
    }
}
