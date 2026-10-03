package com.chatadhd.android

import android.os.Handler
import android.os.Looper
import android.util.Log
import android.webkit.WebView
import org.json.JSONObject
import java.lang.ref.WeakReference

/**
 * The one native -> JS delivery path, called from loom_jni.cpp
 * (`deliver_chunk`) for chat streaming, import progress, and event
 * subscriptions alike. It may be called from any thread — a Loom worker
 * thread as easily as the caller's own background-executor thread — so it
 * always hops to the UI thread before touching the WebView, as
 * [WebView.evaluateJavascript] requires.
 *
 * Bridge contract (documented in full in loom/android/README.md):
 *   window.__loomCallbacks[id](chunkJson, done)
 * `chunkJson` is the raw JSON *text* (a string the JS side JSON.parse()s
 * itself, matching every other LoomBridge method's "JSON string in, JSON
 * string out" convention); `done` is a boolean. `chunkJson` is embedded as a
 * JS string literal via [JSONObject.quote], which escapes exactly what needs
 * escaping for a JS/JSON string (quotes, backslashes, control characters,
 * U+2028/U+2029) — never string-concatenated raw.
 */
object LoomCallbacks {
    private const val TAG = "LoomCallbacks"
    private var webViewRef: WeakReference<WebView>? = null
    private val mainHandler = Handler(Looper.getMainLooper())

    /** Called once by MainActivity after the WebView is created. */
    @JvmStatic
    fun attach(webView: WebView) {
        webViewRef = WeakReference(webView)
    }

    @JvmStatic
    fun detach() {
        webViewRef = null
    }

    /** Called from native code (loom_jni.cpp: deliver_chunk). */
    @JvmStatic
    fun onChunk(callbackId: String, chunkJson: String, done: Boolean) {
        val js = buildString {
            append("(function(){var cb=window.__loomCallbacks&&window.__loomCallbacks[")
            append(JSONObject.quote(callbackId))
            append("];if(cb)cb(")
            append(JSONObject.quote(chunkJson))
            append(",")
            append(done)
            append(");})();")
        }
        mainHandler.post {
            val webView = webViewRef?.get()
            if (webView == null) {
                Log.w(TAG, "dropped chunk for $callbackId: no WebView attached")
                return@post
            }
            webView.evaluateJavascript(js, null)
        }
    }
}
