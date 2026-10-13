package com.chatadhd.android;

import java.io.File;
import java.io.IOException;

/**
 * Pure-Java (no android.* imports, so it is testable with plain javac; see
 * verify/) rules for WHERE the data directory lives on the phone.
 *
 * The location is a setting, never a hard rule:
 *  - {@link #KIND_PRIVATE}      Context.getFilesDir()/ChatADHD. Sandboxed by the
 *                               system: no other app can read it. Removed on
 *                               uninstall. Default for new installs.
 *  - {@link #KIND_APP_EXTERNAL} Context.getExternalFilesDir()/ChatADHD. Private
 *                               to the app on Android 11+, larger volume,
 *                               removed on uninstall.
 *  - {@link #KIND_SHARED}       /storage/emulated/0/Documents/ChatADHD. Survives
 *                               uninstall and can be synced by other tools, but
 *                               every app with "All files access" can read it
 *                               (secrets.json included). Needs
 *                               MANAGE_EXTERNAL_STORAGE. Kept for installs that
 *                               already live there; the user may still choose it.
 *  - {@link #KIND_CUSTOM}       any absolute path the user types.
 *
 * Nothing here forbids a choice; {@link #classify} only tells the UI whether a
 * warning must be shown.
 */
public final class DataLocationPolicy {
    private DataLocationPolicy() {}

    public static final String KIND_PRIVATE = "private";
    public static final String KIND_APP_EXTERNAL = "app_external";
    public static final String KIND_SHARED = "shared";
    public static final String KIND_CUSTOM = "custom";

    /** Same marker file the core and engine/paths.py use to recognise a data dir. */
    public static final String SENTINEL = ".chatadhd_data";
    /** Directory name below the private roots. */
    public static final String SUBDIR = "ChatADHD";

    /** How exposed a directory is to other apps. */
    public enum Exposure { PRIVATE, APP_EXTERNAL, SHARED }

    /** The kind to use and whether the user actually picked it. */
    public static final class Choice {
        public final String kind;
        /** false = derived (new install or legacy shared data), true = saved by the user's action. */
        public final boolean explicit;
        Choice(String kind, boolean explicit) { this.kind = kind; this.explicit = explicit; }
    }

    public static boolean isKnownKind(String k) {
        return KIND_PRIVATE.equals(k) || KIND_APP_EXTERNAL.equals(k) || KIND_SHARED.equals(k) || KIND_CUSTOM.equals(k);
    }

    /**
     * Which kind to use at start-up.
     *  - a saved, known kind always wins;
     *  - otherwise data already in the private dir wins (it is the safe place, e.g. preferences were cleared);
     *  - otherwise data in the shared dir means an install from before this setting existed: it STAYS
     *    there (nothing is moved without the user's consent) and the UI shows a warning;
     *  - otherwise this is a new install: private.
     */
    public static Choice choose(String savedKind, boolean sharedHasData, boolean privateHasData) {
        if (isKnownKind(savedKind)) return new Choice(savedKind, true);
        if (privateHasData) return new Choice(KIND_PRIVATE, false);
        if (sharedHasData) return new Choice(KIND_SHARED, false);
        return new Choice(KIND_PRIVATE, false);
    }

    /** null = the path is acceptable, otherwise a key the UI maps to a resource. */
    public static String validateCustomPath(String raw) {
        if (raw == null || raw.trim().isEmpty()) return "empty";
        if (!raw.trim().startsWith("/")) return "not_absolute";
        return null;
    }

    /**
     * Directory for {@code kind}, or null when it cannot be resolved (custom path invalid,
     * app-external volume missing); the caller then falls back to {@code privateDir}.
     */
    public static File resolve(String kind, String customPath, File privateDir, File appExternalDir, File sharedDir) {
        if (KIND_PRIVATE.equals(kind)) return privateDir;
        if (KIND_APP_EXTERNAL.equals(kind)) return appExternalDir;
        if (KIND_SHARED.equals(kind)) return sharedDir;
        if (KIND_CUSTOM.equals(kind)) return validateCustomPath(customPath) == null ? new File(customPath.trim()) : null;
        return null;
    }

    /** True when {@code dir} already holds a ChatADHD data directory (sentinel present). */
    public static boolean hasData(File dir) {
        return dir != null && new File(dir, SENTINEL).isFile();
    }

    /**
     * How exposed {@code dir} is, from where it really is (canonical paths, so ".." and symlinks
     * cannot disguise a shared path as a private one): below the sandbox root -> PRIVATE, below the
     * app-specific external root -> APP_EXTERNAL, anything else -> SHARED.
     */
    public static Exposure classify(File dir, File filesRoot, File externalFilesRoot) {
        if (dir == null) return Exposure.SHARED;
        if (isBelow(dir, filesRoot)) return Exposure.PRIVATE;
        if (externalFilesRoot != null && isBelow(dir, externalFilesRoot)) return Exposure.APP_EXTERNAL;
        return Exposure.SHARED;
    }

    /** The visible warning is required for exactly this exposure. */
    public static boolean needsWarning(Exposure e) {
        return e == Exposure.SHARED;
    }

    /** All-files access (MANAGE_EXTERNAL_STORAGE) is needed to use this exposure on API 30+. */
    public static boolean needsAllFilesAccess(Exposure e) {
        return e == Exposure.SHARED;
    }

    /**
     * A leftover copy may be deleted by the user's explicit action only if it is a different
     * directory from the live one, neither contains the other, and it really is a ChatADHD data dir
     * (so a mistyped custom path can never turn "delete old copy" into "delete my Downloads").
     */
    public static boolean isSafeToDeleteOldCopy(File old, File live) {
        if (old == null || live == null || !hasData(old)) return false;
        return !sameOrNested(old, live);
    }

    /** True if both paths are the same directory (canonical comparison). */
    public static boolean isSame(File a, File b) {
        return isBelow(a, b) && isBelow(b, a);
    }

    /** True if a == b or either is inside the other (canonical comparison). */
    public static boolean sameOrNested(File a, File b) {
        return isBelow(a, b) || isBelow(b, a);
    }

    static boolean isBelow(File dir, File root) {
        if (dir == null || root == null) return false;
        String d = canon(dir), r = canon(root);
        if (d.equals(r)) return true;
        String prefix = r.endsWith(File.separator) ? r : r + File.separator;
        return d.startsWith(prefix);
    }

    private static String canon(File f) {
        try { return f.getCanonicalPath(); } catch (IOException e) { return f.getAbsoluteFile().toPath().normalize().toString(); }
    }
}
