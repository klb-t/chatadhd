package loomverify;

import com.chatadhd.android.ImportStaging;

import java.io.ByteArrayInputStream;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/** The pure rules of the import from the system picker: safe names, staging copy (any size, progress, cancel), clean-up. */
public final class ImportStagingTest {
    static int fails = 0;

    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) {
            fails++;
            System.out.println("FAIL " + n + ": [" + a + "] != [" + b + "]");
        }
    }

    public static void main(String[] args) throws Exception {
        // --- names: a display name is data from another app ---
        eq(ImportStaging.safeName("conversations.json", "x"), "conversations.json", "plain name");
        eq(ImportStaging.safeName("../../etc/passwd", "x"), "passwd", "path parts dropped");
        eq(ImportStaging.safeName("a\\b\\c.zip", "x"), "c.zip", "windows separators");
        eq(ImportStaging.safeName("..", "fallback"), "fallback", "dot dot");
        eq(ImportStaging.safeName("", "fallback"), "fallback", "empty");
        eq(ImportStaging.safeName(null, "fallback"), "fallback", "null");
        eq(ImportStaging.safeName(".hidden.json", "x"), "hidden.json", "not hidden");
        eq(ImportStaging.safeName("a:b*c?.json", "x"), "a_b_c_.json", "forbidden characters");
        eq(ImportStaging.safeName("a\u0000b\n.zip", "x"), "a_b_.zip", "control characters");
        eq(ImportStaging.safeName("Pobrane żółć (1).zip", "x"), "Pobrane żółć (1).zip", "unicode and parentheses kept");
        StringBuilder longName = new StringBuilder();
        for (int i = 0; i < 300; i++) longName.append('a');
        String cut = ImportStaging.safeName(longName + ".json", "x");
        eq(cut.length(), 120, "long name cut to 120");
        eq(cut.endsWith(".json"), true, "extension survives the cut");
        eq(ImportStaging.safeRelative("a/../b//c.json", "x"), "a/b/c.json", "relative path cleaned");
        eq(ImportStaging.safeRelative("../..", "x"), "x", "only dots");
        eq(ImportStaging.safeRelative("/abs/olute.txt", "x"), "abs/olute.txt", "absolute made relative");

        // --- what is taken from a folder by default ---
        for (String ok : new String[] {"conversations.json", "export.ZIP", "chat.html", "a.mhtml", "n.md", "n.txt", "x.db", "s.PNG", "l.jsonl", "q.ndjson"})
            eq(ImportStaging.looksImportable(ok), true, "importable " + ok);
        for (String no : new String[] {"video.mp4", "noext", ".json", "archive.7z", "x.json.bak", "trailing."})
            eq(ImportStaging.looksImportable(no), false, "not importable " + no);
        eq(ImportStaging.extensionOf("a/b/c.TAR.gz"), "gz", "extension of a path");

        // --- extension from the MIME type when the name has none the importer knows ---
        eq(ImportStaging.withExtensionForMime("conversations", "application/json"), "conversations.json", "json mime");
        eq(ImportStaging.withExtensionForMime("export", "application/zip"), "export.zip", "zip mime");
        eq(ImportStaging.withExtensionForMime("export", "application/zip; charset=binary"), "export.zip", "mime parameters");
        eq(ImportStaging.withExtensionForMime("export.zip", "application/json"), "export.zip", "a known extension stays");
        eq(ImportStaging.withExtensionForMime("blob", "application/octet-stream"), "blob", "unknown mime: unchanged (the kernel sniffs)");
        eq(ImportStaging.withExtensionForMime("blob", null), "blob", "no mime");

        File root = Files.createTempDirectory("loom-staging").toFile();
        try {
            // --- directories and unique names ---
            File dir = ImportStaging.reserveDir(root);
            eq(dir.isDirectory(), true, "staging dir created");
            eq(dir.getParentFile().getName(), "imports", "under imports/");
            eq(ImportStaging.reserveDir(root).equals(dir), false, "every run its own dir");
            File a = ImportStaging.destFor(dir, "conv.json");
            Files.write(a.toPath(), new byte[] {1});
            File b = ImportStaging.destFor(dir, "conv.json");
            eq(b.getName(), "conv (2).json", "name taken -> numbered");
            Files.write(b.toPath(), new byte[] {1});
            eq(ImportStaging.destFor(dir, "conv.json").getName(), "conv (3).json", "third copy");
            File nested = ImportStaging.destFor(dir, "folder/sub/../x.json");
            eq(nested.getParentFile().getParentFile().getName() + "/" + nested.getParentFile().getName(), "folder/sub", "tree structure kept, dot dot dropped");
            eq(nested.getCanonicalPath().startsWith(dir.getCanonicalPath() + File.separator), true, "inside the staging dir");

            // --- streaming copy: bigger than the buffer, exact bytes, progress ---
            byte[] big = new byte[5 * 1024 * 1024 + 123];
            new Random(11).nextBytes(big);
            File dest = ImportStaging.destFor(dir, "big.zip");
            List<Long> seen = new ArrayList<>();
            long copied = ImportStaging.copy(new ByteArrayInputStream(big), dest, big.length, (done, total) -> {
                seen.add(done);
                eq(total, (long) big.length, "total is the announced size");
                return true;
            });
            eq(copied, (long) big.length, "bytes copied");
            eq(java.util.Arrays.equals(Files.readAllBytes(dest.toPath()), big), true, "content identical");
            eq(new File(dest.getPath() + ".part").exists(), false, "no .part left");
            eq(seen.size() >= 3, true, "progress reported several times: " + seen.size());
            eq(seen.get(seen.size() - 1), (long) big.length, "last report is the whole file");
            for (int i = 1; i < seen.size(); i++) if (seen.get(i) < seen.get(i - 1)) eq("decreasing", "increasing", "progress never goes back");

            // --- unknown size, no progress sink, empty document ---
            File unknown = ImportStaging.destFor(dir, "u.json");
            eq(ImportStaging.copy(new ByteArrayInputStream("{\"a\":1}".getBytes(StandardCharsets.UTF_8)), unknown, -1, null), 7L, "unknown size, no sink");
            File empty = ImportStaging.destFor(dir, "empty.txt");
            eq(ImportStaging.copy(new ByteArrayInputStream(new byte[0]), empty, 0, null), 0L, "empty document");
            eq(empty.length(), 0L, "empty file exists");

            // --- cancel: nothing under the real name, no .part ---
            File cancelled = ImportStaging.destFor(dir, "c.zip");
            boolean thrown = false;
            try {
                ImportStaging.copy(new ByteArrayInputStream(big), cancelled, big.length, (done, total) -> false);
            } catch (ImportStaging.CancelledException e) {
                thrown = true;
            }
            eq(thrown, true, "cancel raises CancelledException");
            eq(cancelled.exists(), false, "cancelled copy not left under its name");
            eq(new File(cancelled.getPath() + ".part").exists(), false, "cancelled copy leaves no .part");

            // --- a provider that fails half way ---
            File broken = ImportStaging.destFor(dir, "broken.zip");
            InputStream failing = new InputStream() {
                int n = 0;
                @Override public int read() throws IOException { throw new IOException("provider died"); }
                @Override public int read(byte[] buf) throws IOException {
                    if (n++ < 2) return buf.length > 1000 ? 1000 : buf.length;
                    throw new IOException("provider died");
                }
            };
            String message = null;
            try {
                ImportStaging.copy(failing, broken, 5000, null);
            } catch (IOException e) {
                message = e.getMessage();
            }
            eq(message, "provider died", "provider error propagates");
            eq(broken.exists() || new File(broken.getPath() + ".part").exists(), false, "failed copy leaves nothing");

            // --- clean-up ---
            ImportStaging.deleteTree(dir);
            eq(dir.exists(), false, "deleteTree removes the run");
            File old = ImportStaging.reserveDir(root);
            old.setLastModified(System.currentTimeMillis() - 10L * 3600 * 1000);
            File fresh = ImportStaging.reserveDir(root);
            eq(ImportStaging.sweep(root, 6L * 3600 * 1000) >= 1, true, "sweep removes old runs");
            eq(old.exists(), false, "old run gone");
            eq(fresh.exists(), true, "fresh run kept");
        } finally {
            ImportStaging.deleteTree(root);
        }
        if (fails > 0) {
            System.out.println(fails + " failure(s)");
            System.exit(1);
        }
        System.out.println("ImportStagingTest OK");
    }
}
