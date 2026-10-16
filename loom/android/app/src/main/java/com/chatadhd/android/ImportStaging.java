package com.chatadhd.android;

import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.StandardCopyOption;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicInteger;

/**
 * Pure Java rules for importing files the user picked through the system picker (no android.* imports, so the host
 * tests in loom/android/verify compile this very file).
 *
 * The kernel importer opens a path with std::ifstream; a picker result is a content:// URI that only a ContentResolver
 * can read. The app therefore copies the picked document (streaming, a fixed buffer, any size) into a private staging
 * directory, hands that path to the importer, and removes the copy afterwards. This class holds everything about that
 * which needs no Android: safe file names (a display name is data from another app and is never trusted as a path),
 * unique destinations, the streaming copy with progress and cancellation, and the clean-up of old staging directories.
 */
public final class ImportStaging {
    private ImportStaging() {}

    /** Reports the copy; returning false cancels it (the partial copy is deleted). */
    public interface Progress {
        boolean onBytes(long done, long total);
    }

    /** The copy was cancelled by its Progress. */
    public static final class CancelledException extends IOException {
        private static final long serialVersionUID = 1L;

        public CancelledException() {
            super("cancelled");
        }
    }

    /**
     * Extensions the importer takes by name (importer_core.cpp builtin formats). A folder pick stages only these by
     * default (the preset); "all" stages every file and lets the kernel decide (it also sniffs the content).
     */
    private static final String[] KNOWN = {
        "zip", "db", "json", "jsonl", "ndjson", "html", "htm", "mht", "mhtml", "png", "jpg", "jpeg", "webp", "md", "txt", "log",
    };

    private static final AtomicInteger SEQ = new AtomicInteger();

    public static boolean looksImportable(String name) {
        String ext = extensionOf(name);
        for (String k : KNOWN) if (k.equals(ext)) return true;
        return false;
    }

    /** Lower-case extension without the dot, "" when there is none. */
    public static String extensionOf(String name) {
        if (name == null) return "";
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        String base = name.substring(slash + 1);
        int dot = base.lastIndexOf('.');
        if (dot <= 0 || dot == base.length() - 1) return "";
        return base.substring(dot + 1).toLowerCase(Locale.ROOT);
    }

    /**
     * A file name that is safe inside a directory of ours: no path separators, no control characters, no characters
     * Windows/FAT/FUSE refuse, not hidden, not empty, at most 120 characters (the extension is kept).
     */
    public static String safeName(String raw, String fallback) {
        String name = raw == null ? "" : raw;
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        name = name.substring(slash + 1);
        StringBuilder out = new StringBuilder(name.length());
        for (int i = 0; i < name.length(); i++) {
            char c = name.charAt(i);
            if (c < 0x20 || c == 0x7f || ":*?\"<>|".indexOf(c) >= 0) out.append('_');
            else out.append(c);
        }
        name = out.toString().trim();
        while (name.startsWith(".")) name = name.substring(1);
        while (name.endsWith(".") || name.endsWith(" ")) name = name.substring(0, name.length() - 1);
        if (name.isEmpty()) return fallback;
        if (name.length() > 120) {
            int dot = name.lastIndexOf('.');
            String ext = dot > 0 && name.length() - dot <= 12 ? name.substring(dot) : "";
            name = name.substring(0, 120 - ext.length()) + ext;
        }
        return name;
    }

    /** A relative path ("a/b/c.json") whose every part is safe; empty parts and "." / ".." are dropped. */
    public static String safeRelative(String raw, String fallback) {
        if (raw == null) return fallback;
        StringBuilder out = new StringBuilder();
        for (String part : raw.split("[/\\\\]")) {
            if (part.isEmpty() || part.equals(".") || part.equals("..")) continue;
            String safe = safeName(part, "");
            if (safe.isEmpty()) continue;
            if (out.length() > 0) out.append('/');
            out.append(safe);
        }
        return out.length() == 0 ? fallback : out.toString();
    }

    /**
     * Adds the extension the MIME type implies when the name has none the importer knows. The importer sniffs content
     * too, but a name like "conversations" next to a type of application/json should still end up as .json.
     */
    public static String withExtensionForMime(String name, String mime) {
        if (looksImportable(name) || mime == null) return name;
        String m = mime.toLowerCase(Locale.ROOT);
        int semi = m.indexOf(';');
        if (semi >= 0) m = m.substring(0, semi).trim();
        String ext;
        switch (m) {
            case "application/zip":
            case "application/x-zip-compressed":
                ext = ".zip";
                break;
            case "application/json":
                ext = ".json";
                break;
            case "application/x-ndjson":
            case "application/jsonl":
                ext = ".jsonl";
                break;
            case "text/html":
                ext = ".html";
                break;
            case "text/markdown":
                ext = ".md";
                break;
            case "text/plain":
                ext = ".txt";
                break;
            case "application/vnd.sqlite3":
            case "application/x-sqlite3":
                ext = ".db";
                break;
            case "image/png":
                ext = ".png";
                break;
            case "image/jpeg":
                ext = ".jpg";
                break;
            case "image/webp":
                ext = ".webp";
                break;
            default:
                return name;
        }
        return name + ext;
    }

    /** A fresh, empty staging directory under root/imports. */
    public static File reserveDir(File root) throws IOException {
        File base = new File(root, "imports");
        File dir = new File(base, System.currentTimeMillis() + "-" + SEQ.incrementAndGet());
        Files.createDirectories(dir.toPath());
        return dir;
    }

    /**
     * Where the document goes: dir/rel, parents created, a number added before the extension when the name is taken
     * ("a.json" -> "a (2).json"). The result is always inside dir.
     */
    public static File destFor(File dir, String rel) throws IOException {
        String safe = safeRelative(rel, "import");
        File target = new File(dir, safe.replace('/', File.separatorChar));
        File parent = target.getParentFile();
        if (parent != null) Files.createDirectories(parent.toPath());
        String dirPath = dir.getCanonicalPath() + File.separator;
        if (!target.getCanonicalPath().startsWith(dirPath)) throw new IOException("path leaves the staging directory: " + rel);
        if (!target.exists() && !new File(target.getPath() + ".part").exists()) return target;
        String base = target.getName();
        String ext = "";
        int dot = base.lastIndexOf('.');
        if (dot > 0) {
            ext = base.substring(dot);
            base = base.substring(0, dot);
        }
        for (int n = 2; n < 100000; n++) {
            File candidate = new File(parent, base + " (" + n + ")" + ext);
            if (!candidate.exists() && !new File(candidate.getPath() + ".part").exists()) return candidate;
        }
        throw new IOException("no free name for " + rel);
    }

    /**
     * Streams in to dest through dest.part (a crash or cancel never leaves a half file under the real name).
     * expected is the size the provider announced (-1 unknown); progress is reported about every 512 KiB / 250 ms.
     * Returns the number of bytes copied.
     */
    public static long copy(InputStream in, File dest, long expected, Progress progress) throws IOException {
        File part = new File(dest.getPath() + ".part");
        long done = 0;
        boolean ok = false;
        try (OutputStream out = Files.newOutputStream(part.toPath())) {
            byte[] buffer = new byte[256 * 1024];
            long lastReport = 0;
            long lastBytes = 0;
            int n;
            while ((n = in.read(buffer)) >= 0) {
                if (n == 0) continue;
                out.write(buffer, 0, n);
                done += n;
                long now = System.nanoTime();
                if (progress != null && (done - lastBytes >= 512 * 1024 || now - lastReport >= 250_000_000L)) {
                    lastReport = now;
                    lastBytes = done;
                    if (!progress.onBytes(done, expected)) throw new CancelledException();
                }
            }
            out.flush();
            if (progress != null && !progress.onBytes(done, expected < 0 ? done : expected)) throw new CancelledException();
            ok = true;
        } finally {
            if (!ok) {
                try {
                    Files.deleteIfExists(part.toPath());
                } catch (IOException ignored) {
                    // the staging directory is removed as a whole afterwards
                }
            }
        }
        Files.move(part.toPath(), dest.toPath(), StandardCopyOption.REPLACE_EXISTING);
        return done;
    }

    public static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        File[] children = Files.isSymbolicLink(file.toPath()) ? null : file.listFiles();
        if (children != null) for (File c : children) deleteTree(c);
        // best effort: a leftover is removed by the next sweep
        file.delete();
    }

    /** Removes staging directories older than maxAgeMs (leftovers of a killed app); returns how many. */
    public static int sweep(File root, long maxAgeMs) {
        File[] dirs = new File(root, "imports").listFiles();
        if (dirs == null) return 0;
        int removed = 0;
        long now = System.currentTimeMillis();
        for (File d : dirs) {
            if (now - d.lastModified() >= maxAgeMs) {
                deleteTree(d);
                removed++;
            }
        }
        return removed;
    }
}
