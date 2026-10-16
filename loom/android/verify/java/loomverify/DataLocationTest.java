package loomverify;

import com.chatadhd.android.DataLocationPolicy;
import com.chatadhd.android.DataLocationPolicy.Choice;
import com.chatadhd.android.DataLocationPolicy.Exposure;

import java.io.File;
import java.nio.file.Files;

/** F2: where the data directory lives, and when the visible warning is required. */
public final class DataLocationTest {
    static int fails = 0;
    static void eq(Object a, Object b, String n) {
        if (a == null ? b != null : !a.equals(b)) { fails++; System.out.println("FAIL " + n + ": " + a + " != " + b); }
    }

    public static void main(String[] args) throws Exception {
        // --- choose(): new installs private, old installs keep shared, saved choice always wins ---
        Choice c = DataLocationPolicy.choose(null, false, false);
        eq(c.kind, "private", "new install -> private");
        eq(c.explicit, false, "new install is derived, not explicit");
        c = DataLocationPolicy.choose(null, true, false);
        eq(c.kind, "shared", "existing shared data stays shared");
        eq(c.explicit, false, "legacy shared is not an explicit choice");
        c = DataLocationPolicy.choose(null, true, true);
        eq(c.kind, "private", "data in both -> private (the migration target is the live one)");
        c = DataLocationPolicy.choose("shared", false, false);
        eq(c.kind, "shared", "saved shared wins even with no data");
        eq(c.explicit, true, "saved choice is explicit");
        c = DataLocationPolicy.choose("private", true, false);
        eq(c.kind, "private", "saved private wins over leftover shared data");
        c = DataLocationPolicy.choose("bogus", false, false);
        eq(c.kind, "private", "unknown saved kind is ignored");
        eq(c.explicit, false, "unknown saved kind is not explicit");

        // --- custom path ---
        eq(DataLocationPolicy.validateCustomPath(null), "empty", "null path");
        eq(DataLocationPolicy.validateCustomPath("  "), "empty", "blank path");
        eq(DataLocationPolicy.validateCustomPath("relative/dir"), "not_absolute", "relative path");
        eq(DataLocationPolicy.validateCustomPath(" /sdcard/x "), null, "absolute path ok");

        // --- resolve() ---
        File priv = new File("/p"), ext = new File("/e"), shared = new File("/s");
        eq(DataLocationPolicy.resolve("private", null, priv, ext, shared), priv, "resolve private");
        eq(DataLocationPolicy.resolve("app_external", null, priv, ext, shared), ext, "resolve app_external");
        eq(DataLocationPolicy.resolve("app_external", null, priv, null, shared), null, "no external volume -> null");
        eq(DataLocationPolicy.resolve("shared", null, priv, ext, shared), shared, "resolve shared");
        eq(DataLocationPolicy.resolve("custom", " /x/y ", priv, ext, shared), new File("/x/y"), "resolve custom trims");
        eq(DataLocationPolicy.resolve("custom", "x", priv, ext, shared), null, "custom relative -> null");
        eq(DataLocationPolicy.resolve("nope", null, priv, ext, shared), null, "unknown kind -> null");

        // --- classify(): real directories, ".." and symlinks cannot disguise exposure ---
        File root = Files.createTempDirectory("loom-dl").toFile();
        try {
            File files = new File(root, "data/user/0/pkg/files"); files.mkdirs();
            File extFiles = new File(root, "sdcard/Android/data/pkg/files"); extFiles.mkdirs();
            File docs = new File(root, "sdcard/Documents/ChatADHD"); docs.mkdirs();
            eq(DataLocationPolicy.classify(new File(files, "ChatADHD"), files, extFiles), Exposure.PRIVATE, "below filesDir");
            eq(DataLocationPolicy.classify(files, files, extFiles), Exposure.PRIVATE, "filesDir itself");
            eq(DataLocationPolicy.classify(new File(extFiles, "ChatADHD"), files, extFiles), Exposure.APP_EXTERNAL, "below external files dir");
            eq(DataLocationPolicy.classify(docs, files, extFiles), Exposure.SHARED, "Documents is shared");
            eq(DataLocationPolicy.classify(new File(files, "../../../../../sdcard/Documents/ChatADHD"), files, extFiles),
                Exposure.SHARED, ".. cannot hide a shared dir");
            File lookalike = new File(root, "data/user/0/pkg/files-evil"); lookalike.mkdirs();
            eq(DataLocationPolicy.classify(lookalike, files, extFiles), Exposure.SHARED, "prefix lookalike is not private");
            File link = new File(files, "link");
            Files.createSymbolicLink(link.toPath(), docs.toPath());
            eq(DataLocationPolicy.classify(link, files, extFiles), Exposure.SHARED, "symlink out of the sandbox is shared");
            eq(DataLocationPolicy.classify(docs, files, null), Exposure.SHARED, "no external volume");
            eq(DataLocationPolicy.needsWarning(Exposure.SHARED), true, "shared needs the warning");
            eq(DataLocationPolicy.needsWarning(Exposure.PRIVATE), false, "private: no warning");
            eq(DataLocationPolicy.needsWarning(Exposure.APP_EXTERNAL), false, "app external: no warning");
            eq(DataLocationPolicy.needsAllFilesAccess(Exposure.SHARED), true, "shared needs all-files access");
            eq(DataLocationPolicy.needsAllFilesAccess(Exposure.PRIVATE), false, "private needs no permission");

            // --- hasData / isSafeToDeleteOldCopy ---
            eq(DataLocationPolicy.hasData(docs), false, "no sentinel, no data");
            Files.write(new File(docs, ".chatadhd_data").toPath(), "x".getBytes());
            eq(DataLocationPolicy.hasData(docs), true, "sentinel -> data");
            File live = new File(files, "ChatADHD"); live.mkdirs();
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(docs, live), true, "old copy with sentinel, distinct from live");
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(live, live), false, "never the live dir");
            File noSentinel = new File(root, "sdcard/Download"); noSentinel.mkdirs();
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(noSentinel, live), false, "not a data dir (no sentinel)");
            Files.write(new File(files, ".chatadhd_data").toPath(), "x".getBytes());
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(files, live), false, "ancestor of live is refused");
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(null, live), false, "null old");
            eq(DataLocationPolicy.isSame(live, new File(files, "ChatADHD/../ChatADHD")), true, "isSame through ..");
            eq(DataLocationPolicy.isSame(live, files), false, "isSame: parent is not the same");
            eq(DataLocationPolicy.sameOrNested(live, files), true, "sameOrNested: parent counts");
        } finally {
            deleteAll(root);
        }

        // --- R35: default private, one-time migration decision, notice only for an explicit shared choice ---
        Choice legacy = DataLocationPolicy.choose(null, true, false);
        eq(DataLocationPolicy.shouldAutoMigrate(legacy, true, false, true, false, true), true, "legacy shared -> migrate");
        eq(DataLocationPolicy.shouldAutoMigrate(legacy, true, false, false, false, true), false, "setting off -> no migrate");
        eq(DataLocationPolicy.shouldAutoMigrate(legacy, true, false, true, true, true), false, "already tried -> no migrate");
        eq(DataLocationPolicy.shouldAutoMigrate(legacy, true, false, true, false, false), false, "shared unreadable -> no migrate");
        eq(DataLocationPolicy.shouldAutoMigrate(DataLocationPolicy.choose("shared", true, false), true, false, true, false, true), false, "explicit shared is never moved");
        eq(DataLocationPolicy.shouldAutoMigrate(DataLocationPolicy.choose(null, false, false), false, false, true, false, true), false, "new install: nothing to move");
        eq(DataLocationPolicy.shouldAutoMigrate(DataLocationPolicy.choose(null, true, true), true, true, true, false, true), false, "private already has data");
        eq(DataLocationPolicy.shouldAutoMigrate(null, true, false, true, false, true), false, "null choice");
        Choice explicitShared = DataLocationPolicy.choose("shared", true, false);
        eq(DataLocationPolicy.showSharedNoticeOnce(explicitShared, Exposure.SHARED, false), true, "explicit shared: notice once");
        eq(DataLocationPolicy.showSharedNoticeOnce(explicitShared, Exposure.SHARED, true), false, "notice not repeated");
        eq(DataLocationPolicy.showSharedNoticeOnce(legacy, Exposure.SHARED, false), false, "derived shared: no notice");
        eq(DataLocationPolicy.showSharedNoticeOnce(DataLocationPolicy.choose(null, false, false), Exposure.PRIVATE, false), false, "default private: no notice");
        eq(DataLocationPolicy.showSharedNoticeOnce(DataLocationPolicy.choose("private", false, false), Exposure.PRIVATE, false), false, "explicit private: no notice");
        eq(DataLocationPolicy.showSharedNoticeOnce(DataLocationPolicy.choose("app_external", false, false), Exposure.APP_EXTERNAL, false), false, "app external: no notice");

        File mroot = Files.createTempDirectory("loom-mig").toFile();
        try {
            File sh = new File(mroot, "sdcard/Documents/ChatADHD"); sh.mkdirs();
            Files.write(new File(sh, ".chatadhd_data").toPath(), "x".getBytes());
            Files.write(new File(sh, "secrets.json").toPath(), "{\"k\":1}".getBytes());
            File pv = new File(mroot, "data/files/ChatADHD");
            eq(DataLocationPolicy.hasData(pv), false, "private empty before");
            com.chatadhd.android.DataMigration.Result r = com.chatadhd.android.DataMigration.copyAndVerify(sh, pv, (i, n, rel) -> true);
            eq(r.ok, true, "migration ok");
            eq(DataLocationPolicy.hasData(pv), true, "sentinel copied to private");
            eq(DataLocationPolicy.hasData(sh), true, "old dir kept with its sentinel");
            eq(new File(sh, "secrets.json").isFile(), true, "old data not deleted");
            eq(DataLocationPolicy.isSafeToDeleteOldCopy(sh, pv), true, "old copy deletable by explicit action");
            eq(DataLocationPolicy.shouldAutoMigrate(DataLocationPolicy.choose(null, true, true), true, true, true, false, true), false, "no second migration");
            File pv2 = new File(mroot, "data/files2/ChatADHD"); pv2.mkdirs();
            Files.write(new File(pv2, "stray").toPath(), "x".getBytes());
            eq(com.chatadhd.android.DataMigration.copyAndVerify(sh, pv2, (i, n, rel) -> true).ok, false, "non-empty destination refused");
            eq(new File(pv2, "stray").isFile(), true, "refused destination untouched");
        } finally {
            deleteAll(mroot);
        }
        if (fails > 0) System.exit(1);
        System.out.println("DataLocationTest OK");
    }

    static void deleteAll(File f) {
        File[] kids = f.listFiles();
        if (kids != null && !Files.isSymbolicLink(f.toPath())) for (File k : kids) deleteAll(k);
        f.delete();
    }
}
