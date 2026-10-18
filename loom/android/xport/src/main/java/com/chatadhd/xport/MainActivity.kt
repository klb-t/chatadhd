package com.chatadhd.xport

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Bundle
import android.provider.OpenableColumns
import android.text.InputType
import android.view.View
import android.widget.AdapterView
import android.widget.ArrayAdapter
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.FrameLayout
import android.widget.HorizontalScrollView
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.Spinner
import android.widget.TextView
import com.chaquo.python.Python
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import kotlin.concurrent.thread

// X-Port on the phone. The engine (loom/tools/extract, Python via Chaquopy) does all the work; this class moves bytes
// (Storage Access Framework, copy to app-private storage first) and renders what the engine returns. No network.
// Every screen is a thin form over an engine call: nothing is parsed or decided here.
class MainActivity : Activity() {
    private val py get() = Python.getInstance().getModule("xport_entry")
    private val pad get() = (12 * resources.displayMetrics.density).toInt()
    private lateinit var content: FrameLayout
    private val panels = LinkedHashMap<String, View>()
    private var catalog: JSONObject? = null
    private var opened = false

    // overview
    private lateinit var pick: Button
    private lateinit var status: TextView
    private lateinit var result: TextView
    // selection
    private lateinit var patternBox: LinearLayout
    private lateinit var selInfo: TextView
    private lateinit var expr: EditText
    // export
    private lateinit var exportInfo: TextView
    private lateinit var redactionSpinner: Spinner
    private val profileChecks = ArrayList<Pair<String, CheckBox>>()
    private lateinit var handoffCheck: CheckBox
    private var redactionIds = listOf<String>()
    // browser
    private var browseRoot = "sources"
    private var browsePath = ArrayList<String>()
    private var browseFormat = "markdown"
    private var browseNode: String? = null
    private lateinit var browseList: LinearLayout
    private lateinit var browseCrumb: TextView
    private lateinit var browseView: TextView
    private lateinit var browseListScroll: View
    private lateinit var browseViewBox: View
    private lateinit var browseBack: Button
    private var twoPane = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = getSharedPreferences("xport", Context.MODE_PRIVATE)
        val wideDp = resources.displayMetrics.widthPixels / resources.displayMetrics.density
        twoPane = prefs.getBoolean("two_pane", wideDp >= 600)
        content = FrameLayout(this)
        panels["Przegląd"] = overviewPanel()
        panels["Przeglądarka"] = browserPanel(prefs)
        panels["Wybór"] = selectionPanel()
        panels["Edycje"] = placeholder("Otwórz plik, aby zobaczyć edycje.")
        panels["Operacje"] = placeholder("Otwórz plik, aby zobaczyć operacje.")
        panels["Eksport"] = exportPanel()
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
        for (name in panels.keys) {
            tabs.addView(Button(this).apply { text = name; isAllCaps = false; setOnClickListener { show(name) } })
        }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(HorizontalScrollView(this@MainActivity).apply { addView(tabs) })
            addView(content, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        setContentView(root)
        show("Przegląd")
    }

    private fun show(name: String) {
        content.removeAllViews()
        panels[name]?.let { content.addView(it, FrameLayout.LayoutParams(-1, -1)) }
    }

    private fun placeholder(text: String): View = scroll(LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad); addView(TextView(this@MainActivity).apply { this.text = text })
    })

    private fun scroll(v: View): ScrollView = ScrollView(this).apply { addView(v) }
    private fun col(): LinearLayout = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad) }
    private fun label(t: String, size: Float = 14f) = TextView(this).apply { text = t; textSize = size; setTextIsSelectable(true) }
    private fun btn(t: String, f: () -> Unit) = Button(this).apply { text = t; isAllCaps = false; setOnClickListener { f() } }
    private fun err(e: Throwable) = "Błąd: ${e.javaClass.simpleName}: ${e.message ?: "bez opisu"}"

    // One engine call (api.Session) off the UI thread; the callback runs on the UI thread. Errors come back as text.
    private fun call(req: JSONObject, done: (JSONObject) -> Unit) {
        thread {
            val out = try { JSONObject(py.callAttr("call", req.toString()).toString()) }
            catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread { done(out) }
        }
    }

    private fun clip(s: String, n: Int = 20000) = if (s.length > n) s.substring(0, n) + "\n… (skrócono, ${s.length} znaków)" else s

    // ───────────── Przegląd: pick a file, copy it, run the inventory ─────────────
    private fun overviewPanel(): View {
        pick = btn("Wybierz plik eksportu") { openPicker() }
        status = label("Nic nie wybrano.")
        result = label("")
        return scroll(col().apply { addView(pick); addView(status); addView(result) })
    }

    private fun openPicker() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
        }, REQ_PICK)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        val uri = data?.data
        if (resultCode != RESULT_OK || uri == null) return
        if (requestCode == REQ_SAVE) { exportTo(uri); return }
        if (requestCode != REQ_PICK) return
        pick.isEnabled = false
        status.text = "Kopiuję plik do pamięci aplikacji…"
        result.text = ""
        thread {
            val msg = try {
                val copy = copyToPrivate(uri)
                runOnUiThread { status.text = "Analizuję (${copy.name})…" }
                render(JSONObject(py.callAttr("inventory", copy.absolutePath).toString()))
            } catch (e: Throwable) { err(e) }
            runOnUiThread {
                status.text = ""; result.text = msg; pick.isEnabled = true
                opened = !msg.startsWith("Błąd")
                if (opened) afterOpen()
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

    // After a file is open: fill every screen that depends on the engine's catalog.
    private fun afterOpen() {
        browsePath.clear(); browseNode = null; loadBrowse()
        selInfo.text = "Zaznaczono: nic"
        thread {
            val pats = try { JSONObject(py.callAttr("patterns").toString()) } catch (e: Throwable) { JSONObject().put("ok", false) }
            val cat = try { JSONObject(py.callAttr("catalog").toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread {
                patternBox.removeAllViews()
                if (pats.optBoolean("ok")) {
                    val ps = pats.getJSONArray("patterns")
                    for (i in 0 until ps.length()) {
                        val p = ps.getJSONObject(i)
                        patternBox.addView(btn(p.optString("label")) { runSelect("pattern", p.optString("name")) })
                    }
                }
                if (cat.optBoolean("ok")) {
                    catalog = cat
                    panels["Edycje"] = editsPanel(cat.getJSONArray("edits"))
                    panels["Operacje"] = opsPanel(cat.getJSONArray("ops"))
                    fillExport(cat)
                } else {
                    status.text = cat.optString("error")
                }
            }
        }
    }

    // ───────────── Przeglądarka: engine views, one pane or two ─────────────
    private fun browserPanel(prefs: android.content.SharedPreferences): View {
        val roots = listOf("sources", "package", "selection", "timeline", "attachments", "clusters", "recipe", "inventory", "diff", "bookmarks")
        val rootSpin = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, roots) }
        rootSpin.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (roots[pos] != browseRoot) { browseRoot = roots[pos]; browsePath.clear(); browseNode = null; if (opened) loadBrowse() }
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val fmts = listOf("markdown", "json", "raw")
        val fmtSpin = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, fmts) }
        fmtSpin.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (fmts[pos] != browseFormat) { browseFormat = fmts[pos]; browseNode?.let { loadView(it) } }
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val dual = CheckBox(this).apply {
            text = "Dwa panele"; isChecked = twoPane
            setOnCheckedChangeListener { _, on -> twoPane = on; prefs.edit().putBoolean("two_pane", on).apply(); layoutBrowser() }
        }
        browseCrumb = label("")
        browseList = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        browseView = label("")
        browseListScroll = ScrollView(this).apply { (this as ScrollView).addView(browseList) }
        browseViewBox = ScrollView(this).apply { (this as ScrollView).addView(browseView) }
        browseBack = btn("← Wróć do listy") { browseViewBox.visibility = View.GONE; browseListScroll.visibility = View.VISIBLE }
        val bar = LinearLayout(this).apply {
            orientation = LinearLayout.HORIZONTAL
            addView(rootSpin); addView(fmtSpin); addView(dual)
        }
        val up = btn("↑ Wyżej") { if (browsePath.isNotEmpty()) { browsePath.removeAt(browsePath.size - 1); loadBrowse() } }
        val body = LinearLayout(this).apply {
            tag = "body"
            addView(browseListScroll, LinearLayout.LayoutParams(0, -1, 1f))
            addView(browseViewBox, LinearLayout.LayoutParams(0, -1, 1f))
        }
        val frame = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad)
            addView(bar); addView(up); addView(browseCrumb); addView(browseBack)
            addView(body, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        layoutBrowser()
        return frame
    }

    private fun layoutBrowser() {
        val body = browseListScroll.parent as LinearLayout
        body.orientation = if (twoPane) LinearLayout.HORIZONTAL else LinearLayout.VERTICAL
        val lp = { if (twoPane) LinearLayout.LayoutParams(0, -1, 1f) else LinearLayout.LayoutParams(-1, -1) }
        browseListScroll.layoutParams = lp(); browseViewBox.layoutParams = lp()
        browseListScroll.visibility = View.VISIBLE
        browseViewBox.visibility = if (twoPane) View.VISIBLE else View.GONE
        browseBack.visibility = if (twoPane) View.GONE else View.VISIBLE
        browseBack.isEnabled = !twoPane
    }

    private fun loadBrowse() {
        browseList.removeAllViews()
        browseList.addView(label("Ładuję…"))
        call(JSONObject().put("call", "list").put("root", browseRoot).put("path", JSONArray(browsePath)).put("limit", 300)) { j ->
            browseList.removeAllViews()
            if (!j.optBoolean("ok")) { browseList.addView(label(j.optString("error"))); return@call }
            val crumbs = j.optJSONArray("breadcrumbs")
            browseCrumb.text = (0 until (crumbs?.length() ?: 0)).joinToString(" › ") { crumbs!!.getJSONObject(it).optString("label") } +
                "   (${j.optInt("shown")}/${j.optInt("total")})"
            val rows = j.getJSONArray("rows")
            if (rows.length() == 0) browseList.addView(label(j.optString("hint", "Pusto.")))
            for (i in 0 until rows.length()) {
                val r = rows.getJSONObject(i)
                val id = r.optString("id")
                val line = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL }
                line.addView(btn("${r.optString("kind")}: ${r.optString("label")}") { browseNode = id; loadView(id) },
                    LinearLayout.LayoutParams(0, -2, 1f))
                if (r.optBoolean("children")) line.addView(btn("▸") { browsePath.add(id); loadBrowse() })
                line.addView(btn("✓") { runSelectItem(id) })
                browseList.addView(line)
            }
        }
    }

    private fun loadView(id: String) {
        browseView.text = "Ładuję…"
        if (!twoPane) { browseListScroll.visibility = View.GONE; browseViewBox.visibility = View.VISIBLE }
        call(JSONObject().put("call", "view").put("node", id).put("format", browseFormat)) { j ->
            browseView.text = if (j.optBoolean("ok")) clip(j.optString("text")) else j.optString("error")
        }
    }

    private fun runSelectItem(id: String) {
        call(JSONObject().put("call", "select").put("how", "toggle").put("items", JSONArray().put(id))) { j ->
            selInfo.text = if (j.optBoolean("ok")) "Zaznaczono: ${j.optJSONObject("selection")?.optInt("conversations") ?: 0} rozmów (przełączono)" else j.optString("error")
        }
    }

    // ───────────── Wybór: structural patterns, expression, encoded data ─────────────
    private fun selectionPanel(): View {
        patternBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        expr = EditText(this).apply { hint = "wyrażenie, np. tytuł:\"projekt\" I count(messages) >= 10"; inputType = InputType.TYPE_CLASS_TEXT }
        selInfo = label("Zaznaczono: nic")
        return scroll(col().apply {
            addView(label("Wybór według struktury", 18f))
            addView(patternBox)
            addView(expr)
            addView(row(btn("Zaznacz wyrażeniem") { runSelect("expr", expr.text.toString()) }, btn("Zakodowane dane") { runSelect("encoded", "") }))
            addView(row(btn("Wszystko") { runSelect("all", "") }, btn("Odwróć") { runSelect("invert", "") }, btn("Wyczyść") { runSelect("clear", "") }))
            addView(selInfo)
        })
    }

    private fun row(vararg v: View) = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; v.forEach { addView(it) } }

    private fun runSelect(how: String, value: String) {
        selInfo.text = "Liczę…"
        thread {
            val msg = try {
                val j = JSONObject(py.callAttr("select", how, value, "add").toString())
                if (!j.optBoolean("ok")) "Błąd wyboru: ${j.optString("error")}" else renderSelection(j)
            } catch (e: Throwable) { err(e) }
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
            sb.append("Zakodowane dane (${e.optJSONObject("counts")?.optInt("findings") ?: 0}): ${parts.ifEmpty { "brak" }}\n")
        }
        val items = sel.getJSONArray("items")
        for (i in 0 until items.length()) {
            val it = items.getJSONObject(i)
            sb.append("• ${it.optString("provider")}  ${it.optString("title")}  (${it.optInt("messages")})\n")
        }
        if (sel.optInt("total") > items.length()) sb.append("… i ${sel.optInt("total") - items.length()} kolejnych\n")
        return sb.toString()
    }

    // ───────────── Generic form over an engine parameter schema ─────────────
    private class Form(val view: LinearLayout, val collect: () -> JSONObject)

    private fun buildForm(params: JSONArray): Form {
        val box = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        val getters = ArrayList<Pair<String, () -> Any?>>()
        for (i in 0 until params.length()) {
            val p = params.getJSONObject(i)
            val name = p.optString("name"); val type = p.optString("type")
            val def = if (p.isNull("default")) null else p.opt("default")
            val values = p.optJSONArray("values")
            val req = if (p.optBoolean("required")) " *" else ""
            when {
                type == "bool" -> {
                    val cb = CheckBox(this).apply { text = name; isChecked = def == true }
                    box.addView(cb)
                    val touched = booleanArrayOf(false)
                    cb.setOnCheckedChangeListener { _, _ -> touched[0] = true }
                    getters.add(name to { if (touched[0] || def != null) cb.isChecked else null })
                }
                type == "enum" && values != null && values.length() > 0 -> {
                    val opts = (0 until values.length()).map { values.getString(it) }
                    val sp = Spinner(this).apply { adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, opts) }
                    if (def != null) sp.setSelection(opts.indexOf(def.toString()).coerceAtLeast(0))
                    box.addView(label("$name$req"))
                    box.addView(sp)
                    getters.add(name to { sp.selectedItem?.toString() })
                }
                else -> {
                    val et = EditText(this).apply {
                        hint = "$name ($type)$req"
                        if (p.optBoolean("secret")) inputType = InputType.TYPE_CLASS_TEXT or InputType.TYPE_TEXT_VARIATION_PASSWORD
                        else if (type == "int" || type == "number") inputType = InputType.TYPE_CLASS_NUMBER or InputType.TYPE_NUMBER_FLAG_DECIMAL or InputType.TYPE_NUMBER_FLAG_SIGNED
                        else inputType = InputType.TYPE_CLASS_TEXT
                        if (def != null && def.toString() != "null") setText(def.toString())
                    }
                    box.addView(et)
                    getters.add(name to {
                        val t = et.text.toString()
                        when {
                            t.isEmpty() -> null
                            type == "int" || type == "number" -> t.toDoubleOrNull()?.let { if (type == "int") it.toLong() else it }
                            type in listOf("list", "strings", "paths") -> if (t.trim().startsWith("[")) JSONArray(t) else JSONArray(t.split(",").map { it.trim() }.filter { it.isNotEmpty() })
                            type in listOf("object", "any") && (t.trim().startsWith("{") || t.trim().startsWith("[")) ->
                                if (t.trim().startsWith("{")) JSONObject(t) else JSONArray(t)
                            else -> t
                        }
                    })
                }
            }
        }
        return Form(box) {
            val o = JSONObject()
            for ((n, g) in getters) { try { g()?.let { o.put(n, it) } } catch (e: Exception) { /* a malformed value is left out; the engine reports a missing one */ } }
            o
        }
    }

    private fun picker(labels: List<String>, onPick: (Int) -> Unit): Spinner = Spinner(this).apply {
        adapter = ArrayAdapter(this@MainActivity, android.R.layout.simple_spinner_dropdown_item, labels)
        onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = onPick(pos)
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
    }

    // ───────────── Edycje: batch edits with preview, apply, undo, redo ─────────────
    private fun editsPanel(edits: JSONArray): View {
        val out = label("")
        val formHolder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        var form: Form? = null
        var current = 0
        fun setEdit(i: Int) {
            current = i; formHolder.removeAllViews()
            val e = edits.getJSONObject(i)
            form = buildForm(e.getJSONArray("params")).also { formHolder.addView(it.view) }
        }
        fun req(kind: String): JSONObject = JSONObject().put("call", kind).put("edit", edits.getJSONObject(current).optString("id"))
            .put("params", form?.collect?.invoke() ?: JSONObject())
        val sp = picker((0 until edits.length()).map { edits.getJSONObject(it).optString("label") }) { setEdit(it) }
        if (edits.length() > 0) setEdit(0)
        val show = { j: JSONObject ->
            out.text = if (!j.optBoolean("ok")) j.optString("error")
            else clip((j.optString("patch_text").ifEmpty { "" }) + "\n" + j.toString(1).take(3000))
        }
        return scroll(col().apply {
            addView(label("Edycje seryjne (na zaznaczeniu, jeśli jest; inaczej na wszystkim)", 16f))
            addView(sp); addView(formHolder)
            addView(row(btn("Podgląd") { out.text = "Liczę…"; call(req("edit"), show) },
                btn("Zastosuj") { out.text = "Stosuję…"; call(req("apply"), show) }))
            addView(row(btn("Cofnij") { call(JSONObject().put("call", "undo"), show) }, btn("Ponów") { call(JSONObject().put("call", "redo"), show) }))
            addView(out)
        })
    }

    // ───────────── Operacje: any operation of the engine's registry ─────────────
    private fun opsPanel(ops: JSONArray): View {
        val out = label("")
        val formHolder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        var form: Form? = null
        var current = 0
        fun setOp(i: Int) {
            current = i; formHolder.removeAllViews()
            form = buildForm(ops.getJSONObject(i).getJSONArray("params")).also { formHolder.addView(it.view) }
        }
        val sp = picker((0 until ops.length()).map { ops.getJSONObject(it).optString("label") + "  [" + ops.getJSONObject(it).optString("id") + "]" }) { setOp(it) }
        if (ops.length() > 0) setOp(0)
        return scroll(col().apply {
            addView(label("Operacje silnika", 16f))
            addView(sp); addView(formHolder)
            addView(btn("Uruchom") {
                out.text = "Pracuję…"
                call(JSONObject().put("call", "op").put("op", ops.getJSONObject(current).optString("id"))
                    .put("params", form?.collect?.invoke() ?: JSONObject())) { j ->
                    out.text = if (j.optBoolean("ok")) clip(j.toString(1)) else j.optString("error")
                }
            })
            addView(out)
        })
    }

    // ───────────── Eksport: cut + write -> one zip, with a redaction preset and profiles ─────────────
    private fun exportPanel(): View {
        exportInfo = label("Otwórz plik i zaznacz rozmowy (zakładka Wybór).")
        redactionSpinner = Spinner(this)
        handoffCheck = CheckBox(this).apply { text = "Dołącz przekazanie do importera aplikacji (handoff)" }
        return scroll(col().apply {
            tag = "export"
            addView(label("Eksport zaznaczonego", 18f))
            addView(label("Redakcja:")); addView(redactionSpinner)
            addView(label("Profile cięcia (puste = domyślne):"))
            addView(LinearLayout(this@MainActivity).apply { tag = "profiles"; orientation = LinearLayout.VERTICAL })
            addView(handoffCheck)
            addView(btn("Eksportuj zaznaczone (zip)") { pickExportTarget() })
            addView(exportInfo)
        })
    }

    private fun fillExport(cat: JSONObject) {
        val red = cat.getJSONArray("redaction")
        redactionIds = (0 until red.length()).map { red.getJSONObject(it).optString("id") }
        redactionSpinner.adapter = ArrayAdapter(this, android.R.layout.simple_spinner_dropdown_item,
            (0 until red.length()).map { red.getJSONObject(it).optString("label") })
        val holder = (panels["Eksport"] as ScrollView).findViewWithTag<LinearLayout>("profiles")
        holder.removeAllViews(); profileChecks.clear()
        val pr = cat.getJSONArray("profiles")
        for (i in 0 until pr.length()) {
            val p = pr.getJSONObject(i)
            val cb = CheckBox(this).apply { text = p.optString("label") }
            profileChecks.add(p.optString("id") to cb); holder.addView(cb)
        }
    }

    private fun pickExportTarget() {
        startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "application/zip"; putExtra(Intent.EXTRA_TITLE, "xport-wynik.zip")
        }, REQ_SAVE)
    }

    private fun exportTo(uri: Uri) {
        exportInfo.text = "Eksportuję…"
        val redaction = redactionIds.getOrNull(redactionSpinner.selectedItemPosition)?.takeIf { it != "none" } ?: ""
        val profiles = profileChecks.filter { it.second.isChecked }.joinToString(",") { it.first }
        val handoff = handoffCheck.isChecked
        thread {
            val tmp = File(cacheDir, "xport-out.zip")
            val msg = try {
                val j = JSONObject(py.callAttr("export_selected", tmp.absolutePath, redaction, profiles, handoff).toString())
                if (!j.optBoolean("ok")) "Błąd eksportu: ${j.optString("error")}" else {
                    val out = contentResolver.openOutputStream(uri) ?: throw IllegalStateException("nie można zapisać pliku")
                    out.use { o -> tmp.inputStream().use { it.copyTo(o) } }
                    "Zapisano zip: ${j.optInt("files")} plików."
                }
            } catch (e: Throwable) { err(e) }
            tmp.delete()
            runOnUiThread { exportInfo.text = msg }
        }
    }

    companion object {
        private const val REQ_PICK = 1
        private const val REQ_SAVE = 2
    }
}
