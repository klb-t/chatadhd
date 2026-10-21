package com.chatadhd.xport

import android.app.Activity
import android.content.Context
import android.content.Intent
import android.graphics.Typeface
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
import android.widget.LinearLayout
import android.widget.ListView
import android.widget.RadioButton
import android.widget.RadioGroup
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
    private lateinit var rootSpinner: Spinner
    private lateinit var rootAdapter: ArrayAdapter<String>
    private lateinit var formatSpinner: Spinner
    private lateinit var formatAdapter: ArrayAdapter<String>
    private val browseRoots = listOf("sources", "package", "selection", "timeline", "attachments", "clusters", "recipe", "inventory", "diff", "bookmarks")
    private val browseFormats = listOf("markdown", "json", "raw")
    private var uiLabels: JSONObject? = null     // Polish names of roots, node kinds and viewer formats (engine data, ui_labels call)
    private val tabButtons = LinkedHashMap<String, Button>()
    // The app stores every added file as "<millis>__<original name>"; people see the original name.
    private val copyPrefix = Regex("""^\d+__""")

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
        // Every tab visible at once, whatever the width: two rows of three equal buttons (no scrolling tab bar to discover).
        val tabs = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        for (names in panels.keys.chunked(3)) {
            tabs.addView(LinearLayout(this).apply {
                orientation = LinearLayout.HORIZONTAL
                for (name in names) {
                    val b = Button(this@MainActivity).apply { text = name; isAllCaps = false; setOnClickListener { show(name) } }
                    tabButtons[name] = b
                    addView(b, LinearLayout.LayoutParams(0, LinearLayout.LayoutParams.WRAP_CONTENT, 1f))
                }
            })
        }
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            addView(tabs)
            addView(content, LinearLayout.LayoutParams(-1, 0, 1f))
        }
        setContentView(root)
        show("Przegląd")
        restoreSession()
        loadUiLabels()
    }

    private fun show(name: String) {
        for ((n, b) in tabButtons) b.setTypeface(null, if (n == name) Typeface.BOLD else Typeface.NORMAL)
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

    private fun clip(s: String, n: Int = 20000) = if (s.length > n) s.substring(0, n) + "\n… (skrócono, ${Pl.count(s.length, Nouns.CHAR)})" else s

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
        busy("Kopiuję ${Pl.count(uris.size, Nouns.FILE)} do pamięci aplikacji…")
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
        runOnUiThread { status.text = "Analizuję ${Pl.count(files.size, Nouns.SOURCE)}…" }
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
            TNode(displayName(f), Pl.size(f.length()), onOpen = { showText(this, displayName(f), "${displayName(f)}\n${Pl.size(f.length())} (${Pl.count(f.length(), Nouns.BYTE)})") },
                actions = { listOf("Usuń źródło" to { removeSource(f) }) })
        }.toMutableList()).also { it.expanded = files.size <= 3 })
        val t = j.getJSONObject("totals")
        roots.add(TNode("Razem: ${Pl.count(t.optInt("conversations"), Nouns.CONVERSATION)}, ${Pl.count(t.optInt("messages"), Nouns.MESSAGE)}"))
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
        return TNode(title, "${Pl.count(c.optInt("messages"), Nouns.MESSAGE)} · ${c.optString("first", "—")} → ${c.optString("last", "—")}", id = uid, tint = tintOf(uid),
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
                        TNode(d.optString("day"), Pl.count(d.optInt("count"), Nouns.MESSAGE), loader = { _, cb -> loadDay(d.optString("day"), cb) })
                    }
                    TNode(m.optString("month"), "${Pl.count(total, Nouns.MESSAGE)}, ${Pl.count(dayNodes.size, Nouns.DAY)}", children = dayNodes.toMutableList())
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
        return TNode("⇄ $title", "${b.optString("provider")} · ${Pl.count(ev.length(), Nouns.MESSAGE)} · ${b.optString("first").drop(11).take(5)}–${b.optString("last").drop(11).take(5)}",
            id = conv, children = kids.toMutableList(), tint = tint, actions = { engineActions(conv, title) })
    }

    // Frequent switching between conversations (more than the engine's min_switches, preset 3) = one block of work
    // on several conversations at once; its runs are inside.
    private fun workNode(w: JSONObject): TNode {
        val runs = w.getJSONArray("runs")
        val kids = (0 until runs.length()).map { runNode(runs.getJSONObject(it)) }
        val uids = kids.mapNotNull { it.id }.distinct()
        return TNode("⧉ Praca na ${Pl.count(w.optInt("conversations"), Nouns.CONVERSATION_LOC)} naraz",
            "${Pl.count(w.optInt("switches"), Nouns.SWITCH)} · ${Pl.count(w.optInt("messages"), Nouns.MESSAGE)} · ${w.optString("first").drop(11).take(5)}–${w.optString("last").drop(11).take(5)}",
            children = kids.toMutableList(), tint = android.graphics.Color.argb(40, 128, 128, 128),
            actions = {
                listOf("Zaznacz wszystkie rozmowy z bloku" to {
                    call(JSONObject().put("call", "select").put("how", "items").put("items", JSONArray(uids))) { j ->
                        toast(if (j.optBoolean("ok")) "Zaznaczono ${Pl.count(uids.size, Nouns.CONVERSATION)}" else j.optString("error")); refreshSelection()
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
    private fun originalName(n: String) = n.replaceFirst(copyPrefix, "")
    private fun displayName(f: File) = originalName(f.name)

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
        val dest = File(importDir, "${System.currentTimeMillis()}__" + name.replace(Regex("""[/\\]"""), "_"))
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
                    patternBox.addView(btn("Zawiera dane kodowane (base64, hex…)") { chooseEncoded(pats.optJSONArray("encoded_kinds")) })
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
                    panels["Edycje"] = editsPanel(cat.getJSONArray("edits"), cat.optJSONArray("macros") ?: JSONArray())
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
        val roots = browseRoots
        rootAdapter = spinnerAdapter(roots.map { rootLabel(it) })
        val rootSpin = Spinner(this).apply { adapter = rootAdapter }
        rootSpinner = rootSpin
        rootSpin.onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) {
                if (roots[pos] != browseRoot) { browseRoot = roots[pos]; browsePath.clear(); browseNode = null; if (opened) loadBrowse() }
            }
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
        val fmts = browseFormats
        formatAdapter = spinnerAdapter(fmts.map { formatLabel(it) })
        val fmtSpin = Spinner(this).apply { adapter = formatAdapter }
        formatSpinner = fmtSpin
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

    private fun labelOf(group: String, key: String): String =
        uiLabels?.optJSONObject(group)?.optString(key, "")?.takeIf { it.isNotEmpty() } ?: key
    private fun rootLabel(r: String) = labelOf("roots", r)
    private fun formatLabel(f: String) = labelOf("formats", f)
    private fun kindLabel(k: String) = labelOf("kinds", k)

    // An archive is shown under the name the person gave it, not under the app's stored copy name.
    private fun shownName(kind: String, label: String) = if (kind == "archive") originalName(label) else label

    // The Polish names come from the engine's data (ui_labels); until they arrive the engine's own keys are shown.
    private fun loadUiLabels() {
        thread {
            val j = try { JSONObject(py.callAttr("ui_labels").toString()) } catch (e: Throwable) { JSONObject().put("ok", false) }
            if (!j.optBoolean("ok")) return@thread
            runOnUiThread {
                uiLabels = j
                fun refill(a: ArrayAdapter<String>, sp: Spinner, keys: List<String>, current: String, name: (String) -> String) {
                    a.setNotifyOnChange(false); a.clear(); a.addAll(keys.map(name)); a.notifyDataSetChanged()
                    sp.setSelection(keys.indexOf(current).coerceAtLeast(0))
                }
                refill(rootAdapter, rootSpinner, browseRoots, browseRoot) { rootLabel(it) }
                refill(formatAdapter, formatSpinner, browseFormats, browseFormat) { formatLabel(it) }
                if (opened) loadBrowse()
            }
        }
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
            browseCrumb.text = (0 until (crumbs?.length() ?: 0)).joinToString(" › ") {
                val c = crumbs!!.getJSONObject(it)
                shownName(if (c.optString("id").startsWith("arch:")) "archive" else "", c.optString("label"))
            } +
                "   (${j.optInt("shown")}/${j.optInt("total")})"
            val rows = j.getJSONArray("rows")
            val nodes = ArrayList<TNode>()
            if (rows.length() == 0) nodes.add(TNode(j.optString("hint", "Pusto.")))
            for (i in 0 until rows.length()) {
                val r = rows.getJSONObject(i)
                val id = r.optString("id"); val kind = r.optString("kind")
                val lbl = "${kindLabel(kind)}: ${shownName(kind, r.optString("label"))}"
                val cols = r.optJSONObject("cols")
                val defs = j.optJSONObject("columns")?.optJSONArray(kind)
                val detail = cols?.keys()?.asSequence()?.filter { it != "label" }?.joinToString(" · ") { "${colLabel(defs, it)}: ${colValue(cols.opt(it))}" } ?: ""
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

    // A column is named by the engine's own (Polish) label for the node kind; an empty value is shown as a dash, not as nothing.
    private fun colLabel(defs: JSONArray?, id: String): String {
        if (defs != null) for (i in 0 until defs.length()) {
            val d = defs.getJSONObject(i)
            if (d.optString("id") == id) return d.optString("label", "").ifEmpty { id }
        }
        return id
    }
    private fun colValue(v: Any?): String = if (v == null || v == JSONObject.NULL || v.toString().isEmpty()) "—" else v.toString()

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
            addView(row(btn("Zaznacz wyrażeniem") { runSelect("expr", expr.text.toString(), "add") }))
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

    // "Zawiera dane kodowane": the kinds the engine detects are sub-choices (all on by default); the mode is picked like for
    // the other patterns. Thresholds (min. długość…) are the parameters of the operation "select.encoded" in the Operacje tab.
    private fun chooseEncoded(kinds: JSONArray?) {
        val ids = ArrayList<String>()
        val names = ArrayList<String>()
        if (kinds != null) for (i in 0 until kinds.length()) { ids.add(kinds.getJSONObject(i).optString("id")); names.add(kinds.getJSONObject(i).optString("label")) }
        if (ids.isEmpty()) { runSelect("encoded", "", "add"); return }
        val checked = BooleanArray(ids.size) { true }
        fun go(mode: String) {
            val chosen = ids.filterIndexed { i, _ -> checked[i] }
            if (chosen.isEmpty()) { selInfo.text = "Zaznacz co najmniej jeden rodzaj danych kodowanych."; return }
            runSelect("encoded", chosen.joinToString(","), mode)
        }
        android.app.AlertDialog.Builder(this).setTitle("Zawiera dane kodowane")
            .setMultiChoiceItems(names.toTypedArray(), checked) { _, i, on -> checked[i] = on }
            .setPositiveButton("Dodaj do zaznaczenia") { _, _ -> go("add") }
            .setNeutralButton("Odejmij") { _, _ -> go("subtract") }
            .setNegativeButton("Zamień") { _, _ -> go("replace") }
            .show()
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
        val sb = StringBuilder("Zaznaczono: ${Pl.count(s.optInt("conversations"), Nouns.CONVERSATION)}, ${Pl.count(s.optInt("messages"), Nouns.MESSAGE)}")
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
            TNode(title, "${it.optString("provider")} · ${Pl.count(it.optInt("messages"), Nouns.MESSAGE)}", id = uid, tint = tintOf(uid),
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
            val lab = p.optString("label", "").ifEmpty { name }               // shown text; `name` stays the key that is sent
            val typeLab = p.optString("type_label", "").ifEmpty { type }
            val def = if (p.isNull("default")) null else p.opt("default")
            val values = p.optJSONArray("values")
            val req = if (p.optBoolean("required")) " *" else ""
            when {
                type == "bool" -> {
                    val cb = CheckBox(this).apply { text = lab; isChecked = def == true }
                    box.addView(cb)
                    val touched = booleanArrayOf(false)
                    cb.setOnCheckedChangeListener { _, _ -> touched[0] = true }
                    getters.add(name to { if (touched[0] || def != null) cb.isChecked else null })
                }
                type == "enum" && values != null && values.length() > 0 -> {
                    val opts = (0 until values.length()).map { values.getString(it) }
                    val vl = p.optJSONArray("value_labels")
                    val shown = opts.mapIndexed { k, o -> vl?.optString(k, "")?.ifEmpty { null } ?: o }
                    val sp = Spinner(this).apply { adapter = spinnerAdapter(shown) }
                    if (def != null) sp.setSelection(opts.indexOf(def.toString()).coerceAtLeast(0))
                    box.addView(label("$lab$req"))
                    box.addView(sp)
                    getters.add(name to { opts.getOrNull(sp.selectedItemPosition) })
                }
                else -> {
                    val et = EditText(this).apply {
                        hint = "$lab ($typeLab)$req"
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

    // A closed drop-down is one plain line of text; only the open list uses the checkable row, which draws a radio mark
    // (the closed one used it too, so a batch-edit chooser looked like one more radio under the recipes).
    private fun spinnerAdapter(items: List<String>): ArrayAdapter<String> =
        ArrayAdapter(this, android.R.layout.simple_spinner_item, items.toMutableList()).also { it.setDropDownViewResource(android.R.layout.simple_spinner_dropdown_item) }

    // "Do pakietu roboczego dołączono …" - the engine's own clause (counted noun without an adjective), as the first sentence.
    private fun packageLead(j: JSONObject): String {
        val note = j.optString("package_added_text")
        return if (note.isEmpty()) "" else note.replaceFirstChar { it.uppercase() } + ". "
    }

    private fun picker(labels: List<String>, onPick: (Int) -> Unit): Spinner = Spinner(this).apply {
        adapter = spinnerAdapter(labels)
        onItemSelectedListener = object : AdapterView.OnItemSelectedListener {
            override fun onItemSelected(p: AdapterView<*>?, v: View?, pos: Int, id: Long) = onPick(pos)
            override fun onNothingSelected(p: AdapterView<*>?) {}
        }
    }

    // ───────────── Edycje: batch edits with preview, apply, undo, redo ─────────────
    private fun editsPanel(edits: JSONArray, macros: JSONArray): View {
        val adapter = TreeAdapter(this)
        val info = label("")
        var recipe = 0
        var recipeSteps = 0
        val recipeText = label(if (macros.length() > 0) macros.getJSONObject(0).optString("description") else "", 13f)
        fun recipeReq(preview: Boolean): JSONObject = JSONObject().put("call", "macro").put("name", macros.getJSONObject(recipe).optString("id")).put("preview", preview)
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
                // A ready recipe and an undo come back with `summary_text`: one plain sentence about what changed (built by the
                // engine from the data, different for preview / apply / undo). It stands above the list; the raw result is
                // kept below, collapsed, for the details.
                val sentence = j.optString("summary_text")
                info.text = packageLead(j) + sentence.ifEmpty { "Gotowe." }
                val roots = ArrayList<TNode>()
                val patch = j.optString("patch_text")
                if (patch.isNotEmpty()) roots.add(TNode("Zmiany", Pl.count(patch.lines().size, Nouns.LINE), onOpen = { showText(this, "Zmiany", patch) }, copy = patch))
                roots.add(jsonNode(if (sentence.isEmpty()) "wynik" else "szczegóły wyniku", j, this@MainActivity).also { it.expanded = sentence.isEmpty() })
                adapter.setRoots(roots)
            }
        }
        // Every ready recipe is visible at once (no drop-down); the description of the chosen one stands under the list.
        val recipeSp = RadioGroup(this).apply {
            orientation = RadioGroup.VERTICAL
            val ids = ArrayList<Int>()
            for (i in 0 until macros.length()) {
                val rb = RadioButton(this@MainActivity).apply { id = View.generateViewId(); text = macros.getJSONObject(i).optString("label") }
                ids.add(rb.id); addView(rb)
            }
            setOnCheckedChangeListener { _, checked ->
                val i = ids.indexOf(checked)
                if (i >= 0) { recipe = i; recipeText.text = macros.getJSONObject(i).optString("description") }
            }
            if (ids.isNotEmpty()) check(ids[0])
        }
        val list = ListView(this)
        list.addHeaderView(col().apply {
            if (macros.length() > 0) {
                // The recipes are a group of their own (the only RadioGroup on this screen); the batch-edit chooser below is a
                // drop-down and is not part of it.
                addView(LinearLayout(this@MainActivity).apply {
                    orientation = LinearLayout.VERTICAL
                    addView(label("Gotowe przepisy", 16f))
                    addView(recipeSp); addView(recipeText)
                    addView(row(btn("Podgląd przepisu") { info.text = "Liczę…"; call(recipeReq(true), show) },
                        btn("Zastosuj przepis") { info.text = "Stosuję…"; call(recipeReq(false)) { j -> if (j.optBoolean("ok")) recipeSteps = j.optInt("steps"); show(j) } }))
                    addView(btn("Cofnij przepis") { call(JSONObject().put("call", "undo").put("steps", maxOf(recipeSteps, 1)), show); recipeSteps = 0 })
                })
                addView(View(this@MainActivity).apply {
                    setBackgroundColor(0x44888888)
                    layoutParams = LinearLayout.LayoutParams(LinearLayout.LayoutParams.MATCH_PARENT, 2).apply { setMargins(0, pad, 0, pad) }
                })
            }
            addView(label("Edycje seryjne (na rozmowach zaznaczonych w zakładce Wybór)", 16f))
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
                    else { info.text = packageLead(j) + "Gotowe."; adapter.setRoots(listOf(jsonNode("wynik", j, this@MainActivity).also { it.expanded = true })) }
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
        redactionSpinner.adapter = spinnerAdapter((0 until red.length()).map { red.getJSONObject(it).optString("label") })
        val holder = (panels["Eksport"] as ScrollView).findViewWithTag<LinearLayout>("profiles")
        // The boxes are rebuilt after every opened file; what was ticked stays ticked (a refill must not silently untick a profile).
        val ticked = profileChecks.filter { it.second.isChecked }.map { it.first }.toSet()
        holder.removeAllViews(); profileChecks.clear()
        val pr = cat.getJSONArray("profiles")
        for (i in 0 until pr.length()) {
            val p = pr.getJSONObject(i)
            // android.widget.CheckBox with the platform theme (the manifest sets none): it draws its own box, ticked or empty.
            val cb = CheckBox(this).apply { text = p.optString("label"); isChecked = p.optString("id") in ticked }
            profileChecks.add(p.optString("id") to cb); holder.addView(cb)
            p.optString("description").takeIf { it.isNotEmpty() }?.let { holder.addView(label(it, 12f).apply { setPadding(pad * 3, 0, pad, pad) }) }
        }
    }

    // The choices are read when the button is pressed and kept in the preferences: while the system file dialog is open the
    // activity may be recreated (boxes rebuilt, all empty), and the answer would then be cut with the default profile.
    private fun pickExportTarget() {
        val redaction = redactionIds.getOrNull(redactionSpinner.selectedItemPosition)?.takeIf { it != "none" } ?: ""
        getSharedPreferences("xport", Context.MODE_PRIVATE).edit()
            .putString("export_profiles", profileChecks.filter { it.second.isChecked }.joinToString(",") { it.first })
            .putString("export_redaction", redaction).putBoolean("export_handoff", handoffCheck.isChecked).apply()
        startActivityForResult(Intent(Intent.ACTION_CREATE_DOCUMENT).apply {
            addCategory(Intent.CATEGORY_OPENABLE); type = "application/zip"; putExtra(Intent.EXTRA_TITLE, "xport-wynik.zip")
        }, REQ_SAVE)
    }

    // What the export did, in words: which profiles, how many files, how many conversation directories (and what was done to the package).
    private fun exportReport(j: JSONObject): String {
        val parts = ArrayList<String>()
        parts.add("Zapisano zip: ${Pl.count(j.optInt("files"), Nouns.FILE)}")
        val used = j.optJSONArray("profiles_used")
        val labels = (0 until (used?.length() ?: 0)).map { used!!.getJSONObject(it).optString("label") }
        parts.add(if (labels.isEmpty()) "profil: domyślny (żaden nie zaznaczony)" else (if (labels.size == 1) "profil: " else "profile: ") + labels.joinToString(" + "))
        val convDirs = j.optInt("conversation_dirs")
        val topDirs = j.optJSONArray("top_level_dirs")?.length() ?: 0
        parts.add(when {
            convDirs > 0 -> Pl.count(convDirs, Nouns.CONVERSATION_DIR)
            topDirs > 0 -> "bez katalogów rozmów (katalogów na górze: $topDirs)"
            else -> "bez żadnego katalogu (wszystkie pliki leżą w katalogu głównym zipa)"
        })
        // The two clauses about the working package are the engine's (data/ui_labels.json#effects): the counted noun stands
        // after a colon, so no adjective has to agree with it ("… po jego utworzeniu: 5 rozmów").
        for (k in listOf("package_added_text", "package_left_out_text")) j.optString(k).takeIf { it.isNotEmpty() }?.let { parts.add(it) }
        return parts.joinToString("; ") + "."
    }

    private fun exportTo(uri: Uri) {
        exportInfo.text = "Eksportuję…"
        val prefs = getSharedPreferences("xport", Context.MODE_PRIVATE)
        val redaction = prefs.getString("export_redaction", "") ?: ""
        val profiles = prefs.getString("export_profiles", "") ?: ""
        val handoff = prefs.getBoolean("export_handoff", false)
        thread {
            val tmp = File(cacheDir, "xport-out.zip")
            val msg = try {
                val j = JSONObject(py.callAttr("export_selected", tmp.absolutePath, redaction, profiles, handoff).toString())
                if (!j.optBoolean("ok")) "Błąd eksportu: ${j.optString("error")}" else {
                    val out = contentResolver.openOutputStream(uri) ?: throw IllegalStateException("nie można zapisać pliku")
                    out.use { o -> tmp.inputStream().use { it.copyTo(o) } }
                    exportReport(j)
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
