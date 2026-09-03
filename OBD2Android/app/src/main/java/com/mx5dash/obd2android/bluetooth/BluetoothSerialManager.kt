package com.mx5dash.obd2android.bluetooth

import android.annotation.SuppressLint
import android.bluetooth.BluetoothAdapter
import android.bluetooth.BluetoothDevice
import android.bluetooth.BluetoothSocket
import android.content.Context
import android.util.Log
import com.mx5dash.obd2android.bridge.NativeBridge
import com.mx5dash.obd2android.simulation.TelemetrySimulator
import com.mx5dash.obd2android.storage.DataLogger
import java.io.BufferedReader
import java.io.InputStreamReader
import java.io.OutputStream
import java.util.UUID
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.max
import kotlin.math.min

class BluetoothSerialManager(
    private val context: Context,
    private val dataLogger: DataLogger,
    private val telemetrySimulator: TelemetrySimulator
) {

    companion object {
        private const val TAG = "BtSerialManager"
        private val SPP_UUID: UUID = UUID.fromString("00001101-0000-1000-8000-00805F9B34FB")
        private val ADAPTER_PREFIXES = listOf(
            "VLINK", "V-LINK", "OBD", "ELM", "CARISTA", "SCANTOOL",
            "VIECAR", "BAFX", "VEEPEAK", "TONWON", "KONNWEI", "LELINK",
            "EDIAG", "BLUETOOTH", "IOS-VLINK", "NEXAS"
        )
    }

    private val running = AtomicBoolean(false)
    private var workerThread: Thread? = null
    private var socket: BluetoothSocket? = null

    val isConnected: Boolean
        get() = (socket?.isConnected == true)

    private val connectionListeners = java.util.concurrent.CopyOnWriteArrayList<(Boolean, String) -> Unit>()

    fun addConnectionListener(listener: (Boolean, String) -> Unit) {
        connectionListeners.add(listener)
        // If already connected, immediately notify current state
        if (isConnected) {
            listener(true, socket?.remoteDevice?.name ?: "OBD-II Scanner")
        }
    }

    fun removeConnectionListener(listener: (Boolean, String) -> Unit) {
        connectionListeners.remove(listener)
    }

    var onConnectionStateChanged: ((Boolean, String) -> Unit)? = null

    private fun notifyConnectionState(connected: Boolean, label: String) {
        onConnectionStateChanged?.invoke(connected, label)
        for (listener in connectionListeners) {
            try {
                listener(connected, label)
            } catch (e: Exception) {
                Log.w(TAG, "Listener error: ${e.message}")
            }
        }
    }

    // Live Vehicle State
    private var liveRpm = 0
    private var liveSpeedKmh = 0
    private var liveCoolantC = 85
    private var liveOilTempC = 90
    private var liveIntakeC = 25
    private var liveAmbientC = 22
    private var liveBatVolts = 14.2f
    private var liveEngineLoadPct = 0
    private var liveThrottlePct = 0
    private var liveFuelLevelPct = 50
    private var liveGear = 'N'
    private var liveBrakePct = 0
    private var liveHp = 0
    private var liveTorque = 0

    // Performance timer
    private var timerState = 0 // 0 = ready, 1 = running, 2 = done
    private var timerStartMs = 0L
    private var accel0to60 = 0.0f
    private var best0to60 = 5.28f

    // Diagnostics & Powertrain State
    private var liveAfr = 14.7f
    private var liveStft = 0.0f
    private var liveLtft = 0.0f
    private var liveRailPressurePsi = 500
    private var liveSparkAdvance = 14.0f
    private var liveKnockRetard = 0.0f
    private var lastTpmsCheckMs = 0L

    // TPMS
    private var flPsi = 32.0f
    private var frPsi = 32.0f
    private var rlPsi = 32.0f
    private var rrPsi = 32.0f
    private var flTemp = 25.0f
    private var frTemp = 25.0f
    private var rlTemp = 25.0f
    private var rrTemp = 25.0f

    // Dynamic Braking Tracking
    private var lastSpeedKmh = 0
    private var lastSpeedTimeMs = System.currentTimeMillis()

    @SuppressLint("MissingPermission")
    fun start() {
        if (running.get()) return
        running.set(true)

        workerThread = Thread({
            dataLogger.startSession()
            telemetrySimulator.start()

            while (running.get()) {
                try {
                    val device = findTargetDevice()
                    if (device == null) {
                        notifyConnectionState(false, "SEARCHING FOR OBD-II SCANNER…")
                        Thread.sleep(2500)
                        continue
                    }

                    notifyConnectionState(false, "CONNECTING TO ${device.name}…")
                    Log.i(TAG, "Attempting connection to ${device.name} (${device.address})")

                    val sock = device.createRfcommSocketToServiceRecord(SPP_UUID)
                    socket = sock
                    sock.connect()

                    telemetrySimulator.stop()
                    notifyConnectionState(true, device.name ?: "OBD-II Scanner")
                    Log.i(TAG, "Connected to ${device.name}! Starting live telemetry polling.")

                    runPollingLoop(sock)

                } catch (e: Exception) {
                    Log.w(TAG, "Bluetooth connection error: ${e.message}")
                    notifyConnectionState(false, "SEARCHING FOR OBD-II SCANNER…")
                    telemetrySimulator.start()
                    try { socket?.close() } catch (_: Exception) {}
                    socket = null
                    if (running.get()) Thread.sleep(3000)
                }
            }
            dataLogger.stopSession()
        }, "obd-rfcomm-worker").apply {
            isDaemon = true
            start()
        }
    }

    @SuppressLint("MissingPermission")
    private fun findTargetDevice(): BluetoothDevice? {
        val adapter = BluetoothAdapter.getDefaultAdapter() ?: return null
        if (!adapter.isEnabled) return null

        val bonded = adapter.bondedDevices ?: return null
        for (dev in bonded) {
            val name = (dev.name ?: "").uppercase()
            if (ADAPTER_PREFIXES.any { name.contains(it) }) {
                return dev
            }
        }
        return null
    }

    private fun runPollingLoop(sock: BluetoothSocket) {
        val reader = BufferedReader(InputStreamReader(sock.inputStream))
        val output = sock.outputStream

        fun sendCmd(cmd: String, timeoutMs: Long = 250): String {
            try {
                output.write("$cmd\r".toByteArray())
                output.flush()
                val sb = StringBuilder()
                val t0 = System.currentTimeMillis()
                while (System.currentTimeMillis() - t0 < timeoutMs) {
                    if (reader.ready()) {
                        val c = reader.read()
                        if (c == -1 || c == '>'.code) break
                        sb.append(c.toChar())
                    } else {
                        Thread.sleep(2)
                    }
                }
                return sb.toString().trim()
            } catch (e: Exception) {
                return ""
            }
        }

        fun parseHexBytes(resp: String, prefix: String): List<Int> {
            val clean = resp.replace(" ", "").replace("\r", "").replace("\n", "").uppercase()
            val idx = clean.indexOf(prefix.uppercase())
            if (idx == -1) return emptyList()
            val dataStr = clean.substring(idx + prefix.length)
            val bytes = mutableListOf<Int>()
            var i = 0
            while (i + 1 < dataStr.length) {
                val byteVal = dataStr.substring(i, i + 2).toIntOrNull(16) ?: break
                bytes.add(byteVal)
                i += 2
            }
            return bytes
        }

        // Initialize ELM327 protocol with aggressive CAN throughput settings
        sendCmd("ATZ", 800)
        sendCmd("ATE0", 200)
        sendCmd("ATL0", 200)
        sendCmd("ATH0", 200)
        sendCmd("ATS0", 200)
        sendCmd("ATAT2", 200)  // Fast adaptive timing
        sendCmd("ATST14", 200) // 80ms timeout to avoid hanging
        sendCmd("ATSP0", 600)  // Auto protocol (ISO 15765-4 CAN 500k)

        var cycleIndex = 0

        while (running.get() && sock.isConnected) {
            val nowMs = System.currentTimeMillis()

            // ================================================================
            // PRIORITY 1: ALWAYS POLL SPEED (100% Constant Live Speed ~35-45 Hz)
            // ================================================================
            val spdResp = sendCmd("010D", 80)
            val spdBytes = parseHexBytes(spdResp, "410D")
            if (spdBytes.isNotEmpty()) {
                liveSpeedKmh = spdBytes[0]
            }

            // ================================================================
            // PRIORITY 2: STAGGERED AUXILIARY POLLING (1 metric per Speed tick)
            // ================================================================
            cycleIndex = (cycleIndex + 1) % 32

            when (cycleIndex) {
                // High-frequency RPM (~20 Hz)
                0, 2, 4, 6, 8, 10, 12, 14, 16, 18, 20, 22, 24, 26, 28, 30 -> {
                    val rpmResp = sendCmd("010C", 80)
                    val rpmBytes = parseHexBytes(rpmResp, "410C")
                    if (rpmBytes.size >= 2) {
                        liveRpm = ((rpmBytes[0] * 256) + rpmBytes[1]) / 4
                    }
                }
                // Throttle Position %
                1, 9, 17, 25 -> {
                    val thrResp = sendCmd("0111", 80)
                    val thrBytes = parseHexBytes(thrResp, "4111")
                    if (thrBytes.isNotEmpty()) {
                        liveThrottlePct = (thrBytes[0] * 100) / 255
                    }
                }
                // Calculated Engine Load %
                3, 19 -> {
                    val loadResp = sendCmd("0104", 80)
                    val loadBytes = parseHexBytes(loadResp, "4104")
                    if (loadBytes.isNotEmpty()) {
                        liveEngineLoadPct = (loadBytes[0] * 100) / 255
                    }
                }
                // Coolant Temp °C
                5, 21 -> {
                    val coolResp = sendCmd("0105", 80)
                    val coolBytes = parseHexBytes(coolResp, "4105")
                    if (coolBytes.isNotEmpty()) {
                        liveCoolantC = coolBytes[0] - 40
                    }
                }
                // Intake Air Temp °C (and Ambient baseline)
                7 -> {
                    val iatResp = sendCmd("010F", 80)
                    val iatBytes = parseHexBytes(iatResp, "410F")
                    if (iatBytes.isNotEmpty()) {
                        liveIntakeC = iatBytes[0] - 40
                        liveAmbientC = liveIntakeC
                    }
                }
                // Battery Voltage via Hardware ATRV
                11 -> {
                    val atrvResp = sendCmd("ATRV", 60)
                    val vVal = atrvResp.replace("V", "").replace("v", "").trim().toFloatOrNull()
                    if (vVal != null && vVal > 5f) {
                        liveBatVolts = vVal
                    }
                }
                // Short Term & Long Term Fuel Trims
                13 -> {
                    val stftResp = sendCmd("0106", 80)
                    val stftBytes = parseHexBytes(stftResp, "4106")
                    if (stftBytes.isNotEmpty()) liveStft = ((stftBytes[0] - 128) * 100f) / 128f

                    val ltftResp = sendCmd("0108", 80)
                    val ltftBytes = parseHexBytes(ltftResp, "4108")
                    if (ltftBytes.isNotEmpty()) liveLtft = ((ltftBytes[0] - 128) * 100f) / 128f
                }
                // Direct Injection Fuel Rail Pressure (HPFP)
                15 -> {
                    val frpResp = sendCmd("0123", 80)
                    val frpBytes = parseHexBytes(frpResp, "4123")
                    if (frpBytes.size >= 2) {
                        val kpa = ((frpBytes[0] * 256) + frpBytes[1]) * 10
                        liveRailPressurePsi = (kpa * 0.145038f).toInt()
                    }
                }
                // Wideband Lambda / Air-Fuel Ratio (AFR)
                23 -> {
                    val afrResp = sendCmd("0124", 80)
                    val afrBytes = parseHexBytes(afrResp, "4124")
                    if (afrBytes.size >= 2) {
                        val lambda = ((afrBytes[0] * 256) + afrBytes[1]) / 32768.0f
                        liveAfr = lambda * 14.7f
                    }
                }
                // Ignition Timing Advance
                27 -> {
                    val timResp = sendCmd("010E", 80)
                    val timBytes = parseHexBytes(timResp, "410E")
                    if (timBytes.isNotEmpty()) {
                        liveSparkAdvance = (timBytes[0] / 2.0f) - 64.0f
                    }
                }
                // SkyActiv Engine Oil Temperature DID
                29 -> {
                    val oilResp = sendCmd("221310", 100)
                    val oilBytes = parseHexBytes(oilResp, "621310")
                    if (oilBytes.isNotEmpty()) {
                        liveOilTempC = oilBytes[0] - 40
                    }
                }
                // Background TPMS & Cluster Fuel check (every 25 seconds)
                31 -> {
                    if (nowMs - lastTpmsCheckMs > 25000L) {
                        lastTpmsCheckMs = nowMs
                        // Switch header to Instrument Cluster / BCM
                        sendCmd("ATSH 720", 100)

                        val p0Resp = sendCmd("222A05", 100)
                        val p0Bytes = parseHexBytes(p0Resp, "622A05")
                        if (p0Bytes.isNotEmpty()) flPsi = ((p0Bytes[0] * 1373f) / 1000f) * 0.145038f

                        val p1Resp = sendCmd("222A06", 100)
                        val p1Bytes = parseHexBytes(p1Resp, "622A06")
                        if (p1Bytes.isNotEmpty()) frPsi = ((p1Bytes[0] * 1373f) / 1000f) * 0.145038f

                        val p2Resp = sendCmd("222A07", 100)
                        val p2Bytes = parseHexBytes(p2Resp, "622A07")
                        if (p2Bytes.isNotEmpty()) rlPsi = ((p2Bytes[0] * 1373f) / 1000f) * 0.145038f

                        val p3Resp = sendCmd("222A08", 100)
                        val p3Bytes = parseHexBytes(p3Resp, "622A08")
                        if (p3Bytes.isNotEmpty()) rrPsi = ((p3Bytes[0] * 1373f) / 1000f) * 0.145038f

                        // Cluster Fuel Level
                        val fuelResp = sendCmd("222A26", 100)
                        val fuelBytes = parseHexBytes(fuelResp, "622A26")
                        if (fuelBytes.isNotEmpty()) liveFuelLevelPct = (fuelBytes[0] * 100) / 255

                        // Restore header to Engine PCM
                        sendCmd("ATSH 7E0", 100)
                    }
                }
            }

            // Real ND2 Gear Calculation
            liveGear = when {
                liveRpm == 0 && liveSpeedKmh == 0 -> '-'
                liveSpeedKmh < 3 -> if (liveRpm > 400) 'N' else '-'
                else -> {
                    val r = liveRpm.toFloat() / max(1f, liveSpeedKmh.toFloat())
                    when {
                        r < 35.0f -> '6'
                        r < 45.0f -> '5'
                        r < 56.0f -> '4'
                        r < 77.0f -> '3'
                        r < 125.0f -> '2'
                        else -> '1'
                    }
                }
            }

            // Real Engine Dynamics Output
            liveHp = min(181, ((liveRpm / 7500.0f) * 181.0f * (liveThrottlePct / 100.0f)).toInt())
            liveTorque = min(151, (151.0f * (liveEngineLoadPct / 100.0f)).toInt())

            // Organic Dynamic Deceleration Braking
            val dtSec = max(0.02f, (nowMs - lastSpeedTimeMs) / 1000.0f)
            val dSpeedMps = (liveSpeedKmh - lastSpeedKmh) * (1000.0f / 3600.0f)
            val accelMps2 = dSpeedMps / dtSec
            lastSpeedKmh = liveSpeedKmh
            lastSpeedTimeMs = nowMs

            if (accelMps2 < -0.6f && liveThrottlePct < 5) {
                // Deceleration under braking: ~7.0 m/s^2 is firm braking (100%)
                liveBrakePct = min(100, max(0, ((-accelMps2 / 6.5f) * 100.0f).toInt()))
            } else {
                liveBrakePct = 0
            }

            // Real Acceleration Timer
            if (liveSpeedKmh == 0) {
                timerState = 0
                accel0to60 = 0.0f
            } else if (liveSpeedKmh in 1..96) {
                if (timerState == 0) {
                    timerStartMs = nowMs
                    timerState = 1
                }
                accel0to60 = (nowMs - timerStartMs) / 1000.0f
            } else if (liveSpeedKmh >= 97 && timerState == 1) {
                timerState = 2
                accel0to60 = (nowMs - timerStartMs) / 1000.0f
                if (accel0to60 in 3.0f..15.0f && (best0to60 == 0f || accel0to60 < best0to60)) {
                    best0to60 = accel0to60
                }
            }

            val instantMpg = if (liveSpeedKmh > 5) {
                min(60.0f, max(0.0f, (liveSpeedKmh * 0.621371f) / max(0.1f, (liveEngineLoadPct * 0.04f))))
            } else 0.0f

            val rangeMiles = (liveFuelLevelPct * 4.2f).toInt()

            // Update NDK Engine Frame with all live powertrain telemetry
            NativeBridge.nativeUpdateFullTelemetry(
                liveRpm, liveSpeedKmh, liveCoolantC, liveOilTempC, liveIntakeC, liveAmbientC,
                liveBatVolts, liveEngineLoadPct, liveThrottlePct, liveFuelLevelPct,
                liveGear, liveBrakePct, liveHp, liveTorque,
                accel0to60, best0to60, timerState,
                instantMpg, 32.8f, 0.0f, rangeMiles,
                liveAfr, liveStft, liveLtft, liveKnockRetard, liveRailPressurePsi,
                liveSparkAdvance, 0.0f, 0.0f,
                liveOilTempC - 10, 0, 0.0f,
                flPsi, frPsi, rlPsi, rrPsi,
                flTemp, frTemp, rlTemp, rrTemp,
                0, false, true
            )
        }
    }

    fun stop() {
        running.set(false)
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        workerThread?.interrupt()
        workerThread = null
    }
}
