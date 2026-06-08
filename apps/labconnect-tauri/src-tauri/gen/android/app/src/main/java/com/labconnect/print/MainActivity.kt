package com.labconnect.print

import android.app.admin.DevicePolicyManager
import android.content.BroadcastReceiver
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.IntentFilter
import android.content.ServiceConnection
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.os.Parcel
import android.os.SystemClock
import android.util.Log
import android.view.KeyEvent
import android.view.View
import android.webkit.JavascriptInterface
import android.webkit.WebView
import androidx.activity.enableEdgeToEdge
import org.json.JSONObject

class MainActivity : TauriActivity() {
  private lateinit var sunmiPrinterBridge: SunmiPrinterBridge
  private var backPressWindowStartedAt = 0L
  private var backPressCount = 0
  private var labConnectWebView: WebView? = null
  private var scannerReceiverRegistered = false
  private var scannerServiceBound = false
  private var scannerBinder: IBinder? = null
  private var scannerActive = false
  private var scannerStopRunnable: Runnable? = null
  private val scannerConnection =
    object : ServiceConnection {
      override fun onServiceConnected(name: ComponentName, service: IBinder) {
        Log.d(LOG_TAG, "Scanner service connected: $name")
        scannerBinder = service
      }

      override fun onServiceDisconnected(name: ComponentName) {
        Log.d(LOG_TAG, "Scanner service disconnected: $name")
        scannerBinder = null
        scannerServiceBound = false
      }
    }
  private val scannerReceiver =
    object : BroadcastReceiver() {
      override fun onReceive(context: Context, intent: Intent) {
        if (intent.action != SUNMI_SCANNER_ACTION) return
        val scanData = intent.getStringExtra(SUNMI_SCANNER_DATA_EXTRA)?.trim()
        if (scanData.isNullOrBlank()) return
        dispatchScannerData(scanData)
      }
    }

  override fun onCreate(savedInstanceState: Bundle?) {
    enableEdgeToEdge()
    sunmiPrinterBridge = SunmiPrinterBridge(this)
    super.onCreate(savedInstanceState)
    registerScannerReceiver()
    bindScannerService()
    enterKioskModeIfAllowed()
  }

  override fun onWebViewCreate(webView: WebView) {
    super.onWebViewCreate(webView)
    labConnectWebView = webView
    webView.addJavascriptInterface(sunmiPrinterBridge, "LabConnectSunmiPrinter")
    webView.addJavascriptInterface(IntegratedScannerBridge(), "LabConnectScanner")
  }

  override fun onResume() {
    super.onResume()
    keepFullscreen()
    enterKioskModeIfAllowed()
  }

  override fun onWindowFocusChanged(hasFocus: Boolean) {
    super.onWindowFocusChanged(hasFocus)
    if (hasFocus) {
      keepFullscreen()
    }
  }

  override fun onDestroy() {
    if (::sunmiPrinterBridge.isInitialized) {
      sunmiPrinterBridge.release()
    }
    unregisterScannerReceiver()
    unbindScannerService()
    labConnectWebView = null
    super.onDestroy()
  }

  override fun onKeyDown(keyCode: Int, event: KeyEvent?): Boolean {
    if (keyCode == KeyEvent.KEYCODE_BACK && registerKioskEscapePress()) {
      exitKioskMode()
      return true
    }
    return super.onKeyDown(keyCode, event)
  }

  private fun enterKioskModeIfAllowed() {
    if (Build.VERSION.SDK_INT < Build.VERSION_CODES.LOLLIPOP) return

    val devicePolicyManager =
      getSystemService(Context.DEVICE_POLICY_SERVICE) as DevicePolicyManager
    val admin = ComponentName(this, LabConnectDeviceAdminReceiver::class.java)

    if (devicePolicyManager.isDeviceOwnerApp(packageName)) {
      try {
        devicePolicyManager.setLockTaskPackages(admin, arrayOf(packageName))
        val homeFilter =
          IntentFilter(Intent.ACTION_MAIN).apply {
            addCategory(Intent.CATEGORY_HOME)
            addCategory(Intent.CATEGORY_DEFAULT)
          }
        devicePolicyManager.addPersistentPreferredActivity(
          admin,
          homeFilter,
          ComponentName(packageName, MainActivity::class.java.name)
        )
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
          devicePolicyManager.setStatusBarDisabled(admin, true)
          devicePolicyManager.setKeyguardDisabled(admin, true)
        }
      } catch (_: RuntimeException) {
        // The app still works normally when the device has not been provisioned for kiosk mode.
      }
    }

    if (!devicePolicyManager.isLockTaskPermitted(packageName)) return

    try {
      startLockTask()
    } catch (_: IllegalStateException) {
      // Android can reject this during early activity startup; onResume will try again.
    } catch (_: SecurityException) {
      // Keeps development installs usable before Device Owner provisioning.
    }
  }

  private fun registerScannerReceiver() {
    if (scannerReceiverRegistered) return
    val filter = IntentFilter(SUNMI_SCANNER_ACTION)
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
      registerReceiver(scannerReceiver, filter, Context.RECEIVER_EXPORTED)
    } else {
      @Suppress("DEPRECATION")
      registerReceiver(scannerReceiver, filter)
    }
    scannerReceiverRegistered = true
  }

  private fun unregisterScannerReceiver() {
    if (!scannerReceiverRegistered) return
    try {
      unregisterReceiver(scannerReceiver)
    } catch (_: RuntimeException) {
    }
    scannerReceiverRegistered = false
  }

  private fun dispatchScannerData(data: String) {
    val escapedData = JSONObject.quote(data)
    labConnectWebView?.post {
      labConnectWebView?.evaluateJavascript(
        "window.dispatchEvent(new CustomEvent('labconnect-integrated-scan',{detail:{data:$escapedData,source:'integrated-scanner'}}));",
        null
      )
    }
  }

  inner class IntegratedScannerBridge {
    @JavascriptInterface
    fun startScan(timeoutMs: Int): String {
      return try {
        val serviceReady = ensureScannerService()
        if (!serviceReady) {
          return JSONObject()
            .put("ok", false)
            .put("error", "Service scanner indisponible")
            .toString()
        }
        resetScannerBeforeStart()
        callScannerTransaction(SUNMI_SCANNER_TRANSACTION_SCAN)
        scannerActive = true
        if (timeoutMs > 0) {
          scannerStopRunnable?.let { labConnectWebView?.removeCallbacks(it) }
          scannerStopRunnable = Runnable { stopScannerIfActive() }
          labConnectWebView?.postDelayed(scannerStopRunnable, timeoutMs.toLong())
        }
        """{"ok":true}"""
      } catch (error: Exception) {
        JSONObject().put("ok", false).put("error", error.message ?: "Scanner indisponible").toString()
      }
    }

    @JavascriptInterface
    fun stopScan(): String {
      return try {
        stopScannerIfActive()
        """{"ok":true}"""
      } catch (error: Exception) {
        JSONObject().put("ok", false).put("error", error.message ?: "Arrêt scanner impossible").toString()
      }
    }
  }

  private fun bindScannerService(): Boolean {
    if (scannerServiceBound) return true
    return try {
      val intent =
        Intent(SUNMI_SCANNER_SERVICE_ACTION).apply {
          setPackage(SUNMI_SCANNER_PACKAGE)
        }
      scannerServiceBound = bindService(intent, scannerConnection, Context.BIND_AUTO_CREATE)
      Log.d(LOG_TAG, "Scanner service bind requested: $scannerServiceBound")
      scannerServiceBound
    } catch (_: RuntimeException) {
      scannerServiceBound = false
      false
    }
  }

  private fun ensureScannerService(timeoutMs: Long = 900L): Boolean {
    if (scannerBinder != null) return true
    if (!bindScannerService()) return false

    val startedAt = SystemClock.elapsedRealtime()
    while (scannerBinder == null && SystemClock.elapsedRealtime() - startedAt < timeoutMs) {
      SystemClock.sleep(30)
    }
    return scannerBinder != null
  }

  private fun unbindScannerService() {
    if (!scannerServiceBound) return
    try {
      unbindService(scannerConnection)
    } catch (_: RuntimeException) {
    }
    scannerServiceBound = false
    scannerBinder = null
  }

  private fun stopScannerIfActive() {
    scannerStopRunnable?.let { labConnectWebView?.removeCallbacks(it) }
    scannerStopRunnable = null
    if (!scannerActive || scannerBinder == null) return
    callScannerTransaction(SUNMI_SCANNER_TRANSACTION_STOP)
    scannerActive = false
  }

  private fun resetScannerBeforeStart() {
    scannerStopRunnable?.let { labConnectWebView?.removeCallbacks(it) }
    scannerStopRunnable = null
    if (scannerBinder == null) return
    runCatching { callScannerTransaction(SUNMI_SCANNER_TRANSACTION_STOP) }
    scannerActive = false
    SystemClock.sleep(220)
  }

  private fun callScannerTransaction(transactionCode: Int) {
    val binder = scannerBinder ?: throw IllegalStateException("Service scanner non connecte")
    val data = Parcel.obtain()
    val reply = Parcel.obtain()
    try {
      data.writeInterfaceToken(SUNMI_SCANNER_INTERFACE)
      val ok = binder.transact(transactionCode, data, reply, 0)
      if (!ok) {
        scannerBinder = null
        throw IllegalStateException("Commande scanner refusee")
      }
      reply.readException()
      Log.d(LOG_TAG, "Scanner transaction $transactionCode ok")
    } finally {
      reply.recycle()
      data.recycle()
    }
  }

  private fun keepFullscreen() {
    @Suppress("DEPRECATION")
    window.decorView.systemUiVisibility =
      View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY or
        View.SYSTEM_UI_FLAG_LAYOUT_STABLE or
        View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION or
        View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN or
        View.SYSTEM_UI_FLAG_HIDE_NAVIGATION or
        View.SYSTEM_UI_FLAG_FULLSCREEN
  }

  private fun registerKioskEscapePress(): Boolean {
    val now = System.currentTimeMillis()
    if (now - backPressWindowStartedAt > 5000L) {
      backPressWindowStartedAt = now
      backPressCount = 0
    }

    backPressCount += 1
    return backPressCount >= 7
  }

  private fun exitKioskMode() {
    backPressCount = 0

    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.LOLLIPOP) {
      try {
        stopLockTask()
      } catch (_: RuntimeException) {
      }
    }

    val devicePolicyManager =
      getSystemService(Context.DEVICE_POLICY_SERVICE) as DevicePolicyManager
    val admin = ComponentName(this, LabConnectDeviceAdminReceiver::class.java)

    if (devicePolicyManager.isDeviceOwnerApp(packageName)) {
      try {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
          devicePolicyManager.setStatusBarDisabled(admin, false)
          devicePolicyManager.setKeyguardDisabled(admin, false)
        }
      } catch (_: RuntimeException) {
      }
    }

    @Suppress("DEPRECATION")
    window.decorView.systemUiVisibility = View.SYSTEM_UI_FLAG_VISIBLE
  }

  companion object {
    private const val SUNMI_SCANNER_PACKAGE = "com.sunmi.scanner"
    private const val LOG_TAG = "LabConnectPrint"
    private const val SUNMI_SCANNER_SERVICE_ACTION = "com.sunmi.scanner.IScanInterface"
    private const val SUNMI_SCANNER_INTERFACE = "com.sunmi.scanner.IScanInterface"
    private const val SUNMI_SCANNER_ACTION = "com.sunmi.scanner.ACTION_DATA_CODE_RECEIVED"
    private const val SUNMI_SCANNER_DATA_EXTRA = "data"
    private const val SUNMI_SCANNER_TRANSACTION_SCAN = 2
    private const val SUNMI_SCANNER_TRANSACTION_STOP = 3
  }
}
