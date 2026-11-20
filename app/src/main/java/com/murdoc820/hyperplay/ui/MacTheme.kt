package com.murdoc820.hyperplay.ui

import androidx.compose.foundation.background
import androidx.compose.foundation.border
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp

/**
 * Tokens del estilo HyperPlay (inspirado en el modo oscuro de macOS): superficies grises
 * neutras, un único acento azul, texto secundario al 60 % y radios pequeños. Sin gradientes ni
 * sombras. Compartido por el launcher y la capa de sesión (Quick Menu, carga, fin).
 */
object Mac {
    // Superficies (de fondo a primer plano).
    val Window = Color(0xFF1C1C1E)
    val Sidebar = Color(0xFF232325)
    val Group = Color(0xFF2C2C2E)
    val Control = Color(0xFF3A3A3C)
    val Separator = Color(0xFF38383A)
    val Scrim = Color(0xB3000000)
    val Sheet = Color(0xF21C1C1E)  // panel sobre el juego (casi opaco)

    // Texto.
    val Label = Color(0xFFFFFFFF)
    val Secondary = Color(0x99EBEBF5)
    val Tertiary = Color(0x4DEBEBF5)

    // Colores del sistema (modo oscuro).
    val Blue = Color(0xFF0A84FF)
    val BlueSoft = Color(0x330A84FF)
    val Green = Color(0xFF30D158)
    val Orange = Color(0xFFFF9F0A)
    val Red = Color(0xFFFF453A)

    // Escala de espacios (4 dp).
    val S1 = 4.dp
    val S2 = 8.dp
    val S3 = 12.dp
    val S4 = 16.dp
    val S5 = 20.dp
    val S6 = 24.dp

    val RadiusSmall = RoundedCornerShape(6.dp)
    val Radius = RoundedCornerShape(8.dp)
    val RadiusLarge = RoundedCornerShape(12.dp)

    // Tipografía.
    val LargeTitle = 26.sp
    val Title = 20.sp
    val Headline = 15.sp
    val Body = 14.sp
    val Callout = 13.sp
    val Caption = 11.sp
}

/** Anillo de foco de macOS (mando/teclado): borde azul de 2 dp. */
fun Modifier.macFocusRing(focused: Boolean, shape: RoundedCornerShape = Mac.Radius): Modifier =
    if (focused) border(2.dp, Mac.Blue, shape) else this

/** Cabecera de sección en gris pequeño (como "Biblioteca" en la barra lateral de macOS). */
@Composable
fun MacSectionHeader(text: String, modifier: Modifier = Modifier) {
    Text(
        text,
        color = Mac.Secondary,
        fontSize = Mac.Caption,
        fontWeight = FontWeight.SemiBold,
        modifier = modifier.padding(start = Mac.S3, bottom = Mac.S1),
    )
}

/** Grupo de filas con fondo y separadores (estilo Ajustes del Sistema). */
@Composable
fun MacGroup(modifier: Modifier = Modifier, content: @Composable ColumnScope.() -> Unit) {
    Column(modifier.fillMaxWidth().background(Mac.Group, Mac.RadiusLarge).padding(vertical = 2.dp), content = content)
}

@Composable
fun MacSeparator(inset: Dp = Mac.S3) {
    Box(Modifier.fillMaxWidth().padding(start = inset).height(0.5.dp).background(Mac.Separator))
}

/** Fila etiqueta / valor de solo lectura. */
@Composable
fun MacInfoRow(label: String, value: String, valueColor: Color = Mac.Secondary) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = Mac.S3, vertical = 9.dp),
        verticalAlignment = Alignment.CenterVertically,
        horizontalArrangement = Arrangement.SpaceBetween,
    ) {
        Text(label, color = Mac.Label, fontSize = Mac.Body)
        Spacer(Modifier.width(Mac.S4))
        Text(
            value, color = valueColor, fontSize = Mac.Body, maxLines = 1, overflow = TextOverflow.Ellipsis,
            modifier = Modifier.weight(1f, fill = false),
        )
    }
}

/** Etiqueta en cápsula (arquitectura, estado). */
@Composable
fun MacCapsule(text: String, color: Color = Mac.Secondary, fill: Color = Mac.Control) {
    Text(
        text,
        color = color,
        fontSize = Mac.Caption,
        fontWeight = FontWeight.SemiBold,
        modifier = Modifier.background(fill, RoundedCornerShape(50)).padding(horizontal = 8.dp, vertical = 3.dp),
    )
}

/** Punto de estado (verde = bien, naranja = aviso, rojo = mal). */
@Composable
fun MacStatusDot(color: Color) {
    Box(Modifier.size(8.dp).background(color, CircleShape))
}
