package com.chatadhd.android

import android.util.Base64
import android.util.Log
import org.json.JSONObject
import java.io.IOException
import java.net.HttpURLConnection
import java.net.SocketTimeoutException
import java.net.URL
import java.nio.charset.StandardCharsets

/**
 * The platform HTTP transport, injected into Loom through
 * `loom_set_http_transport` (see MainActivity.onCreate / loom_jni.cpp's
 * `http_send_trampoline`). Every network call Loom makes — chat completions,
 * model refresh, semantic-LLM analysis, batch API polling, ASR/OCR provider
 * calls, GitHub sync — arrives here, on whichever thread made the call (a
 * background-executor thread for a user-initiated action, or a Loom worker
 * thread for SemanticWorker/batch polling). [sendRequest] blocks that thread
 * until the exchange is complete, exactly like every other HttpTransport
 * implementation Loom can use (see loom/include/loom/net/http.h).
 *
 * Deliberately OkHttp-free (java.net.HttpURLConnection only), per the
 * project's "inject HTTP from Kotlin instead of linking OpenSSL into the
 * native library" decision (see loom/android/README.md).
 */
object LoomHttp {
    private const val TAG = "LoomHttp"
    private const val READ_CHUNK = 8 * 1024

    /** Called from native code (loom_jni.cpp: http_send_trampoline). */
    @JvmStatic
    fun sendRequest(requestJson: String, handle: Long) {
        try {
            val req = JSONObject(requestJson)
            val method = req.optString("method", "GET").ifEmpty { "GET" }
            val url = req.getString("url")
            val headers = req.optJSONObject("headers")
            val timeoutMs = req.optInt("timeout_ms", 30000).let { if (it <= 0) 30000 else it }

            val body: ByteArray? = when {
                req.has("body_base64") && !req.isNull("body_base64") ->
                    Base64.decode(req.getString("body_base64"), Base64.NO_WRAP)
                req.has("body") && !req.isNull("body") && req.getString("body").isNotEmpty() ->
                    req.getString("body").toByteArray(StandardCharsets.UTF_8)
                else -> null
            }

            val connection = URL(url).openConnection() as HttpURLConnection
            connection.requestMethod = method
            connection.connectTimeout = timeoutMs
            connection.readTimeout = timeoutMs
            connection.instanceFollowRedirects = true
            connection.doInput = true

            headers?.keys()?.forEach { name ->
                connection.setRequestProperty(name, headers.getString(name))
            }

            if (body != null) {
                connection.doOutput = true
                connection.setFixedLengthStreamingMode(body.size)
                connection.outputStream.use { it.write(body) }
            }

            val status = connection.responseCode
            val responseHeaders = JSONObject()
            for (i in 0 until Int.MAX_VALUE) {
                val name = connection.getHeaderFieldKey(i) ?: break
                val value = connection.getHeaderField(i)
                responseHeaders.put(
                    name,
                    if (responseHeaders.has(name)) "${responseHeaders.getString(name)}, $value" else value,
                )
            }

            val beginRc = LoomNative.nativeHttpResponseBegin(handle, status, responseHeaders.toString())
            if (beginRc != 0) {
                // LOOM_E_CANCELLED (the sink aborted the transfer) or a bad
                // argument; either way Loom no longer wants the body.
                connection.disconnect()
                return
            }

            val stream = if (status >= 400) connection.errorStream else connection.inputStream
            if (stream != null) {
                stream.use { input ->
                    val buffer = ByteArray(READ_CHUNK)
                    while (true) {
                        if (LoomNative.nativeHttpResponseCancelled(handle)) break
                        val n = input.read(buffer)
                        if (n < 0) break
                        if (n == 0) continue
                        val chunk = if (n == buffer.size) buffer else buffer.copyOf(n)
                        val writeRc = LoomNative.nativeHttpResponseWrite(handle, chunk)
                        if (writeRc != 0) break // cancelled or invalid handle
                    }
                }
            }
            connection.disconnect()
        } catch (e: SocketTimeoutException) {
            Log.w(TAG, "timeout", e)
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_TIMEOUT, e.message ?: "timeout")
        } catch (e: IOException) {
            Log.w(TAG, "network error", e)
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_NETWORK, e.message ?: "network error")
        } catch (e: Exception) {
            Log.e(TAG, "unexpected error", e)
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_INTERNAL, e.message ?: "unexpected error")
        }
    }

    // Mirrors the LOOM_E_* constants in loom/include/loom/loom.h (kept in
    // sync by hand: this file must never depend on native headers).
    private const val LOOM_E_NETWORK = -7
    private const val LOOM_E_TIMEOUT = -11
    private const val LOOM_E_INTERNAL = -20
}
