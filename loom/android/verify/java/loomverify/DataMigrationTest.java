package loomverify;

import com.chatadhd.android.DataMigration;
import com.chatadhd.android.DataMigration.Result;

import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

/** F2: copy -> verify sha256 per file -> (caller switches) -> old copy untouched. */
public final class DataMigrationTest {
    static int fails = 0;
    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) { fails++; System.out.println("FAIL " + n + ": " + a + " != " + b); }
    }

    public static void main(String[] args) throws Exception {
        File root = Files.createTempDirectory("loom-mig").toFile();
        try {
            File src = new File(root, "shared/ChatADHD");
            make(src, ".chatadhd_data", "ChatADHD data directory\n");
            make(src, "config.json", "{\"theme\":\"dark\"}\n");
            make(src, "secrets.json", "{\"api_key\":\"sk-test\"}\n");
            make(src, "chatadhd.db", "SQLite format 3\u0000 pretend database");
            make(src, "attachments/a/b/photo.bin", "");  // empty file
            byte[] big = new byte[3 * 1024 * 1024 + 17];
            new Random(7).nextBytes(big);
            Files.createDirectories(new File(src, "blobs/ab/cd").toPath());
            Files.write(new File(src, "blobs/ab/cd/blob").toPath(), big);
            new File(src, "exports").mkdirs();   // empty directory must survive too
            new File(src, "logs").mkdirs();
            Files.createSymbolicLink(new File(src, "link").toPath(), new File(src, "config.json").toPath());
            List<String> before = DataMigration.listFiles(src);
            File secrets = new File(src, "secrets.json");
            Files.setPosixFilePermissions(secrets.toPath(), java.nio.file.attribute.PosixFilePermissions.fromString("rw-r--r--"));

            // --- happy path ---
            File dst = new File(root, "private/files/ChatADHD");
            List<String> order = new ArrayList<>();
            Result r = DataMigration.copyAndVerify(src, dst, (i, n, rel) -> { order.add(rel); return true; });
            eq(r.ok, true, "copy ok (" + r.error + " " + r.detail + ")");
            eq(r.files, before.size(), "file count");
            eq(r.skippedLinks, 1, "symlink skipped and reported");
            eq(order.get(order.size() - 1), ".chatadhd_data", "sentinel copied last");
            eq(DataMigration.listFiles(dst), before, "same file list");
            for (String rel : before) eq(DataMigration.sha256(new File(dst, rel)), DataMigration.sha256(new File(src, rel)), "sha256 " + rel);
            eq(new File(dst, "exports").isDirectory(), true, "empty dir exported");
            eq(new File(dst, "logs").isDirectory(), true, "empty dir logs");
            eq(new File(dst, "chatadhd.db.part").exists(), false, "no .part left");
            eq(java.nio.file.Files.getPosixFilePermissions(new File(dst, "secrets.json").toPath()),
               java.nio.file.attribute.PosixFilePermissions.fromString("rw-------"), "secrets.json owner-only in the destination");
            // the SOURCE is untouched
            eq(DataMigration.listFiles(src), before, "source file list unchanged");
            eq(new String(Files.readAllBytes(new File(src, "secrets.json").toPath()), StandardCharsets.UTF_8),
               "{\"api_key\":\"sk-test\"}\n", "source secrets unchanged");

            // --- refuses to overwrite: destination with content ---
            Result again = DataMigration.copyAndVerify(src, dst, null);
            eq(again.error, "destination_not_empty", "never overwrite existing data");
            eq(new String(Files.readAllBytes(new File(dst, "config.json").toPath()), StandardCharsets.UTF_8),
               "{\"theme\":\"dark\"}\n", "existing destination file untouched after refusal");

            // --- nested / same / missing / not a directory ---
            eq(DataMigration.copyAndVerify(src, src, null).error, "nested", "same dir");
            eq(DataMigration.copyAndVerify(src, new File(src, "inside"), null).error, "nested", "dst inside src");
            eq(DataMigration.copyAndVerify(new File(src, "blobs"), src, null).error, "nested", "dst is parent of src");
            eq(DataMigration.copyAndVerify(new File(root, "nope"), new File(root, "d2"), null).error, "source_missing", "missing source");
            File notDir = new File(root, "file.txt"); Files.write(notDir.toPath(), new byte[]{1});
            eq(DataMigration.copyAndVerify(src, notDir, null).error, "destination_invalid", "destination is a file");
            eq(new File(root, "d2").exists(), false, "failed precondition creates nothing");

            // --- existing EMPTY destination dir is fine and is kept on failure ---
            File emptyDst = new File(root, "empty-dst"); emptyDst.mkdirs();
            Result ok2 = DataMigration.copyAndVerify(src, emptyDst, null);
            eq(ok2.ok, true, "empty existing destination ok");

            // --- cancel: destination is cleaned up, source untouched ---
            File cdst = new File(root, "cancelled/ChatADHD");
            Result c = DataMigration.copyAndVerify(src, cdst, (i, n, rel) -> i < 3);
            eq(c.error, "cancelled", "cancel reported");
            eq(cdst.exists(), false, "cancel removes the destination it created");
            File cdst2 = new File(root, "cancelled2"); cdst2.mkdirs();
            c = DataMigration.copyAndVerify(src, cdst2, (i, n, rel) -> i < 3);
            eq(c.error, "cancelled", "cancel with pre-existing empty dir");
            eq(cdst2.isDirectory() && cdst2.list().length == 0, true, "pre-existing empty dir kept, emptied");
            eq(DataMigration.listFiles(src), before, "source unchanged after cancel");

            // --- failure in the middle (unreadable source file) is rolled back ---
            // (root can read anything, so this case only runs for an ordinary user)
            if (!"root".equals(System.getProperty("user.name"))) {
                File fsrc = new File(root, "fsrc"); make(fsrc, ".chatadhd_data", "x"); make(fsrc, "a.txt", "aaa"); make(fsrc, "z.txt", "zzz");
                File z = new File(fsrc, "z.txt"); z.setReadable(false, false);
                File fdst = new File(root, "fdst");
                Result f = DataMigration.copyAndVerify(fsrc, fdst, null);
                eq(f.error, "copy_failed", "unreadable file fails the copy");
                eq(fdst.exists(), false, "failed copy leaves no destination");
                z.setReadable(true, true);
            }

            // --- sha256 of a known vector ---
            File v = new File(root, "vec"); Files.write(v.toPath(), "abc".getBytes(StandardCharsets.US_ASCII));
            eq(DataMigration.sha256(v), "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "sha256(abc)");

            // --- deleteTree: removes the tree, never follows a link out of it ---
            File outside = new File(root, "outside"); make(outside, "keep.txt", "keep");
            File victim = new File(root, "victim"); make(victim, "x/y.txt", "y");
            Files.createSymbolicLink(new File(victim, "escape").toPath(), outside.toPath());
            eq(DataMigration.deleteTree(victim), true, "deleteTree ok");
            eq(victim.exists(), false, "tree gone");
            eq(new File(outside, "keep.txt").isFile(), true, "link target outside is untouched");
            eq(DataMigration.deleteTree(new File(root, "never-existed")), true, "deleting a missing dir is fine");
        } finally {
            DataLocationTest.deleteAll(root);
        }
        if (fails > 0) System.exit(1);
        System.out.println("DataMigrationTest OK");
    }

    static void make(File dir, String rel, String content) throws Exception {
        File f = new File(dir, rel);
        f.getParentFile().mkdirs();
        Files.write(f.toPath(), content.getBytes(StandardCharsets.UTF_8));
    }
}
