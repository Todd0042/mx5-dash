package com.mx5dash.obd2android.ui

import android.Manifest
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Bundle
import android.view.View
import android.view.WindowManager
import android.widget.TextView
import android.widget.Toast
import androidx.activity.result.contract.ActivityResultContracts
import androidx.appcompat.app.AppCompatActivity
import androidx.core.content.ContextCompat
import com.mx5dash.obd2android.Mx5Application
import com.mx5dash.obd2android.R
import com.mx5dash.obd2android.bluetooth.BluetoothService
import com.mx5dash.obd2android.bridge.NativeBridge
import com.mx5dash.obd2android.storage.IniExporter

class MainActivity : AppCompatActivity() {

    private val app get() = application as Mx5Application
    private lateinit var statusPill: TextView

    private val requestBluetoothPermission =
        registerForActivityResult(ActivityResultContracts.RequestPermission()) { isGranted ->
            if (isGranted) {
                BluetoothService.start(this)
            } else {
                Toast.makeText(this, R.string.bt_permission_denied, Toast.LENGTH_SHORT).show()
            }
        }

    val exportDocumentLauncher =
        registerForActivityResult(ActivityResultContracts.CreateDocument("text/plain")) { uri: Uri? ->
            if (uri != null) {
                val ok = IniExporter.exportToUri(contentResolver, uri, app.preferencesManager)
                if (ok) {
                    Toast.makeText(this, R.string.export_success, Toast.LENGTH_SHORT).show()
                }
            }
        }

    private var lastConnected: Boolean? = null
    private var lastStatusLabel: String? = null

    private val connectionListener: (Boolean, String) -> Unit = { connected, label ->
        runOnUiThread {
            if (lastConnected == connected && lastStatusLabel == label) {
                return@runOnUiThread
            }
            lastConnected = connected
            lastStatusLabel = label

            statusPill.animate().cancel()
            statusPill.alpha = 1.0f
            statusPill.visibility = View.VISIBLE

            if (connected) {
                statusPill.text = "LIVE OBD-II • $label"
                statusPill.setTextColor(ContextCompat.getColor(this, R.color.mx5_ok))

                // Auto-fade status pill after 3 seconds when successfully connected
                statusPill.postDelayed({
                    if (app.bluetoothManager.isConnected) {
                        statusPill.animate()
                            .alpha(0f)
                            .setDuration(500)
                            .withEndAction {
                                statusPill.visibility = View.GONE
                                statusPill.alpha = 1.0f
                            }
                            .start()
                    }
                }, 3000)
            } else {
                statusPill.text = label
                val isWarn = label.contains("DISCONNECTED", ignoreCase = true) ||
                             label.contains("RECONNECTING", ignoreCase = true)
                statusPill.setTextColor(
                    ContextCompat.getColor(this, if (isWarn) R.color.mx5_accent else R.color.mx5_dim)
                )
            }
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setFullscreenImmersive()

        setContentView(R.layout.activity_main)
        statusPill = findViewById(R.id.connStatusPill)

        statusPill.visibility = View.VISIBLE
        statusPill.text = "SEARCHING FOR OBD-II SCANNER…"
        statusPill.setOnClickListener {
            if (!app.bluetoothManager.isConnected) {
                statusPill.text = "RECONNECTING TO OBD-II SCANNER…"
                statusPill.setTextColor(ContextCompat.getColor(this, R.color.mx5_accent))
                app.bluetoothManager.reconnectNow()
            }
        }

        app.bluetoothManager.addConnectionListener(connectionListener)

        checkAndRequestPermissions()
    }

    override fun onDestroy() {
        app.bluetoothManager.removeConnectionListener(connectionListener)
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        setFullscreenImmersive()
    }

    private fun setFullscreenImmersive() {
        window.decorView.systemUiVisibility = (
            View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
            or View.SYSTEM_UI_FLAG_LAYOUT_STABLE
            or View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
            or View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
            or View.SYSTEM_UI_FLAG_FULLSCREEN
        )
        window.addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON)
    }

    private fun checkAndRequestPermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            if (ContextCompat.checkSelfPermission(this, Manifest.permission.BLUETOOTH_CONNECT)
                == PackageManager.PERMISSION_GRANTED) {
                BluetoothService.start(this)
            } else {
                requestBluetoothPermission.launch(Manifest.permission.BLUETOOTH_CONNECT)
            }
        } else {
            BluetoothService.start(this)
        }
    }

    @Deprecated("Deprecated in Java")
    override fun onBackPressed() {
        NativeBridge.nativeToggleMenu()
    }
}
