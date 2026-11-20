package com.murdoc820.hyperplay.art

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import java.io.File
import java.io.RandomAccessFile
import java.nio.ByteBuffer
import java.nio.ByteOrder

/**
 * Icono de un `.exe` de Windows: recorre los recursos PE (RT_GROUP_ICON → RT_ICON), elige la imagen
 * más grande y con más color, la envuelve en un `.ico` de una entrada (o la usa tal cual si es PNG)
 * y la decodifica con BitmapFactory (Android lee ICO). Solo lectura del archivo, sin red.
 */
object ExeIconExtractor {
    private const val RT_ICON = 3
    private const val RT_GROUP_ICON = 14
    private const val MAX_FILE = 512L * 1024 * 1024

    fun extractPng(exe: File, out: File): Boolean = runCatching {
        val bmp = extract(exe) ?: return false
        out.outputStream().use { bmp.compress(Bitmap.CompressFormat.PNG, 100, it) }
        true
    }.getOrDefault(false)

    fun extract(exe: File): Bitmap? = runCatching {
        if (!exe.isFile || exe.length() > MAX_FILE) return null
        RandomAccessFile(exe, "r").use { raf -> Pe(raf).icon() }
    }.getOrNull()

    private class Section(val va: Long, val size: Long, val raw: Long)

    private class Pe(private val raf: RandomAccessFile) {
        private val sections = ArrayList<Section>()
        private var rsrcRva = 0L

        init {
            val dos = read(0, 64)
            require(dos.short.toInt() == 0x5A4D)
            val peOff = dos.getInt(0x3C).toLong()
            val hdr = read(peOff, 24)
            require(hdr.int == 0x00004550)
            val numSections = hdr.getShort(6).toInt() and 0xFFFF
            val optSize = hdr.getShort(20).toInt() and 0xFFFF
            val opt = read(peOff + 24, optSize)
            val magic = opt.getShort(0).toInt() and 0xFFFF
            val dirBase = if (magic == 0x20B) 112 else 96   // PE32+ / PE32
            rsrcRva = opt.getInt(dirBase + 2 * 8).toLong() and 0xFFFFFFFFL
            val sec = read(peOff + 24 + optSize, numSections * 40)
            for (i in 0 until numSections) {
                val b = i * 40
                val vsize = sec.getInt(b + 8).toLong() and 0xFFFFFFFFL
                val va = sec.getInt(b + 12).toLong() and 0xFFFFFFFFL
                val rawSize = sec.getInt(b + 16).toLong() and 0xFFFFFFFFL
                val raw = sec.getInt(b + 20).toLong() and 0xFFFFFFFFL
                sections += Section(va, maxOf(vsize, rawSize), raw)
            }
        }

        private fun read(off: Long, len: Int): ByteBuffer {
            val a = ByteArray(len)
            raf.seek(off)
            raf.readFully(a)
            return ByteBuffer.wrap(a).order(ByteOrder.LITTLE_ENDIAN)
        }

        private fun rvaToOff(rva: Long): Long {
            val s = sections.firstOrNull { rva >= it.va && rva < it.va + it.size } ?: error("RVA fuera de secciones")
            return rva - s.va + s.raw
        }

        /** Entradas (id o -1 si es nombre, desplazamiento, esDirectorio) de un directorio de recursos. */
        private fun entries(dirOff: Long): List<Triple<Int, Long, Boolean>> {
            val d = read(rvaToOff(rsrcRva) + dirOff, 16)
            val n = (d.getShort(12).toInt() and 0xFFFF) + (d.getShort(14).toInt() and 0xFFFF)
            val e = read(rvaToOff(rsrcRva) + dirOff + 16, n * 8)
            return (0 until n).map { i ->
                val name = e.getInt(i * 8)
                val off = e.getInt(i * 8 + 4)
                Triple(if (name < 0) -1 else name, (off.toLong() and 0x7FFFFFFFL), off < 0)
            }
        }

        /** Datos del primer recurso hoja bajo (tipo, id); id null = el primero. */
        private fun resource(type: Int, id: Int?): ByteArray? {
            val t = entries(0).firstOrNull { it.first == type && it.third } ?: return null
            val names = entries(t.second)
            val n = (if (id == null) names.firstOrNull() else names.firstOrNull { it.first == id }) ?: return null
            var leaf = n
            while (leaf.third) leaf = entries(leaf.second).firstOrNull() ?: return null
            val data = read(rvaToOff(rsrcRva) + leaf.second, 16)
            val rva = data.getInt(0).toLong() and 0xFFFFFFFFL
            val size = data.getInt(4)
            if (size <= 0 || size > 16 * 1024 * 1024) return null
            return read(rvaToOff(rva), size).array()
        }

        fun icon(): Bitmap? {
            if (rsrcRva == 0L) return null
            val group = ByteBuffer.wrap(resource(RT_GROUP_ICON, null) ?: return null).order(ByteOrder.LITTLE_ENDIAN)
            val count = group.getShort(4).toInt() and 0xFFFF
            var bestId = -1
            var bestScore = -1
            for (i in 0 until count) {
                val b = 6 + i * 14
                if (b + 14 > group.capacity()) break
                val w = (group.get(b).toInt() and 0xFF).let { if (it == 0) 256 else it }
                val bits = group.getShort(b + 6).toInt() and 0xFFFF
                val score = w * 64 + bits
                if (score > bestScore) { bestScore = score; bestId = group.getShort(b + 12).toInt() and 0xFFFF }
            }
            if (bestId < 0) return null
            val img = resource(RT_ICON, bestId) ?: return null
            val isPng = img.size > 8 && img[0] == 0x89.toByte() && img[1] == 'P'.code.toByte()
            if (isPng) return BitmapFactory.decodeByteArray(img, 0, img.size)
            // .ico de una entrada: ICONDIR (6) + ICONDIRENTRY (16) + DIB.
            val dib = ByteBuffer.wrap(img).order(ByteOrder.LITTLE_ENDIAN)
            val w = dib.getInt(4)
            val h = dib.getInt(8) / 2
            val bits = dib.getShort(14).toInt() and 0xFFFF
            val ico = ByteBuffer.allocate(22 + img.size).order(ByteOrder.LITTLE_ENDIAN)
            val zero: Short = 0
            val one: Short = 1
            ico.putShort(zero).putShort(one).putShort(one)
            ico.put((if (w >= 256) 0 else w).toByte()).put((if (h >= 256) 0 else h).toByte())
            ico.put(0.toByte()).put(0.toByte())
            ico.putShort(one).putShort(bits.toShort()).putInt(img.size).putInt(22)
            ico.put(img)
            return BitmapFactory.decodeByteArray(ico.array(), 0, ico.capacity())
        }
    }
}
