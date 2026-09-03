package com.mx5dash.obd2android.storage

import android.content.Context
import android.util.Log
import java.io.File
import java.io.FileWriter
import java.text.SimpleDateFormat
import java.util.Date
import java.util.Locale
import java.util.concurrent.atomic.AtomicBoolean

class DataLogger(private val context: Context) {

    companion object {
        private const val TAG = "DataLogger"
        private const val DIR_SESSIONS = "sessions"
        private const val DIR_INCIDENTS = "incidents"
    }

    private val enabled = AtomicBoolean(true)
    private var writer: FileWriter? = null
    private var sessionFile: File? = null
    private var headerWritten = false

    private val fileDateFormat = SimpleDateFormat("yyyyMMdd_HHmmss", Locale.US)
    private val rowDateFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.US)

    val sessionsDir: File get() = File(context.getExternalFilesDir(null), DIR_SESSIONS)
    val incidentsDir: File get() = File(context.getExternalFilesDir(null), DIR_INCIDENTS)

    fun startSession() {
        if (!enabled.get()) return
        try {
            val dir = sessionsDir.apply { mkdirs() }
            val file = File(dir, "session_${fileDateFormat.format(Date())}.csv")
            sessionFile = file
            writer = FileWriter(file, true)
            headerWritten = false
            Log.i(TAG, "Started rolling session: ${file.absolutePath}")
        } catch (e: Exception) {
            Log.e(TAG, "Error starting session file", e)
        }
    }

    fun logTelemetry(
        rpm: Int, speedKmh: Int, coolantC: Int, loadPct: Int,
        throttlePct: Int, fuelPct: Int, batVolts: Float, afr: Float, knock: Float
    ) {
        val w = writer ?: return
        try {
            if (!headerWritten) {
                w.write("timestamp,rpm,speed_kmh,coolant_c,load_pct,throttle_pct,fuel_pct,bat_volts,afr,knock_deg\n")
                headerWritten = true
            }
            w.write("${rowDateFormat.format(Date())},$rpm,$speedKmh,$coolantC,$loadPct,$throttlePct,$fuelPct,${"%.2f".format(Locale.US, batVolts)},${"%.2f".format(Locale.US, afr)},${"%.2f".format(Locale.US, knock)}\n")
            w.flush()
        } catch (e: Exception) {
            Log.e(TAG, "Failed writing telemetry row", e)
        }
    }

    fun captureIncident(reason: String) {
        val src = sessionFile ?: return
        try {
            val dir = incidentsDir.apply { mkdirs() }
            val cleanReason = reason.replace(Regex("[^A-Za-z0-9_]"), "_")
            val dst = File(dir, "incident_${fileDateFormat.format(Date())}_$cleanReason.csv")
            if (src.exists()) {
                src.copyTo(dst, overwrite = true)
                Log.i(TAG, "Incident archived successfully: ${dst.absolutePath}")
            }
        } catch (e: Exception) {
            Log.e(TAG, "Failed capturing incident archive", e)
        }
    }

    fun stopSession() {
        try {
            writer?.close()
        } catch (e: Exception) {
            Log.e(TAG, "Error closing session writer", e)
        }
        writer = null
    }

    fun setEnabled(e: Boolean) {
        enabled.set(e)
    }
}
