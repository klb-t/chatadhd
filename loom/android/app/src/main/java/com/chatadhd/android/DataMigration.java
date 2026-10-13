package com.chatadhd.android;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.FileVisitResult;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.SimpleFileVisitor;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.BasicFileAttributes;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

/**
 * Pure-Java (no android.* imports, so it is testable with plain javac; see verify/)
 * copy of a whole data directory with per-file SHA-256 verification.
 *
 * The contract the UI relies on:
 *  - the SOURCE IS NEVER MODIFIED OR DELETED here (deleting the old copy is a separate, explicit
 *    user action: {@link #deleteTree});
 *  - the destination must be absent or empty, so nothing already there can be overwritten;
 *  - every file is written as {@code name.part}, fsync'ed, re-read from disk and compared with the
 *    digest of the bytes read from the source, and only then renamed to {@code name};
 *  - the {@code .chatadhd_data} sentinel is copied LAST, so a half-copied destination is never
 *    mistaken for a data directory (the core and DataLocationPolicy.hasData look for it);
 *  - on any failure (or cancel) everything this call created in the destination is removed again.
 *
 * The caller must have shut the Loom core down first, so SQLite has checkpointed its WAL and no
 * file changes while it is read.
 */
public final class DataMigration {
    private DataMigration() {}

    /** Called before each file; return false to cancel (the destination is cleaned up). */
    public interface Progress {
        boolean onFile(int index, int total, String relPath);
    }

    public static final class Result {
        public final boolean ok;
        /** null on success; else: source_missing, nested, destination_invalid, destination_not_empty,
         *  copy_failed, verify_failed, source_changed, cancelled. */
        public final String error;
        /** The relative path (or OS message) the error is about; for display/logging. */
        public final String detail;
        public final int files;
        public final long bytes;
        /** Symbolic links in the source that were not copied. */
        public final int skippedLinks;

        Result(boolean ok, String error, String detail, int files, long bytes, int skippedLinks) {
            this.ok = ok; this.error = error; this.detail = detail;
            this.files = files; this.bytes = bytes; this.skippedLinks = skippedLinks;
        }
        static Result failure(String error, String detail) { return new Result(false, error, detail, 0, 0, 0); }
    }

    private static final String SENTINEL = DataLocationPolicy.SENTINEL;

    /** Copy {@code src} into {@code dst} and verify. See the class comment for the guarantees. */
    public static Result copyAndVerify(File src, File dst, Progress progress) {
        if (src == null || !src.isDirectory()) return Result.failure("source_missing", String.valueOf(src));
        if (dst == null) return Result.failure("destination_invalid", "null");
        if (DataLocationPolicy.sameOrNested(src, dst)) return Result.failure("nested", dst.getPath());

        boolean dstExisted = dst.exists();
        if (dstExisted) {
            if (!dst.isDirectory()) return Result.failure("destination_invalid", dst.getPath());
            String[] kids = dst.list();
            if (kids == null) return Result.failure("destination_invalid", dst.getPath());
            if (kids.length > 0) return Result.failure("destination_not_empty", dst.getPath());
        } else if (!dst.mkdirs()) {
            return Result.failure("destination_invalid", dst.getPath());
        }

        List<String> dirs = new ArrayList<>();
        List<String> files = new ArrayList<>();
        int[] links = {0};
        try {
            scan(src.toPath(), dirs, files, links);
        } catch (IOException e) {
            undo(dst, dstExisted);
            return Result.failure("copy_failed", "scan: " + e.getMessage());
        }
        Collections.sort(dirs);
        Collections.sort(files);
        // The sentinel goes last: a partial destination must not look like a data directory.
        boolean hadSentinel = files.remove(SENTINEL);
        if (hadSentinel) files.add(SENTINEL);

        try {
            for (String d : dirs) {
                File target = new File(dst, d);
                if (!target.isDirectory() && !target.mkdirs()) throw new IOException("cannot create directory " + d);
            }
        } catch (IOException e) {
            undo(dst, dstExisted);
            return Result.failure("copy_failed", e.getMessage());
        }

        long total = 0;
        for (int i = 0; i < files.size(); i++) {
            String rel = files.get(i);
            if (progress != null && !progress.onFile(i + 1, files.size(), rel)) {
                undo(dst, dstExisted);
                return Result.failure("cancelled", rel);
            }
            try {
                total += copyOne(new File(src, rel), new File(dst, rel));
            } catch (VerifyException e) {
                undo(dst, dstExisted);
                return Result.failure(e.key, rel);
            } catch (IOException e) {
                undo(dst, dstExisted);
                return Result.failure("copy_failed", rel + ": " + e.getMessage());
            }
        }
        return new Result(true, null, null, files.size(), total, links[0]);
    }

    private static final class VerifyException extends IOException {
        private static final long serialVersionUID = 1L;
        final String key;
        VerifyException(String key, String msg) { super(msg); this.key = key; }
    }

    private static long copyOne(File from, File to) throws IOException {
        File parent = to.getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs()) throw new IOException("cannot create " + parent);
        File part = new File(to.getPath() + ".part");
        MessageDigest md = newDigest();
        long n = 0;
        try (InputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(part)) {
            byte[] buf = new byte[64 * 1024];
            int r;
            while ((r = in.read(buf)) != -1) {
                md.update(buf, 0, r);
                out.write(buf, 0, r);
                n += r;
            }
            out.flush();
            out.getFD().sync();
        }
        if (from.length() != n) throw new VerifyException("source_changed", "size changed while copying");
        String want = hex(md.digest());
        String got = sha256(part);
        if (!want.equals(got)) throw new VerifyException("verify_failed", "sha256 mismatch");
        if (part.length() != n) throw new VerifyException("verify_failed", "size mismatch");
        restrictIfSecret(part, to.getName());
        Files.move(part.toPath(), to.toPath(), StandardCopyOption.ATOMIC_MOVE);
        // Best effort: keeps "last modified" meaningful for file managers and sync tools.
        to.setLastModified(from.lastModified());
        return n;
    }

    /** secrets.json (and its Keystore-sealed twin) stay owner-only wherever they land. */
    private static void restrictIfSecret(File f, String finalName) {
        if (!finalName.startsWith("secrets.json")) return;
        f.setReadable(false, false);
        f.setWritable(false, false);
        f.setReadable(true, true);
        f.setWritable(true, true);
    }

    private static void scan(Path root, List<String> dirs, List<String> files, int[] links) throws IOException {
        Files.walkFileTree(root, new SimpleFileVisitor<Path>() {
            @Override public FileVisitResult preVisitDirectory(Path d, BasicFileAttributes a) {
                if (!d.equals(root)) dirs.add(rel(root, d));
                return FileVisitResult.CONTINUE;
            }
            @Override public FileVisitResult visitFile(Path f, BasicFileAttributes a) {
                if (a.isSymbolicLink() || !a.isRegularFile()) { links[0]++; return FileVisitResult.CONTINUE; }
                files.add(rel(root, f));
                return FileVisitResult.CONTINUE;
            }
        });
    }

    private static String rel(Path root, Path p) {
        return root.relativize(p).toString().replace(File.separatorChar, '/');
    }

    /** Relative paths ('/'-separated, sorted) of the regular files below {@code root}. */
    public static List<String> listFiles(File root) throws IOException {
        List<String> dirs = new ArrayList<>(), files = new ArrayList<>();
        scan(root.toPath(), dirs, files, new int[1]);
        Collections.sort(files);
        return files;
    }

    /** Lower-case hex SHA-256 of a file's bytes. */
    public static String sha256(File f) throws IOException {
        MessageDigest md = newDigest();
        try (InputStream in = new FileInputStream(f)) {
            byte[] buf = new byte[64 * 1024];
            int r;
            while ((r = in.read(buf)) != -1) md.update(buf, 0, r);
        }
        return hex(md.digest());
    }

    /**
     * Deletes a directory tree without following symbolic links. Used only for the explicit
     * "delete the old copy" action (callers gate it with DataLocationPolicy.isSafeToDeleteOldCopy).
     * Returns false if anything could not be removed.
     */
    public static boolean deleteTree(File dir) {
        if (dir == null) return true;
        Path p = dir.toPath();
        if (!Files.exists(p, LinkOption.NOFOLLOW_LINKS)) return true;
        try {
            Files.walkFileTree(p, new SimpleFileVisitor<Path>() {
                @Override public FileVisitResult visitFile(Path f, BasicFileAttributes a) throws IOException {
                    Files.delete(f);
                    return FileVisitResult.CONTINUE;
                }
                @Override public FileVisitResult postVisitDirectory(Path d, IOException e) throws IOException {
                    if (e != null) throw e;
                    Files.delete(d);
                    return FileVisitResult.CONTINUE;
                }
            });
            return true;
        } catch (IOException e) {
            return false;
        }
    }

    /** Removes what this call created: the destination was empty or absent, so "everything in it". */
    private static void undo(File dst, boolean dstExisted) {
        if (dstExisted) {
            File[] kids = dst.listFiles();
            if (kids != null) for (File k : kids) deleteTree(k);
        } else {
            deleteTree(dst);
        }
    }

    private static MessageDigest newDigest() {
        try { return MessageDigest.getInstance("SHA-256"); }
        catch (NoSuchAlgorithmException e) { throw new IllegalStateException("SHA-256 missing", e); }
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder(b.length * 2);
        for (byte x : b) s.append(String.format(Locale.ROOT, "%02x", x & 0xff));
        return s.toString();
    }
}
