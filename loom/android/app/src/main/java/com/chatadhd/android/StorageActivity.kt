package com.chatadhd.android

import android.app.Activity
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.provider.Settings
import android.text.InputType
import android.view.View
import android.view.ViewGroup
import android.widget.Button
import android.widget.CheckBox
import android.widget.EditText
import android.widget.LinearLayout
import android.widget.RadioButton
import android.widget.RadioGroup
import android.widget.ScrollView
import android.widget.TextView
import androidx.activity.OnBackPressedCallback
import androidx.appcompat.app.AlertDialog
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import java.io.File
import kotlin.concurrent.thread

/**
 * Where the local data lives (a setting, never a rule) and how to move it.
 *
 *  - New installs use the app's private storage. An install that already has its data in
 *    Documents/ChatADHD keeps it there, with a visible warning and a "move to private storage"
 *    button here; nothing is moved without pressing it.
 *  - Moving = copy -> verify sha256 of every file -> switch the setting -> keep the old copy
 *    until the user deletes it (see [DataMigration]).
 *  - Shared storage stays selectable: it survives an uninstall and other tools can sync it.
 *    The warning is only a warning.
 *
 * Returns RESULT_OK when something changed, so the caller restarts the local session.
 */
class StorageActivity : AppCompatActivity() {
    private lateinit var loc: DataLocation
    private lateinit var current: TextView
    private lateinit var warning: TextView
    private lateinit var moveToPrivate: Button
    private lateinit var group: RadioGroup
    private lateinit var custom: EditText
    private lateinit var wrap: CheckBox
    private lateinit var autoMig: CheckBox
    private lateinit var applyBtn: Button
    private lateinit var cancelCopy: Button
    private lateinit var status: TextView
    private lateinit var oldBox: LinearLayout

    @Volatile private var busy = false
    @Volatile private var cancelRequested = false

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        loc = DataLocation(this)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad)
        }
        current = TextView(this)
        warning = TextView(this).apply {
            setText(R.string.storage_warn_shared)
            setTextColor(ContextCompat.getColor(this@StorageActivity, R.color.loom_warning_fg))
            setBackgroundColor(ContextCompat.getColor(this@StorageActivity, R.color.loom_warning_bg))
            setPadding(pad / 2, pad / 2, pad / 2, pad / 2)
        }
        moveToPrivate = Button(this).apply {
            setText(R.string.storage_move_to_private)
            setOnClickListener { askMove(DataLocationPolicy.KIND_PRIVATE, null) }
        }
        group = RadioGroup(this)
        listOf(
            R_PRIVATE to R.string.storage_loc_private,
            R_EXTERNAL to R.string.storage_loc_app_external,
            R_SHARED to R.string.storage_loc_shared,
            R_CUSTOM to R.string.storage_loc_custom,
        ).forEach { (id, label) -> group.addView(RadioButton(this).apply { this.id = id; setText(label) }) }
        custom = EditText(this).apply {
            setHint(R.string.storage_custom_hint); setText(loc.customPath)
            inputType = InputType.TYPE_TEXT_VARIATION_URI or InputType.TYPE_CLASS_TEXT
        }
        wrap = CheckBox(this).apply { setText(R.string.storage_secrets_wrap); isChecked = loc.secretsWrap }
        autoMig = CheckBox(this).apply { setText(R.string.storage_auto_migrate_opt); isChecked = loc.autoMigrate }
        applyBtn = Button(this).apply { setText(R.string.storage_apply); setOnClickListener { onApply() } }
        cancelCopy = Button(this).apply {
            setText(R.string.storage_cancel_copy); visibility = View.GONE
            setOnClickListener { cancelRequested = true }
        }
        status = TextView(this)
        oldBox = LinearLayout(this).apply { orientation = LinearLayout.VERTICAL }
        listOf(current, warning, moveToPrivate, group, custom, wrap, autoMig, applyBtn, cancelCopy, status, oldBox).forEach {
            root.addView(it, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        setContentView(ScrollView(this).apply { addView(root) })

        group.setOnCheckedChangeListener { _, id -> custom.isEnabled = id == R_CUSTOM }
        onBackPressedDispatcher.addCallback(this, object : OnBackPressedCallback(true) {
            override fun handleOnBackPressed() { if (!busy) finish() }
        })
        refresh(selectCurrent = true)
    }

    private fun kindOf(id: Int): String = when (id) {
        R_EXTERNAL -> DataLocationPolicy.KIND_APP_EXTERNAL
        R_SHARED -> DataLocationPolicy.KIND_SHARED
        R_CUSTOM -> DataLocationPolicy.KIND_CUSTOM
        else -> DataLocationPolicy.KIND_PRIVATE
    }

    private fun idOf(kind: String): Int = when (kind) {
        DataLocationPolicy.KIND_APP_EXTERNAL -> R_EXTERNAL
        DataLocationPolicy.KIND_SHARED -> R_SHARED
        DataLocationPolicy.KIND_CUSTOM -> R_CUSTOM
        else -> R_PRIVATE
    }

    private fun refresh(selectCurrent: Boolean) {
        val dir = loc.currentDir()
        val exposure = loc.exposure(dir)
        val label = when (exposure) {
            DataLocationPolicy.Exposure.PRIVATE -> getString(R.string.storage_exposure_private)
            DataLocationPolicy.Exposure.APP_EXTERNAL -> getString(R.string.storage_exposure_app_external)
            else -> getString(R.string.storage_exposure_shared)
        }
        current.text = getString(R.string.storage_current, dir.absolutePath, label)
        val shared = DataLocationPolicy.needsWarning(exposure)
        warning.visibility = if (shared) View.VISIBLE else View.GONE
        moveToPrivate.visibility = if (shared) View.VISIBLE else View.GONE
        if (selectCurrent) {
            group.check(idOf(loc.choice().kind))
            custom.isEnabled = group.checkedRadioButtonId == R_CUSTOM
        }
        oldBox.removeAllViews()
        for (old in loc.oldCopies) {
            oldBox.addView(TextView(this).apply { text = getString(R.string.storage_old_copy, old.absolutePath) })
            oldBox.addView(Button(this).apply {
                setText(R.string.storage_delete_old)
                isEnabled = !busy
                setOnClickListener { askDeleteOld(old) }
            })
        }
    }

    private fun setBusy(b: Boolean, copying: Boolean = false) {
        busy = b
        listOf<View>(moveToPrivate, group, custom, wrap, applyBtn).forEach { it.isEnabled = !b }
        for (i in 0 until group.childCount) group.getChildAt(i).isEnabled = !b
        cancelCopy.visibility = if (b && copying) View.VISIBLE else View.GONE
        if (!b) custom.isEnabled = group.checkedRadioButtonId == R_CUSTOM
    }

    private fun onApply() {
        val kind = kindOf(group.checkedRadioButtonId)
        val customPath = custom.text.toString().trim()
        loc.autoMigrate = autoMig.isChecked
        loc.secretsWrap = wrap.isChecked           // takes effect when the local session restarts
        if (kind == DataLocationPolicy.KIND_CUSTOM) {
            val err = DataLocationPolicy.validateCustomPath(customPath)
            if (err != null) { status.setText(if (err == "empty") R.string.storage_err_custom_empty else R.string.storage_err_custom_relative); return }
        }
        val target = loc.dirFor(kind, customPath)
        if (target == null) { status.setText(R.string.storage_err_unavailable); return }
        val live = loc.currentDir()
        if (DataLocationPolicy.isSame(target, live)) {
            // Same place (or only the key-wrapping option changed): save and go back.
            loc.switchTo(kind, customPath, null)
            setResult(Activity.RESULT_OK)
            finish()
            return
        }
        askMove(kind, customPath)
    }

    /** Asks how to get to [kind]: move the data (copy+verify) or just use the target as it is. */
    private fun askMove(kind: String, customPath: String?) {
        val target = loc.dirFor(kind, customPath)
        if (target == null) { status.setText(R.string.storage_err_unavailable); return }
        if (!ensureAllFilesAccessIfNeeded(target)) return
        val live = loc.currentDir()
        val targetHasData = DataLocationPolicy.hasData(target)
        val liveHasData = DataLocationPolicy.hasData(live)
        val b = AlertDialog.Builder(this).setTitle(R.string.storage_move_title)
        when {
            targetHasData -> b.setMessage(getString(R.string.storage_confirm_use_existing, target.absolutePath))
                .setPositiveButton(R.string.storage_use_existing) { _, _ -> switchOnly(kind, customPath) }
            !liveHasData -> b.setMessage(getString(R.string.storage_confirm_use_empty, target.absolutePath))
                .setPositiveButton(R.string.storage_use_empty) { _, _ -> switchOnly(kind, customPath) }
            else -> b.setMessage(getString(R.string.storage_confirm_move, live.absolutePath, target.absolutePath))
                .setPositiveButton(R.string.storage_move_copy) { _, _ -> startMove(kind, customPath, target) }
                .setNeutralButton(R.string.storage_use_empty) { _, _ -> switchOnly(kind, customPath) }
        }
        b.setNegativeButton(R.string.storage_cancel, null).show()
    }

    private fun switchOnly(kind: String, customPath: String?) {
        // The core is stopped by the caller's recreate(); nothing is copied, nothing is deleted.
        loc.switchTo(kind, customPath, null)
        status.setText(R.string.storage_switched)
        setResult(Activity.RESULT_OK)
        refresh(selectCurrent = true)
    }

    private fun startMove(kind: String, customPath: String?, target: File) {
        cancelRequested = false
        setBusy(true, copying = true)
        status.setText(R.string.storage_preparing)
        thread {
            val r = loc.migrate(kind, customPath, target) { i, n, rel ->
                runOnUiThread { status.text = getString(R.string.storage_copying, i, n, rel) }
                !cancelRequested
            }
            runOnUiThread {
                setBusy(false)
                // The core was stopped for the copy: the caller must restart the local session either way.
                setResult(Activity.RESULT_OK)
                if (r.ok) {
                    status.text = getString(R.string.storage_move_done, r.files)
                    refresh(selectCurrent = true)
                } else {
                    status.text = getString(R.string.storage_move_failed, errorText(r.error), r.detail ?: "")
                    refresh(selectCurrent = false)
                }
            }
        }
    }

    private fun errorText(key: String?): String = getString(when (key) {
        "source_missing" -> R.string.storage_err_source_missing
        "nested" -> R.string.storage_err_nested
        "destination_invalid" -> R.string.storage_err_destination_invalid
        "destination_not_empty" -> R.string.storage_err_destination_not_empty
        "verify_failed" -> R.string.storage_err_verify_failed
        "source_changed" -> R.string.storage_err_source_changed
        "cancelled" -> R.string.storage_err_cancelled
        else -> R.string.storage_err_copy_failed
    })

    private fun askDeleteOld(old: File) {
        AlertDialog.Builder(this)
            .setTitle(R.string.storage_delete_old)
            .setMessage(getString(R.string.storage_delete_old_confirm, old.absolutePath))
            .setPositiveButton(R.string.storage_delete_old) { _, _ ->
                setBusy(true)
                status.setText(R.string.storage_deleting)
                thread {
                    val ok = loc.deleteOldCopy(old)
                    runOnUiThread {
                        setBusy(false)
                        status.setText(if (ok) R.string.storage_deleted else R.string.storage_delete_failed)
                        refresh(selectCurrent = false)
                    }
                }
            }
            .setNegativeButton(R.string.storage_cancel, null)
            .show()
    }

    /** Shared / outside-the-app targets need "All files access" from API 30. Returns true when usable now. */
    private fun ensureAllFilesAccessIfNeeded(target: File): Boolean {
        val needs = DataLocationPolicy.needsAllFilesAccess(loc.exposure(target))
        if (!needs || Build.VERSION.SDK_INT < Build.VERSION_CODES.R || Environment.isExternalStorageManager()) return true
        status.setText(R.string.storage_need_all_files)
        startActivity(Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName")))
        return false
    }

    private companion object {
        const val R_PRIVATE = 11
        const val R_EXTERNAL = 12
        const val R_SHARED = 13
        const val R_CUSTOM = 14
    }
}
