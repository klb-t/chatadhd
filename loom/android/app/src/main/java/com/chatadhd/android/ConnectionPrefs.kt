package com.chatadhd.android

import android.content.Context

/** Connection setting in app preferences (not in the shared data dir: it is per-device). */
class ConnectionPrefs(context: Context) {
    private val p = context.getSharedPreferences("connection", Context.MODE_PRIVATE)

    var mode: String
        get() = p.getString("mode", ConnectionPolicy.MODE_LOCAL) ?: ConnectionPolicy.MODE_LOCAL
        set(v) = p.edit().putString("mode", v).apply()
    var serverUrl: String
        get() = p.getString("server_url", "") ?: ""
        set(v) = p.edit().putString("server_url", v).apply()
    var allowPlainHttp: Boolean
        get() = p.getBoolean("allow_http", false)
        set(v) = p.edit().putBoolean("allow_http", v).apply()

    /** Server mode is only effective with a saved, validated URL. */
    val effectiveMode: String
        get() = if (mode == ConnectionPolicy.MODE_SERVER && serverUrl.isNotEmpty()) ConnectionPolicy.MODE_SERVER
                else ConnectionPolicy.MODE_LOCAL
}
