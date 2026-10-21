package com.chatadhd.xport

import java.util.Locale

// Polish forms of a counted noun: the one place that knows the plural rule (every "N rozmów / wiadomości / źródeł…"
// of the app goes through it) and the one place that writes a byte size for a human.
class Forms(val one: String, val few: String, val many: String)

object Pl {
    // CLDR rule for Polish integers: 1 -> one; ends in 2..4 except 12..14 -> few; everything else (0, 5..21, 25..31…) -> many.
    // The rule is this single table, so a new noun is one line in `Nouns`, never a new condition at a call site.
    fun form(n: Long, f: Forms): String {
        val a = if (n < 0) -n else n
        val m10 = a % 10
        val m100 = a % 100
        return when {
            a == 1L -> f.one
            m10 in 2L..4L && m100 !in 12L..14L -> f.few
            else -> f.many
        }
    }

    fun count(n: Long, f: Forms): String = "$n ${form(n, f)}"
    fun count(n: Int, f: Forms): String = count(n.toLong(), f)

    // Size in bytes for a human: bytes below 1 KB ("812 B"), then KB / MB / GB with one decimal only when it says something
    // ("1,5 KB", "12 KB"); a small non-empty file is never shown as "0 KB".
    fun size(bytes: Long): String {
        if (bytes < 1024) return "$bytes B"
        val units = arrayOf("KB", "MB", "GB", "TB")
        var v = bytes / 1024.0
        var i = 0
        while (Math.round(v * 10) / 10.0 >= 1024.0 && i < units.size - 1) { v /= 1024.0; i++ }
        val loc = Locale.forLanguageTag("pl-PL")
        val s = if (v >= 100) String.format(loc, "%.0f", v) else String.format(loc, "%.1f", v).removeSuffix(",0")
        return "$s ${units[i]}"
    }
}

object Nouns {
    val CONVERSATION = Forms("rozmowa", "rozmowy", "rozmów")
    val CONVERSATION_LOC = Forms("rozmowie", "rozmowach", "rozmowach")   // "praca na 3 rozmowach"
    val MESSAGE = Forms("wiadomość", "wiadomości", "wiadomości")
    val SOURCE = Forms("źródło", "źródła", "źródeł")
    val FILE = Forms("plik", "pliki", "plików")
    val DAY = Forms("dzień", "dni", "dni")
    val SWITCH = Forms("przełączenie", "przełączenia", "przełączeń")
    val LINE = Forms("linia", "linie", "linii")
    val CHAR = Forms("znak", "znaki", "znaków")
    val BYTE = Forms("bajt", "bajty", "bajtów")
}
