package com.mx5dash.obd2android.storage

import android.content.Context
import com.mx5dash.obd2android.bridge.PreferencesBridge

class PreferencesManager(context: Context) {
    val bridge = PreferencesBridge(context)

    var rotation: Int
        get() = bridge.getInt("rotation", 1)
        set(value) = bridge.saveInt("rotation", value)

    var unitsUs: Boolean
        get() = bridge.getBoolean("units_us", true)
        set(value) = bridge.saveBoolean("units_us", value)

    var themeMode: Int
        get() = bridge.getInt("theme_mode", 0)
        set(value) = bridge.saveInt("theme_mode", value)

    var brightness: Int
        get() = bridge.getInt("brightness", 95)
        set(value) = bridge.saveInt("brightness", value)

    var autoLog: Boolean
        get() = bridge.getBoolean("auto_log", true)
        set(value) = bridge.saveBoolean("auto_log", value)

    var speedMask: Boolean
        get() = bridge.getBoolean("speed_mask", true)
        set(value) = bridge.saveBoolean("speed_mask", value)

    var blePrefix: String
        get() = bridge.getString("ble_prefix", "vLinker")
        set(value) = bridge.saveString("ble_prefix", value)

    fun getWheelDid(wheel: Int): String = bridge.getString("wheel_did_$wheel", when(wheel) {
        0 -> "2A05"
        1 -> "2A08"
        2 -> "2A0B"
        else -> "2A0E"
    })

    fun setWheelDid(wheel: Int, did: String) = bridge.saveString("wheel_did_$wheel", did)
}
