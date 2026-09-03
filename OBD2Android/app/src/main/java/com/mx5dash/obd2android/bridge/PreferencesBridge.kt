package com.mx5dash.obd2android.bridge

import android.content.Context
import android.content.SharedPreferences

class PreferencesBridge(context: Context) {
    private val prefs: SharedPreferences =
        context.getSharedPreferences("mx5_dash_prefs", Context.MODE_PRIVATE)

    fun saveInt(key: String, value: Int) {
        prefs.edit().putInt(key, value).apply()
    }

    fun saveBoolean(key: String, value: Boolean) {
        prefs.edit().putBoolean(key, value).apply()
    }

    fun saveString(key: String, value: String) {
        prefs.edit().putString(key, value).apply()
    }

    fun getInt(key: String, default: Int): Int = prefs.getInt(key, default)
    fun getBoolean(key: String, default: Boolean): Boolean = prefs.getBoolean(key, default)
    fun getString(key: String, default: String): String = prefs.getString(key, default) ?: default
}
