package com.example.bikecomputer

import android.Manifest
import android.app.Notification
import android.app.NotificationChannel
import android.app.NotificationManager
import android.app.PendingIntent
import android.app.Service
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothGatt
import android.bluetooth.BluetoothGattCallback
import android.bluetooth.BluetoothGattCharacteristic
import android.bluetooth.BluetoothGattDescriptor
import android.bluetooth.BluetoothManager
import android.bluetooth.BluetoothProfile
import android.bluetooth.BluetoothStatusCodes
import android.bluetooth.le.ScanCallback
import android.bluetooth.le.ScanFilter
import android.bluetooth.le.ScanResult
import android.bluetooth.le.ScanSettings
import android.content.Context
import android.content.Intent
import android.content.pm.PackageManager
import android.content.pm.ServiceInfo
import android.location.Location
import android.location.LocationListener
import android.location.LocationManager
import android.os.Binder
import android.os.Build
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.ParcelUuid
import android.os.SystemClock
import android.provider.Settings
import android.util.Log
import java.nio.charset.StandardCharsets
import java.util.Locale
import java.util.UUID

class BikeBleLocationService : Service() {
    interface Listener {
        fun onStateChanged(newState: Int, status: String, location: Location?)
        fun onLog(message: String)
    }

    inner class LocalBinder : Binder() {
        val service: BikeBleLocationService
            get() = this@BikeBleLocationService
    }

    private val binder = LocalBinder()
    private val mainHandler = Handler(Looper.getMainLooper())
    private val writeLock = Any()

    var listener: Listener? = null
        set(value) {
            field = value
            notifyState()
        }

    private var bluetoothAdapter: BluetoothAdapter? = null
    private var scanner: android.bluetooth.le.BluetoothLeScanner? = null
    private var gatt: BluetoothGatt? = null
    private var writeCharacteristic: BluetoothGattCharacteristic? = null
    private var controlCharacteristic: BluetoothGattCharacteristic? = null
    private var locationManager: LocationManager? = null

    var lastLocation: Location? = null
        private set
    var state: Int = STATE_IDLE
        private set
    var status: String = "Idle"
        private set

    private var foreground = false
    private var workRequested = false
    private var scanning = false
    private var manualDisconnect = false
    private var locationStreamingRequested = false

    private var pendingWriteBytes: ByteArray? = null
    private var pendingWriteText: String? = null
    private var activeWriteBytes: ByteArray? = null
    private var activeWriteText: String? = null
    private var writeInFlight = false
    private var writeAttemptSerial = 0
    private var lastLocationQueuedAtMs = 0L

    private var periodicLocationSendScheduled = false
    private val periodicLocationSend = object : Runnable {
        override fun run() {
            if (!periodicLocationSendScheduled) return

            if (workRequested && state == STATE_CONNECTED && locationStreamingRequested) {
                val nowMs = SystemClock.elapsedRealtime()
                if (lastLocationQueuedAtMs == 0L ||
                    nowMs - lastLocationQueuedAtMs >= LOCATION_RESEND_INTERVAL_MS) {
                    lastLocation?.let(::queueLocation)
                }
            }

            if (periodicLocationSendScheduled) {
                mainHandler.postDelayed(this, LOCATION_RESEND_INTERVAL_MS)
            }
        }
    }

    private val scanCallback = object : ScanCallback() {
        override fun onScanResult(callbackType: Int, result: ScanResult) {
            val device = result.device
            val uuids = result.scanRecord?.serviceUuids
            val serviceMatch = uuids?.contains(ParcelUuid(SERVICE_UUID)) == true
            val nameMatch = DEVICE_NAME == deviceName(device)
            if (serviceMatch || nameMatch) {
                log("Found ${displayName(device)}; connecting")
                stopScanInternal()
                connect(device)
            }
        }

        override fun onScanFailed(errorCode: Int) {
            scanning = false
            setState(STATE_IDLE, "Scan failed: $errorCode")
            log("BLE scan failed: $errorCode")
        }
    }

    private val gattCallback = object : BluetoothGattCallback() {
        override fun onConnectionStateChange(
            callbackGatt: BluetoothGatt,
            statusCode: Int,
            newState: Int
        ) {
            when (newState) {
                BluetoothProfile.STATE_CONNECTED -> {
                    gatt = callbackGatt
                    manualDisconnect = false
                    clearWriteQueue()
                    setState(STATE_CONNECTING, "Connected; discovering services")
                    log("Connected to ${displayName(callbackGatt.device)} status=$statusCode")
                    if (hasConnectPermission()) callbackGatt.discoverServices()
                }
                BluetoothProfile.STATE_DISCONNECTED -> {
                    log("Disconnected status=$statusCode")
                    closeGatt(callbackGatt)
                    gatt = null
                    writeCharacteristic = null
                    controlCharacteristic = null
                    clearWriteQueue()
                    stopLocationStreaming()

                    if (manualDisconnect || !workRequested) {
                        manualDisconnect = false
                        stopForegroundWork("Disconnected")
                    } else {
                        setState(STATE_SCANNING, "Disconnected; scanning")
                        mainHandler.postDelayed(::startScanInternal, 500L)
                    }
                }
            }
        }

        override fun onServicesDiscovered(callbackGatt: BluetoothGatt, statusCode: Int) {
            if (statusCode != BluetoothGatt.GATT_SUCCESS) {
                log("Service discovery failed: $statusCode")
                disconnectGatt()
                return
            }

            val service = callbackGatt.getService(SERVICE_UUID)
            val rx = service?.getCharacteristic(RX_UUID)
            if (rx == null) {
                log("Location write characteristic not found")
                setState(STATE_CONNECTED, "Connected, but BLE service is incomplete")
                return
            }

            writeCharacteristic = rx
            controlCharacteristic = service.getCharacteristic(TX_UUID)
            if (hasConnectPermission()) {
                callbackGatt.requestConnectionPriority(BluetoothGatt.CONNECTION_PRIORITY_BALANCED)
            }
            log("Location write characteristic ready")
            enableControlNotifications(callbackGatt, controlCharacteristic)
            setState(STATE_CONNECTED, "Connected; waiting for maps screen")
        }

        override fun onCharacteristicWrite(
            callbackGatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            statusCode: Int
        ) {
            completeWrite(statusCode)
        }

        override fun onCharacteristicChanged(
            callbackGatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic,
            value: ByteArray
        ) {
            handleControlMessage(value)
        }

        @Deprecated("Deprecated in Android")
        override fun onCharacteristicChanged(
            callbackGatt: BluetoothGatt,
            characteristic: BluetoothGattCharacteristic
        ) {
            handleControlMessage(characteristic.value ?: return)
        }

        override fun onDescriptorWrite(
            callbackGatt: BluetoothGatt,
            descriptor: BluetoothGattDescriptor,
            statusCode: Int
        ) {
            if (descriptor.uuid == CCCD_UUID) {
                if (statusCode == BluetoothGatt.GATT_SUCCESS) {
                    log("BLE control notifications enabled")
                } else {
                    log("BLE control notification setup failed: $statusCode")
                }
            }
        }
    }

    private val locationListener = object : LocationListener {
        override fun onLocationChanged(location: Location) {
            lastLocation = location
            notifyState()
            if (locationStreamingRequested) queueLocation(location)
        }

        @Deprecated("Deprecated in Android")
        override fun onStatusChanged(provider: String?, status: Int, extras: Bundle?) = Unit

        override fun onProviderEnabled(provider: String) {
            log("Location provider enabled: $provider")
        }

        override fun onProviderDisabled(provider: String) {
            log("Location provider disabled: $provider")
        }
    }

    override fun onCreate() {
        super.onCreate()
        val bluetoothManager = getSystemService(BLUETOOTH_SERVICE) as BluetoothManager?
        bluetoothAdapter = bluetoothManager?.adapter
        locationManager = getSystemService(LOCATION_SERVICE) as LocationManager?
        createNotificationChannel()
    }

    override fun onStartCommand(intent: Intent?, flags: Int, startId: Int): Int {
        when (intent?.action) {
            ACTION_START_SCAN -> startScanning()
            ACTION_STOP_SCAN -> stopScanningAndIdle()
            ACTION_DISCONNECT -> disconnectAndStop()
        }
        return START_REDELIVER_INTENT
    }

    override fun onBind(intent: Intent): IBinder = binder

    override fun onDestroy() {
        disconnectGatt()
        stopScanInternal()
        stopLocationUpdates()
        stopPeriodicLocationSend()
        super.onDestroy()
    }

    fun startScanning() {
        workRequested = true
        manualDisconnect = false
        ensureForeground("Scanning for $DEVICE_NAME")
        startScanInternal()
    }

    fun stopScanningAndIdle() {
        workRequested = false
        manualDisconnect = true
        stopScanInternal()
        stopForegroundWork("Idle")
    }

    fun disconnectAndStop() {
        workRequested = false
        manualDisconnect = true
        stopScanInternal()
        stopLocationUpdates()
        setState(STATE_IDLE, "Disconnected")

        val localGatt = gatt
        gatt = null
        writeCharacteristic = null
        controlCharacteristic = null
        stopLocationStreaming()
        clearWriteQueue()
        if (localGatt != null && hasConnectPermission()) {
            localGatt.disconnect()
            mainHandler.postDelayed({ closeGatt(localGatt) }, 750L)
        }

        stopForegroundWork("Disconnected")
    }

    private fun startScanInternal() {
        if (!workRequested) return
        val adapter = bluetoothAdapter
        if (adapter == null) {
            setState(STATE_IDLE, "Bluetooth unavailable")
            log("This phone does not expose a Bluetooth adapter")
            return
        }
        if (!adapter.isEnabled) {
            setState(STATE_IDLE, "Bluetooth is off")
            log("Bluetooth is disabled")
            return
        }
        if (!hasScanPermission()) {
            setState(STATE_IDLE, "Missing Bluetooth scan permission")
            return
        }
        if (gatt != null || scanning) return

        scanner = adapter.bluetoothLeScanner
        val localScanner = scanner
        if (localScanner == null) {
            setState(STATE_IDLE, "BLE scanner unavailable")
            log("BluetoothLeScanner is null")
            return
        }

        val filter = ScanFilter.Builder().setServiceUuid(ParcelUuid(SERVICE_UUID)).build()
        val settings = ScanSettings.Builder()
            .setScanMode(ScanSettings.SCAN_MODE_LOW_LATENCY)
            .setMatchMode(ScanSettings.MATCH_MODE_AGGRESSIVE)
            .setCallbackType(ScanSettings.CALLBACK_TYPE_ALL_MATCHES)
            .build()
        localScanner.startScan(listOf(filter), settings, scanCallback)
        scanning = true
        setState(STATE_SCANNING, "Scanning for $DEVICE_NAME")
        log("BLE scan started")
    }

    private fun stopScanInternal() {
        val localScanner = scanner
        if (!scanning || localScanner == null || !hasScanPermission()) {
            scanning = false
            return
        }
        localScanner.stopScan(scanCallback)
        scanning = false
        log("BLE scan stopped")
    }

    private fun connect(device: BluetoothDevice) {
        if (!hasConnectPermission()) {
            setState(STATE_IDLE, "Missing Bluetooth connect permission")
            return
        }

        setState(STATE_CONNECTING, "Connecting to ${displayName(device)}")
        gatt = device.connectGatt(this, false, gattCallback, BluetoothDevice.TRANSPORT_LE)
    }

    private fun disconnectGatt() {
        val localGatt = gatt
        gatt = null
        writeCharacteristic = null
        controlCharacteristic = null
        stopLocationStreaming()
        clearWriteQueue()
        if (localGatt != null && hasConnectPermission()) {
            localGatt.disconnect()
            closeGatt(localGatt)
        }
    }

    private fun closeGatt(localGatt: BluetoothGatt?) {
        if (localGatt != null && hasConnectPermission()) localGatt.close()
    }

    private fun queueLocation(location: Location) {
        if (!locationStreamingRequested || state != STATE_CONNECTED) return

        lastLocationQueuedAtMs = SystemClock.elapsedRealtime()
        val payload = String.format(Locale.US, "%.6f,%.6f,%.1f",
            location.latitude, location.longitude, location.accuracy)
        synchronized(writeLock) {
            pendingWriteBytes = payload.toByteArray(StandardCharsets.UTF_8)
            pendingWriteText = payload
        }
        drainWriteQueue()
    }

    private fun enableControlNotifications(
        localGatt: BluetoothGatt,
        characteristic: BluetoothGattCharacteristic?
    ) {
        if (characteristic == null) {
            log("BLE control characteristic not found; location streaming commands unavailable")
            return
        }
        if (!hasConnectPermission()) return

        if (!localGatt.setCharacteristicNotification(characteristic, true)) {
            log("Failed to enable BLE control notifications locally")
            return
        }

        val descriptor = characteristic.getDescriptor(CCCD_UUID)
        if (descriptor == null) {
            log("BLE control CCCD not found")
            return
        }

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val result = localGatt.writeDescriptor(
                descriptor, BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE)
            if (result != BluetoothStatusCodes.SUCCESS) {
                log("BLE control CCCD write not accepted: $result")
            }
        } else {
            @Suppress("DEPRECATION")
            descriptor.value = BluetoothGattDescriptor.ENABLE_NOTIFICATION_VALUE
            @Suppress("DEPRECATION")
            if (!localGatt.writeDescriptor(descriptor)) {
                log("BLE control CCCD write not accepted")
            }
        }
    }

    private fun handleControlMessage(value: ByteArray) {
        val message = String(value, StandardCharsets.UTF_8).trim()
        when (message) {
            CONTROL_LOCATION_START -> {
                log("ESP32 requested location streaming")
                startLocationStreaming()
            }
            CONTROL_LOCATION_STOP -> {
                log("ESP32 paused location streaming")
                stopLocationStreaming()
                if (state == STATE_CONNECTED) {
                    setState(STATE_CONNECTED, "Connected; location paused")
                }
            }
            else -> log("ESP32 status: $message")
        }
    }

    private fun startLocationStreaming() {
        locationStreamingRequested = true
        startLocationUpdates()
        startPeriodicLocationSend()
        if (state == STATE_CONNECTED) {
            setState(STATE_CONNECTED, "Connected; sending location")
        }
        lastLocation?.let(::queueLocation)
    }

    private fun stopLocationStreaming() {
        locationStreamingRequested = false
        stopLocationUpdates()
        stopPeriodicLocationSend()
        clearWriteQueue()
    }

    private fun drainWriteQueue() {
        val localGatt: BluetoothGatt
        val characteristic: BluetoothGattCharacteristic
        val bytes: ByteArray
        val text: String
        val writeSerial: Int

        synchronized(writeLock) {
            if (writeInFlight || pendingWriteBytes == null) return
            localGatt = gatt ?: return
            characteristic = writeCharacteristic ?: return
            if (!hasConnectPermission()) return

            bytes = pendingWriteBytes ?: return
            text = pendingWriteText ?: ""
            pendingWriteBytes = null
            pendingWriteText = null
            activeWriteBytes = bytes
            activeWriteText = text
            writeInFlight = true
            writeAttemptSerial += 1
            writeSerial = writeAttemptSerial
        }

        characteristic.writeType = BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU) {
            val result = localGatt.writeCharacteristic(
                characteristic, bytes, BluetoothGattCharacteristic.WRITE_TYPE_DEFAULT)
            if (result != BluetoothStatusCodes.SUCCESS) {
                retryActiveWrite("Location write not accepted: $text result=$result")
            } else {
                log("Location write queued: $text")
                scheduleWriteTimeout(writeSerial)
            }
        } else {
            characteristic.value = bytes
            if (!localGatt.writeCharacteristic(characteristic)) {
                retryActiveWrite("Location write not accepted: $text")
            } else {
                log("Location write queued: $text")
                scheduleWriteTimeout(writeSerial)
            }
        }
    }

    private fun completeWrite(statusCode: Int) {
        val shouldRetry = statusCode != BluetoothGatt.GATT_SUCCESS
        val text: String?
        synchronized(writeLock) {
            text = activeWriteText
            if (shouldRetry && pendingWriteBytes == null && activeWriteBytes != null) {
                pendingWriteBytes = activeWriteBytes
                pendingWriteText = activeWriteText
            }
            activeWriteBytes = null
            activeWriteText = null
            writeInFlight = false
        }

        if (shouldRetry) {
            log("Location write failed: $text status=$statusCode; retrying")
            mainHandler.postDelayed(::drainWriteQueue, 500L)
        } else {
            log("Location write completed: $text")
            drainWriteQueue()
        }
    }

    private fun retryActiveWrite(message: String) {
        synchronized(writeLock) {
            if (pendingWriteBytes == null && activeWriteBytes != null) {
                pendingWriteBytes = activeWriteBytes
                pendingWriteText = activeWriteText
            }
            activeWriteBytes = null
            activeWriteText = null
            writeInFlight = false
        }
        log("$message; retrying")
        mainHandler.postDelayed(::drainWriteQueue, 500L)
    }

    private fun scheduleWriteTimeout(writeSerial: Int) {
        mainHandler.postDelayed({ handleWriteTimeout(writeSerial) }, WRITE_TIMEOUT_MS)
    }

    private fun handleWriteTimeout(writeSerial: Int) {
        val text: String?
        synchronized(writeLock) {
            if (!writeInFlight || writeAttemptSerial != writeSerial) return

            text = activeWriteText
            if (pendingWriteBytes == null && activeWriteBytes != null) {
                pendingWriteBytes = activeWriteBytes
                pendingWriteText = activeWriteText
            }
            activeWriteBytes = null
            activeWriteText = null
            writeInFlight = false
        }

        log("Location write timed out: $text; retrying")
        drainWriteQueue()
    }

    private fun clearWriteQueue() {
        synchronized(writeLock) {
            pendingWriteBytes = null
            pendingWriteText = null
            activeWriteBytes = null
            activeWriteText = null
            writeInFlight = false
            writeAttemptSerial += 1
        }
    }

    private fun startLocationUpdates() {
        val manager = locationManager
        if (manager == null || !hasLocationPermission()) {
            setState(state, "Missing location permission")
            return
        }

        stopLocationUpdates()
        var anyProvider = false
        if (manager.isProviderEnabled(LocationManager.GPS_PROVIDER)) {
            manager.requestLocationUpdates(
                LocationManager.GPS_PROVIDER, LOCATION_UPDATE_INTERVAL_MS, 0.0f,
                locationListener, Looper.getMainLooper())
            anyProvider = true
            log("GPS location updates started")
        }
        if (manager.isProviderEnabled(LocationManager.NETWORK_PROVIDER)) {
            manager.requestLocationUpdates(
                LocationManager.NETWORK_PROVIDER, LOCATION_UPDATE_INTERVAL_MS, 0.0f,
                locationListener, Looper.getMainLooper())
            anyProvider = true
            log("Network location updates started")
        }

        val lastKnown = when {
            manager.isProviderEnabled(LocationManager.GPS_PROVIDER) ->
                manager.getLastKnownLocation(LocationManager.GPS_PROVIDER)
            manager.isProviderEnabled(LocationManager.NETWORK_PROVIDER) ->
                manager.getLastKnownLocation(LocationManager.NETWORK_PROVIDER)
            else -> null
        }
        if (lastKnown != null) locationListener.onLocationChanged(lastKnown)

        if (!anyProvider) {
            setState(state, "Enable phone location")
            log("No location provider enabled; open ${Settings.ACTION_LOCATION_SOURCE_SETTINGS}")
        }
    }

    private fun stopLocationUpdates() {
        locationManager?.removeUpdates(locationListener)
    }

    private fun startPeriodicLocationSend() {
        if (periodicLocationSendScheduled) return

        periodicLocationSendScheduled = true
        mainHandler.postDelayed(periodicLocationSend, LOCATION_RESEND_INTERVAL_MS)
        log("Periodic location resend started")
    }

    private fun stopPeriodicLocationSend() {
        if (!periodicLocationSendScheduled) return

        periodicLocationSendScheduled = false
        mainHandler.removeCallbacks(periodicLocationSend)
        log("Periodic location resend stopped")
    }

    private fun ensureForeground(text: String) {
        val notification = buildNotification(text)
        if (!foreground) {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE) {
                startForeground(NOTIFICATION_ID, notification,
                    ServiceInfo.FOREGROUND_SERVICE_TYPE_LOCATION or ServiceInfo.FOREGROUND_SERVICE_TYPE_CONNECTED_DEVICE)
            } else {
                startForeground(NOTIFICATION_ID, notification)
            }
            foreground = true
        } else {
            (getSystemService(NOTIFICATION_SERVICE) as NotificationManager?)
                ?.notify(NOTIFICATION_ID, notification)
        }
    }

    private fun stopForegroundWork(text: String) {
        locationStreamingRequested = false
        stopLocationUpdates()
        stopScanInternal()
        stopPeriodicLocationSend()
        workRequested = false
        setState(STATE_IDLE, text)
        if (foreground) {
            stopForeground(STOP_FOREGROUND_REMOVE)
            foreground = false
        }
        stopSelf()
    }

    private fun buildNotification(text: String): Notification {
        val pendingIntent = PendingIntent.getActivity(
            this, 0, Intent(this, MainActivity::class.java), PendingIntent.FLAG_IMMUTABLE)
        val builder = Notification.Builder(this, CHANNEL_ID)
        return builder
            .setContentTitle("Bike computer location")
            .setContentText(text)
            .setSmallIcon(android.R.drawable.stat_sys_data_bluetooth)
            .setContentIntent(pendingIntent)
            .setOngoing(true)
            .build()
    }

    private fun createNotificationChannel() {
        val channel = NotificationChannel(
            CHANNEL_ID, "Bike computer location", NotificationManager.IMPORTANCE_LOW)
        (getSystemService(NOTIFICATION_SERVICE) as NotificationManager?)
            ?.createNotificationChannel(channel)
    }

    private fun setState(newState: Int, newStatus: String) {
        state = newState
        status = newStatus
        Log.i(TAG, "State changed: $newStatus")
        if (foreground) ensureForeground(newStatus)
        notifyState()
    }

    private fun notifyState() {
        val localListener = listener ?: return
        mainHandler.post { localListener.onStateChanged(state, status, lastLocation) }
    }

    private fun log(message: String) {
        Log.i(TAG, message)
        val localListener = listener ?: return
        mainHandler.post { localListener.onLog(message) }
    }

    private fun hasScanPermission() =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                hasPermission(Manifest.permission.BLUETOOTH_SCAN)

    private fun hasConnectPermission() =
        Build.VERSION.SDK_INT < Build.VERSION_CODES.S ||
                hasPermission(Manifest.permission.BLUETOOTH_CONNECT)

    private fun hasLocationPermission() = hasPermission(Manifest.permission.ACCESS_FINE_LOCATION)

    private fun hasPermission(permission: String) =
        checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED

    private fun deviceName(device: BluetoothDevice?): String? {
        if (device == null || !hasConnectPermission()) return null
        return try {
            device.name
        } catch (ex: SecurityException) {
            null
        }
    }

    private fun displayName(device: BluetoothDevice?): String {
        if (device == null) return "unknown"
        val address = if (hasConnectPermission()) device.address else "no-address-permission"
        return "${deviceName(device) ?: "unnamed"} ($address)"
    }

    companion object {
        const val ACTION_START_SCAN = "com.example.bikecomputer.action.START_SCAN"
        const val ACTION_STOP_SCAN = "com.example.bikecomputer.action.STOP_SCAN"
        const val ACTION_DISCONNECT = "com.example.bikecomputer.action.DISCONNECT"

        const val STATE_IDLE = 0
        const val STATE_SCANNING = 1
        const val STATE_CONNECTING = 2
        const val STATE_CONNECTED = 3

        private const val NOTIFICATION_ID = 42
        private const val CHANNEL_ID = "bike_ble_location"
        private const val TAG = "BikeBleLocationService"
        private const val DEVICE_NAME = "BikeComputer"
        private const val LOCATION_UPDATE_INTERVAL_MS = 2_000L
        private const val LOCATION_RESEND_INTERVAL_MS = 3_000L
        private const val WRITE_TIMEOUT_MS = 5_000L
        private const val CONTROL_LOCATION_START = "location_start"
        private const val CONTROL_LOCATION_STOP = "location_stop"

        private val SERVICE_UUID: UUID =
            UUID.fromString("6E400001-B5A3-F393-E0A9-E50E24DCCA9E")
        private val RX_UUID: UUID =
            UUID.fromString("6E400002-B5A3-F393-E0A9-E50E24DCCA9E")
        private val TX_UUID: UUID =
            UUID.fromString("6E400003-B5A3-F393-E0A9-E50E24DCCA9E")
        private val CCCD_UUID: UUID =
            UUID.fromString("00002902-0000-1000-8000-00805F9B34FB")
    }
}
