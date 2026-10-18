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
import android.widget.ListView
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
    // selection
    private lateinit var patternBox: LinearLayout
    private lateinit var selInfo: TextView
    private lateinit var selAdapter: TreeAdapter
    private lateinit var overviewAdapter: TreeAdapter
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
    private lateinit var browseAdapter: TreeAdapter
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
        restoreSession()
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

    // ───────────── Przegląd: add sources (copied, kept), open them all in one engine session ─────────────
    private val importDir get() = File(filesDir, "import")
    private lateinit var sourcesBox: LinearLayout

    private fun overviewPanel(): View {
        pick = btn("Dodaj źródło (jedno lub kilka plików)") { openPicker() }
        status = label("Nic nie wybrano.")
        overviewAdapter = TreeAdapter(this)
        val list = ListView(this)
        list.addHeaderView(col().apply {
            addView(pick)
            addView(btn("Usuń wszystkie źródła") { clearSources() })
            addView(status)
        }, null, false)
        overviewAdapter.attach(list)
        return list
    }

    private fun openPicker() {
        startActivityForResult(Intent(Intent.ACTION_OPEN_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "*/*"
            putExtra(Intent.EXTRA_ALLOW_MULTIPLE, true)
        }, REQ_PICK)
    }

    @Deprecated("Deprecated in Java")
    override fun onActivityResult(requestCode: Int, resultCode: Int, data: Intent?) {
        super.onActivityResult(requestCode, resultCode, data)
        if (resultCode != RESULT_OK || data == null) return
        if (requestCode == REQ_SAVE) { data.data?.let { exportTo(it) }; return }
        if (requestCode != REQ_PICK) return
        val uris = ArrayList<Uri>()
        data.data?.let { uris.add(it) }
        data.clipData?.let { c -> for (i in 0 until c.itemCount) uris.add(c.getItemAt(i).uri) }
        if (uris.isEmpty()) return
        busy("Kopiuję ${uris.size} plik(ów) do pamięci aplikacji…")
        thread {
            try { uris.forEach { copyToPrivate(it) } } catch (e: Throwable) {
                runOnUiThread { idle(err(e)) }; return@thread
            }
            reopenAll()
        }
    }

    private fun busy(msg: String) { pick.isEnabled = false; status.text = msg }
    private fun idle(msg: String) { pick.isEnabled = true; status.text = msg }

    // All copies in the private directory are the sources; the engine opens them together (and merges duplicates).
    private fun reopenAll() {
        val files = (importDir.listFiles() ?: emptyArray()).filter { it.isFile }.sortedBy { it.name }
        runOnUiThread { status.text = "Analizuję ${files.size} źródeł…" }
        val msg = try {
            JSONObject(py.callAttr("open_sources", JSONArray(files.map { it.absolutePath }).toString()).toString())
        } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
        runOnUiThread { showOverview(msg) }
    }

    private fun showOverview(j: JSONObject) {
        pick.isEnabled = true
        status.text = ""
        if (j.optBoolean("empty")) { opened = false; overviewAdapter.setRoots(emptyList()); status.text = "Nic nie wybrano."; return }
        if (!j.optBoolean("ok")) { opened = false; status.text = "Błąd analizy: ${j.optString("error", "nieznany")}"; return }
        opened = true
        overviewAdapter.setRoots(overviewNodes(j))
        afterOpen()
    }

    private fun overviewNodes(j: JSONObject): List<TNode> {
        val roots = ArrayList<TNode>()
        val files = (importDir.listFiles() ?: emptyArray()).filter { it.isFile }.sortedBy { it.name }
        roots.add(TNode("Źródła (${files.size})", children = files.map { f ->
            TNode(displayName(f), "${f.length() / 1024} KB", onOpen = { showText(this, displayName(f), "${displayName(f)}\n${f.length()} bajtów") },
                actions = { listOf("Usuń źródło" to { removeSource(f) }) })
        }.toMutableList()).also { it.expanded = files.size <= 3 })
        val t = j.getJSONObject("totals")
        roots.add(TNode("Razem: ${t.optInt("conversations")} rozmów, ${t.optInt("messages")} wiadomości"))
        val ps = j.getJSONArray("providers")
        val provNodes = (0 until ps.length()).map { providerNode(ps.getJSONObject(it)) }
        roots.add(TNode("Dostawcy (${provNodes.size})", children = provNodes.toMutableList()).also { it.expanded = true })
        roots.add(TNode("Oś czasu (wszystkie wiadomości, wg miesięcy)", loader = { _, done -> loadTimeline(done) }))
        return roots
    }

    private fun providerNode(p: JSONObject): TNode {
        val name = p.optString("provider")
        val items = p.getJSONArray("items")
        val kids = (0 until items.length()).map { convNode(items.getJSONObject(it)) }.toMutableList()
        val detail = "rozmowy: ${p.optInt("conversations")}, wiadomości: ${p.optInt("messages")}\n" +
            "zakres: ${p.optString("first", "—")} → ${p.optString("last", "—")}\n" +
            "załączniki: ${p.optInt("attachments")}, usage: ${yn(p.optBoolean("has_usage"))}, ślady narzędzi: ${yn(p.optBoolean("has_tool_traces"))}"
        return TNode(name, detail, children = kids, actions = {
            listOf("Zaznacz wszystko tego dostawcy" to { runSelect("expr", "service:$name", "add") },
                "Odznacz wszystko tego dostawcy" to { runSelect("expr", "service:$name", "subtract") },
                "Zaznacz tylko to (zamień zaznaczenie)" to { runSelect("expr", "service:$name", "replace") })
        })
    }

    private fun convNode(c: JSONObject): TNode {
        val uid = c.optString("uid")
        val title = c.optString("title").ifEmpty { "(bez tytułu)" }
        return TNode(title, "${c.optInt("messages")} wiadomości · ${c.optString("first", "—")} → ${c.optString("last", "—")}", id = uid, tint = tintOf(uid),
            loader = { _, done -> loadEvents(uid, done) }, actions = { engineActions(uid, title) })
    }

    private fun loadEvents(uid: String, done: (List<TNode>) -> Unit) {
        thread {
            val j = try { JSONObject(py.callAttr("events", uid).toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread {
                if (!j.optBoolean("ok")) { done(listOf(TNode(j.optString("error")))); return@runOnUiThread }
                val ev = j.getJSONArray("events")
                done((0 until ev.length()).map { msgNode(ev.getJSONObject(it), false, tintOf(uid)) })
            }
        }
    }

    private fun msgNode(e: JSONObject, withTitle: Boolean, tint: Int = 0): TNode {
        val id = e.optString("uid")
        val title = if (withTitle) "  ${e.optString("provider")} · ${e.optString("title")}" else e.optString("preview")
        val label = "${e.optString("role")} · ${e.optString("t")}"
        return TNode(label, title, id = id, tint = tint, onOpen = { showView(id, label) }, actions = { engineActions(id, label) })
    }

    // Global time axis, collapsed: month -> day -> messages of the day (loaded when the day is expanded).
    private fun loadTimeline(done: (List<TNode>) -> Unit) {
        thread {
            val j = try { JSONObject(py.callAttr("timeline_groups").toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread {
                if (!j.optBoolean("ok")) { done(listOf(TNode(j.optString("error")))); return@runOnUiThread }
                val ms = j.getJSONArray("months")
                done((0 until ms.length()).map { mi ->
                    val m = ms.getJSONObject(mi)
                    val days = m.getJSONArray("days")
                    var total = 0
                    val dayNodes = (0 until days.length()).map { di ->
                        val d = days.getJSONObject(di); total += d.optInt("count")
                        TNode(d.optString("day"), "${d.optInt("count")} wiadomości", loader = { _, cb -> loadDay(d.optString("day"), cb) })
                    }
                    TNode(m.optString("month"), "$total wiadomości, ${dayNodes.size} dni", children = dayNodes.toMutableList())
                })
            }
        }
    }

    // One colour per conversation, stable: the switch from one conversation to another is visible at a glance.
    private fun tintOf(uid: String): Int {
        val h = ((uid.hashCode() % 360) + 360) % 360
        return android.graphics.Color.HSVToColor(48, floatArrayOf(h.toFloat(), 0.7f, 0.95f))
    }

    private fun loadDay(day: String, done: (List<TNode>) -> Unit) {
        thread {
            val j = try { JSONObject(py.callAttr("day_events", day).toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread {
                if (!j.optBoolean("ok")) { done(listOf(TNode(j.optString("error")))); return@runOnUiThread }
                val items = j.getJSONArray("items")
                done((0 until items.length()).map { ii ->
                    val it = items.getJSONObject(ii)
                    if (it.optString("type") == "work") workNode(it) else runNode(it)
                })
            }
        }
    }

    // A run: consecutive messages of one conversation. The header names it, so every switch is visible.
    private fun runNode(b: JSONObject): TNode {
        val conv = b.optString("conversation"); val title = b.optString("title").ifEmpty { "(bez tytułu)" }
        val tint = tintOf(conv)
        val ev = b.getJSONArray("events")
        val kids = (0 until ev.length()).map { ei ->
            val e = ev.getJSONObject(ei); val id = e.optString("uid"); val lbl = "${e.optString("role")} · ${e.optString("t")}"
            TNode(lbl, e.optString("preview"), id = id, tint = tint, onOpen = { showView(id, lbl) }, actions = { engineActions(id, lbl) })
        }
        return TNode("⇄ $title", "${b.optString("provider")} · ${ev.length()} wiadomości · ${b.optString("first").drop(11).take(5)}–${b.optString("last").drop(11).take(5)}",
            id = conv, children = kids.toMutableList(), tint = tint, actions = { engineActions(conv, title) })
    }

    // Frequent switching between conversations (more than the engine's min_switches, preset 3) = one block of work
    // on several conversations at once; its runs are inside.
    private fun workNode(w: JSONObject): TNode {
        val runs = w.getJSONArray("runs")
        val kids = (0 until runs.length()).map { runNode(runs.getJSONObject(it)) }
        val uids = kids.mapNotNull { it.id }.distinct()
        return TNode("⧉ Praca na ${w.optInt("conversations")} rozmowach naraz",
            "${w.optInt("switches")} przełączeń · ${w.optInt("messages")} wiadomości · ${w.optString("first").drop(11).take(5)}–${w.optString("last").drop(11).take(5)}",
            children = kids.toMutableList(), tint = android.graphics.Color.argb(40, 128, 128, 128),
            actions = {
                listOf("Zaznacz wszystkie rozmowy z bloku" to {
                    call(JSONObject().put("call", "select").put("how", "items").put("items", JSONArray(uids))) { j ->
                        toast(if (j.optBoolean("ok")) "Zaznaczono ${uids.size} rozmów" else j.optString("error")); refreshSelection()
                    }
                })
            })
    }

    // The menu every engine object (conversation, message, row) shares: all of it is the engine's own calls.
    private fun engineActions(id: String, title: String): List<Pair<String, () -> Unit>> = listOf(
        "Podgląd (tekst)" to { showView(id, title) },
        "Podgląd (JSON)" to { showView(id, title, "json") },
        "Zaznacz / odznacz" to { runSelectItem(id) },
        "Zaznacz podobne" to {
            call(JSONObject().put("call", "select").put("how", "similar").put("items", JSONArray().put(id))) { j ->
                toast(if (j.optBoolean("ok")) "Zaznaczono podobne" else j.optString("error")); refreshSelection()
            }
        },
        "Zakładka" to {
            call(JSONObject().put("call", "bookmark").put("node", id).put("name", title.take(60))) { j -> toast(if (j.optBoolean("ok")) "Dodano zakładkę" else j.optString("error")) }
        },
        "Etykieta…" to {
            val et = EditText(this)
            android.app.AlertDialog.Builder(this).setTitle("Etykieta").setView(et).setNegativeButton("Anuluj", null)
                .setPositiveButton("Dodaj") { _, _ ->
                    call(JSONObject().put("call", "tag").put("node", id).put("tag", et.text.toString())) { j -> toast(if (j.optBoolean("ok")) "Dodano etykietę" else j.optString("error")) }
                }.show()
        },
        "Kopiuj id" to { copyToClipboard(this, id) },
    )

    private fun toast(t: String) = android.widget.Toast.makeText(this, t, android.widget.Toast.LENGTH_SHORT).show()

    private fun showView(id: String, title: String, format: String = "markdown") {
        call(JSONObject().put("call", "view").put("node", id).put("format", format)) { j ->
            showText(this, title, if (j.optBoolean("ok")) j.optString("text") else j.optString("error"))
        }
    }

    // stored as <millis>__<original name>
    private fun displayName(f: File) = f.name.substringAfter("__", f.name)

    private fun removeSource(f: File) {
        f.delete()
        busy("Usuwam źródło…")
        thread { reopenAll() }
    }

    private fun clearSources() {
        importDir.listFiles()?.forEach { it.delete() }
        busy("Usuwam źródła…")
        thread { reopenAll() }
    }

    // Copy first, never read the document from where it lives. Earlier copies are kept: they are the other sources.
    private fun copyToPrivate(uri: Uri): File {
        var name = "eksport"
        contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME), null, null, null)?.use {
            if (it.moveToFirst() && !it.isNull(0)) name = it.getString(0)
        }
        importDir.mkdirs()
        val dest = File(importDir, "${System.currentTimeMillis()}__" + name.replace(Regex("[/\\]"), "_"))
        val input = contentResolver.openInputStream(uri) ?: throw IllegalStateException("nie można otworzyć pliku")
        input.use { src -> dest.outputStream().use { src.copyTo(it) } }
        return dest
    }

    // The UI can be recreated (the process can be killed) while the engine session lives on or the copies stay on disk.
    private fun restoreSession() {
        thread {
            val o = try { JSONObject(py.callAttr("overview").toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            if (o.optBoolean("empty") && (importDir.listFiles() ?: emptyArray()).any { it.isFile }) {
                runOnUiThread { status.text = "Przywracam źródła…" }
                reopenAll()
            } else if (!o.optBoolean("empty")) {
                runOnUiThread { showOverview(o) }
            }
        }
    }

    private fun yn(b: Boolean) = if (b) "tak" else "nie"

    // After a file is open: fill every screen that depends on the engine's catalog.
    private fun afterOpen() {
        browsePath.clear(); browseNode = null; loadBrowse()
        refreshSelection()
        thread {
            val pats = try { JSONObject(py.callAttr("patterns").toString()) } catch (e: Throwable) { JSONObject().put("ok", false) }
            val cat = try { JSONObject(py.callAttr("catalog").toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread {
                patternBox.removeAllViews()
                if (pats.optBoolean("ok")) {
                    val ps = pats.getJSONArray("patterns")
                    for (i in 0 until ps.length()) {
                        val p = ps.getJSONObject(i)
                        patternBox.addView(btn(p.optString("label")) { runSelect("pattern", p.optString("name"), "add") }.apply {
                            setOnLongClickListener {
                                val nm = p.optString("name")
                                android.app.AlertDialog.Builder(this@MainActivity).setTitle(p.optString("label"))
                                    .setItems(arrayOf("Dodaj do zaznaczenia", "Odejmij od zaznaczenia", "Zamień zaznaczenie", "Pokaż wyrażenie", "Wstaw wyrażenie do pola")) { _, k ->
                                        when (k) {
                                            0 -> runSelect("pattern", nm, "add"); 1 -> runSelect("pattern", nm, "subtract"); 2 -> runSelect("pattern", nm, "replace")
                                            3 -> showText(this@MainActivity, p.optString("label"), p.optString("expr"))
                                            else -> expr.setText(p.optString("expr"))
                                        }
                                    }.show()
                                true
                            }
                        })
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
        browseAdapter = TreeAdapter(this)
        browseView = label("")
        browseListScroll = ListView(this).also { browseAdapter.attach(it) }
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
        browseAdapter.setRoots(listOf(TNode("Ładuję…")))
        call(JSONObject().put("call", "list").put("root", browseRoot).put("path", JSONArray(browsePath)).put("limit", 0)) { j ->
            if (!j.optBoolean("ok")) { browseAdapter.setRoots(listOf(TNode(j.optString("error")))); return@call }
            val crumbs = j.optJSONArray("breadcrumbs")
            browseCrumb.text = (0 until (crumbs?.length() ?: 0)).joinToString(" › ") { crumbs!!.getJSONObject(it).optString("label") } +
                "   (${j.optInt("shown")}/${j.optInt("total")})"
            val rows = j.getJSONArray("rows")
            val nodes = ArrayList<TNode>()
            if (rows.length() == 0) nodes.add(TNode(j.optString("hint", "Pusto.")))
            for (i in 0 until rows.length()) {
                val r = rows.getJSONObject(i)
                val id = r.optString("id"); val lbl = "${r.optString("kind")}: ${r.optString("label")}"
                val cols = r.optJSONObject("cols")
                val detail = cols?.keys()?.asSequence()?.filter { it != "label" }?.joinToString(" · ") { "$it=${cols.opt(it)}" } ?: ""
                val deeper = r.optBoolean("children")
                nodes.add(TNode(lbl, detail, id = id,
                    onOpen = { if (deeper) { browsePath.add(id); loadBrowse() } else { browseNode = id; loadView(id) } },
                    actions = {
                        (if (deeper) listOf("Wejdź głębiej" to { browsePath.add(id); loadBrowse() }) else emptyList()) +
                            listOf("Pokaż w panelu podglądu" to { browseNode = id; loadView(id) }) + engineActions(id, lbl)
                    }))
            }
            browseAdapter.setRoots(nodes)
        }
    }

    private fun loadView(id: String) {
        browseView.text = "Ładuję…"
        if (!twoPane) { browseListScroll.visibility = View.GONE; browseViewBox.visibility = View.VISIBLE }
        browseView.setOnLongClickListener { copyToClipboard(this, browseView.text.toString()); true }
        call(JSONObject().put("call", "view").put("node", id).put("format", browseFormat)) { j ->
            browseView.text = if (j.optBoolean("ok")) clip(j.optString("text")) else j.optString("error")
        }
    }

    private fun runSelectItem(id: String) {
        call(JSONObject().put("call", "select").put("how", "toggle").put("items", JSONArray().put(id))) { j ->
            if (!j.optBoolean("ok")) toast(j.optString("error")) else { toast("Przełączono zaznaczenie"); refreshSelection() }
        }
    }

    // ───────────── Wybór: structural patterns, expression, encoded data ─────────────
    private fun selectionPanel(): View {
        patternBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        expr = EditText(this).apply { hint = "wyrażenie, np. tytuł:\"projekt\" I count(messages) >= 10"; inputType = InputType.TYPE_CLASS_TEXT }
        selInfo = label("Zaznaczono: nic")
        selAdapter = TreeAdapter(this)
        val list = ListView(this)
        list.addHeaderView(col().apply {
            addView(label("Wybór według struktury (przytrzymaj wzorzec: odejmij / zamień / wyrażenie)", 16f))
            addView(patternBox)
            addView(expr)
            addView(row(btn("Zaznacz wyrażeniem") { runSelect("expr", expr.text.toString(), "add") },
                btn("Zakodowane dane") { runSelect("encoded", "", "add") }))
            addView(row(btn("Wszystko") { runSelect("all", "", "add") }, btn("Odwróć") { runSelect("invert", "", "add") },
                btn("Wyczyść") { runSelect("clear", "", "add") }))
            addView(selInfo)
        }, null, false)
        selAdapter.attach(list)
        return list
    }

    private fun row(vararg v: View) = LinearLayout(this).apply { orientation = LinearLayout.HORIZONTAL; v.forEach { addView(it) } }

    private fun runSelect(how: String, value: String, mode: String = "add") {
        selInfo.text = "Liczę…"
        thread {
            val j = try { JSONObject(py.callAttr("select", how, value, mode).toString()) } catch (e: Throwable) { JSONObject().put("ok", false).put("error", err(e)) }
            runOnUiThread { showSelection(j) }
        }
    }

    private fun refreshSelection() {
        call(JSONObject().put("call", "selection").put("limit", 0)) { j ->
            showSelection(if (j.optBoolean("ok")) JSONObject().put("ok", true).put("selection", j) else j)
        }
    }

    private fun showSelection(j: JSONObject) {
        if (!j.optBoolean("ok")) { selInfo.text = "Błąd wyboru: ${j.optString("error")}"; selAdapter.setRoots(emptyList()); return }
        val sel = j.getJSONObject("selection")
        val s = sel.getJSONObject("summary")
        val sb = StringBuilder("Zaznaczono: ${s.optInt("conversations")} rozmów, ${s.optInt("messages")} wiadomości")
        j.optJSONObject("encoded")?.let { e ->
            val k = e.optJSONObject("by_kind")
            val parts = k?.keys()?.asSequence()?.map { "$it: ${k.optInt(it)}" }?.joinToString(", ") ?: ""
            sb.append("\nZakodowane dane (${e.optJSONObject("counts")?.optInt("findings") ?: 0}): ${parts.ifEmpty { "brak" }}")
        }
        selInfo.text = sb.toString()
        val items = sel.getJSONArray("items")
        selAdapter.setRoots((0 until items.length()).map { i ->
            val it = items.getJSONObject(i)
            val uid = it.optString("uid"); val title = it.optString("title").ifEmpty { "(bez tytułu)" }
            TNode(title, "${it.optString("provider")} · ${it.optInt("messages")} wiadomości", id = uid, tint = tintOf(uid),
                loader = { _, done -> loadEvents(uid, done) },
                actions = { engineActions(uid, title) + listOf("Usuń z zaznaczenia" to { runSelectItem(uid) }) })
        })
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
        val adapter = TreeAdapter(this)
        val info = label("")
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
            if (!j.optBoolean("ok")) { info.text = j.optString("error"); adapter.setRoots(emptyList()) }
            else {
                info.text = "Gotowe."
                val roots = ArrayList<TNode>()
                val patch = j.optString("patch_text")
                if (patch.isNotEmpty()) roots.add(TNode("Zmiany", patch.lines().size.toString() + " linii", onOpen = { showText(this, "Zmiany", patch) }, copy = patch))
                roots.add(jsonNode("wynik", j, this@MainActivity).also { it.expanded = true })
                adapter.setRoots(roots)
            }
        }
        val list = ListView(this)
        list.addHeaderView(col().apply {
            addView(label("Edycje seryjne (na zaznaczeniu, jeśli jest; inaczej na wszystkim)", 16f))
            addView(sp); addView(formHolder)
            addView(row(btn("Podgląd") { info.text = "Liczę…"; call(req("edit"), show) },
                btn("Zastosuj") { info.text = "Stosuję…"; call(req("apply"), show) }))
            addView(row(btn("Cofnij") { call(JSONObject().put("call", "undo"), show) }, btn("Ponów") { call(JSONObject().put("call", "redo"), show) }))
            addView(info)
        }, null, false)
        adapter.attach(list)
        return list
    }

    // ───────────── Operacje: any operation of the engine's registry ─────────────
    private fun opsPanel(ops: JSONArray): View {
        val adapter = TreeAdapter(this)
        val info = label("")
        val formHolder = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        var form: Form? = null
        var current = 0
        fun setOp(i: Int) {
            current = i; formHolder.removeAllViews()
            form = buildForm(ops.getJSONObject(i).getJSONArray("params")).also { formHolder.addView(it.view) }
        }
        val sp = picker((0 until ops.length()).map { ops.getJSONObject(it).optString("label") + "  [" + ops.getJSONObject(it).optString("id") + "]" }) { setOp(it) }
        if (ops.length() > 0) setOp(0)
        val list = ListView(this)
        list.addHeaderView(col().apply {
            addView(label("Operacje silnika", 16f))
            addView(sp); addView(formHolder)
            addView(btn("Uruchom") {
                info.text = "Pracuję…"
                call(JSONObject().put("call", "op").put("op", ops.getJSONObject(current).optString("id"))
                    .put("params", form?.collect?.invoke() ?: JSONObject())) { j ->
                    if (!j.optBoolean("ok")) { info.text = j.optString("error"); adapter.setRoots(emptyList()) }
                    else { info.text = "Gotowe."; adapter.setRoots(listOf(jsonNode("wynik", j, this@MainActivity).also { it.expanded = true })) }
                }
            })
            addView(info)
        }, null, false)
        adapter.attach(list)
        return list
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
