package com.chatadhd.xport

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.widget.Button
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
        }
        setContentView(ScrollView(this).apply { addView(col) })
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
            runOnUiThread { status.text = ""; result.text = msg; pick.isEnabled = true }
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

    companion object { private const val REQ_PICK = 1 }
}
