package com.example.bikecomputer

import android.Manifest
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothManager
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.content.pm.PackageManager
import android.graphics.Typeface
import android.graphics.drawable.GradientDrawable
import android.location.Location
import android.os.Build
import android.os.Bundle
import android.os.IBinder
import android.text.method.ScrollingMovementMethod
import android.view.View
import android.widget.LinearLayout
import android.widget.ScrollView
import android.widget.TextView
import androidx.appcompat.app.AppCompatActivity
import com.google.android.material.R as MaterialR
import com.google.android.material.button.MaterialButton
import com.google.android.material.color.DynamicColors
import com.google.android.material.color.MaterialColors
import java.util.Locale

class MainActivity : AppCompatActivity(), BikeBleLocationService.Listener {
    private var service: BikeBleLocationService? = null
    private var bound = false
    private var pendingStartScan = false
    private var state = BikeBleLocationService.STATE_IDLE

    private lateinit var statusView: TextView
    private lateinit var locationView: TextView
    private lateinit var logView: TextView
    private lateinit var logScroll: ScrollView
    private lateinit var actionButton: MaterialButton
    private lateinit var palette: Palette

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName, binder: IBinder) {
            service = (binder as BikeBleLocationService.LocalBinder).service
            bound = true
            service?.listener = this@MainActivity
            service?.let { onStateChanged(it.state, it.status, it.lastLocation) }

            if (pendingStartScan) {
                pendingStartScan = false
                startScanWork()
            }
        }

        override fun onServiceDisconnected(name: ComponentName) {
            service?.listener = null
            service = null
            bound = false
            onStateChanged(BikeBleLocationService.STATE_IDLE, "Service stopped", null)
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        DynamicColors.applyToActivityIfAvailable(this)
        super.onCreate(savedInstanceState)
        palette = Palette.from(this)
        buildUi()
        actionButton.setOnClickListener { handleActionButton() }
        requestNeededPermissions()
    }

    override fun onStart() {
        super.onStart()
        bindService(Intent(this, BikeBleLocationService::class.java), connection, BIND_AUTO_CREATE)
    }

    override fun onResume() {
        super.onResume()
        if (pendingStartScan && hasAllPermissions() && isBluetoothEnabled()) {
            startScanWork()
        }
    }

    override fun onStop() {
        if (bound) {
            service?.listener = null
            unbindService(connection)
            bound = false
            service = null
        }
        super.onStop()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode != REQUEST_PERMISSIONS) return

        if (hasAllPermissions()) {
            appendLog("Permissions granted")
            if (pendingStartScan) startScanWork()
        } else {
            pendingStartScan = false
            onStateChanged(BikeBleLocationService.STATE_IDLE, "Missing permissions", null)
            appendLog("Bluetooth, notification, and fine location permissions are required")
        }
    }

    override fun onStateChanged(newState: Int, status: String, location: Location?) {
        runOnUiThread {
            state = newState
            statusView.text = status
            if (location != null) locationView.text = formatLocation(location)
            updateActionButton()
        }
    }

    override fun onLog(message: String) {
        appendLog(message)
    }

    private fun buildUi() {
        val root = LinearLayout(this).apply {
            orientation = LinearLayout.VERTICAL
            setPadding(dp(20), dp(18), dp(20), dp(18))
            setBackgroundColor(palette.background)
        }

        root.addView(TextView(this).apply {
            text = "Bike Computer"
            textSize = 28f
            typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
            setTextColor(palette.onBackground)
        })

        root.addView(surface().apply {
            addView(TextView(context).apply {
                text = "CONNECTION"
                textSize = 11f
                typeface = Typeface.DEFAULT_BOLD
                setTextColor(palette.secondaryText)
                letterSpacing = 0.05f
            })
            statusView = TextView(context).apply {
                text = "Idle"
                textSize = 22f
                typeface = Typeface.create("sans-serif-medium", Typeface.NORMAL)
                setTextColor(palette.onSurface)
            }
            addView(statusView)
        }, blockParams(top = 24))

        root.addView(surface().apply {
            addView(TextView(context).apply {
                text = "PHONE LOCATION"
                textSize = 11f
                typeface = Typeface.DEFAULT_BOLD
                setTextColor(palette.secondaryText)
                letterSpacing = 0.05f
            })
            locationView = TextView(context).apply {
                text = "No location yet"
                textSize = 18f
                setTextColor(palette.onSurface)
            }
            addView(locationView)
        }, blockParams(top = 12))

        actionButton = MaterialButton(this).apply {
            text = "Start scanning"
            textSize = 15f
            cornerRadius = dp(28)
            setPadding(0, dp(14), 0, dp(14))
            insetBottom = 0
            insetTop = 0
            stateListAnimator = null
        }
        root.addView(actionButton, blockParams(top = 20))

        logView = TextView(this).apply {
            textSize = 12f
            setTextColor(palette.onSurface)
            movementMethod = ScrollingMovementMethod()
            setPadding(dp(16), dp(14), dp(16), dp(14))
            background = rounded(palette.surfaceVariant, 16f)
            typeface = Typeface.MONOSPACE
        }
        logScroll = ScrollView(this).apply { 
            isVerticalScrollBarEnabled = false
            addView(logView) 
        }
        root.addView(logScroll, LinearLayout.LayoutParams(
            LinearLayout.LayoutParams.MATCH_PARENT, 0, 1f
        ).apply { topMargin = dp(20) })

        setContentView(root)
    }

    private fun surface() = LinearLayout(this).apply {
        orientation = LinearLayout.VERTICAL
        setPadding(dp(20), dp(18), dp(20), dp(18))
        background = rounded(palette.surfaceContainer, 24f)
    }

    private fun blockParams(top: Int) = LinearLayout.LayoutParams(
        LinearLayout.LayoutParams.MATCH_PARENT,
        LinearLayout.LayoutParams.WRAP_CONTENT
    ).apply { topMargin = dp(top) }

    private fun handleActionButton() {
        when (state) {
            BikeBleLocationService.STATE_IDLE -> {
                pendingStartScan = true
                if (!hasAllPermissions()) {
                    requestNeededPermissions()
                    return
                }
                if (!isBluetoothEnabled()) {
                    appendLog("Requesting Bluetooth enable")
                    startActivity(Intent(BluetoothAdapter.ACTION_REQUEST_ENABLE))
                    return
                }
                startScanWork()
            }
            BikeBleLocationService.STATE_SCANNING -> stopScanWork()
            else -> disconnectWork()
        }
    }

    private fun startScanWork() {
        pendingStartScan = false
        if (!hasAllPermissions()) {
            requestNeededPermissions()
            return
        }

        val intent = Intent(this, BikeBleLocationService::class.java)
            .setAction(BikeBleLocationService.ACTION_START_SCAN)
        startForegroundService(intent)
        service?.startScanning()
    }

    private fun stopScanWork() {
        service?.stopScanningAndIdle() ?: startService(
            Intent(this, BikeBleLocationService::class.java)
                .setAction(BikeBleLocationService.ACTION_STOP_SCAN)
        )
    }

    private fun disconnectWork() {
        service?.disconnectAndStop() ?: startService(
            Intent(this, BikeBleLocationService::class.java)
                .setAction(BikeBleLocationService.ACTION_DISCONNECT)
        )
    }

    private fun updateActionButton() {
        actionButton.text = when (state) {
            BikeBleLocationService.STATE_SCANNING -> "Stop scanning"
            BikeBleLocationService.STATE_CONNECTING -> "Cancel"
            BikeBleLocationService.STATE_CONNECTED -> "Disconnect"
            else -> "Start scanning"
        }
    }

    private fun requestNeededPermissions() {
        val missing = requiredPermissions().filterNot(::hasPermission)
        if (missing.isNotEmpty()) {
            requestPermissions(missing.toTypedArray(), REQUEST_PERMISSIONS)
        }
    }

    private fun requiredPermissions(): List<String> = buildList {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.S) {
            add(Manifest.permission.BLUETOOTH_SCAN)
            add(Manifest.permission.BLUETOOTH_CONNECT)
        }
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            add(Manifest.permission.POST_NOTIFICATIONS)
        }
        add(Manifest.permission.ACCESS_FINE_LOCATION)
    }

    private fun hasAllPermissions() = requiredPermissions().all(::hasPermission)

    private fun hasPermission(permission: String) =
        checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private fun isBluetoothEnabled(): Boolean =
        getSystemService(BluetoothManager::class.java).adapter?.isEnabled == true

    private fun formatLocation(location: Location): String =
        String.format(Locale.US, "%.6f, %.6f, %.1fm",
            location.latitude, location.longitude, location.accuracy)

    private fun appendLog(message: String) {
        runOnUiThread {
            logView.append("$message\n")
            logScroll.postDelayed({ logScroll.fullScroll(View.FOCUS_DOWN) }, 50L)
        }
    }

    private fun rounded(color: Int, radiusDp: Float) = GradientDrawable().apply {
        setColor(color)
        cornerRadius = dp(radiusDp.toInt()).toFloat()
    }

    private fun dp(value: Int): Int =
        (value * resources.displayMetrics.density + 0.5f).toInt()

    private data class Palette(
        val background: Int,
        val surfaceContainer: Int,
        val surfaceVariant: Int,
        val primary: Int,
        val onPrimary: Int,
        val onBackground: Int,
        val onSurface: Int,
        val secondaryText: Int
    ) {
        companion object {
            fun from(context: Context): Palette {
                return Palette(
                    background = MaterialColors.getColor(context, android.R.attr.colorBackground, 0),
                    surfaceContainer = MaterialColors.getColor(context, MaterialR.attr.colorSurfaceContainer, 0),
                    surfaceVariant = MaterialColors.getColor(context, MaterialR.attr.colorSurfaceVariant, 0),
                    primary = MaterialColors.getColor(context, android.R.attr.colorPrimary, 0),
                    onPrimary = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnPrimary, 0),
                    onBackground = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnBackground, 0),
                    onSurface = MaterialColors.getColor(context, com.google.android.material.R.attr.colorOnSurface, 0),
                    secondaryText = MaterialColors.getColor(context, android.R.attr.colorSecondary, 0)
                )
            }
        }
    }

    companion object {
        private const val REQUEST_PERMISSIONS = 1001
    }
}
