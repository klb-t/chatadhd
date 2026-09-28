package com.chatadhd.android

import android.webkit.JavascriptInterface
import android.webkit.WebView
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.ExecutorService
import java.util.concurrent.Executors
import java.util.concurrent.ConcurrentHashMap
import java.util.UUID

/**
 * The object exposed to the web UI as `window.LoomBridge`
 * (`webView.addJavascriptInterface(bridge, "LoomBridge")` in MainActivity).
 * Every method takes and returns JSON strings, matching src/api/loom-jni.ts's
 * expectations; the full contract is documented in loom/android/README.md.
 *
 * Two shapes of method here:
 *  - Fast ones (config, conversations, messages, memory, graph, crypto, ...)
 *    call straight into [LoomNative.nativeInvoke] and return synchronously.
 *    The WebView already runs JavascriptInterface calls off the UI thread,
 *    and these are local SQLite/JSON operations, so blocking briefly here is
 *    fine and keeps the JS side simple (no callback plumbing for a
 *    millisecond-scale call).
 *  - Long-running or networked ones (chat, import, export, model refresh,
 *    graph reindex, transcribe/OCR, GitHub sync) return immediately and run
 *    on [executor], delivering their result through
 *    [LoomCallbacks.onChunk](callbackId, json, done) — see that class for
 *    the exact `window.__loomCallbacks[id](chunkJson, done)` contract. `chat`
 *    and `importFile` stream multiple chunks (native calls back into
 *    [LoomCallbacks] directly, once per token / once per progress tick);
 *    the rest deliver exactly one chunk with done = true.
 */
class LoomBridge(webView: WebView) {
    private val executor: ExecutorService = Executors.newFixedThreadPool(4)
    private val cancellations = ConcurrentHashMap<String, () -> Unit>()

    init {
        LoomCallbacks.attach(webView)
    }

    /** Called from MainActivity.onDestroy(). */
    fun shutdown() {
        executor.shutdown()
        LoomCallbacks.detach()
        LoomNative.nativeShutdown()
    }

    // ── lifecycle ──────────────────────────────────────────────────────
    @JavascriptInterface
    fun init(dataDir: String?, optionsJson: String?): String = LoomNative.nativeInit(dataDir, optionsJson)

    @JavascriptInterface
    fun version(): String = LoomNative.nativeVersion()

    @JavascriptInterface
    fun info(): String = invokeArgs("info")

    /** Escape hatch: any loom.h call this class has no named wrapper for yet. */
    @JavascriptInterface
    fun invoke(method: String, argsJson: String): String = LoomNative.nativeInvoke(method, argsJson)

    /** Web client's canonical named-argument protocol. Native also accepts
     * the positional arrays used by the older named Kotlin wrappers. */
    @JavascriptInterface
    fun call(method: String, argsJson: String): String = LoomNative.nativeInvoke(method, argsJson)

    @JavascriptInterface
    fun startStream(method: String, argsJson: String, callbackId: String) {
        try {
            val args = JSONObject(argsJson)
            when (method) {
                "subscribe" -> {
                    val token = LoomNative.nativeSubscribe(args.getString("event"), callbackId)
                    if (token < 0) LoomCallbacks.onChunk(callbackId, errorJson(token.toInt()), true)
                    else cancellations[callbackId] = { LoomNative.nativeUnsubscribe(token); Unit }
                }
                "chat_ex" -> {
                    val request = args.getJSONObject("request")
                    if (!request.has("request_id")) request.put("request_id", "jni_" + UUID.randomUUID())
                    val requestId = request.getString("request_id")
                    cancellations[callbackId] = { invokeArgs("chat_cancel", requestId); Unit }
                    executor.execute {
                        try { LoomNative.nativeChat(request.toString(), callbackId) }
                        finally { cancellations.remove(callbackId) }
                    }
                }
                "import_file" -> executor.execute {
                    LoomNative.nativeImportFile(args.getString("path"), args.optString("title").ifEmpty { null }, callbackId)
                }
                else -> {
                    if (method == "knowledge_run")
                        cancellations[callbackId] = { invokeArgs("knowledge_cancel"); Unit }
                    runAsync(callbackId) {
                        try { call(method, argsJson) }
                        finally { cancellations.remove(callbackId) }
                    }
                }
            }
        } catch (e: Exception) {
            LoomCallbacks.onChunk(callbackId, JSONObject().put("error", JSONObject()
                .put("code", "invalid_argument").put("message", e.message ?: "Invalid bridge request")).toString(), true)
        }
    }

    @JavascriptInterface
    fun cancelStream(callbackId: String) {
        cancellations.remove(callbackId)?.invoke()
    }

    // ── config & secrets ──────────────────────────────────────────────
    @JavascriptInterface fun getConfig(): String = invokeArgs("get_config")
    @JavascriptInterface fun setConfig(key: String, value: String): String = invokeArgs("set_config", key, value)
    @JavascriptInterface fun setConfigJson(patchJson: String): String = invokeArgs("set_config_json", patchJson)
    @JavascriptInterface fun setSecret(key: String, value: String): String = invokeArgs("set_secret", key, value)
    @JavascriptInterface fun hasSecret(key: String): String = invokeArgs("has_secret", key)
    @JavascriptInterface fun deleteSecret(key: String): String = invokeArgs("delete_secret", key)
    @JavascriptInterface fun listSecretKeys(): String = invokeArgs("list_secret_keys")

    // ── conversations & messages ──────────────────────────────────────
    @JavascriptInterface fun listConversations(limit: Int): String = invokeArgs("list_conversations", limit)
    @JavascriptInterface fun createConversation(title: String?): String = invokeArgs("create_conversation", title)
    @JavascriptInterface fun getConversation(convId: String): String = invokeArgs("get_conversation", convId)
    @JavascriptInterface fun updateConversation(convId: String, patchJson: String): String =
        invokeArgs("update_conversation", convId, patchJson)
    @JavascriptInterface fun deleteConversation(convId: String): String = invokeArgs("delete_conversation", convId)
    @JavascriptInterface fun getMessages(convId: String): String = invokeArgs("get_messages", convId)
    @JavascriptInterface fun getMessagesEx(convId: String, includeAll: Boolean): String =
        invokeArgs("get_messages_ex", convId, if (includeAll) 1 else 0)
    @JavascriptInterface fun getMessage(msgId: String): String = invokeArgs("get_message", msgId)
    @JavascriptInterface fun editMessage(msgId: String, newText: String): String = invokeArgs("edit_message", msgId, newText)
    @JavascriptInterface fun restoreVersion(msgId: String): String = invokeArgs("restore_version", msgId)
    @JavascriptInterface fun getVersions(msgOrGroupId: String): String = invokeArgs("get_versions", msgOrGroupId)
    @JavascriptInterface fun setMessageStatus(msgId: String, status: String): String =
        invokeArgs("set_message_status", msgId, status)
    @JavascriptInterface fun updateMessage(msgId: String, patchJson: String): String =
        invokeArgs("update_message", msgId, patchJson)
    @JavascriptInterface fun search(query: String, optionsJson: String?): String = invokeArgs("search", query, optionsJson)

    // ── chat ───────────────────────────────────────────────────────────
    /** Returns {"accepted":true} immediately; the exchange streams through [LoomCallbacks]. */
    @JavascriptInterface
    fun chat(requestJson: String, callbackId: String): String {
        executor.execute { LoomNative.nativeChat(requestJson, callbackId) }
        return accepted()
    }

    @JavascriptInterface
    fun chatCancel(requestId: String): String = invokeArgs("chat_cancel", requestId)

    // ── models & providers ────────────────────────────────────────────
    @JavascriptInterface fun getModels(): String = invokeArgs("get_models")
    @JavascriptInterface fun refreshModels(callbackId: String): String = runAsync(callbackId) { invokeArgs("refresh_models") }
    @JavascriptInterface fun getProviders(): String = invokeArgs("get_providers")
    @JavascriptInterface fun can(resource: String, capability: String, constraintsJson: String?): String =
        invokeArgs("can", resource, capability, constraintsJson)

    // ── knowledge graph & context ─────────────────────────────────────
    @JavascriptInterface fun getNodes(filterJson: String?): String = invokeArgs("get_nodes", filterJson)
    @JavascriptInterface fun getEdges(filterJson: String?): String = invokeArgs("get_edges", filterJson)
    @JavascriptInterface fun expandGraph(seedIdsJson: String, depth: Int): String =
        invokeArgs("expand_graph", seedIdsJson, depth)
    @JavascriptInterface fun getGraphData(convId: String?): String = invokeArgs("get_graph_data", convId)
    @JavascriptInterface fun graphReindex(convId: String?, callbackId: String): String =
        runAsync(callbackId) { invokeArgs("graph_reindex", convId) }
    @JavascriptInterface fun selectContext(text: String, depth: Int, maxTokens: Int): String =
        invokeArgs("select_context", text, depth, maxTokens)
    @JavascriptInterface fun selectContextEx(requestJson: String): String = invokeArgs("select_context_ex", requestJson)

    // ── semantic worker ────────────────────────────────────────────────
    @JavascriptInterface fun semanticStatus(): String = invokeArgs("semantic_status")
    @JavascriptInterface fun semanticPause(): String = invokeArgs("semantic_pause")
    @JavascriptInterface fun semanticResume(): String = invokeArgs("semantic_resume")
    @JavascriptInterface fun semanticWake(): String = invokeArgs("semantic_wake")

    // ── memory tree ────────────────────────────────────────────────────
    @JavascriptInterface fun listMemory(): String = invokeArgs("list_memory")
    @JavascriptInterface fun createMemory(json: String): String = invokeArgs("create_memory", json)
    @JavascriptInterface fun updateMemory(nodeId: String, json: String): String = invokeArgs("update_memory", nodeId, json)
    @JavascriptInterface fun deleteMemory(nodeId: String): String = invokeArgs("delete_memory", nodeId)
    @JavascriptInterface fun getMemoryContext(maxChars: Int): String = invokeArgs("get_memory_context", maxChars)

    // ── import / export ───────────────────────────────────────────────
    @JavascriptInterface fun detectFormat(path: String): String = invokeArgs("detect_format", path)

    /** Returns {"accepted":true} immediately; progress + result stream through [LoomCallbacks]. */
    @JavascriptInterface
    fun importFile(path: String, title: String?, callbackId: String): String {
        executor.execute { LoomNative.nativeImportFile(path, title, callbackId) }
        return accepted()
    }

    @JavascriptInterface
    fun exportConversation(convId: String, fmt: String, callbackId: String): String =
        runAsync(callbackId) { invokeArgs("export_conversation", convId, fmt) }

    // ── provenance, events & tasks ────────────────────────────────────
    @JavascriptInterface fun listSources(limit: Int): String = invokeArgs("list_sources", limit)
    @JavascriptInterface fun getProvenance(subjectId: String): String = invokeArgs("get_provenance", subjectId)
    @JavascriptInterface fun queryEvents(queryJson: String?): String = invokeArgs("query_events", queryJson)
    @JavascriptInterface fun listTasks(filterJson: String?): String = invokeArgs("list_tasks", filterJson)
    @JavascriptInterface fun getTask(taskId: String): String = invokeArgs("get_task", taskId)
    @JavascriptInterface fun resumeTasks(): String = invokeArgs("resume_tasks")
    @JavascriptInterface fun cancelTask(taskId: String): String = invokeArgs("cancel_task", taskId)

    // ── encryption ─────────────────────────────────────────────────────
    @JavascriptInterface fun cryptoStatus(): String = invokeArgs("crypto_status")
    @JavascriptInterface fun cryptoSetup(password: String): String = invokeArgs("crypto_setup", password)
    @JavascriptInterface fun cryptoUnlock(password: String): String = invokeArgs("crypto_unlock", password)
    @JavascriptInterface fun cryptoLock(): String = invokeArgs("crypto_lock")
    @JavascriptInterface fun cryptoEncrypt(plaintext: String): String = invokeArgs("crypto_encrypt", plaintext)
    @JavascriptInterface fun cryptoDecrypt(blobJson: String): String = invokeArgs("crypto_decrypt", blobJson)

    // ── media (ASR / OCR) ──────────────────────────────────────────────
    @JavascriptInterface
    fun transcribe(audioPath: String, optionsJson: String?, callbackId: String): String =
        runAsync(callbackId) { invokeArgs("transcribe", audioPath, optionsJson) }

    @JavascriptInterface
    fun ocr(imagePath: String, optionsJson: String?, callbackId: String): String =
        runAsync(callbackId) { invokeArgs("ocr", imagePath, optionsJson) }

    @JavascriptInterface fun mediaStatus(): String = invokeArgs("media_status")

    // ── GitHub sync ────────────────────────────────────────────────────
    @JavascriptInterface
    fun githubSync(requestJson: String, callbackId: String): String = runAsync(callbackId) { invokeArgs("github_sync", requestJson) }

    // ── events ─────────────────────────────────────────────────────────
    /** Delivers {"event":name,"payload":...} through [LoomCallbacks] until [unsubscribe]. */
    @JavascriptInterface
    fun subscribe(event: String, callbackId: String): String {
        val token = LoomNative.nativeSubscribe(event, callbackId)
        return if (token > 0) JSONObject().put("token", token).toString() else errorJson(token.toInt())
    }

    @JavascriptInterface
    fun unsubscribe(token: String): String {
        val t = token.toLongOrNull() ?: return errorJson(-1)
        return wrapInt(LoomNative.nativeUnsubscribe(t))
    }

    // ── helpers ────────────────────────────────────────────────────────
    private fun invokeArgs(method: String, vararg args: Any?): String = LoomNative.nativeInvoke(method, jsonArgs(*args))

    private fun jsonArgs(vararg args: Any?): String {
        val arr = JSONArray()
        for (a in args) arr.put(a)
        return arr.toString()
    }

    private fun accepted(): String = JSONObject().put("accepted", true).toString()

    /** Runs [block] on [executor], delivers its result through [LoomCallbacks] as one chunk (done = true). */
    private inline fun runAsync(callbackId: String, crossinline block: () -> String): String {
        executor.execute {
            val result = try {
                block()
            } catch (e: Exception) {
                JSONObject().put("error", JSONObject().put("code", "internal").put("message", e.message ?: "")).toString()
            }
            LoomCallbacks.onChunk(callbackId, result, true)
        }
        return accepted()
    }

    private fun errorJson(code: Int): String =
        JSONObject().put("error", JSONObject().put("code", "invalid_argument").put("message", "rc=$code")).toString()

    private fun wrapInt(rc: Int): String = if (rc < 0) errorJson(rc) else JSONObject().put("ok", true).toString()
}
