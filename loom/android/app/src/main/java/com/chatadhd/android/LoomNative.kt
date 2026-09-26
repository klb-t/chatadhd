package com.chatadhd.android

/**
 * Raw JNI surface onto libloom_jni.so (app/src/main/cpp/loom_jni.cpp), which
 * in turn wraps the Loom C ABI (loom/include/loom/loom.h). Nothing here talks
 * to the WebView directly — that's [LoomBridge]'s job. This object only:
 *
 *  - loads the native library once,
 *  - exposes [nativeInvoke] as one generic, synchronous "JSON method name +
 *    JSON args array -> JSON result" call for every loom.h function that
 *    completes quickly (DB reads/writes, config, memory tree, graph
 *    queries, crypto, ...). The method names are the loom.h function names
 *    with the "loom_" prefix dropped (e.g. "get_conversation"); the mapping
 *    lives in loom_jni.cpp's dispatch table, one entry per name.
 *  - exposes three calls that need more than one native->Java hop:
 *    [nativeChat] (token/reasoning/done chunks), [nativeImportFile]
 *    (progress + final result) and [nativeSubscribe] (an open-ended event
 *    stream). All three deliver through [LoomCallbacks.onChunk].
 *  - exposes the four `nativeHttpResponse*` calls [LoomHttp] uses to feed a
 *    platform HTTP response back into `loom_http_response_*`.
 *
 * All JSON crossing this boundary is real UTF-8 (native re-encodes through
 * UTF-16 by hand rather than JNI's modified-UTF-8 NewStringUTF/
 * GetStringUTFChars, which mishandle astral code points such as most emoji;
 * see the comment at the top of loom_jni.cpp).
 */
internal object LoomNative {
    init {
        System.loadLibrary("loom_jni")
    }

    /**
     * Starts the (process-wide, single) Loom context. `dataDir` NULL/empty
     * resolves like the Python app (CHATADHD_DATA, then the sentinel file,
     * then the platform default). `optionsJson` is the loom_init_ex options
     * object; "data_dir" in it is overridden by a non-empty `dataDir` arg.
     * Returns {"ok":true,"info":{...}} or {"ok":false,"error":{...}}.
     * Safe to call again after [nativeShutdown]; a second call while already
     * initialized just returns the current info.
     */
    @JvmStatic external fun nativeInit(dataDir: String?, optionsJson: String?): String

    /** Stops workers and closes the database. Safe to call when not initialized. */
    @JvmStatic external fun nativeShutdown()

    /** {"version","abi","sqlite","fts5","openssl","unicode"} — no ctx needed. */
    @JvmStatic external fun nativeVersion(): String

    /**
     * The generic synchronous dispatcher. `argsJson` is a JSON array of the
     * method's positional arguments in loom.h's own order (a missing
     * trailing argument is treated as the ABI's default/NULL). Always
     * returns JSON: either the call's normal result, or
     * {"error":{"code","message"}} — including "not_implemented" for an
     * unknown method name, which means this Kotlin/native pair is out of
     * sync with loom.h.
     */
    @JvmStatic external fun nativeInvoke(method: String, argsJson: String): String

    /**
     * Blocks the calling thread for the whole exchange (loom_chat_ex
     * semantics) — always call this from [LoomBridge]'s background
     * executor, never from the JS-interface thread directly. Every chunk
     * (start/delta/reasoning/done/error) is delivered through
     * [LoomCallbacks.onChunk] as it happens; the returned String duplicates
     * the final chunk's JSON and is normally ignored by the caller.
     */
    @JvmStatic external fun nativeChat(requestJson: String, callbackId: String): String

    /**
     * Blocks for the whole import. Progress chunks ({"type":"progress",...})
     * and the final result (loom_import_file's JSON, done = true) both go
     * through [LoomCallbacks.onChunk]; the returned String is the same final
     * result JSON.
     */
    @JvmStatic external fun nativeImportFile(path: String, title: String?, callbackId: String): String

    /**
     * Subscribes to a Loom event (or "*"); each event delivers
     * {"event":name,"payload":...} through [LoomCallbacks.onChunk] with
     * done = false (the stream only ends when you call [nativeUnsubscribe]).
     * Returns a token > 0, or a negative LOOM_E_* code.
     */
    @JvmStatic external fun nativeSubscribe(event: String, callbackId: String): Long

    @JvmStatic external fun nativeUnsubscribe(token: Long): Int

    /** Global log threshold (LOOM_LOG_DEBUG/INFO/WARNING/ERROR); logcat sink is always on. */
    @JvmStatic external fun nativeSetLogLevel(level: Int)

    // -- called BY LoomHttp.sendRequest to feed a response into the transport --
    @JvmStatic external fun nativeHttpResponseBegin(handle: Long, status: Int, headersJson: String): Int
    @JvmStatic external fun nativeHttpResponseWrite(handle: Long, data: ByteArray): Int
    @JvmStatic external fun nativeHttpResponseFail(handle: Long, code: Int, message: String): Int
    @JvmStatic external fun nativeHttpResponseCancelled(handle: Long): Boolean
}
