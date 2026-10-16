package com.chatadhd.android

import android.net.Uri
import android.provider.DocumentsContract
import android.provider.OpenableColumns
import android.util.Log
import androidx.activity.ComponentActivity
import androidx.activity.result.contract.ActivityResultContracts
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.io.IOException
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors

/**
 * Importing conversations on the phone: the system picker (Storage Access Framework) -> a private copy -> the kernel.
 *
 * Why this exists: the importer opens a path (`loom_import_file`), while the picker gives a content:// URI that only a
 * ContentResolver can read, and a `<input type=file>` of the WebView gives the page a File whose bytes the page would
 * have to push through the string bridge. This flow does it natively and streams: the picked document(s) are copied
 * (fixed buffer, any size) into `cacheDir/imports/<run>/`, each copy is imported with [LoomNative.nativeImportFile]
 * and removed at once, and the page gets ONE stream under its callback id:
 *
 *   {"type":"progress","current","total","status"}   while copying and while the kernel imports
 *   {"type":"done", ...kernel result..., "files":[...], "imported", "failed", "messages", "conversations", "warnings"}
 *   {"error":{"code","message"}}                      when nothing could be imported (or nothing was chosen)
 *
 * Page API (LoomBridge.startStream): `pick_import` with {"kind":"file"|"folder","title"?,"all"?}. "file" lets the user
 * choose one or more documents (ZIP / JSON of Claude.ai and ChatGPT, HTML, Markdown, text, SQLite, screenshots...),
 * "folder" a directory tree whose importable files are all imported (all=true tries every file, the kernel decides).
 *
 * Must be constructed while the activity is being created (it registers the picker contracts).
 */
class ImportFlow(private val activity: ComponentActivity) {
    private class Request(val kind: String, val title: String?, val all: Boolean, val callbackId: String) {
        @Volatile var cancelled = false
    }

    private class Item(val uri: Uri, val rel: String, val size: Long, val mime: String)

    private val executor: ExecutorService = Executors.newSingleThreadExecutor()
    private val running = ConcurrentHashMap<String, Request>()
    @Volatile private var pending: Request? = null

    private val pickFiles =
        activity.registerForActivityResult(ActivityResultContracts.OpenMultipleDocuments()) { uris ->
            onPicked(uris, false)
        }
    private val pickTree =
        activity.registerForActivityResult(ActivityResultContracts.OpenDocumentTree()) { uri ->
            onPicked(if (uri == null) emptyList() else listOf(uri), true)
        }

    /** Opens the system picker; the result streams to [callbackId]. Any thread. */
    fun start(kind: String, title: String?, all: Boolean, callbackId: String) {
        val request = Request(kind, title, all, callbackId)
        val replaced = pending
        pending = request
        if (replaced != null) fail(replaced.callbackId, "cancelled", "Another import was started")
        activity.runOnUiThread {
            try {
                if (kind == "folder") pickTree.launch(null) else pickFiles.launch(arrayOf("*/*"))
            } catch (e: Exception) {
                Log.w(TAG, "the file picker could not be opened", e)
                if (pending === request) pending = null
                fail(callbackId, "picker_unavailable", "The system file picker could not be opened: " + (e.message ?: e.javaClass.simpleName))
            }
        }
    }

    /** A document the caller already has a content:// URI of (no picker): copy, import, stream to [callbackId]. Any thread. */
    fun startUri(uri: String, title: String?, callbackId: String) {
        val request = Request("file", title, false, callbackId)
        val parsed = try { Uri.parse(uri) } catch (e: Exception) { null }
        if (parsed == null) {
            fail(callbackId, "invalid_argument", "Not a valid document address: $uri")
            return
        }
        running[callbackId] = request
        try {
            executor.execute {
                try {
                    run(request, listOf(parsed), false)
                } catch (e: Throwable) {
                    Log.e(TAG, "import failed", e)
                    fail(callbackId, "internal", e.message ?: e.javaClass.simpleName)
                } finally {
                    running.remove(callbackId)
                }
            }
        } catch (e: Exception) {
            running.remove(callbackId)
            fail(callbackId, "internal", e.message ?: "The import could not be started")
        }
    }

    /** The page gave up (cancelStream): a picker not answered yet, or a copy in progress, stops. */
    fun cancel(callbackId: String) {
        val waiting = pending
        if (waiting != null && waiting.callbackId == callbackId) {
            pending = null
            fail(callbackId, "cancelled", "Import cancelled")
        }
        running[callbackId]?.cancelled = true
    }

    fun shutdown() {
        executor.shutdownNow()
    }

    private fun onPicked(uris: List<Uri>, tree: Boolean) {
        val request = pending ?: return
        pending = null
        if (uris.isEmpty()) {
            fail(request.callbackId, "cancelled", "Nothing was chosen")
            return
        }
        running[request.callbackId] = request
        try {
            executor.execute {
                try {
                    run(request, uris, tree)
                } catch (e: Throwable) {
                    Log.e(TAG, "import failed", e)
                    fail(request.callbackId, "internal", e.message ?: e.javaClass.simpleName)
                } finally {
                    running.remove(request.callbackId)
                }
            }
        } catch (e: Exception) {
            running.remove(request.callbackId)
            fail(request.callbackId, "internal", e.message ?: "The import could not be started")
        }
    }

    // ── the work (background thread) ─────────────────────────────────

    private fun run(request: Request, uris: List<Uri>, tree: Boolean) {
        val root = activity.cacheDir
        ImportStaging.sweep(root, 6L * 3600 * 1000)
        progress(request.callbackId, 0, 0, "Reading the list of files")
        val items = ArrayList<Item>()
        for (uri in uris) {
            if (tree) walkTree(uri, request.all, items) else items.add(describe(uri))
        }
        if (tree) items.sortBy { it.rel }
        if (items.isEmpty()) {
            fail(request.callbackId, "no_files",
                "The folder has no files the importer knows (zip, json, jsonl, html, mht, md, txt, db, png, jpg, webp)")
            return
        }
        val dir = ImportStaging.reserveDir(root)
        val files = JSONArray()
        val conversations = JSONArray()
        val warnings = JSONArray()
        var messages = 0L
        var imported = 0
        var failed = 0
        var only: JSONObject? = null
        val formats = LinkedHashSet<String>()
        var firstError: String? = null
        try {
            for ((index, item) in items.withIndex()) {
                if (request.cancelled) break
                val label = "${index + 1}/${items.size} ${item.rel}"
                val entry = JSONObject().put("name", item.rel)
                var copy: File? = null
                try {
                    val rel = if (tree) item.rel
                    else ImportStaging.withExtensionForMime(ImportStaging.safeName(item.rel, "import"), item.mime)
                    val dest = ImportStaging.destFor(dir, rel)
                    copy = dest
                    val input = activity.contentResolver.openInputStream(item.uri)
                        ?: throw IOException("the provider returned no data for $label")
                    input.use {
                        ImportStaging.copy(it, dest, item.size, ImportStaging.Progress { done, total ->
                            progress(request.callbackId, done, total, "Copying $label")
                            !request.cancelled
                        })
                    }
                    val result = importStaged(request, dest, label, if (items.size == 1) request.title else null)
                    val err = result.optJSONObject("error")
                    if (err != null) throw IOException(err.optString("message", "import failed"))
                    imported++
                    messages += result.optLong("messages", 0)
                    result.optString("format").takeIf { it.isNotEmpty() }?.let { formats.add(it) }
                    result.optJSONArray("conversations")?.let { for (i in 0 until it.length()) conversations.put(it.get(i)) }
                    result.optJSONArray("warnings")?.let { for (i in 0 until it.length()) warnings.put(it.get(i)) }
                    entry.put("ok", true).put("messages", result.optLong("messages", 0)).put("format", result.optString("format"))
                    if (items.size == 1) only = result
                } catch (e: ImportStaging.CancelledException) {
                    break
                } catch (e: Throwable) {
                    failed++
                    val message = e.message ?: e.javaClass.simpleName
                    Log.w(TAG, "import of ${item.rel} failed", e)
                    if (firstError == null) firstError = if (items.size == 1) message else "${item.rel}: $message"
                    entry.put("ok", false).put("error", message)
                    warnings.put("${item.rel}: $message")
                } finally {
                    // the copy is only a means: remove it at once (a big archive twice on the phone is not wanted)
                    copy?.let { c -> c.delete(); File(c.path + ".part").delete() }
                }
                files.put(entry)
            }
        } finally {
            ImportStaging.deleteTree(dir)
        }
        if (request.cancelled) {
            fail(request.callbackId, "cancelled", "Import cancelled")
            return
        }
        if (imported == 0) {
            val message = if (items.size == 1) firstError ?: "Import failed"
            else "None of the ${items.size} files could be imported. First error: ${firstError ?: "unknown"}"
            fail(request.callbackId, "import_failed", message)
            return
        }
        val done = JSONObject()
        val single = only
        if (single != null) for (key in single.keys()) done.put(key, single.get(key))
        done.put("type", "done")
        done.put("messages", messages)
        done.put("conversations", conversations)
        done.put("format", formats.joinToString(","))
        done.put("warnings", warnings)
        done.put("files", files)
        done.put("imported", imported)
        done.put("failed", failed)
        LoomCallbacks.onChunk(request.callbackId, done.toString(), true)
    }

    /** One staged file through the kernel; the kernel's progress is forwarded to the page, its result is returned. */
    private fun importStaged(request: Request, file: File, label: String, title: String?): JSONObject {
        val inner = "imp_" + UUID.randomUUID()
        LoomCallbacks.intercept(inner) { chunkJson, done ->
            if (!done) {
                try {
                    val p = JSONObject(chunkJson)
                    if (p.optString("type") == "progress") {
                        progress(request.callbackId, p.optLong("current"), p.optLong("total"), "$label: ${p.optString("status")}")
                    }
                } catch (e: Exception) {
                    Log.d(TAG, "progress chunk ignored", e)
                }
            }
        }
        val raw = try {
            LoomNative.nativeImportFile(file.absolutePath, title, inner)
        } finally {
            LoomCallbacks.release(inner)
        }
        return try {
            JSONObject(raw)
        } catch (e: Exception) {
            throw IOException("the importer answered something that is not JSON")
        }
    }

    // ── what was picked ─────────────────────────────────────────────

    private fun describe(uri: Uri): Item {
        var name = uri.lastPathSegment ?: "import"
        var size = -1L
        try {
            activity.contentResolver.query(uri, arrayOf(OpenableColumns.DISPLAY_NAME, OpenableColumns.SIZE), null, null, null)?.use { c ->
                if (c.moveToFirst()) {
                    val n = c.getColumnIndex(OpenableColumns.DISPLAY_NAME)
                    if (n >= 0 && !c.isNull(n)) name = c.getString(n)
                    val s = c.getColumnIndex(OpenableColumns.SIZE)
                    if (s >= 0 && !c.isNull(s)) size = c.getLong(s)
                }
            }
        } catch (e: Exception) {
            Log.d(TAG, "no metadata for $uri", e)
        }
        val mime = try { activity.contentResolver.getType(uri) ?: "" } catch (e: Exception) { "" }
        return Item(uri, name, size, mime)
    }

    /** Every file of a picked tree (depth first); without [all] only names the importer knows. */
    private fun walkTree(tree: Uri, all: Boolean, out: MutableList<Item>) {
        val resolver = activity.contentResolver
        val stack = ArrayList<Pair<String, String>>()
        stack.add(Pair(DocumentsContract.getTreeDocumentId(tree), ""))
        val columns = arrayOf(
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE,
            DocumentsContract.Document.COLUMN_SIZE,
        )
        while (stack.isNotEmpty()) {
            val (docId, relDir) = stack.removeAt(stack.size - 1)
            val children = DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId)
            try {
                resolver.query(children, columns, null, null, null)?.use { c ->
                    while (c.moveToNext()) {
                        val id = c.getString(0) ?: continue
                        val name = if (c.isNull(1)) id else c.getString(1)
                        val mime = if (c.isNull(2)) "" else c.getString(2)
                        val size = if (c.isNull(3)) -1L else c.getLong(3)
                        val rel = if (relDir.isEmpty()) name else "$relDir/$name"
                        if (mime == DocumentsContract.Document.MIME_TYPE_DIR) stack.add(Pair(id, rel))
                        else if (all || ImportStaging.looksImportable(name))
                            out.add(Item(DocumentsContract.buildDocumentUriUsingTree(tree, id), rel, size, mime))
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "folder $relDir could not be listed", e)
            }
        }
    }

    // ── chunks ──────────────────────────────────────────────────────

    private fun progress(callbackId: String, current: Long, total: Long, status: String) {
        val chunk = JSONObject().put("type", "progress").put("current", current).put("total", if (total < 0) 0 else total).put("status", status)
        LoomCallbacks.onChunk(callbackId, chunk.toString(), false)
    }

    private fun fail(callbackId: String, code: String, message: String) {
        val chunk = JSONObject().put("error", JSONObject().put("code", code).put("message", message))
        LoomCallbacks.onChunk(callbackId, chunk.toString(), true)
    }

    private companion object {
        const val TAG = "ImportFlow"
    }
}
