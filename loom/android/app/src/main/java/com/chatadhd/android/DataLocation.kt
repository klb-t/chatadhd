package com.chatadhd.android

import android.content.Context
import android.os.Build
import android.os.Environment
import java.io.File

/**
 * The data-location setting (SharedPreferences "storage": per device, not inside the data
 * directory it describes) plus the actions on it. The rules themselves are in the pure-Java
 * [DataLocationPolicy] / [DataMigration] / [SecretsVaultCore], which have host tests.
 *
 * Nothing here decides for the user: a saved kind always wins, "shared" is always selectable, and
 * nothing is moved or deleted without an explicit action in [StorageActivity].
 */
class DataLocation(private val ctx: Context) {
    private val p = ctx.getSharedPreferences("storage", Context.MODE_PRIVATE)

    val privateDir: File get() = File(ctx.filesDir, DataLocationPolicy.SUBDIR)
    val appExternalDir: File? get() = ctx.getExternalFilesDir(null)?.let { File(it, DataLocationPolicy.SUBDIR) }
    val sharedDir: File get() = File(SHARED_PATH)

    val savedKind: String? get() = p.getString("kind", null)
    var customPath: String
        get() = p.getString("custom_path", "") ?: ""
        set(v) { p.edit().putString("custom_path", v).apply() }
    /** Seal secrets.json with the Android Keystore (default on). */
    var secretsWrap: Boolean
        get() = p.getBoolean("secrets_wrap", true)
        set(v) { p.edit().putBoolean("secrets_wrap", v).apply() }

    /** One-time move of a legacy shared-storage dir to private storage at start-up (preset on). */
    var autoMigrate: Boolean
        get() = p.getBoolean("auto_migrate", true)
        set(v) { p.edit().putBoolean("auto_migrate", v).apply() }
    private var autoMigrateTried: Boolean
        get() = p.getBoolean("auto_migrate_tried", false)
        set(v) { p.edit().putBoolean("auto_migrate_tried", v).commit() }
    /** The one-time "data is in shared storage" notice was shown for the current choice. */
    var sharedNoticeShown: Boolean
        get() = p.getBoolean("shared_notice_shown", false)
        set(v) { p.edit().putBoolean("shared_notice_shown", v).commit() }

    private fun canReadShared(): Boolean =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()

    fun shouldAutoMigrate(): Boolean = DataLocationPolicy.shouldAutoMigrate(
        choice(), DataLocationPolicy.hasData(sharedDir), DataLocationPolicy.hasData(privateDir),
        autoMigrate, autoMigrateTried, canReadShared()
    )

    /**
     * Copy shared -> private, verify every file (sha256), and only then switch. The shared dir is left
     * untouched and remembered as an "old copy" (deletable later by the user in [StorageActivity]).
     * Attempted once; on failure the data stays where it was. Call from a background thread, before
     * the core is initialised.
     */
    fun runAutoMigration(): DataMigration.Result {
        autoMigrateTried = true
        val r = DataMigration.copyAndVerify(sharedDir, privateDir) { _, _, _ -> true }
        if (r.ok) switchTo(DataLocationPolicy.KIND_PRIVATE, null, sharedDir)
        return r
    }

    fun choice(): DataLocationPolicy.Choice = DataLocationPolicy.choose(
        savedKind, DataLocationPolicy.hasData(sharedDir), DataLocationPolicy.hasData(privateDir)
    )

    fun dirFor(kind: String, custom: String?): File? =
        DataLocationPolicy.resolve(kind, custom, privateDir, appExternalDir, sharedDir)

    /** The directory the app uses right now. */
    fun currentDir(): File {
        val c = choice()
        return dirFor(c.kind, customPath) ?: privateDir
    }

    fun exposure(dir: File): DataLocationPolicy.Exposure =
        DataLocationPolicy.classify(dir, ctx.filesDir, ctx.getExternalFilesDir(null))

    fun vault(dir: File): SecretsVaultCore = SecretsVaultCore(dir, secretsWrap, KeystoreKeys)

    /** Old copies kept after a move; only ones that may safely be deleted are returned. */
    val oldCopies: List<File>
        get() {
            val live = currentDir()
            return (p.getStringSet("old_copies", emptySet()) ?: emptySet())
                .map { File(it) }
                .filter { DataLocationPolicy.isSafeToDeleteOldCopy(it, live) }
                .sortedBy { it.path }
        }

    /** Save the choice (commit(), not apply(): a kill right after must not lose it). */
    fun switchTo(kind: String, custom: String?, oldCopy: File?) {
        val olds = HashSet(p.getStringSet("old_copies", emptySet()) ?: emptySet())
        if (oldCopy != null) olds.add(oldCopy.absolutePath)
        p.edit()
            .putString("kind", kind)
            .putString("custom_path", custom ?: "")
            .putStringSet("old_copies", olds)
            .putBoolean("shared_notice_shown", false)
            .commit()
    }

    /**
     * Copy the whole data directory to [to], verify every file (sha256), and only then switch the
     * setting. The old directory is left exactly as it was (and remembered as an "old copy").
     * Runs on the caller's thread: call it from a background thread.
     */
    fun migrate(kind: String, custom: String?, to: File, progress: DataMigration.Progress): DataMigration.Result {
        val from = currentDir()
        // Stop the core first: closing SQLite checkpoints the WAL, so every file is stable while copied.
        LoomNative.nativeShutdown()
        val r = DataMigration.copyAndVerify(from, to, progress)
        if (r.ok) switchTo(kind, custom, from)
        return r
    }

    /** Explicit user action. Refuses anything that is not a ChatADHD data dir distinct from the live one. */
    fun deleteOldCopy(dir: File): Boolean {
        if (!DataLocationPolicy.isSafeToDeleteOldCopy(dir, currentDir())) return false
        if (!DataMigration.deleteTree(dir)) return false
        val olds = HashSet(p.getStringSet("old_copies", emptySet()) ?: emptySet())
        olds.remove(dir.absolutePath)
        p.edit().putStringSet("old_copies", olds).commit()
        return true
    }

    companion object {
        /** Same path the core and engine/paths.py use as the Android default. */
        const val SHARED_PATH = "/storage/emulated/0/Documents/ChatADHD"
    }
}
