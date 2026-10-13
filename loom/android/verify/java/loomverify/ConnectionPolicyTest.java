package loomverify;

import com.chatadhd.android.ConnectionPolicy;
import com.chatadhd.android.ConnectionPolicy.Result;

public final class ConnectionPolicyTest {
    static int fails = 0;
    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) { fails++; System.out.println("FAIL " + n + ": " + a + " != " + b); }
    }
    public static void main(String[] a) {
        eq(ConnectionPolicy.normalizeServerUrl("chat.example.com", false).url, "https://chat.example.com", "bare host");
        eq(ConnectionPolicy.normalizeServerUrl(" https://Chat.Example.com:8443/x// ", false).url, "https://chat.example.com:8443/x", "trim/path");
        eq(ConnectionPolicy.normalizeServerUrl("http://chat.example.com", false).error, "https_required", "http denied");
        eq(ConnectionPolicy.normalizeServerUrl("http://chat.example.com", true).error, "http_not_lan", "http public denied even if allowed");
        eq(ConnectionPolicy.normalizeServerUrl("http://192.168.1.5:8080", true).url, "http://192.168.1.5:8080", "http lan allowed");
        eq(ConnectionPolicy.normalizeServerUrl("http://192.168.1.5", false).error, "https_required", "http lan needs opt-in");
        eq(ConnectionPolicy.normalizeServerUrl("ftp://x.com", true).error, "invalid", "scheme");
        eq(ConnectionPolicy.normalizeServerUrl("https://u:p@x.com", true).error, "invalid", "userinfo");
        eq(ConnectionPolicy.normalizeServerUrl("", true).error, "empty", "empty");
        eq(ConnectionPolicy.isLanHost("172.32.0.1"), false, "172.32 public");
        eq(ConnectionPolicy.isLanHost("172.20.0.1"), true, "172.20 private");
        eq(ConnectionPolicy.isLanHost("8.8.8.8"), false, "public ip");
        String h = "appassets.androidx.startup";
        eq(ConnectionPolicy.shouldInjectBridge("local", "https://" + h + "/assets/web/index.html", h), true, "bridge local");
        eq(ConnectionPolicy.shouldInjectBridge("server", "https://" + h + "/assets/web/index.html", h), false, "no bridge server mode");
        eq(ConnectionPolicy.shouldInjectBridge("local", "https://evil.com/", h), false, "no bridge foreign");
        eq(ConnectionPolicy.shouldInjectBridge("local", "https://" + h + ".evil.com/assets/web/", h), false, "no bridge lookalike");
        eq(ConnectionPolicy.shouldInjectBridge("local", null, h), false, "null url");
        eq(ConnectionPolicy.isInAppUrl("server", "https://s.com/chat", h, "https://s.com"), true, "in server");
        eq(ConnectionPolicy.isInAppUrl("server", "https://s.com.evil.io/", h, "https://s.com"), false, "lookalike server");
        if (fails > 0) System.exit(1);
        System.out.println("ConnectionPolicyTest OK");
    }
}
