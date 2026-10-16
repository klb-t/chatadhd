package com.chatadhd.android

import android.annotation.SuppressLint
import android.Manifest
import android.content.Intent
import android.content.pm.ApplicationInfo
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.os.Environment
import android.view.View
import android.view.ViewGroup
import android.webkit.PermissionRequest
import android.webkit.ValueCallback
import android.webkit.WebChromeClient
import android.webkit.WebResourceRequest
import android.webkit.WebResourceResponse
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
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
    private lateinit var gear: android.widget.TextView
    private lateinit var dataLocation: DataLocation
    /** Keeps secrets.json sealed with the Keystore between uses (see SecretsVaultCore). */
    private var vault: SecretsVaultCore? = null

    private val requestManageStorage =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()) {
            loadApp()
        }

    // ── devices of the page (P13) ───────────────────────────────────────
    // The page declares the microphone / camera it can use (client.declare); the WebView asks this activity before
    // it hands them over. DevicePermissionPolicy decides which requests are served (the page this app loaded, the
    // devices this shell serves); Android's own runtime permission comes first.
    private var pendingDevices: Pair<PermissionRequest, List<String>>? = null
    private val requestDevices =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.RequestMultiplePermissions()) { granted ->
            val pending = pendingDevices ?: return@registerForActivityResult
            pendingDevices = null
            val allowed = pending.second.filter { resource ->
                val permission = DevicePermissionPolicy.androidPermissionFor(resource)
                permission != null && (granted[permission] == true ||
                    ContextCompat.checkSelfPermission(this, permission) == PackageManager.PERMISSION_GRANTED)
            }
            if (allowed.isEmpty()) pending.first.deny() else pending.first.grant(allowed.toTypedArray())
        }
    private var fileChooserCallback: ValueCallback<Array<Uri>>? = null
    private val chooseFile =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()) { result ->
            val chosen = WebChromeClient.FileChooserParams.parseResult(result.resultCode, result.data)
            fileChooserCallback?.onReceiveValue(chosen)
            fileChooserCallback = null
        }
    /** Import through the system picker (content:// -> private copy -> kernel); registers its pickers here, at construction. */
    private val importFlow = ImportFlow(this)
    private val requestNotifications =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.RequestPermission()) { }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        dataLocation = DataLocation(this)

        // Debug builds only: the page can be inspected and driven over the WebView DevTools socket
        // (chrome://inspect, and loom/android/smoke/drive.mjs in CI). A release build never opens it.
        if (applicationInfo.flags and ApplicationInfo.FLAG_DEBUGGABLE != 0) WebView.setWebContentsDebuggingEnabled(true)

        webView = WebView(this).apply {
            layoutParams = ViewGroup.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT)
        }
        // The theme has no action bar, so the connection screen is reached via
        // a small translucent button over the WebView corner.
        gear = android.widget.TextView(this).apply {
            text = "\u2699"; textSize = 20f; alpha = 0.5f
            contentDescription = getString(R.string.menu_connection)
            setPadding(24, 8, 24, 8)
            setOnClickListener { requestConnection.launch(Intent(this@MainActivity, ConnectionActivity::class.java)) }
        }
        val column = android.widget.LinearLayout(this).apply {
            orientation = android.widget.LinearLayout.VERTICAL
            addView(webView, android.widget.LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, 0, 1f))
        }
        setContentView(android.widget.FrameLayout(this).apply {
            addView(column, android.widget.FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT))
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

        webView.webChromeClient = object : WebChromeClient() {
            override fun onPermissionRequest(request: PermissionRequest) {
                val servable = DevicePermissionPolicy.servable(request.resources, request.origin.toString(), "https://$APP_HOST", serverBase)
                if (servable.isEmpty()) {
                    request.deny()
                    return
                }
                val missing = servable.mapNotNull { DevicePermissionPolicy.androidPermissionFor(it) }
                    .filter { ContextCompat.checkSelfPermission(this@MainActivity, it) != PackageManager.PERMISSION_GRANTED }
                if (missing.isEmpty()) {
                    request.grant(servable.toTypedArray())
                    return
                }
                pendingDevices?.first?.deny()
                pendingDevices = Pair(request, servable)
                requestDevices.launch(missing.toTypedArray())
            }

            override fun onShowFileChooser(view: WebView, callback: ValueCallback<Array<Uri>>, params: FileChooserParams): Boolean {
                fileChooserCallback?.onReceiveValue(null)
                fileChooserCallback = callback
                return try {
                    chooseFile.launch(params.createIntent())
                    true
                } catch (e: Exception) {
                    fileChooserCallback = null
                    false
                }
            }
        }

        bridge = LoomBridge(webView)
        // Notifications of the shell (the WebView has no Notification API): Android 13 asks for the permission at runtime.
        bridge.onRequestNotifications = {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) requestNotifications.launch(Manifest.permission.POST_NOTIFICATIONS)
        }
        bridge.onPickImport = { kind, title, all, callbackId -> importFlow.start(kind, title, all, callbackId) }
        bridge.onImportUri = { uri, title, callbackId -> importFlow.startUri(uri, title, callbackId) }
        bridge.onCancelImport = { callbackId -> importFlow.cancel(callbackId) }
        bridge.onOpenConnection = { runOnUiThread { requestConnection.launch(Intent(this, ConnectionActivity::class.java)) } }
        startForMode()
    }

    private val requestConnection =
        registerForActivityResult(androidx.activity.result.contract.ActivityResultContracts.StartActivityForResult()) {
            recreate()
        }

    /** First run (no saved choice) shows the chooser; otherwise load per mode. */
    private fun startForMode() {
        val prefs = ConnectionPrefs(this)
        // R56: no saved choice opens LOCAL mode straight into the chat; the choice is in the chat's overflow menu ("Połączenie").
        // In local mode the page reaches Connection through LoomBridge.openConnection; a server page has no bridge, so the corner button stays there.
        gear.visibility = if (prefs.effectiveMode == ConnectionPolicy.MODE_SERVER) View.VISIBLE else View.GONE
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
     * Where the data lives is a setting (see [DataLocation]): the app's private storage by
     * default, or the shared Documents/ChatADHD (kept for installs that already use it, and
     * selectable on purpose). Only a location outside the app's own storage needs All Files
     * Access from API 30 on, so a new install is never asked for it. The older-device runtime
     * read/write permission request is still a documented gap. Once this flow completes,
     * loadApp initialises the native context before mounting the web app.
     */
    private fun ensureStoragePermissionThenLoad() {
        val needsAllFiles = DataLocationPolicy.needsAllFilesAccess(dataLocation.exposure(dataLocation.currentDir())) &&
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.R && !Environment.isExternalStorageManager()
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
        if (dataLocation.shouldAutoMigrate()) { runAutoMigration(); return }
        // The web client queries its backend as soon as it mounts. Initialise
        // it first; the asset handler maps /assets/web/... to assets/web/....
        val dir = dataLocation.currentDir()
        val v = dataLocation.vault(dir)
        vault = v
        // The core reads secrets.json once, at start: make it available if only the sealed copy exists.
        val unseal = v.beforeInit()
        val result = org.json.JSONObject(bridge.init(dir.absolutePath, null))
        if (!result.optBoolean("ok")) {
            v.afterChange()
            Snackbar.make(webView, result.optJSONObject("error")?.optString("message")
                ?: getString(R.string.data_open_failed), Snackbar.LENGTH_INDEFINITE).show()
            return
        }
        // The core now holds the secrets in memory: seal the file again, and keep doing so
        // after every call that may save a secret.
        val seal = v.afterChange()
        bridge.onSecretsMayHaveChanged = { v.afterChange() }
        showStorageNotices(dir, unseal, seal)
        webView.loadUrl(LOCAL_URL)
    }

    /** One-time copy+verify of a legacy shared-storage dir into private storage; then continues to [loadApp]. */
    private fun runAutoMigration() {
        Snackbar.make(webView, R.string.storage_auto_migrating, Snackbar.LENGTH_INDEFINITE).show()
        Thread {
            val r = dataLocation.runAutoMigration()
            runOnUiThread {
                Snackbar.make(webView,
                    if (r.ok) R.string.storage_auto_migrated else R.string.storage_auto_migrate_failed,
                    Snackbar.LENGTH_LONG).show()
                loadApp()
            }
        }.start()
    }

    private fun showStorageNotices(dir: java.io.File, unseal: SecretsVaultCore.Outcome, seal: SecretsVaultCore.Outcome) {
        // No permanent bar: one calm notice, only when the user chose shared storage themselves.
        if (DataLocationPolicy.showSharedNoticeOnce(dataLocation.choice(), dataLocation.exposure(dir), dataLocation.sharedNoticeShown)) {
            dataLocation.sharedNoticeShown = true
            Snackbar.make(webView, R.string.storage_notice_shared, Snackbar.LENGTH_LONG).show()
        }
        val message = when {
            unseal == SecretsVaultCore.Outcome.UNSEAL_FAILED -> R.string.secrets_unreadable
            seal == SecretsVaultCore.Outcome.SEAL_FAILED -> R.string.secrets_seal_failed
            else -> null
        }
        if (message != null) Snackbar.make(webView, message, Snackbar.LENGTH_LONG).show()
    }

    override fun onStop() {
        // Safety net for secrets the core saved on its own (not through a bridge call we watch).
        vault?.afterChange()
        super.onStop()
    }

    override fun onDestroy() {
        importFlow.shutdown()
        if (::bridge.isInitialized) bridge.shutdown()
        vault?.afterChange()
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
