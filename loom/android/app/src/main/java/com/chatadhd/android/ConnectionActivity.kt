package com.chatadhd.android

import android.app.Activity
import android.os.Bundle
import android.view.ViewGroup
import android.widget.*
import androidx.appcompat.app.AppCompatActivity
import org.json.JSONObject
import java.net.HttpURLConnection
import java.net.URL
import kotlin.concurrent.thread

/**
 * Connection chooser: local (data on the phone) or a Loom server account.
 * Server URL is validated with [ConnectionPolicy] and probed with
 * GET /api/auth/me (200 = signed in, 401 = server reachable, sign-in needed).
 * Local data is never touched here.
 */
class ConnectionActivity : AppCompatActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val prefs = ConnectionPrefs(this)
        val pad = (16 * resources.displayMetrics.density).toInt()
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL; setPadding(pad, pad, pad, pad)
        }
        val group = RadioGroup(this)
        val rLocal = RadioButton(this).apply { id = 1; setText(R.string.conn_mode_local) }
        val rServer = RadioButton(this).apply { id = 2; setText(R.string.conn_mode_server) }
        group.addView(rLocal); group.addView(rServer)
        val url = EditText(this).apply {
            setHint(R.string.conn_url_hint); setText(prefs.serverUrl)
            inputType = android.text.InputType.TYPE_TEXT_VARIATION_URI or android.text.InputType.TYPE_CLASS_TEXT
        }
        val http = CheckBox(this).apply { setText(R.string.conn_allow_http); isChecked = prefs.allowPlainHttp }
        val status = TextView(this)
        val save = Button(this).apply { setText(R.string.conn_save) }
        val note = TextView(this).apply { setText(R.string.conn_switch_note) }
        listOf(group, url, http, save, status, note).forEach {
            root.addView(it, ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT)
        }
        setContentView(ScrollView(this).apply { addView(root) })
        group.check(if (prefs.mode == ConnectionPolicy.MODE_SERVER) 2 else 1)
        val toggle = { s: Boolean -> url.isEnabled = s; http.isEnabled = s }
        toggle(prefs.mode == ConnectionPolicy.MODE_SERVER)
        group.setOnCheckedChangeListener { _, id -> toggle(id == 2) }

        fun finishWith(mode: String) { setResult(Activity.RESULT_OK); finish() }

        save.setOnClickListener {
            if (group.checkedRadioButtonId != 2) {
                prefs.mode = ConnectionPolicy.MODE_LOCAL; finishWith(prefs.mode); return@setOnClickListener
            }
            val r = ConnectionPolicy.normalizeServerUrl(url.text.toString(), http.isChecked)
            if (!r.ok()) { status.text = errorText(r.error); return@setOnClickListener }
            status.setText(R.string.conn_checking); save.isEnabled = false
            thread {
                val err = probe(r.url!!)
                runOnUiThread {
                    save.isEnabled = true
                    if (err != null) { status.text = err; return@runOnUiThread }
                    prefs.serverUrl = r.url; prefs.allowPlainHttp = http.isChecked
                    prefs.mode = ConnectionPolicy.MODE_SERVER
                    finishWith(prefs.mode)
                }
            }
        }
    }

    private fun errorText(key: String?): String = when (key) {
        "empty" -> getString(R.string.conn_err_empty)
        "https_required" -> getString(R.string.conn_err_https_required)
        "http_not_lan" -> getString(R.string.conn_err_http_not_lan)
        else -> getString(R.string.conn_err_invalid)
    }

    /** null = OK (200 or 401 with a JSON body); otherwise the reason to show. */
    private fun probe(base: String): String? = try {
        val c = URL("$base/api/auth/me").openConnection() as HttpURLConnection
        c.connectTimeout = 10_000; c.readTimeout = 10_000; c.instanceFollowRedirects = false
        val code = c.responseCode
        val body = (if (code < 400) c.inputStream else c.errorStream)?.bufferedReader()?.readText() ?: ""
        val json = runCatching { JSONObject(body) }.isSuccess
        if ((code == 200 || code == 401) && json) null else getString(R.string.conn_err_not_loom, code)
    } catch (e: Exception) {
        getString(R.string.conn_err_unreachable, e.message ?: e.javaClass.simpleName)
    }
}
