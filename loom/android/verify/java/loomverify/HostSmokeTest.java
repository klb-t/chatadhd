package loomverify;

import com.chatadhd.android.LoomCallbacks;
import com.chatadhd.android.LoomNative;
import com.sun.net.httpserver.HttpExchange;
import com.sun.net.httpserver.HttpServer;

import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;

/**
 * Host-side JNI smoke test for the Android bridge (loom/android/README.md
 * "Verification without an Android SDK"). Exercises the exact same JNI glue
 * (loom_jni.cpp) and the same native method signatures LoomBridge.kt calls,
 * against a real libloom.so, from plain Java (no Kotlin, no Android SDK):
 *
 *   1. nativeInit against a throwaway data directory.
 *   2. create a conversation; send a chat message containing Polish
 *      diacritics, BMP and astral-plane emoji, and a ZWJ family-emoji
 *      sequence, against a local mock SSE server reached through the
 *      injected Java HTTP transport (LoomHttp.sendRequest).
 *   3. read the streamed chunks back through LoomCallbacks.onChunk.
 *   4. read the conversation's messages back through nativeInvoke and
 *      confirm the emoji/Polish text round-tripped byte-for-byte through
 *      UTF-8 <-> UTF-16 <-> SQLite <-> UTF-8 <-> UTF-16.
 *   5. nativeShutdown.
 *
 * Usage: java -Djava.library.path=<dir with libloom_jni_host.so>
 *   -cp <classes dir> loomverify.HostSmokeTest
 * Exit code 0 on success, non-zero (with a message on stderr) on failure —
 * meant to be wired into CTest (see loom/android/verify/CMakeLists.txt).
 */
public final class HostSmokeTest {
    private static final String USER_TEXT =
        "Zazółć gęślą jaźń - test 😀🔥🧠" // Polish + emoji
        + "👨‍👩‍👧‍👦"; // family ZWJ sequence
    private static final String ASSISTANT_TEXT = "Cześć 🌍 世界 👍";

    public static void main(String[] args) throws Exception {
        if (args.length < 1) {
            System.err.println("usage: HostSmokeTest <path to libloom_jni_host.so>");
            System.exit(2);
        }
        System.load(args[0]);

        HttpServer server = HttpServer.create(new InetSocketAddress("127.0.0.1", 0), 0);
        StringBuilder receivedBody = new StringBuilder();
        server.createContext("/chat/completions", exchange -> handleChat(exchange, receivedBody));
        server.start();
        int port = server.getAddress().getPort();

        Path dataDir = Files.createTempDirectory("loom_host_smoke");
        int failures = 0;
        try {
            failures += check("early stream errors settle callbacks before init", () -> {
                terminalError("chat-before-init", () -> LoomNative.nativeChat("{}", "chat-before-init"), "invalid_argument");
                terminalError("import-before-init", () -> LoomNative.nativeImportFile("missing", null, "import-before-init"),
                    "invalid_argument");
            });
            failures += check("invalid init options stay inside JNI error boundary", () -> {
                for (String options : new String[]{"not-json", "[]", "null"}) {
                    String r = LoomNative.nativeInit(dataDir.toString(), options);
                    require(r.contains("\"ok\":false") && r.contains("invalid_argument"), "invalid options accepted: " + r);
                }
            });
            failures += check("nativeInit", () -> {
                String r = LoomNative.nativeInit(dataDir.toString(), null);
                require(r.contains("\"ok\":true"), "init failed: " + r);
            });

            failures += check("chat validation emits exactly one terminal error", () -> {
                terminalError("chat-empty", () -> LoomNative.nativeChat("{}", "chat-empty"), "invalid_argument");
                terminalError("chat-malformed", () -> LoomNative.nativeChat("not-json", "chat-malformed"), "parse");
            });

            failures += check("configure mock endpoint", () -> {
                require(!LoomNative.nativeInvoke("set_config", "[\"base_url\",\"http://127.0.0.1:" + port + "\"]")
                    .contains("\"error\""), "set_config base_url failed");
                require(!LoomNative.nativeInvoke("set_secret", "[\"api_key\",\"test-key\"]")
                    .contains("\"error\""), "set_secret api_key failed");
            });

            String[] convId = new String[1];
            failures += check("create_conversation", () -> {
                String r = LoomNative.nativeInvoke("create_conversation", "[\"host smoke test\"]");
                require(!r.contains("\"error\""), "create_conversation failed: " + r);
                convId[0] = extractField(r, "id");
                require(convId[0] != null && !convId[0].isEmpty(), "no conversation id in: " + r);
            });

            failures += check("chat round-trip (streaming + emoji/Polish UTF-8)", () -> {
                BlockingQueue<LoomCallbacks.Chunk> chunks = LoomCallbacks.register("host-cb-1");
                String requestJson = "{\"message\":" + jsonString(USER_TEXT) + ",\"conv_id\":" + jsonString(convId[0])
                    + ",\"stream\":true}";
                Thread t = new Thread(() -> LoomNative.nativeChat(requestJson, "host-cb-1"));
                t.start();

                StringBuilder assembled = new StringBuilder();
                boolean sawDone = false;
                long deadline = System.currentTimeMillis() + 15000;
                while (System.currentTimeMillis() < deadline) {
                    LoomCallbacks.Chunk c = chunks.poll(500, TimeUnit.MILLISECONDS);
                    if (c == null) continue;
                    String type = extractField(c.json, "type");
                    if ("delta".equals(type)) assembled.append(extractField(c.json, "text"));
                    if ("error".equals(type)) throw new AssertionError("chat error chunk: " + c.json);
                    if (c.done) {
                        sawDone = true;
                        break;
                    }
                }
                t.join(5000);
                require(sawDone, "never received a done=true chunk");
                require(assembled.toString().equals(ASSISTANT_TEXT),
                    "assistant text mismatch: got [" + assembled + "] want [" + ASSISTANT_TEXT + "]");
                require(receivedBody.toString().contains(USER_TEXT),
                    "request body sent to mock server did not contain the emoji/Polish user text verbatim");
            });

            failures += check("get_messages round-trip", () -> {
                String r = LoomNative.nativeInvoke("get_messages", "[" + jsonString(convId[0]) + "]");
                require(r.contains(USER_TEXT), "stored user message did not round-trip verbatim through SQLite: " + r);
            });

            failures += check("WebView named-argument protocol", () -> {
                String r = LoomNative.nativeInvoke("get_messages", "{\"conv_id\":" + jsonString(convId[0]) + "}");
                require(r.contains(USER_TEXT), "named conv_id was discarded: " + r);
                r = LoomNative.nativeInvoke("get_config", "{}");
                require(!r.contains("\"error\""), "empty named arguments failed: " + r);
                r = LoomNative.nativeInvoke("get_logs", "{\"max_lines\":5}");
                require(!r.contains("not_implemented"), "web log endpoint absent: " + r);
                r = LoomNative.nativeInvoke("create_conversation", "not-json");
                require(r.contains("invalid_argument"), "malformed JSON silently created conversation: " + r);
                r = LoomNative.nativeInvoke("create_memory", "{\"content\":\"bridge memory\"}");
                require(!r.contains("\"error\""), "whole-object memory argument lost: " + r);
                String memoryId = extractField(r, "id");
                r = LoomNative.nativeInvoke("update_memory", "{\"id\":" + jsonString(memoryId)
                    + ",\"content\":\"updated via named fields\"}");
                require(r.contains("updated via named fields") && !r.contains("\"error\""), "flattened memory patch lost: " + r);
                r = LoomNative.nativeInvoke("update_conversation", "{\"conv_id\":" + jsonString(convId[0])
                    + ",\"patch\":{\"title\":\"named patch title\"}}");
                require(r.contains("named patch title") && !r.contains("\"error\""), "nested conversation patch lost: " + r);
            });

            failures += check("unsubscribe retains userdata for an in-flight event", () -> {
                CountDownLatch entered = new CountDownLatch(1);
                CountDownLatch release = new CountDownLatch(1);
                LoomCallbacks.register("event-blocker");
                BlockingQueue<LoomCallbacks.Chunk> pending = LoomCallbacks.register("event-unsubscribed");
                LoomCallbacks.setDeliveryHook(id -> {
                    if (!id.equals("event-blocker")) return;
                    entered.countDown();
                    try { release.await(5, TimeUnit.SECONDS); }
                    catch (InterruptedException e) { Thread.currentThread().interrupt(); }
                });
                long first = LoomNative.nativeSubscribe("conv:created", "event-blocker");
                long second = LoomNative.nativeSubscribe("conv:created", "event-unsubscribed");
                Thread emitter = new Thread(() -> LoomNative.nativeInvoke("create_conversation", "{\"title\":\"event test\"}"));
                try {
                    require(first > 0 && second > 0, "subscriptions failed");
                    emitter.start();
                    require(entered.await(5, TimeUnit.SECONDS), "emitter never reached blocker");
                    require(LoomNative.nativeUnsubscribe(second) == 0, "unsubscribe failed");
                    release.countDown();
                    emitter.join(5000);
                    require(!emitter.isAlive(), "emitter deadlocked");
                    // EventBus explicitly permits delivery of the already-copied
                    // handler; its user_data must still contain the correct ID.
                    LoomCallbacks.Chunk event = pending.poll(2, TimeUnit.SECONDS);
                    require(event != null && event.json.contains("conv:created"), "in-flight callback lost its userdata");
                } finally {
                    release.countDown();
                    LoomCallbacks.setDeliveryHook(null);
                    LoomNative.nativeUnsubscribe(first);
                    LoomNative.nativeUnsubscribe(second);
                    emitter.join(5000);
                }
            });

            failures += check("knowledge workbench through native dispatcher", () -> {
                Path source = dataDir.resolve("knowledge-source.md");
                Files.writeString(source, "# ChatADHD\nWe decided to store raw sources unchanged. "
                    + "ChatADHD uses Loom and SQLite for its knowledge graph.\n");
                String config = "{\"sources\":[" + jsonString(source.toString()) + "],\"stage_params\":{\"catalog\":{\"import\":{\"mode\":\"full\"}}},\"priors\":false}";
                String r = LoomNative.nativeInvoke("knowledge_run", "{\"config\":" + config + "}");
                require(r.contains("\"status\":\"done\""), "knowledge run failed: " + r);
                r = LoomNative.nativeInvoke("kb_runs", "{\"limit\":5}");
                require(r.contains("\"status\":\"done\""), "knowledge run not queryable: " + r);
                r = LoomNative.nativeInvoke("kb_query", "{\"query\":{\"what\":\"entities\"}}");
                require(r.contains("\"items\"") && !r.contains("\"error\""), "knowledge entities query failed: " + r);
                r = LoomNative.nativeInvoke("catalog_query", "{\"query\":{}}");
                require(r.contains("chatadhd") && r.contains("content_hash"), "catalog lost source content: " + r);
                r = LoomNative.nativeInvoke("context_build", "{\"request\":{\"text\":\"Explain ChatADHD\",\"budget_tokens\":200}}");
                require(r.contains("\"context_set\""), "goal context failed: " + r);
            });

            failures += check("nativeShutdown", () -> LoomNative.nativeShutdown());
            failures += check("stream errors settle after shutdown too", () -> {
                terminalError("chat-after-shutdown", () -> LoomNative.nativeChat("{}", "chat-after-shutdown"), "invalid_argument");
                terminalError("import-after-shutdown", () -> LoomNative.nativeImportFile("missing", null, "import-after-shutdown"),
                    "invalid_argument");
            });
        } finally {
            server.stop(0);
            deleteRecursive(dataDir);
        }

        if (failures > 0) {
            System.err.println(failures + " check(s) FAILED");
            System.exit(1);
        }
        System.out.println("HostSmokeTest: all checks passed");
    }

    private static void handleChat(HttpExchange exchange, StringBuilder receivedBody) throws java.io.IOException {
        byte[] reqBody = exchange.getRequestBody().readAllBytes();
        receivedBody.append(new String(reqBody, StandardCharsets.UTF_8));

        String sse = "data: {\"choices\":[{\"delta\":{\"content\":\"Cze\\u015b\\u0107 \"}}]}\n\n"
            + "data: {\"choices\":[{\"delta\":{\"content\":\"\\ud83c\\udf0d \\u4e16\\u754c \"}}]}\n\n"
            + "data: {\"choices\":[{\"delta\":{\"content\":\"\\ud83d\\udc4d\"}},{\"index\":0}],\"usage\":{\"total_tokens\":3}}\n\n"
            + "data: [DONE]\n\n";
        byte[] out = sse.getBytes(StandardCharsets.UTF_8);
        exchange.getResponseHeaders().add("Content-Type", "text/event-stream");
        exchange.sendResponseHeaders(200, out.length);
        try (OutputStream os = exchange.getResponseBody()) {
            os.write(out);
        }
    }

    // ── tiny helpers (no JSON library on the classpath; see JsonObj) ────
    private static String jsonString(String s) {
        StringBuilder sb = new StringBuilder("\"");
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                default:
                    if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
                    else sb.append(c);
            }
        }
        return sb.append('"').toString();
    }

    /** Minimal "find "field":"value"" extractor for flat top-level string fields. */
    private static String extractField(String json, String field) {
        String needle = "\"" + field + "\":\"";
        int start = json.indexOf(needle);
        if (start < 0) return null;
        start += needle.length();
        StringBuilder sb = new StringBuilder();
        for (int i = start; i < json.length(); i++) {
            char c = json.charAt(i);
            if (c == '\\' && i + 1 < json.length()) {
                char n = json.charAt(++i);
                switch (n) {
                    case 'n': sb.append('\n'); break;
                    case 't': sb.append('\t'); break;
                    case '"': sb.append('"'); break;
                    case '\\': sb.append('\\'); break;
                    case 'u':
                        sb.append((char) Integer.parseInt(json.substring(i + 1, i + 5), 16));
                        i += 4;
                        break;
                    default: sb.append(n);
                }
                continue;
            }
            if (c == '"') break;
            sb.append(c);
        }
        return sb.toString();
    }

    private interface Check {
        void run() throws Exception;
    }

    private static void terminalError(String callbackId, Check action, String code) throws Exception {
        BlockingQueue<LoomCallbacks.Chunk> chunks = LoomCallbacks.register(callbackId);
        action.run();
        LoomCallbacks.Chunk terminal = chunks.poll(2, TimeUnit.SECONDS);
        require(terminal != null && terminal.done && terminal.json.contains(code), "missing terminal error for " + callbackId);
        require(chunks.isEmpty(), "duplicate terminal callback for " + callbackId);
    }

    private static int check(String name, Check c) {
        try {
            c.run();
            System.out.println("[PASS] " + name);
            return 0;
        } catch (Throwable t) {
            System.err.println("[FAIL] " + name + ": " + t);
            return 1;
        }
    }

    private static void require(boolean cond, String message) {
        if (!cond) throw new AssertionError(message);
    }

    private static void deleteRecursive(Path p) {
        try {
            Files.walk(p).sorted((a, b) -> b.compareTo(a)).forEach(x -> {
                try {
                    Files.deleteIfExists(x);
                } catch (java.io.IOException ignored) {
                }
            });
        } catch (java.io.IOException ignored) {
        }
    }
}
