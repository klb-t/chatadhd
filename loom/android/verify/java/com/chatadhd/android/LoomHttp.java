package com.chatadhd.android;

import java.io.IOException;
import java.io.InputStream;
import java.net.HttpURLConnection;
import java.net.SocketTimeoutException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.util.Base64;
import java.util.Iterator;

/**
 * Host stand-in for LoomHttp.kt: same HttpURLConnection-based transport
 * logic (streamed reads, nativeHttpResponseBegin/Write/Fail/Cancelled), with
 * the two Android-only calls (android.util.Base64, android.util.Log)
 * swapped for java.util.Base64 and stderr. This is what exercises "chat
 * against a local mock SSE server via the injected Java HTTP transport" in
 * the smoke test: loom_jni.cpp's http_send_trampoline calls
 * Java_..._LoomHttp.sendRequest exactly as it would call into the Kotlin
 * version on-device.
 */
public final class LoomHttp {
    private static final int READ_CHUNK = 8192;

    private static final int LOOM_E_NETWORK = -7;
    private static final int LOOM_E_TIMEOUT = -11;
    private static final int LOOM_E_INTERNAL = -20;

    private LoomHttp() {}

    /** Called from native code (loom_jni.cpp: http_send_trampoline). */
    public static void sendRequest(String requestJson, long handle) {
        try {
            JsonObj req = JsonObj.parse(requestJson);
            String method = req.optString("method", "GET");
            String url = req.getString("url");
            JsonObj headers = req.optObject("headers");
            int timeoutMs = req.optInt("timeout_ms", 30000);
            if (timeoutMs <= 0) timeoutMs = 30000;

            byte[] body = null;
            if (req.has("body_base64")) {
                body = Base64.getDecoder().decode(req.getString("body_base64"));
            } else if (req.has("body") && !req.getString("body").isEmpty()) {
                body = req.getString("body").getBytes(StandardCharsets.UTF_8);
            }

            HttpURLConnection connection = (HttpURLConnection) new URL(url).openConnection();
            connection.setRequestMethod(method);
            connection.setConnectTimeout(timeoutMs);
            connection.setReadTimeout(timeoutMs);
            connection.setInstanceFollowRedirects(true);
            connection.setDoInput(true);

            if (headers != null) {
                for (Iterator<String> it = headers.keys(); it.hasNext(); ) {
                    String name = it.next();
                    connection.setRequestProperty(name, headers.getString(name));
                }
            }

            if (body != null) {
                connection.setDoOutput(true);
                connection.setFixedLengthStreamingMode(body.length);
                connection.getOutputStream().write(body);
                connection.getOutputStream().close();
            }

            int status = connection.getResponseCode();
            JsonObj responseHeaders = new JsonObj();
            for (int i = 0; ; i++) {
                String name = connection.getHeaderFieldKey(i);
                if (name == null) {
                    if (connection.getHeaderField(i) == null) break;
                    continue; // status line at i==0 has a null key; skip, don't stop
                }
                String value = connection.getHeaderField(i);
                responseHeaders.put(name, responseHeaders.has(name) ? responseHeaders.getString(name) + ", " + value : value);
            }

            int beginRc = LoomNative.nativeHttpResponseBegin(handle, status, responseHeaders.toString());
            if (beginRc != 0) {
                connection.disconnect();
                return;
            }

            InputStream stream = status >= 400 ? connection.getErrorStream() : connection.getInputStream();
            if (stream != null) {
                try {
                    byte[] buffer = new byte[READ_CHUNK];
                    while (true) {
                        if (LoomNative.nativeHttpResponseCancelled(handle)) break;
                        int n = stream.read(buffer);
                        if (n < 0) break;
                        if (n == 0) continue;
                        byte[] chunk = (n == buffer.length) ? buffer : java.util.Arrays.copyOf(buffer, n);
                        int writeRc = LoomNative.nativeHttpResponseWrite(handle, chunk);
                        if (writeRc != 0) break;
                    }
                } finally {
                    stream.close();
                }
            }
            connection.disconnect();
        } catch (SocketTimeoutException e) {
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_TIMEOUT, String.valueOf(e.getMessage()));
        } catch (IOException e) {
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_NETWORK, String.valueOf(e.getMessage()));
        } catch (Exception e) {
            LoomNative.nativeHttpResponseFail(handle, LOOM_E_INTERNAL, String.valueOf(e.getMessage()));
        }
    }
}
