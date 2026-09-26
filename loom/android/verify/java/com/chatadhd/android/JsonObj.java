package com.chatadhd.android;

import java.util.LinkedHashMap;
import java.util.Iterator;
import java.util.Map;

/**
 * The smallest JSON object reader/writer that can round-trip the shapes
 * loom_jni.cpp's http_send_trampoline hands to LoomHttp.sendRequest
 * ({"method","url","headers":{name:value},"body","timeout_ms","stream"})
 * and the response headers object LoomHttp builds back. Deliberately not a
 * general JSON library — this verify harness has no dependency manager, so
 * it only implements flat objects of strings/numbers/booleans plus one
 * level of string-keyed-string-valued nesting (exactly what "headers"
 * needs). Loom's own nlohmann-based JSON is the real implementation; this
 * exists purely so the host smoke test can build/parse the tiny envelopes
 * that cross the JNI boundary without adding a Maven dependency.
 */
final class JsonObj {
    private final Map<String, Object> values = new LinkedHashMap<>();

    static JsonObj parse(String text) {
        return new Parser(text).parseObject();
    }

    boolean has(String key) {
        return values.containsKey(key);
    }

    String getString(String key) {
        Object v = values.get(key);
        return v == null ? null : String.valueOf(v);
    }

    String optString(String key, String fallback) {
        Object v = values.get(key);
        return v == null ? fallback : String.valueOf(v);
    }

    int optInt(String key, int fallback) {
        Object v = values.get(key);
        if (v instanceof Number) return ((Number) v).intValue();
        if (v instanceof String) {
            try {
                return Integer.parseInt((String) v);
            } catch (NumberFormatException ignored) {
                return fallback;
            }
        }
        return fallback;
    }

    JsonObj optObject(String key) {
        Object v = values.get(key);
        return v instanceof JsonObj ? (JsonObj) v : null;
    }

    Iterator<String> keys() {
        return values.keySet().iterator();
    }

    JsonObj put(String key, String value) {
        values.put(key, value);
        return this;
    }

    JsonObj put(String key, int value) {
        values.put(key, value);
        return this;
    }

    JsonObj put(String key, boolean value) {
        values.put(key, value);
        return this;
    }

    @Override
    public String toString() {
        StringBuilder sb = new StringBuilder("{");
        boolean first = true;
        for (Map.Entry<String, Object> e : values.entrySet()) {
            if (!first) sb.append(',');
            first = false;
            sb.append('"').append(escape(e.getKey())).append("\":");
            Object v = e.getValue();
            if (v instanceof String) sb.append('"').append(escape((String) v)).append('"');
            else if (v instanceof JsonObj) sb.append(v);
            else sb.append(v);
        }
        return sb.append('}').toString();
    }

    private static String escape(String s) {
        StringBuilder sb = new StringBuilder();
        for (int i = 0; i < s.length(); i++) {
            char c = s.charAt(i);
            switch (c) {
                case '"': sb.append("\\\""); break;
                case '\\': sb.append("\\\\"); break;
                case '\n': sb.append("\\n"); break;
                case '\r': sb.append("\\r"); break;
                case '\t': sb.append("\\t"); break;
                default:
                    if (c < 0x20) sb.append(String.format("\\u%04x", (int) c));
                    else sb.append(c);
            }
        }
        return sb.toString();
    }

    private static final class Parser {
        private final String s;
        private int i = 0;

        Parser(String s) {
            this.s = s == null ? "{}" : s;
        }

        JsonObj parseObject() {
            skipWs();
            expect('{');
            JsonObj obj = new JsonObj();
            skipWs();
            if (peek() == '}') {
                i++;
                return obj;
            }
            while (true) {
                skipWs();
                String key = parseString();
                skipWs();
                expect(':');
                skipWs();
                Object value = parseValue();
                obj.values.put(key, value);
                skipWs();
                char c = s.charAt(i++);
                if (c == '}') break;
                if (c != ',') throw new IllegalArgumentException("expected , or } at " + i + " in " + s);
            }
            return obj;
        }

        private Object parseValue() {
            char c = peek();
            if (c == '"') return parseString();
            if (c == '{') return parseObject();
            if (c == '[') return parseArraySkip();
            if (s.startsWith("true", i)) {
                i += 4;
                return Boolean.TRUE;
            }
            if (s.startsWith("false", i)) {
                i += 5;
                return Boolean.FALSE;
            }
            if (s.startsWith("null", i)) {
                i += 4;
                return null;
            }
            int start = i;
            while (i < s.length() && "-+.eE0123456789".indexOf(s.charAt(i)) >= 0) i++;
            String num = s.substring(start, i);
            if (num.contains(".") || num.contains("e") || num.contains("E")) return Double.parseDouble(num);
            return Long.parseLong(num);
        }

        // Arrays aren't needed by anything this harness reads; skip and drop.
        private Object parseArraySkip() {
            expect('[');
            int depth = 1;
            while (depth > 0) {
                char c = s.charAt(i++);
                if (c == '[') depth++;
                else if (c == ']') depth--;
                else if (c == '"') {
                    i--;
                    parseString();
                }
            }
            return null;
        }

        private String parseString() {
            expect('"');
            StringBuilder sb = new StringBuilder();
            while (true) {
                char c = s.charAt(i++);
                if (c == '"') break;
                if (c == '\\') {
                    char esc = s.charAt(i++);
                    switch (esc) {
                        case '"': sb.append('"'); break;
                        case '\\': sb.append('\\'); break;
                        case '/': sb.append('/'); break;
                        case 'n': sb.append('\n'); break;
                        case 't': sb.append('\t'); break;
                        case 'r': sb.append('\r'); break;
                        case 'b': sb.append('\b'); break;
                        case 'f': sb.append('\f'); break;
                        case 'u':
                            sb.append((char) Integer.parseInt(s.substring(i, i + 4), 16));
                            i += 4;
                            break;
                        default: sb.append(esc);
                    }
                } else {
                    sb.append(c);
                }
            }
            return sb.toString();
        }

        private char peek() {
            return s.charAt(i);
        }

        private void expect(char c) {
            if (s.charAt(i) != c) throw new IllegalArgumentException("expected '" + c + "' at " + i + " in " + s);
            i++;
        }

        private void skipWs() {
            while (i < s.length() && Character.isWhitespace(s.charAt(i))) i++;
        }
    }
}
