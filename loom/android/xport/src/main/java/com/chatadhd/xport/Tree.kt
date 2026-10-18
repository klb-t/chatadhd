package com.chatadhd.xport

import android.app.Activity
import android.app.AlertDialog
import android.content.ClipData
import android.content.ClipboardManager
import android.content.Context
import android.graphics.Color
import android.text.SpannableStringBuilder
import android.text.Spanned
import android.text.style.ForegroundColorSpan
import android.text.style.RelativeSizeSpan
import android.view.View
import android.view.ViewGroup
import android.widget.BaseAdapter
import android.widget.ListView
import android.widget.ScrollView
import android.widget.TextView
import android.widget.Toast
import org.json.JSONArray
import org.json.JSONObject

// A collapsible, lazily filled tree shown in a (virtualised) ListView: nothing is capped, long lists cost only what is
// visible. Tap = expand/collapse (or open a leaf); long-press = context menu of the node. Used for every list in the app.
class TNode(
    val label: String,
    val detail: String = "",
    val id: String? = null,                                   // engine node id, when the node is an engine object
    var children: MutableList<TNode>? = null,
    val loader: ((TNode, (List<TNode>) -> Unit) -> Unit)? = null,   // fills children on first expand
    val onOpen: (() -> Unit)? = null,                         // what a tap does on a leaf (or "Otwórz" in the menu)
    val actions: () -> List<Pair<String, () -> Unit>> = { emptyList() },
    val copy: String? = null,                                 // text for "Kopiuj" (default: label + detail)
) {
    var expanded = false
    var loading = false
    var depth = 0
    val expandable get() = children != null || loader != null
    val copyText get() = copy ?: if (detail.isEmpty()) label else "$label\n$detail"
}

class TreeAdapter(private val act: Activity) : BaseAdapter() {
    private val roots = ArrayList<TNode>()
    private val visible = ArrayList<TNode>()
    private val dp = act.resources.displayMetrics.density

    fun setRoots(r: List<TNode>) { roots.clear(); roots.addAll(r); rebuild() }

    fun rebuild() {
        visible.clear()
        fun walk(n: TNode, d: Int) {
            n.depth = d; visible.add(n)
            if (n.expanded) n.children?.forEach { walk(it, d + 1) }
        }
        roots.forEach { walk(it, 0) }
        notifyDataSetChanged()
    }

    fun toggle(n: TNode) {
        if (!n.expandable) { n.onOpen?.invoke(); return }
        if (n.expanded) { n.expanded = false; rebuild(); return }
        if (n.children == null && n.loader != null) {
            if (n.loading) return
            n.loading = true; notifyDataSetChanged()
            n.loader.invoke(n) { kids -> n.children = kids.toMutableList(); n.loading = false; n.expanded = true; rebuild() }
        } else { n.expanded = true; rebuild() }
    }

    fun expandAll(n: TNode) {
        n.children?.let { n.expanded = true; it.forEach { c -> if (c.children != null) expandAll(c) } }
        rebuild()
    }

    override fun getCount() = visible.size
    override fun getItem(position: Int) = visible[position]
    override fun getItemId(position: Int) = position.toLong()

    override fun getView(position: Int, convertView: View?, parent: ViewGroup?): View {
        val n = visible[position]
        val tv = (convertView as? TextView) ?: TextView(act).apply {
            minHeight = (48 * dp).toInt(); textSize = 14f; gravity = android.view.Gravity.CENTER_VERTICAL
        }
        tv.setPadding(((12 + 18 * n.depth) * dp).toInt(), (6 * dp).toInt(), (8 * dp).toInt(), (6 * dp).toInt())
        val mark = when { n.loading -> "… "; n.expandable -> if (n.expanded) "▾ " else "▸ "; else -> "• " }
        val sb = SpannableStringBuilder(mark + n.label)
        if (n.detail.isNotEmpty()) {
            val start = sb.length
            sb.append("\n").append(n.detail)
            sb.setSpan(RelativeSizeSpan(0.85f), start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
            sb.setSpan(ForegroundColorSpan(Color.GRAY), start, sb.length, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
        }
        tv.text = sb
        return tv
    }

    // Standard menu of every node: its own actions, then open / expand all / copy.
    fun attach(list: ListView) {
        list.adapter = this
        list.setOnItemClickListener { _, _, pos, _ -> toggle(visible[pos]) }
        list.setOnItemLongClickListener { _, _, pos, _ -> menu(visible[pos]); true }
    }

    fun menu(n: TNode) {
        val items = ArrayList<Pair<String, () -> Unit>>()
        n.onOpen?.let { items.add("Otwórz" to it) }
        items.addAll(n.actions())
        if (n.children != null) items.add((if (n.expanded) "Zwiń" else "Rozwiń") to { toggle(n) })
        if (n.children != null) items.add("Rozwiń wszystko poniżej" to { expandAll(n) })
        items.add("Kopiuj tekst" to { copyToClipboard(act, n.copyText) })
        AlertDialog.Builder(act).setTitle(n.label.take(80))
            .setItems(items.map { it.first }.toTypedArray()) { _, i -> items[i].second() }.show()
    }
}

fun copyToClipboard(ctx: Context, text: String) {
    (ctx.getSystemService(Context.CLIPBOARD_SERVICE) as ClipboardManager).setPrimaryClip(ClipData.newPlainText("X-Port", text))
    Toast.makeText(ctx, "Skopiowano", Toast.LENGTH_SHORT).show()
}

fun showText(act: Activity, title: String, text: String) {
    val tv = TextView(act).apply {
        this.text = text; setTextIsSelectable(true); val p = (16 * act.resources.displayMetrics.density).toInt(); setPadding(p, p, p, p)
    }
    AlertDialog.Builder(act).setTitle(title.take(80)).setView(ScrollView(act).apply { addView(tv) })
        .setPositiveButton("Zamknij", null).setNeutralButton("Kopiuj") { _, _ -> copyToClipboard(act, text) }.show()
}

// JSON as a collapsible tree: any engine result becomes browsable, every key and value clickable and copyable.
fun jsonNode(key: String, v: Any?, act: Activity): TNode {
    val full = when (v) { is JSONObject -> v.toString(1); is JSONArray -> v.toString(1); else -> v?.toString() ?: "null" }
    val acts = { listOf<Pair<String, () -> Unit>>("Kopiuj klucz" to { copyToClipboard(act, key) }, "Kopiuj wartość" to { copyToClipboard(act, full) }) }
    return when (v) {
        is JSONObject -> TNode("$key {${v.length()}}", children = v.keys().asSequence().map { jsonNode(it, v.opt(it), act) }.toMutableList(), actions = acts, copy = full)
        is JSONArray -> TNode("$key [${v.length()}]", children = (0 until v.length()).map { jsonNode("#$it", v.opt(it), act) }.toMutableList(), actions = acts, copy = full)
        else -> TNode("$key: ${full.take(300)}", actions = acts, copy = full, onOpen = { showText(act, key, full) })
    }
}
