package com.mx5dash.obd2android

import android.app.Application
import com.mx5dash.obd2android.bluetooth.BluetoothSerialManager
import com.mx5dash.obd2android.bridge.NativeBridge
import com.mx5dash.obd2android.simulation.TelemetrySimulator
import com.mx5dash.obd2android.storage.DataLogger
import com.mx5dash.obd2android.storage.PreferencesManager

class Mx5Application : Application() {

    lateinit var preferencesManager: PreferencesManager
        private set

    lateinit var dataLogger: DataLogger
        private set

    lateinit var telemetrySimulator: TelemetrySimulator
        private set

    lateinit var bluetoothManager: BluetoothSerialManager
        private set

    override fun onCreate() {
        super.onCreate()
        preferencesManager = PreferencesManager(this)
        dataLogger = DataLogger(this)
        telemetrySimulator = TelemetrySimulator(dataLogger)
        bluetoothManager = BluetoothSerialManager(this, dataLogger, telemetrySimulator)

        // Initialize Native C++ LVGL Engine with SharedPreferences bridge
        NativeBridge.nativeInit(preferencesManager.bridge)

        // Start Dev/Demo Live Telemetry Simulator by default
        telemetrySimulator.start()
    }
}
