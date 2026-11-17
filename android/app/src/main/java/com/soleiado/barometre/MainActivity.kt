package com.soleiado.barometre

import android.annotation.SuppressLint
import android.app.Activity
import android.app.AlertDialog
import android.content.Context
import android.graphics.Bitmap
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.Bundle
import android.view.View
import android.webkit.WebSettings
import android.webkit.WebView
import android.webkit.WebViewClient
import android.widget.Button
import android.widget.EditText
import android.widget.ProgressBar
import android.widget.TextView
import android.widget.Toast

class MainActivity : Activity() {

    private lateinit var webView: WebView
    private lateinit var progress: ProgressBar
    private lateinit var title: TextView

    private val prefs by lazy { getSharedPreferences("baro", Context.MODE_PRIVATE) }

    @SuppressLint("SetJavaScriptEnabled")
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)

        webView = findViewById(R.id.webview)
        progress = findViewById(R.id.progress)
        title = findViewById(R.id.title)

        webView.settings.apply {
            javaScriptEnabled = true
            domStorageEnabled = true
            databaseEnabled = true
            cacheMode = WebSettings.LOAD_DEFAULT
            loadWithOverviewMode = true
            useWideViewPort = true
        }

        webView.webViewClient = object : WebViewClient() {
            override fun onPageStarted(view: WebView?, url: String?, favicon: Bitmap?) {
                progress.visibility = View.VISIBLE
            }

            override fun onPageFinished(view: WebView?, url: String?) {
                progress.visibility = View.GONE
            }

            override fun onReceivedError(
                view: WebView?,
                errorCode: Int,
                description: String?,
                failingUrl: String?
            ) {
                progress.visibility = View.GONE
            }
        }

        findViewById<Button>(R.id.connect_btn).setOnClickListener { autoConnect() }

        val saved = prefs.getString("addr", null)
        if (saved != null) {
            loadUrl(saved)
        } else {
            autoConnect()
        }
    }

    private fun loadUrl(address: String) {
        val url = if (address.startsWith("http")) address else "http://$address/"
        webView.loadUrl(url)
        prefs.edit().putString("addr", url).apply()
        title.text = url
    }

    //-----------------------------------------------------------------
    // Connexion automatique : mDNS (barometre.local) puis adresse manuelle
    //-----------------------------------------------------------------

    private fun autoConnect() {
        val dialog = AlertDialog.Builder(this)
            .setMessage(R.string.scanning)
            .setCancelable(false)
            .create()
        dialog.show()

        resolveBarometer { ip ->
            dialog.dismiss()
            if (ip != null) {
                loadUrl("http://$ip/")
            } else {
                showAddressDialog()
            }
        }
    }

    private fun resolveBarometer(cb: (String?) -> Unit) {
        val manager = getSystemService(Context.NSD_SERVICE) as NsdManager
        var finished = false
        var discovery: NsdManager.DiscoveryListener? = null

        fun done(ip: String?) {
            if (finished) return
            finished = true
            discovery?.let {
                try {
                    manager.stopServiceDiscovery(it)
                } catch (_: Exception) {
                }
            }
            // Le callback NsdManager arrive sur un thread dedie : tout ce qui
            // touche a l'UI (WebView, dialogs) doit repasser sur le main thread.
            runOnUiThread { cb(ip) }
        }

        val resolveListener = object : NsdManager.ResolveListener {
            override fun onResolveFailed(serviceInfo: NsdServiceInfo, errorCode: Int) {
                // on continue la decouverte, un autre service peut matcher
            }

            override fun onServiceResolved(serviceInfo: NsdServiceInfo) {
                val host = serviceInfo.host
                done(if (host != null) host.hostAddress else null)
            }
        }

        discovery = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(serviceType: String) {}
            override fun onDiscoveryStopped(serviceType: String) {}

            override fun onServiceFound(serviceInfo: NsdServiceInfo) {
                if (serviceInfo.serviceName.contains("barometre", ignoreCase = true)) {
                    manager.resolveService(serviceInfo, resolveListener)
                }
            }

            override fun onServiceLost(serviceInfo: NsdServiceInfo) {}
            override fun onStartDiscoveryFailed(serviceType: String, errorCode: Int) { done(null) }
            override fun onStopDiscoveryFailed(serviceType: String, errorCode: Int) {}
        }

        // Garde-fou : si rien n'est trouve en 4 secondes, on passe au manuel.
        webView.postDelayed({ done(null) }, 4000)

        manager.discoverServices("_http._tcp.", NsdManager.PROTOCOL_DNS_SD, discovery)
    }

    private fun showAddressDialog() {
        val input = EditText(this)
        input.hint = getString(R.string.address_hint)
        input.setText(
            prefs.getString("addr", "barometre.local")
                ?.removePrefix("http://")
                ?.removeSuffix("/")
        )

        AlertDialog.Builder(this)
            .setTitle(R.string.connect_title)
            .setMessage(R.string.connect_message)
            .setView(input)
            .setPositiveButton(R.string.ok) { _, _ ->
                val address = input.text.toString().trim()
                if (address.isNotEmpty()) {
                    loadUrl(address)
                } else {
                    Toast.makeText(this, R.string.not_found, Toast.LENGTH_SHORT).show()
                }
            }
            .setNegativeButton(R.string.cancel, null)
            .show()
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        if (webView.canGoBack()) webView.goBack() else super.onBackPressed()
    }
}
