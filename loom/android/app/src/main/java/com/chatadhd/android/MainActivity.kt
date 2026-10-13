package com.chatadhd.android

import android.annotation.SuppressLint
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.view.ViewGroup
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.appcompat.app.AppCompatActivity
import androidx.webkit.WebViewAssetLoader
import com.google.android.material.snackbar.Snackbar

/**
 * Hosts the Loom web UI (loom/web, built by a different agent) inside a
 * hardened WebView and wires up [LoomBridge] as `window.LoomBridge`.
 *
 * WebView hardening:
 *  - The page is never loaded from a `file://` URL. [WebViewAssetLoader]
 *    serves app/src/main/assets/web/ (populated by the `copyWebAssets`
 *    Gradle task from loom/web/dist) over a virtual `https://` origin
 *    instead, so `allowFileAccess`/`allowFileAccessFromFileURLs`/
 *    `allowUniversalAccessFromFileURLs` can all stay off: the WebView has no
 *    filesystem access beyond what the asset loader explicitly hands it.
 *  - [LoomBridge] is added once, to this one WebView, which only ever loads
 *    pages under our own asset origin (`shouldOverrideUrlLoading` below
 *    hands anything else to the system browser instead of navigating this
 *    WebView there) — so the interface is never exposed to a third-party
 *    origin.
 */
class MainActivity : AppCompatActivity() {
    private lateinit var webView: WebView
    private lateinit var bridge: LoomBridge

    private val requestManageStorage =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()) {
            loadApp()
        }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

        webView = WebView(this).apply {
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        }
        // The theme has no action bar, so the connection screen is reached via
        // a small translucent button over the WebView corner.
        val gear = android.widget.TextView(this).apply {
            text = "\u2699"; textSize = 20f; alpha = 0.5f
            contentDescription = getString(R.string.menu_connection)
            setPadding(24, 8, 24, 8)
            setOnClickListener { requestConnection.launch(Intent(this@MainActivity, ConnectionActivity::class.java)) }
        }
        setContentView(android.widget.FrameLayout(this).apply {
            addView(webView)
            addView(gear, android.widget.FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.WRAP_CONTENT, ViewGroup.LayoutParams.WRAP_CONTENT,
                android.view.Gravity.BOTTOM or android.view.Gravity.END))
        })

        with(webView.settings) {
            javaScriptEnabled = true
            domStorageEnabled = true
            // No raw filesystem access: everything comes through the asset
            // loader's virtual https origin (see class doc above).
            allowFileAccess = false
            allowContentAccess = false
            allowFileAccessFromFileURLs = false
            allowUniversalAccessFromFileURLs = false
            mixedContentMode = WebSettings.MIXED_CONTENT_NEVER_ALLOW
            mediaPlaybackRequiresUserGesture = false
        }

        val assetLoader = WebViewAssetLoader.Builder()
            .setDomain(APP_HOST)
            .addPathHandler("/assets/", WebViewAssetLoader.AssetsPathHandler(this))
            .build()

        webView.webViewClient = object : WebViewClient() {
            override fun shouldInterceptRequest(view: WebView, request: WebResourceRequest): WebResourceResponse? =
                assetLoader.shouldInterceptRequest(request.url)

            override fun shouldOverrideUrlLoading(view: WebView, request: WebResourceRequest): Boolean {
                val mode = if (serverBase != null) ConnectionPolicy.MODE_SERVER else ConnectionPolicy.MODE_LOCAL
                if (ConnectionPolicy.isInAppUrl(mode, request.url.toString(), APP_HOST, serverBase)) return false
                startActivity(Intent(Intent.ACTION_VIEW, request.url))
                return true
            }
        }

        bridge = LoomBridge(webView)
        startForMode()
    }

    private val requestConnection =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()) {
            recreate()
        }

    /** First run (no saved choice) shows the chooser; otherwise load per mode. */
    private fun startForMode() {
        val prefs = ConnectionPrefs(this)
        if (!getSharedPreferences("connection", MODE_PRIVATE).contains("mode")) {
            requestConnection.launch(Intent(this, ConnectionActivity::class.java))
            return
        }
        if (prefs.effectiveMode == ConnectionPolicy.MODE_SERVER) {
            serverBase = prefs.serverUrl
            // SECURITY: NO addJavascriptInterface here. A remote page must never
            // reach LoomBridge (local data, secrets, API keys). The web app sees
            // no window.LoomBridge, so hasNativeBridge() is false and it uses
            // HTTP transport + the sign-in screen. The session cookie is kept
            // by CookieManager.
            android.webkit.CookieManager.getInstance().setAcceptCookie(true)
            webView.loadUrl(serverBase!!)
        } else {
            // Only here, for the bundled local origin (see ConnectionPolicy.shouldInjectBridge).
            if (ConnectionPolicy.shouldInjectBridge(ConnectionPolicy.MODE_LOCAL, LOCAL_URL, APP_HOST)) {
                webView.addJavascriptInterface(bridge, "LoomBridge")
            }
            ensureStoragePermissionThenLoad()
        }
    }

    private var serverBase: String? = null

    /**
     * The shared data directory (/storage/emulated/0/Documents/ChatADHD,
     * see loom/include/loom/config.h and engine/paths.py) lives outside
     * app-specific storage on purpose, so it needs All Files Access from
     * API 30 on. The older-device runtime read/write permission request is
     * still a documented gap. Once this flow completes, loadApp initialises
     * the native context before mounting the web app.
     */
    private fun ensureStoragePermissionThenLoad() {
        val needsAllFiles = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()
        if (!needsAllFiles) {
            loadApp()
            return
        }
        Snackbar.make(webView, R.string.storage_permission_rationale, Snackbar.LENGTH_INDEFINITE)
            .setAction(R.string.storage_permission_open_settings) {
                val intent = Intent(android.provider.Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:$packageName"))
                requestManageStorage.launch(intent)
            }
            .show()
    }

    private fun loadApp() {
        // The web client queries its backend as soon as it mounts. Initialise
        // it first; the asset handler maps /assets/web/... to assets/web/....
        val result = org.json.JSONObject(bridge.init(null, null))
        if (!result.optBoolean("ok")) {
            Snackbar.make(webView, result.optJSONObject("error")?.optString("message")
                ?: "Could not open ChatADHD data", Snackbar.LENGTH_INDEFINITE).show()
            return
        }
        webView.loadUrl(LOCAL_URL)
    }

    override fun onDestroy() {
        if (::bridge.isInitialized) bridge.shutdown()
        super.onDestroy()
    }

    companion object {
        // WebViewAssetLoader's conventional virtual domain; nothing on the
        // real internet resolves this, so it can never collide with a live
        // origin the way a made-up https hostname could.
        private const val APP_HOST = "appassets.androidx.startup"
        private const val LOCAL_URL = "https://$APP_HOST/assets/web/index.html"
    }
}
