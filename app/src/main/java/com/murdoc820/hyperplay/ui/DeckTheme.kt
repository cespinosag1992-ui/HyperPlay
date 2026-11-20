package com.murdoc820.hyperplay.ui

import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.graphics.Bitmap
import android.os.BatteryManager
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.produceState
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.murdoc820.hyperplay.art.ArtBitmaps
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.withContext
import java.io.File
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale

/**
 * Estilo "consola portátil" de HyperPlay (inspirado en la interfaz de las portátiles con SteamOS):
 * fondo azul noche, el arte del juego enfocado de fondo, cápsulas verticales, barra superior con
 * hora y batería y barra inferior de botones del mando. Colores propios: violeta HyperPlay + cian.
 */
object Deck {
    val Bg = Color(0xFF0A0D16)
    val BgTop = Color(0xFF12172A)
    val Panel = Color(0xFF141A2B)
    val Raised = Color(0xFF1D2438)
    val Line = Color(0xFF2A3350)
    val Scrim = Color(0xCC060810)

    val Accent = Color(0xFF7C5CFF)
    val AccentHi = Color(0xFFA38BFF)
    val AccentGlow = Color(0x667C5CFF)
    val Cyan = Color(0xFF2FD6FF)
    val Green = Color(0xFF3DDC97)
    val Orange = Color(0xFFFFB547)
    val Red = Color(0xFFFF5D6C)

    val Text = Color(0xFFF4F6FB)
    val Sub = Color(0xFFB2BAD1)
    val Dim = Color(0xFF6E7899)

    val Capsule = RoundedCornerShape(6.dp)
    val Card = RoundedCornerShape(10.dp)
    val Pill = RoundedCornerShape(50)

    /** Degradado del fondo cuando no hay arte. */
    val BackdropBrush = Brush.verticalGradient(listOf(BgTop, Bg))
}

/** Imagen de un archivo local, decodificada en segundo plano al ancho visible; [fallback] mientras. */
@Composable
fun ArtImage(
    file: File?,
    modifier: Modifier = Modifier,
    contentScale: ContentScale = ContentScale.Crop,
    alignment: Alignment = Alignment.Center,
    fallback: @Composable () -> Unit = {},
) {
    BoxWithConstraints(modifier) {
        val widthPx = with(LocalDensity.current) { maxWidth.toPx().toInt().coerceIn(64, 2688) }
        val bitmap by produceState<Bitmap?>(initialValue = null, file?.path, file?.lastModified(), widthPx) {
            value = if (file == null) null else withContext(Dispatchers.IO) { ArtBitmaps.get(file, widthPx) }
        }
        val b = bitmap
        if (b == null) {
            fallback()
        } else {
            Image(b.asImageBitmap(), contentDescription = null, contentScale = contentScale,
                alignment = alignment, modifier = Modifier.matchParentSize())
        }
    }
}

/** Glifo de botón del mando (A verde, B rojo, X azul, Y amarillo, LB/RB/Start en gris). */
@Composable
fun ButtonGlyph(label: String, size: Dp = 22.dp) {
    val color = when (label) {
        "A" -> Color(0xFF3DDC97)
        "B" -> Color(0xFFFF5D6C)
        "X" -> Color(0xFF4DA3FF)
        "Y" -> Color(0xFFFFC44D)
        else -> Deck.Raised
    }
    val wide = label.length > 1
    Box(
        Modifier
            .then(if (wide) Modifier.padding(0.dp) else Modifier.size(size))
            .background(if (wide) Deck.Raised else color, if (wide) RoundedCornerShape(6.dp) else CircleShape)
            .then(if (wide) Modifier.padding(horizontal = 7.dp, vertical = 2.dp) else Modifier),
        contentAlignment = Alignment.Center,
    ) {
        Text(label, color = if (wide) Deck.Sub else Color(0xFF0A0D16), fontSize = (size.value * 0.55f).sp,
            fontWeight = FontWeight.Bold)
    }
}

/** Barra inferior: acciones del mando disponibles en esta pantalla. */
@Composable
fun HintBar(hints: List<Pair<String, String>>, modifier: Modifier = Modifier) {
    Row(
        modifier
            .background(Brush.verticalGradient(listOf(Color.Transparent, Deck.Scrim)))
            .padding(horizontal = 28.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.End,
        verticalAlignment = Alignment.CenterVertically,
    ) {
        Spacer(Modifier.weight(1f))
        hints.forEachIndexed { i, (button, text) ->
            if (i > 0) Spacer(Modifier.width(22.dp))
            ButtonGlyph(button)
            Spacer(Modifier.width(8.dp))
            Text(text, color = Deck.Sub, fontSize = 14.sp, fontWeight = FontWeight.Medium)
        }
    }
}

/** Hora (HH:mm) y batería (%), actualizadas cada 20 s. */
@Composable
fun ClockAndBattery() {
    val context = LocalContext.current
    var now by remember { mutableStateOf(Date()) }
    var battery by remember { mutableIntStateOf(batteryPercent(context)) }
    var charging by remember { mutableStateOf(false) }
    LaunchedEffect(Unit) {
        while (true) {
            now = Date()
            battery = batteryPercent(context)
            charging = isCharging(context)
            delay(20_000)
        }
    }
    val fmt = remember { SimpleDateFormat("HH:mm", Locale.getDefault()) }
    Row(verticalAlignment = Alignment.CenterVertically) {
        if (battery >= 0) {
            BatteryIcon(battery, charging)
            Spacer(Modifier.width(6.dp))
            Text("$battery%", color = Deck.Sub, fontSize = 14.sp, fontWeight = FontWeight.Medium)
            Spacer(Modifier.width(18.dp))
        }
        Text(fmt.format(now), color = Deck.Text, fontSize = 15.sp, fontWeight = FontWeight.SemiBold)
    }
}

@Composable
private fun BatteryIcon(percent: Int, charging: Boolean) {
    val fill = when {
        charging -> Deck.Green
        percent <= 15 -> Deck.Red
        else -> Deck.Text
    }
    Row(verticalAlignment = Alignment.CenterVertically) {
        Box(
            Modifier.size(width = 24.dp, height = 12.dp)
                .background(Deck.Line, RoundedCornerShape(3.dp))
                .padding(2.dp),
        ) {
            Box(Modifier.size(width = (20 * percent / 100f).coerceAtLeast(2f).dp, height = 8.dp)
                .background(fill, RoundedCornerShape(1.5.dp)))
        }
        Box(Modifier.size(width = 2.dp, height = 5.dp).background(Deck.Line))
    }
}

private fun batteryIntent(context: Context): Intent? =
    context.registerReceiver(null, IntentFilter(Intent.ACTION_BATTERY_CHANGED))

private fun batteryPercent(context: Context): Int {
    val i = batteryIntent(context) ?: return -1
    val level = i.getIntExtra(BatteryManager.EXTRA_LEVEL, -1)
    val scale = i.getIntExtra(BatteryManager.EXTRA_SCALE, -1)
    return if (level < 0 || scale <= 0) -1 else level * 100 / scale
}

private fun isCharging(context: Context): Boolean {
    val status = batteryIntent(context)?.getIntExtra(BatteryManager.EXTRA_STATUS, -1) ?: return false
    return status == BatteryManager.BATTERY_STATUS_CHARGING || status == BatteryManager.BATTERY_STATUS_FULL
}
