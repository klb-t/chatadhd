package com.chatadhd.xport

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.InputType
import android.widget.Button
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import com.chaquo.python.Python
import org.json.JSONObject
import java.io.File
import kotlin.concurrent.thread

// Pick an export (Storage Access Framework) -> copy it into app-private storage -> run the engine's
// inventory (Python, loom/tools/extract) on the copy -> show per-provider totals and a short timeline.
// Parsing lives in the engine; this class only moves bytes and renders text. No network.
class MainActivity : Activity() {
    private lateinit var status: TextView
    private lateinit var result: TextView
    private lateinit var pick: Button
    private lateinit var selectionBox: LinearLayout
    private lateinit var patternBox: LinearLayout
    private lateinit var selInfo: TextView
    private lateinit var expr: EditText
    private lateinit var exportBtn: Button
    private val py get() = Python.getInstance().getModule("xport_entry")

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val pad = (16 * resources.displayMetrics.density).toInt()
        pick = Button(this).apply { text = "Wybierz plik eksportu"; setOnClickListener { openPicker() } }
        status = TextView(this).apply { textSize = 14f; text = "Nic nie wybrano." }
        result = TextView(this).apply { textSize = 14f; setTextIsSelectable(true) }
        val col = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(pad, pad, pad, pad)
            addView(pick); addView(status); addView(result)
            addView(buildSelectionBox(pad))
        }
        setContentView(ScrollView(this).apply { addView(col) })
    }

    // Selection by structure + batch export. All logic is the engine's; the buttons only pass names/expressions.
    private fun buildSelectionBox(pad: Int): LinearLayout {
        selectionBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; visibility = android.view.View.GONE }
        selectionBox.addView(TextView(this).apply { text = "\nWybór według struktury"; textSize = 18f })
        patternBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        selectionBox.addView(patternBox)
        expr = EditText(this).apply {
            hint = "wyrażenie, np. tytuł:\"projekt\" I count(messages) >= 10"
            inputType = InputType.TYPE_CLASS_TEXT
        }
        selectionBox.addView(expr)
        val row = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        fun b(label: String, f: () -> Unit) = Button(this).apply { text = label; setOnClickListener { f() } }
        row.addView(b("Zaznacz wyrażeniem") { runSelect("expr", expr.text.toString()) })
        row.addView(b("Zakodowane dane") { runSelect("encoded", "") })
        selectionBox.addView(row)
        val row2 = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        row2.addView(b("Wszystko") { runSelect("all", "") })
        row2.addView(b("Odwróć") { runSelect("invert", "") })
        row2.addView(b("Wyczyść") { runSelect("clear", "") })
        selectionBox.addView(row2)
        selInfo = TextView(this).apply { textSize = 14f; setTextIsSelectable(true); text = "Zaznaczono: nic" }
        selectionBox.addView(selInfo)
        exportBtn = Button(this).apply { text = "Eksportuj zaznaczone (zip)"; setOnClickListener { pickExportTarget() } }
        selectionBox.addView(exportBtn)
        return selectionBox
    }

    private fun loadPatterns() {
        val j = JSONObject(py.callAttr("patterns").toString())
        patternBox.removeAllViews()
        if (!j.optBoolean("ok")) return
        val ps = j.getJSONArray("patterns")
        for (i in 0 until ps.length()) {
            val p = ps.getJSONObject(i)
            patternBox.addView(Button(this).apply {
                text = p.optString("label"); isAllCaps = false
                setOnClickListener { runSelect("pattern", p.optString("name")) }
            })
        }
    }

    private fun runSelect(how: String, value: String) {
        selInfo.text = "Liczę…"
        thread {
            val msg = try {
                val j = JSONObject(py.callAttr("select", how, value, "add").toString())
                if (!j.optBoolean("ok")) "Błąd wyboru: ${j.optString("error")}" else renderSelection(j)
            } catch (e: Throwable) { "Błąd: ${e.javaClass.simpleName}: ${e.message ?: "bez opisu"}" }
            runOnUiThread { selInfo.text = msg }
        }
    }

    private fun renderSelection(j: JSONObject): String {
        val sel = j.getJSONObject("selection")
        val s = sel.getJSONObject("summary")
        val sb = StringBuilder("Zaznaczono: ${s.optInt("conversations")} rozmów, ${s.optInt("messages")} wiadomości\n")
        j.optJSONObject("encoded")?.let { e ->
            val k = e.optJSONObject("by_kind")
            val parts = k?.keys()?.asSequence()?.map { "$it: ${k.optInt(it)}" }?.joinToString(", ") ?: ""
            sb.append("Znaleziono zakodowane dane (${e.optJSONObject("counts")?.optInt("findings") ?: 0}): ${parts.ifEmpty { "brak" }}\n")
        }
        val items = sel.getJSONArray("items")
        for (i in 0 until items.length()) {
            val it = items.getJSONObject(i)
            sb.append("• ${it.optString("provider")}  ${it.optString("title")}  (${it.optInt("messages")})\n")
        }
        if (sel.optInt("total") > items.length()) sb.append("… i ${sel.optInt("total") - items.length()} kolejnych\n")
        return sb.toString()
    }

    private fun pickExportTarget() {
        val i = Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "application/zip"
            putExtra(Intent.EXTRA_TITLE, "xport-wynik.zip")
        }
        startActivityForResult(i, REQ_SAVE)
    }

    private fun exportTo(uri: Uri) {
        exportBtn.isEnabled = false
        selInfo.text = "Eksportuję…"
        thread {
            val msg = try {
                val tmp = File(cacheDir, "xport-out.zip")
                val j = JSONObject(py.callAttr("export_selected", tmp.absolutePath).toString())
                if (!j.optBoolean("ok")) "Błąd eksportu: ${j.optString("error")}" else {
                    val out = contentResolver.openOutputStream(uri) ?: throw IllegalStateException("nie można zapisać pliku")
                    out.use { o -> tmp.inputStream().use { it.copyTo(o) } }
                    "Zapisano zip: ${j.optInt("files")} plików."
                }
            } catch (e: Throwable) { "Błąd: ${e.javaClass.simpleName}: ${e.message ?: "bez opisu"}" }
            File(cacheDir, "xport-out.zip").delete()
            runOnUiThread { selInfo.text = msg; exportBtn.isEnabled = true }
        }
    }

    private fun openPicker() {
        val i = Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE)
            type = "*/*"
        }
        startActivityForResult(i, REQ_PICK)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (requestCode == REQ_SAVE && resultCode == RESULT_OK && uri != null) { exportTo(uri); return }
        if (requestCode != REQ_PICK || resultCode != RESULT_OK || uri == null) return
        pick.isEnabled = false
        status.text = "Kopiuję plik do pamięci aplikacji…"
        result.text = ""
        thread {
            val msg = try {
                val copy = copyToPrivate(uri)
                runOnUiThread { status.text = "Analizuję (${copy.name})…" }
                render(JSONObject(Python.getInstance().getModule("xport_entry")
                    .callAttr("inventory", copy.absolutePath).toString()))
            } catch (e: Throwable) {
                "Błąd: ${e.javaClass.simpleName}: ${e.message ?: "bez opisu"}"
            }
            runOnUiThread {
                status.text = ""; result.text = msg; pick.isEnabled = true
                val ok = !msg.startsWith("Błąd")
                selectionBox.visibility = if (ok) android.view.View.VISIBLE else android.view.View.GONE
                if (ok) { selInfo.text = "Zaznaczono: nic"; loadPatterns() }
            }
        }
    }

    // Copy first, never read the document from where it lives; only one copy is kept at a time.
    private fun copyToPrivate(uri: Uri): File {
        var name = "eksport"
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) name = it.getString(0)
        }
        val dir = File(filesDir, "import")
        dir.deleteRecursively(); dir.mkdirs()
        val dest = File(dir, name.replace(Regex("[/\\\\]"), "_"))
        val input = contentResolver.openInputStream(uri) ?: throw IllegalStateException("nie można otworzyć pliku")
        input.use { src -> dest.outputStream().use { src.copyTo(it) } }
        return dest
    }

    private fun render(j: JSONObject): String {
        if (!j.optBoolean("ok")) return "Błąd analizy: ${j.optString("error", "nieznany")}"
        val sb = StringBuilder()
        val t = j.getJSONObject("totals")
        sb.append("Razem: ${t.optInt("conversations")} rozmów, ${t.optInt("messages")} wiadomości\n\n")
        val ps = j.getJSONArray("providers")
        for (i in 0 until ps.length()) {
            val p = ps.getJSONObject(i)
            sb.append("• ${p.optString("provider")}\n")
            sb.append("  rozmowy: ${p.optInt("conversations")}, wiadomości: ${p.optInt("messages")}\n")
            sb.append("  zakres dat: ${p.optString("first", "—")} → ${p.optString("last", "—")}\n")
            sb.append("  załączniki: ${p.optInt("attachments")}\n")
            sb.append("  zużycie (usage): ${yn(p.optBoolean("has_usage"))}, ślady narzędzi: ${yn(p.optBoolean("has_tool_traces"))}\n\n")
        }
        val ev = j.getJSONArray("events")
        sb.append("Oś czasu (${ev.length()} z ${j.optInt("events_total")}):\n")
        for (i in 0 until ev.length()) {
            val e = ev.getJSONObject(i)
            sb.append("${e.optString("t")}  ${e.optString("provider")}  ${e.optString("title")}  [${e.optString("role")}]\n")
        }
        return sb.toString()
    }

    private fun yn(b: Boolean) = if (b) "tak" else "nie"

    companion object { private const val REQ_PICK = 1
        private const val REQ_SAVE = 2 }
}
