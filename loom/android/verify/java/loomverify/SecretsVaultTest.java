package loomverify;

import com.chatadhd.android.SecretsEnvelope;
import com.chatadhd.android.SecretsVaultCore;
import com.chatadhd.android.SecretsVaultCore.Outcome;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.security.GeneralSecurityException;
import java.security.Key;
import java.util.Arrays;

import javax.crypto.spec.SecretKeySpec;

/** F2: Keystore-sealed secrets.json (the Keystore key is replaced by a software key on the host). */
public final class SecretsVaultTest {
    static int fails = 0;
    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) { fails++; System.out.println("FAIL " + n + ": " + a + " != " + b); }
    }
    static Key key(int seed) {
        byte[] k = new byte[32];
        Arrays.fill(k, (byte) seed);
        return new SecretKeySpec(k, "AES");
    }
    static String read(File f) throws Exception { return new String(Files.readAllBytes(f.toPath()), StandardCharsets.UTF_8); }
    static void write(File f, String s) throws Exception { Files.write(f.toPath(), s.getBytes(StandardCharsets.UTF_8)); }

    public static void main(String[] args) throws Exception {
        // --- envelope ---
        byte[] msg = "{\"api_key\":\"sk-or-v1-secret\"}".getBytes(StandardCharsets.UTF_8);
        byte[] a = SecretsEnvelope.seal(key(1), msg);
        byte[] b = SecretsEnvelope.seal(key(1), msg);
        eq(SecretsEnvelope.looksSealed(a), true, "sealed blob is recognised");
        eq(SecretsEnvelope.looksSealed(msg), false, "plain JSON is not sealed");
        eq(Arrays.equals(a, b), false, "fresh IV each time");
        eq(new String(a, StandardCharsets.ISO_8859_1).contains("sk-or-v1"), false, "no plaintext in the blob");
        eq(Arrays.equals(SecretsEnvelope.unseal(key(1), a), msg), true, "round trip");
        try { SecretsEnvelope.unseal(key(2), a); eq("no exception", "exception", "wrong key must fail"); }
        catch (GeneralSecurityException expected) { /* ok */ }
        byte[] t = a.clone(); t[t.length - 1] ^= 1;
        try { SecretsEnvelope.unseal(key(1), t); eq("no exception", "exception", "tampered tag must fail"); }
        catch (GeneralSecurityException expected) { /* ok */ }
        byte[] h = a.clone(); h[4] ^= 1;  // iv length byte
        try { SecretsEnvelope.unseal(key(1), h); eq("no exception", "exception", "tampered header must fail"); }
        catch (GeneralSecurityException expected) { /* ok */ }
        try { SecretsEnvelope.unseal(key(1), Arrays.copyOf(a, 10)); eq("no exception", "exception", "truncated must fail"); }
        catch (SecretsEnvelope.FormatException expected) { /* ok */ }
        try { SecretsEnvelope.unseal(key(1), msg); eq("no exception", "exception", "plain must fail"); }
        catch (SecretsEnvelope.FormatException expected) { /* ok */ }

        // --- pure decisions ---
        eq(SecretsVaultCore.planStart(true, true, true), SecretsVaultCore.Action.NOTHING, "plain wins at start");
        eq(SecretsVaultCore.planStart(true, false, true), SecretsVaultCore.Action.UNSEAL, "sealed only -> unseal");
        eq(SecretsVaultCore.planStart(false, false, true), SecretsVaultCore.Action.UNSEAL_AND_DROP_SEALED, "wrap off -> back to plain");
        eq(SecretsVaultCore.planStart(true, false, false), SecretsVaultCore.Action.NOTHING, "nothing at all");
        eq(SecretsVaultCore.planChange(true, true), SecretsVaultCore.Action.SEAL, "seal plain");
        eq(SecretsVaultCore.planChange(false, true), SecretsVaultCore.Action.NOTHING, "wrap off: keep plain");

        File dir = Files.createTempDirectory("loom-sec").toFile();
        try {
            File plain = new File(dir, "secrets.json"), sealed = new File(dir, "secrets.json.keystore");
            File unreadable = new File(dir, "secrets.json.keystore.unreadable");
            String v1 = "{\n  \"api_key\": \"sk-1\"\n}";

            // --- migration: an existing plain file is readable by the core, then sealed ---
            write(plain, v1);
            SecretsVaultCore vault = new SecretsVaultCore(dir, true, () -> key(1));
            eq(vault.beforeInit(), Outcome.NOTHING, "plain present: leave it for the core");
            eq(read(plain), v1, "plain untouched before init");
            eq(vault.afterChange(), Outcome.SEALED, "plain sealed after init");
            eq(plain.exists(), false, "plain removed");
            eq(SecretsEnvelope.looksSealed(Files.readAllBytes(sealed.toPath())), true, "sealed file in place");
            eq(new String(SecretsEnvelope.unseal(key(1), Files.readAllBytes(sealed.toPath())), StandardCharsets.UTF_8), v1, "sealed content");
            eq(vault.afterChange(), Outcome.NOTHING, "nothing to do when no plain file");

            // --- next start: unseal for the core, then seal again WITHOUT rewriting the sealed file ---
            byte[] sealedBefore = Files.readAllBytes(sealed.toPath());
            SecretsVaultCore v2 = new SecretsVaultCore(dir, true, () -> key(1));
            eq(v2.beforeInit(), Outcome.UNSEALED, "sealed -> plain for the core");
            eq(read(plain), v1, "plain content restored");
            eq(Files.getPosixFilePermissions(plain.toPath()),
               java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"), "plain is owner-only");
            eq(v2.afterChange(), Outcome.SEALED, "sealed again");
            eq(plain.exists(), false, "plain removed again");
            eq(Arrays.equals(sealedBefore, Files.readAllBytes(sealed.toPath())), true, "unchanged secrets: sealed file not rewritten");

            // --- the user sets a new key: core rewrites plain, hook seals ---
            v2.beforeInit();
            String v3 = "{\n  \"api_key\": \"sk-2\",\n  \"github_token\": \"ghp_x\"\n}";
            write(plain, v3);
            eq(v2.afterChange(), Outcome.SEALED, "changed secrets sealed");
            eq(new String(SecretsEnvelope.unseal(key(1), Files.readAllBytes(sealed.toPath())), StandardCharsets.UTF_8), v3, "new content sealed");

            // --- crash between core write and seal: plain AND (older) sealed exist -> plain wins ---
            String v4 = "{\n  \"api_key\": \"sk-3\"\n}";
            write(plain, v4);
            SecretsVaultCore v5 = new SecretsVaultCore(dir, true, () -> key(1));
            eq(v5.beforeInit(), Outcome.NOTHING, "plain beats stale sealed at start");
            eq(read(plain), v4, "newer plain not overwritten by the older sealed copy");
            eq(v5.afterChange(), Outcome.SEALED, "recovery seals the newer plain");
            eq(new String(SecretsEnvelope.unseal(key(1), Files.readAllBytes(sealed.toPath())), StandardCharsets.UTF_8), v4, "recovered content");

            // --- the Keystore key is gone (reinstall): sealed is kept, nothing invented, then replaced by aside-rename ---
            SecretsVaultCore lost = new SecretsVaultCore(dir, true, () -> key(9));
            byte[] keep = Files.readAllBytes(sealed.toPath());
            eq(lost.beforeInit(), Outcome.UNSEAL_FAILED, "lost key reported");
            eq(plain.exists(), false, "no plain file invented");
            eq(Arrays.equals(keep, Files.readAllBytes(sealed.toPath())), true, "unreadable sealed file untouched");
            write(plain, "{\n  \"api_key\": \"sk-new\"\n}");
            eq(lost.afterChange(), Outcome.SEALED, "new secrets sealed with the new key");
            eq(Arrays.equals(keep, Files.readAllBytes(unreadable.toPath())), true, "old unreadable file kept aside, not destroyed");
            eq(new String(SecretsEnvelope.unseal(key(9), Files.readAllBytes(sealed.toPath())), StandardCharsets.UTF_8),
               "{\n  \"api_key\": \"sk-new\"\n}", "new sealed content");

            // --- sealing impossible: the plain file stays, nothing lost ---
            File dir2 = Files.createTempDirectory("loom-sec2").toFile();
            try {
                File plain2 = new File(dir2, "secrets.json");
                write(plain2, v1);
                SecretsVaultCore broken = new SecretsVaultCore(dir2, true, () -> { throw new GeneralSecurityException("keystore down"); });
                eq(broken.afterChange(), Outcome.SEAL_FAILED, "keystore failure reported");
                eq(read(plain2), v1, "plain file kept");
                eq(new File(dir2, "secrets.json.keystore").exists(), false, "no half-written sealed file");
                eq(new File(dir2, "secrets.json.keystore.tmp").exists(), false, "no tmp left");

                // --- wrapping switched off: back to a single plain copy ---
                SecretsVaultCore on = new SecretsVaultCore(dir2, true, () -> key(1));
                eq(on.afterChange(), Outcome.SEALED, "sealed with wrapping on");
                SecretsVaultCore off = new SecretsVaultCore(dir2, false, () -> key(1));
                eq(off.beforeInit(), Outcome.UNSEALED, "wrapping off: unsealed");
                eq(read(plain2), v1, "plain restored");
                eq(new File(dir2, "secrets.json.keystore").exists(), false, "sealed copy dropped when wrapping is off");
                eq(off.afterChange(), Outcome.NOTHING, "wrapping off: plain stays");
                eq(plain2.exists(), true, "plain stays when wrapping is off");
            } finally { DataLocationTest.deleteAll(dir2); }
        } finally {
            DataLocationTest.deleteAll(dir);
        }
        if (fails > 0) System.exit(1);
        System.out.println("SecretsVaultTest OK");
    }
}
