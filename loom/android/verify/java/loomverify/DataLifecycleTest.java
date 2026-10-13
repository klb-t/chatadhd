package loomverify;

import com.chatadhd.android.DataMigration;
import com.chatadhd.android.LoomNative;
import com.chatadhd.android.SecretsEnvelope;
import com.chatadhd.android.SecretsVaultCore;
import com.chatadhd.android.SecretsVaultCore.Outcome;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.Arrays;
import java.util.List;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

import javax.crypto.spec.SecretKeySpec;

/**
 * F2 end to end on the host, against the REAL core (libloom through the same JNI glue the app
 * uses): Keystore-style sealing around a running context, and moving a live data directory
 * (SQLite WAL database included) with the app's own DataMigration.
 *
 * Proves the premises the Android code relies on:
 *   - the core reads secrets.json once and rewrites the whole file on set_secret;
 *   - after "seal + delete plain" the running core still knows its secrets;
 *   - after nativeShutdown the copied directory opens with the same conversation and secrets.
 *
 * Usage: java -Djava.library.path=... -cp <classes> loomverify.DataLifecycleTest <libloom_jni_host.so>
 */
public final class DataLifecycleTest {
    static int fails = 0;
    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) { fails++; System.out.println("FAIL " + n + ": " + a + " != " + b); }
    }
    static void yes(boolean c, String n) { eq(c, true, n); }

    public static void main(String[] args) throws Exception {
        System.load(args[0]);
        java.security.Key key = new SecretKeySpec(new byte[32], "AES");
        File root = Files.createTempDirectory("loom-life").toFile();
        try {
            File a = new File(root, "private-A"), b = new File(root, "private-B");
            a.mkdirs();
            Files.write(new File(a, "secrets.json").toPath(), "{\"api_key\": \"sk-A\"}".getBytes(StandardCharsets.UTF_8));

            // --- an install from before sealing: plain file is read by the core, then sealed ---
            SecretsVaultCore va = new SecretsVaultCore(a, true, () -> key);
            eq(va.beforeInit(), Outcome.NOTHING, "plain file left for the core");
            String init = LoomNative.nativeInit(a.getPath(), null);
            yes(init.contains("\"ok\":true"), "core starts on A: " + init);
            yes(LoomNative.nativeInvoke("has_secret", "[\"api_key\"]").contains("true"), "core knows api_key from the plain file");
            eq(va.afterChange(), Outcome.SEALED, "sealed after init");
            yes(!new File(a, "secrets.json").exists(), "no plain file while running");
            yes(LoomNative.nativeInvoke("has_secret", "[\"api_key\"]").contains("true"), "running core still has api_key");

            // --- the user saves a second key: the core rewrites the whole plain file ---
            LoomNative.nativeInvoke("set_secret", "[\"github_token\",\"ghp_x\"]");
            File plain = new File(a, "secrets.json");
            yes(plain.exists(), "core wrote secrets.json on set_secret");
            String written = new String(Files.readAllBytes(plain.toPath()), StandardCharsets.UTF_8);
            yes(written.contains("sk-A") && written.contains("ghp_x"), "the whole store was rewritten (both keys)");
            eq(va.afterChange(), Outcome.SEALED, "sealed after set_secret");
            yes(!plain.exists(), "plain file gone again");
            String sealedText = new String(SecretsEnvelope.unseal(key, Files.readAllBytes(new File(a, "secrets.json.keystore").toPath())), StandardCharsets.UTF_8);
            yes(sealedText.contains("sk-A") && sealedText.contains("ghp_x"), "sealed file holds both keys");

            // --- data to carry over: a conversation in the SQLite database ---
            String created = LoomNative.nativeInvoke("create_conversation", "[\"Zażółć gęślą 😀\"]");
            Matcher m = Pattern.compile("\"id\"\\s*:\\s*\"(c_[0-9a-f]+)\"").matcher(created);
            yes(m.find(), "conversation created: " + created);
            String convId = m.group(1);

            // --- move: stop the core, copy + verify, start on the copy ---
            LoomNative.nativeShutdown();
            List<String> before = DataMigration.listFiles(a);
            DataMigration.Result r = DataMigration.copyAndVerify(a, b, null);
            eq(r.ok, true, "copy ok: " + r.error + " " + r.detail);
            eq(DataMigration.listFiles(b), before, "same files in the copy");
            for (String rel : before) eq(DataMigration.sha256(new File(b, rel)), DataMigration.sha256(new File(a, rel)), "sha256 " + rel);
            eq(DataMigration.listFiles(a), before, "old copy untouched");
            yes(new File(b, "chatadhd.db").length() > 0, "database copied");

            SecretsVaultCore vb = new SecretsVaultCore(b, true, () -> key);
            eq(vb.beforeInit(), Outcome.UNSEALED, "sealed secrets unsealed for the core on the copy");
            String init2 = LoomNative.nativeInit(b.getPath(), null);
            yes(init2.contains("\"ok\":true"), "core starts on the copy: " + init2);
            String conv = LoomNative.nativeInvoke("get_conversation", "[\"" + convId + "\"]");
            yes(conv.contains("Zażółć gęślą 😀"), "conversation (UTF-8, emoji) is in the copy: " + conv);
            yes(LoomNative.nativeInvoke("has_secret", "[\"api_key\"]").contains("true"), "api_key survived the move");
            yes(LoomNative.nativeInvoke("has_secret", "[\"github_token\"]").contains("true"), "github_token survived the move");
            eq(vb.afterChange(), Outcome.SEALED, "sealed on the copy");
            yes(!new File(b, "secrets.json").exists(), "no plain file on the copy while running");
            LoomNative.nativeShutdown();

            // --- the old copy can still be started (nothing was taken from it) ---
            SecretsVaultCore va2 = new SecretsVaultCore(a, true, () -> key);
            eq(va2.beforeInit(), Outcome.UNSEALED, "old copy unseals too");
            yes(LoomNative.nativeInit(a.getPath(), null).contains("\"ok\":true"), "core starts on the old copy");
            yes(LoomNative.nativeInvoke("get_conversation", "[\"" + convId + "\"]").contains("Zażółć gęślą 😀"), "old copy still has the conversation");
            va2.afterChange();
            LoomNative.nativeShutdown();
        } finally {
            DataLocationTest.deleteAll(root);
        }
        if (fails > 0) System.exit(1);
        System.out.println("DataLifecycleTest OK");
    }
}
