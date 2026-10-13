package com.chatadhd.android;

import java.net.URI;
import java.util.Locale;

/**
 * Pure-Java (no android.* imports, so it is testable with plain javac; see
 * verify/) rules for the connection setting: local vs server mode.
 */
public final class ConnectionPolicy {
    private ConnectionPolicy() {}

    public static final String MODE_LOCAL = "local";
    public static final String MODE_SERVER = "server";

    /** Result of {@link #normalizeServerUrl}: either a base URL or a reason. */
    public static final class Result {
        public final String url;     // normalized origin (+ optional path), no trailing slash; null on error
        public final String error;   // human-readable reason (English key, UI maps to resources); null on success
        Result(String url, String error) { this.url = url; this.error = error; }
        public boolean ok() { return url != null; }
    }

    /**
     * Validates the server URL typed by the user. HTTPS is required. Plain
     * HTTP is accepted only when {@code allowPlainHttp} was explicitly set by
     * the user AND the host is a LAN/loopback address: cleartext sends the
     * password and the session cookie in the clear, which is only tolerable on
     * a network the user controls. The user can override by choosing so.
     */
    public static Result normalizeServerUrl(String raw, boolean allowPlainHttp) {
        if (raw == null || raw.trim().isEmpty()) return new Result(null, "empty");
        String s = raw.trim();
        if (!s.contains("://")) s = "https://" + s;
        URI u;
        try { u = new URI(s); } catch (Exception e) { return new Result(null, "invalid"); }
        String scheme = u.getScheme() == null ? "" : u.getScheme().toLowerCase(Locale.ROOT);
        String host = u.getHost();
        if (host == null || host.isEmpty()) return new Result(null, "invalid");
        if (u.getUserInfo() != null) return new Result(null, "invalid");
        if (scheme.equals("http")) {
            if (!allowPlainHttp) return new Result(null, "https_required");
            if (!isLanHost(host)) return new Result(null, "http_not_lan");
        } else if (!scheme.equals("https")) {
            return new Result(null, "invalid");
        }
        String path = u.getPath() == null ? "" : u.getPath();
        while (path.endsWith("/")) path = path.substring(0, path.length() - 1);
        StringBuilder b = new StringBuilder(scheme).append("://").append(host.toLowerCase(Locale.ROOT));
        if (u.getPort() != -1) b.append(':').append(u.getPort());
        b.append(path);
        return new Result(b.toString(), null);
    }

    /** Loopback, RFC1918, link-local, *.local / *.lan hosts and single-label names. */
    public static boolean isLanHost(String host) {
        String h = host.toLowerCase(Locale.ROOT);
        if (h.startsWith("[") && h.endsWith("]")) h = h.substring(1, h.length() - 1);
        if (h.equals("localhost") || h.equals("::1") || h.endsWith(".local") || h.endsWith(".lan")) return true;
        String[] p = h.split("\\.");
        if (p.length == 4) {
            int[] o = new int[4];
            try { for (int i = 0; i < 4; i++) o[i] = Integer.parseInt(p[i]); } catch (NumberFormatException e) { return false; }
            for (int x : o) if (x < 0 || x > 255) return false;
            return o[0] == 10 || o[0] == 127 || (o[0] == 192 && o[1] == 168)
                || (o[0] == 172 && o[1] >= 16 && o[1] <= 31) || (o[0] == 169 && o[1] == 254);
        }
        return p.length == 1 && !h.contains(":");
    }

    /**
     * SECURITY-CRITICAL: the JNI bridge gives a page full access to the local
     * data directory, secrets and API keys. It may be injected ONLY into a
     * WebView showing our bundled local origin. A remote server page (even our
     * own server) must never get it: the server mode uses HTTP transport.
     */
    public static boolean shouldInjectBridge(String mode, String url, String appHost) {
        if (!MODE_LOCAL.equals(mode) || url == null || appHost == null) return false;
        return url.equals("https://" + appHost) || url.startsWith("https://" + appHost + "/assets/web/");
    }

    /** True if navigation to {@code url} stays inside the WebView in the given mode. */
    public static boolean isInAppUrl(String mode, String url, String appHost, String serverBase) {
        if (url == null) return false;
        if (MODE_SERVER.equals(mode)) {
            if (serverBase == null) return false;
            return url.equals(serverBase) || url.startsWith(serverBase + "/") || url.startsWith(serverBase + "?");
        }
        return url.startsWith("https://" + appHost + "/assets/web/");
    }
}
