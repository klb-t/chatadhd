package com.chatadhd.android;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.security.GeneralSecurityException;
import java.security.Key;
import java.util.Arrays;

/**
 * Pure-Java (no android.* imports, so it is testable with plain javac; see verify/)
 * flow that keeps the core's secrets file sealed with a key the caller supplies (on a phone:
 * the Android Keystore AES key, see SecretsVault.kt).
 *
 * Why a flow around a file and not a hook inside the core: the Loom core reads secrets.json once
 * at start (it keeps the values in memory) and rewrites the whole file when a secret is set or
 * deleted. So the plain file only has to exist
 *   - for the moment between {@link #beforeInit} and the end of context start-up, and
 *   - for the moment between a "set secret" and {@link #afterChange}.
 * Everything else of the time only {@code secrets.json.keystore} is on disk.
 *
 * Rules that make it safe to crash or be killed at any point:
 *  - a plain secrets.json that exists is AUTHORITATIVE: it is either what the core wrote last, or
 *    the file of an install from before sealing existed ("the plain file stays readable for
 *    migration"). It is never overwritten by the sealed copy;
 *  - the plain file is deleted only after the sealed copy was written, re-read, unsealed and
 *    compared byte for byte, and only if it did not change while that happened;
 *  - when sealing is impossible (key problem) the plain file simply stays; nothing is lost;
 *  - a sealed file that cannot be unsealed (the Keystore key is gone after an uninstall or a
 *    device restore) is never deleted; it is renamed to {@code secrets.json.keystore.unreadable}
 *    when it would otherwise be replaced.
 */
public final class SecretsVaultCore {
    public static final String PLAIN_NAME = "secrets.json";
    public static final String SEALED_NAME = "secrets.json.keystore";
    public static final String UNREADABLE_NAME = "secrets.json.keystore.unreadable";

    /** Supplies the AES key; on a phone this is the Android Keystore. */
    public interface KeyProvider {
        Key key() throws GeneralSecurityException;
    }

    public enum Outcome { NOTHING, UNSEALED, UNSEAL_FAILED, SEALED, SEAL_FAILED }
    public enum Action { NOTHING, UNSEAL, UNSEAL_AND_DROP_SEALED, SEAL }

    private final File plain;
    private final File sealed;
    private final File unreadable;
    private final boolean wrap;
    private final KeyProvider keys;
    /** Plain bytes produced by beforeInit; lets afterChange skip re-sealing an unchanged file. */
    private byte[] lastUnsealed;

    public SecretsVaultCore(File dataDir, boolean wrap, KeyProvider keys) {
        this.plain = new File(dataDir, PLAIN_NAME);
        this.sealed = new File(dataDir, SEALED_NAME);
        this.unreadable = new File(dataDir, UNREADABLE_NAME);
        this.wrap = wrap;
        this.keys = keys;
    }

    /** What to do before the core starts. Pure decision, exposed for tests. */
    public static Action planStart(boolean wrap, boolean plainExists, boolean sealedExists) {
        if (plainExists) return Action.NOTHING;                       // plain is authoritative
        if (!sealedExists) return Action.NOTHING;
        return wrap ? Action.UNSEAL : Action.UNSEAL_AND_DROP_SEALED;  // wrapping switched off: one plain copy again
    }

    /** What to do after the core (maybe) wrote secrets.json. */
    public static Action planChange(boolean wrap, boolean plainExists) {
        return wrap && plainExists ? Action.SEAL : Action.NOTHING;
    }

    /** Call BEFORE the core is initialised: makes secrets.json available if only the sealed copy exists. */
    public synchronized Outcome beforeInit() {
        Action a = planStart(wrap, plain.isFile(), sealed.isFile());
        if (a == Action.NOTHING) return Outcome.NOTHING;
        try {
            byte[] bytes = SecretsEnvelope.unseal(keys.key(), Files.readAllBytes(sealed.toPath()));
            writeAtomic(plain, bytes);
            lastUnsealed = bytes;
            if (a == Action.UNSEAL_AND_DROP_SEALED) {
                lastUnsealed = null;
                if (!sealed.delete()) return Outcome.UNSEAL_FAILED;
            }
            return Outcome.UNSEALED;
        } catch (GeneralSecurityException | IOException | RuntimeException e) {
            return Outcome.UNSEAL_FAILED;  // the sealed file is left exactly as it is
        }
    }

    /**
     * Call AFTER the core was initialised and again after anything that may have saved a secret:
     * seals secrets.json and removes the plain file. Safe to call at any time and any number of times.
     */
    public synchronized Outcome afterChange() {
        if (planChange(wrap, plain.isFile()) != Action.SEAL) return Outcome.NOTHING;
        try {
            for (int attempt = 0; attempt < 3; attempt++) {
                byte[] bytes = Files.readAllBytes(plain.toPath());
                boolean reuse = lastUnsealed != null && Arrays.equals(lastUnsealed, bytes) && sealed.isFile();
                if (!reuse) {
                    Key key = keys.key();
                    if (sealed.isFile() && !canUnseal(key)) {
                        Files.move(sealed.toPath(), unreadable.toPath(), StandardCopyOption.REPLACE_EXISTING);
                    }
                    writeAtomic(sealed, SecretsEnvelope.seal(key, bytes));
                    byte[] back = SecretsEnvelope.unseal(key, Files.readAllBytes(sealed.toPath()));
                    if (!Arrays.equals(back, bytes)) throw new IOException("sealed copy does not match");
                }
                // The core may have saved a newer file while we were sealing: then go round again.
                if (!Arrays.equals(Files.readAllBytes(plain.toPath()), bytes)) continue;
                lastUnsealed = null;
                return plain.delete() ? Outcome.SEALED : Outcome.SEAL_FAILED;
            }
            return Outcome.SEAL_FAILED;
        } catch (GeneralSecurityException | IOException | RuntimeException e) {
            return Outcome.SEAL_FAILED;  // plain file stays
        }
    }

    private boolean canUnseal(Key key) {
        try {
            SecretsEnvelope.unseal(key, Files.readAllBytes(sealed.toPath()));
            return true;
        } catch (GeneralSecurityException | IOException | RuntimeException e) {
            return false;
        }
    }

    /** tmp file created owner-only BEFORE any byte is written, fsync, then rename over the target. */
    private static void writeAtomic(File target, byte[] data) throws IOException {
        File tmp = new File(target.getPath() + ".tmp");
        if (tmp.exists() && !tmp.delete()) throw new IOException("cannot replace " + tmp);
        if (!tmp.createNewFile()) throw new IOException("cannot create " + tmp);
        tmp.setReadable(false, false);
        tmp.setWritable(false, false);
        tmp.setReadable(true, true);
        tmp.setWritable(true, true);
        try (FileOutputStream out = new FileOutputStream(tmp)) {
            out.write(data);
            out.flush();
            out.getFD().sync();
        }
        Files.move(tmp.toPath(), target.toPath(), StandardCopyOption.ATOMIC_MOVE);
    }
}
