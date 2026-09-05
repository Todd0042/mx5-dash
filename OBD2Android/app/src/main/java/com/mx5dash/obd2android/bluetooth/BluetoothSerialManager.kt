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
import kotlin.math.roundToInt

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

    // Volatile thread-safe state shared with UI
    @Volatile var currentActiveScreen: Int = 0
    @Volatile var targetTransitionScreen: Int = -1
    @Volatile var isFreshPayloadReady: Boolean = false

    private val running = AtomicBoolean(false)
    private var workerThread: Thread? = null
    private var socket: BluetoothSocket? = null
    private var consecutiveFailures = 0

    val isConnected: Boolean
        get() = (socket?.isConnected == true)

    private val connectionListeners = java.util.concurrent.CopyOnWriteArrayList<(Boolean, String) -> Unit>()

    fun addConnectionListener(listener: (Boolean, String) -> Unit) {
        connectionListeners.add(listener)
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

    // Retained Live Vehicle State (Freeze-Protected: Never reset on dropped packets)
    private var liveRpm = 0
    private var liveSpeedKmh = 0
    private var liveCoolantC = 0
    private var liveOilTempC = 0
    private var liveIntakeC = 0
    private var liveAmbientC = 0
    private var liveBatVolts = 0.0f
    private var liveEngineLoadPct = 0
    private var liveThrottlePct = 0
    private var liveFuelLevelPct = 0
    private var liveGear = '-'
    private var liveBrakePct = 0
    private var liveHp = 0
    private var liveTorque = 0

    // Performance timer
    private var timerState = 0 // 0 = ready, 1 = running, 2 = done
    private var timerStartMs = 0L
    private var accel0to60 = 0.0f
    private var best0to60 = 0.0f

    // Diagnostics & Powertrain State
    private var liveAfr = 0.0f
    private var liveStft = 0.0f
    private var liveLtft = 0.0f
    private var liveRailPressurePsi = 0
    private var liveSparkAdvance = 0.0f
    private var liveKnockRetard = 0.0f

    // TPMS
    private var flPsi = 0.0f
    private var frPsi = 0.0f
    private var rlPsi = 0.0f
    private var rrPsi = 0.0f
    private var flTemp = 0.0f
    private var frTemp = 0.0f
    private var rlTemp = 0.0f
    private var rrTemp = 0.0f

    // Dynamic Braking Tracking
    private var lastSpeedKmh = 0
    private var lastSpeedTimeMs = System.currentTimeMillis()
    private var lastSafetySweepMs = 0L
    private var tcmPrnd = '-'

    // Trip Integration Accumulator
    private var tripTotalDistanceMiles = 0.0f
    private var tripTotalGallons = 0.0f
    private var liveTripAvgMpg = 0.0f
    private var lastTripCalcTimeMs = 0L

    @SuppressLint("MissingPermission")
    fun start() {
        if (running.get()) return
        running.set(true)

        workerThread = Thread({
            dataLogger.startSession()

            while (running.get()) {
                try {
                    val device = findTargetDevice()
                    if (device == null) {
                        notifyConnectionState(false, "SEARCHING FOR OBD-II SCANNER…")
                        Thread.sleep(2000)
                        continue
                    }

                    notifyConnectionState(false, "CONNECTING TO ${device.name}…")
                    Log.i(TAG, "Attempting connection to ${device.name} (${device.address})")

                    val sock = device.createRfcommSocketToServiceRecord(SPP_UUID)
                    socket = sock
                    sock.connect()

                    consecutiveFailures = 0
                    telemetrySimulator.stop()
                    notifyConnectionState(true, device.name ?: "OBD-II Scanner")
                    Log.i(TAG, "Connected to ${device.name}! Starting view-driven telemetry loop.")

                    runPollingLoop(sock)

                } catch (e: Exception) {
                    Log.w(TAG, "Bluetooth connection dropped or failed: ${e.message}")
                    attemptSilentSocketReconnect()
                }
            }
            dataLogger.stopSession()
        }, "obd-rfcomm-worker").apply {
            isDaemon = true
            start()
        }
    }

    private fun attemptSilentSocketReconnect() {
        try { socket?.close() } catch (_: Exception) {}
        socket = null

        consecutiveFailures++
        // Exponential backoff: 1s, 2s, 4s, 8s, up to 10s max
        val backoffMs = min(10000L, 1000L * (1L shl min(consecutiveFailures - 1, 3)))
        Log.e(TAG, "Serial connection error. Silent backoff delay: ${backoffMs}ms (attempt $consecutiveFailures)...")
        notifyConnectionState(false, "RECONNECTING TO OBD-II SCANNER…")
        telemetrySimulator.start()

        if (running.get()) {
            try {
                Thread.sleep(backoffMs)
            } catch (_: InterruptedException) {
                // Thread interrupted
            }
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

    /**
     * Non-blocking OBD command sender with guaranteed 200ms socket timeout protection.
     * Prevents thread stalls on lost or truncated Bluetooth packets.
     */
    private fun sendObdCommand(
        output: OutputStream,
        input: java.io.InputStream,
        cmd: String,
        timeoutMs: Long = 180L
    ): String {
        // Drain any stale residual bytes before writing
        while (input.available() > 0) {
            input.read()
        }

        output.write("$cmd\r".toByteArray(Charsets.US_ASCII))
        output.flush()

        val sb = StringBuilder()
        val start = System.currentTimeMillis()
        val byteBuf = ByteArray(64)

        while (System.currentTimeMillis() - start < timeoutMs) {
            val avail = input.available()
            if (avail > 0) {
                val toRead = min(avail, byteBuf.size)
                val readCount = input.read(byteBuf, 0, toRead)
                if (readCount > 0) {
                    for (i in 0 until readCount) {
                        val ch = byteBuf[i].toInt().toChar()
                        if (ch == '>') {
                            return sb.toString().trim()
                        }
                        if (ch != '\r' && ch != '\n' && ch != '\u0000') {
                            sb.append(ch)
                        }
                    }
                }
            } else {
                Thread.sleep(3) // Yield CPU to avoid battery drain
            }
        }
        return sb.toString().trim()
    }

    private fun parseHexBytes(resp: String, prefix: String): List<Int> {
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

    private fun parseCalibratedThrottle(rawByte: Int): Int {
        val rawPct = (rawByte * 100f) / 255f
        return if (rawPct <= 13.0f) {
            0
        } else {
            min(100, (((rawPct - 13.0f) * 100f) / 87.0f).roundToInt())
        }
    }

    private fun parseCalibratedFuel(rawByte: Int): Int {
        // Mazda ND2 fuel sender: ~8 counts at empty reserve to ~224 counts at 100% full
        if (rawByte <= 8) return 0
        val pct = ((rawByte - 8) * 100f) / 216f
        return min(100, max(0, pct.roundToInt()))
    }

    private fun runPollingLoop(sock: BluetoothSocket) {
        val input = sock.inputStream
        val output = sock.outputStream

        // Initialize ELM327 protocol with fast CAN throughput
        sendObdCommand(output, input, "ATZ", 600)
        sendObdCommand(output, input, "ATE0", 150)
        sendObdCommand(output, input, "ATL0", 150)
        sendObdCommand(output, input, "ATH0", 150)
        sendObdCommand(output, input, "ATS0", 150)
        sendObdCommand(output, input, "ATAT2", 150)  // Fast adaptive timing
        sendObdCommand(output, input, "ATST32", 150) // 200ms timeout for Mode 22 DIDs
        sendObdCommand(output, input, "ATSP0", 500)  // Auto protocol (ISO 15765-4 CAN 500k)

        var screenTick = 0

        while (running.get() && sock.isConnected) {
            val nowMs = System.currentTimeMillis()

            // ================================================================
            // 1. ALWAYS POLL HIGH-PRIORITY: Vehicle Speed (Mode 010D)
            // ================================================================
            val spdResp = sendObdCommand(output, input, "010D", 90)
            val spdBytes = parseHexBytes(spdResp, "410D")
            if (spdBytes.isNotEmpty()) {
                liveSpeedKmh = spdBytes[0]
            }

            // Mandatory thread throttle yield (35ms) to conserve phone battery & prevent vLinker buffer overrun
            Thread.sleep(35)

            // ================================================================
            // 2. VIEW-DRIVEN SCREEN-SPECIFIC COMMAND QUEUE
            // ================================================================
            val activeScreen = if (targetTransitionScreen >= 0) targetTransitionScreen else currentActiveScreen
            screenTick = (screenTick + 1) % 16

            // Check 25-Second Safety Warning Sweep (TPMS + Critical Overheat + Ambient Temp)
            if (nowMs - lastSafetySweepMs > 25000L && activeScreen != 1) { // 1 = SCREEN_TPMS
                lastSafetySweepMs = nowMs
                executeSafetySweep(output, input)
            } else {
                executeScreenQueue(output, input, activeScreen, screenTick)
            }

            // Signal fresh payload ready if a transition was holding for data
            if (targetTransitionScreen >= 0 && !isFreshPayloadReady) {
                isFreshPayloadReady = true
            }

            // ================================================================
            // 3. TELEMETRY MATH & SNAPSHOT PUSH (RETAINS LAST VALID VALUES)
            // ================================================================
            pushTelemetrySnapshot(nowMs)
        }
    }

    private fun executeScreenQueue(
        output: OutputStream,
        input: java.io.InputStream,
        screen: Int,
        tick: Int
    ) {
        when (screen) {
            // Screen 0: Hero Speedometer -> RPM, Fuel %
            0 -> {
                when (tick % 2) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "010C", 80)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "012F", 90)
                        val bytes = parseHexBytes(resp, "412F")
                        if (bytes.isNotEmpty()) liveFuelLevelPct = parseCalibratedFuel(bytes[0])
                    }
                }
            }
            // Screen 1: TPMS -> 4-Corner Pressure & Temp + PRND on BCM 720, then Ambient Temp on PCM 7E0
            1 -> {
                sendObdCommand(output, input, "ATSH 720", 90)
                val p0 = parseHexBytes(sendObdCommand(output, input, "222A05", 90), "622A05")
                if (p0.isNotEmpty()) {
                    flPsi = ((p0[0] * 1373f) / 1000f) * 0.145038f
                    if (p0.size >= 2) flTemp = (p0[1] - 40).toFloat()
                }

                val p1 = parseHexBytes(sendObdCommand(output, input, "222A06", 90), "622A06")
                if (p1.isNotEmpty()) {
                    frPsi = ((p1[0] * 1373f) / 1000f) * 0.145038f
                    if (p1.size >= 2) frTemp = (p1[1] - 40).toFloat()
                }

                val p2 = parseHexBytes(sendObdCommand(output, input, "222A07", 90), "622A07")
                if (p2.isNotEmpty()) {
                    rlPsi = ((p2[0] * 1373f) / 1000f) * 0.145038f
                    if (p2.size >= 2) rlTemp = (p2[1] - 40).toFloat()
                }

                val p3 = parseHexBytes(sendObdCommand(output, input, "222A08", 90), "622A08")
                if (p3.isNotEmpty()) {
                    rrPsi = ((p3[0] * 1373f) / 1000f) * 0.145038f
                    if (p3.size >= 2) rrTemp = (p3[1] - 40).toFloat()
                }

                // PRND Selector Position (DID 222A27 on Header 720)
                val prndResp = sendObdCommand(output, input, "222A27", 90)
                val prndBytes = parseHexBytes(prndResp, "622A27")
                if (prndBytes.isNotEmpty()) {
                    tcmPrnd = when (prndBytes[0]) {
                        1 -> 'P'
                        2 -> 'R'
                        3 -> 'N'
                        4 -> 'D'
                        5 -> 'M'
                        else -> '-'
                    }
                }

                sendObdCommand(output, input, "ATSH 7E0", 90)

                // Ambient Air Temp (Mode 01 PID 46 on PCM 7E0)
                val ambResp = sendObdCommand(output, input, "0146", 80)
                val ambBytes = parseHexBytes(ambResp, "4146")
                if (ambBytes.isNotEmpty()) {
                    val temp = ambBytes[0] - 40
                    if (temp in -40..60) {
                        liveAmbientC = temp
                    }
                }
            }
            // Screen 2: Temperatures -> Coolant, Oil Temp, Intake Air, Battery
            2 -> {
                when (tick % 4) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0105", 80)
                        val bytes = parseHexBytes(resp, "4105")
                        if (bytes.isNotEmpty()) liveCoolantC = bytes[0] - 40
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "221310", 200)
                        val bytes = parseHexBytes(resp, "621310")
                        if (bytes.isNotEmpty() && bytes[0] > 40) liveOilTempC = bytes[0] - 40
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "010F", 80)
                        val bytes = parseHexBytes(resp, "410F")
                        if (bytes.isNotEmpty()) liveIntakeC = bytes[0] - 40
                    }
                    3 -> {
                        // Query Mode 01 PID 42 (ECU Control Module Supply Voltage) with ATRV fallback
                        val resp = sendObdCommand(output, input, "0142", 100)
                        val bytes = parseHexBytes(resp, "4142")
                        if (bytes.size >= 2) {
                            val v = ((bytes[0] * 256) + bytes[1]) / 1000.0f
                            if (v > 5f) liveBatVolts = v
                        } else {
                            val atrvResp = sendObdCommand(output, input, "ATRV", 80)
                            val v = atrvResp.replace("V", "").replace("v", "").trim().toFloatOrNull()
                            if (v != null && v > 5f) liveBatVolts = v
                        }
                    }
                }
            }
            // Screen 3: Diagnostic Hub -> Trims (STFT & LTFT), Fuel Rail, AFR, Timing
            3 -> {
                when (tick % 5) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0106", 80)
                        val bytes = parseHexBytes(resp, "4106")
                        if (bytes.isNotEmpty()) liveStft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0107", 80)
                        val bytes = parseHexBytes(resp, "4107")
                        if (bytes.isNotEmpty()) liveLtft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0123", 80)
                        val bytes = parseHexBytes(resp, "4123")
                        if (bytes.size >= 2) liveRailPressurePsi = ((((bytes[0] * 256) + bytes[1]) * 10) * 0.145038f).toInt()
                    }
                    3 -> {
                        val resp = sendObdCommand(output, input, "0124", 80)
                        val bytes = parseHexBytes(resp, "4124")
                        if (bytes.size >= 2) liveAfr = (((bytes[0] * 256) + bytes[1]) / 32768.0f) * 14.7f
                    }
                    4 -> {
                        val resp = sendObdCommand(output, input, "010E", 80)
                        val bytes = parseHexBytes(resp, "410E")
                        if (bytes.isNotEmpty()) liveSparkAdvance = (bytes[0] / 2.0f) - 64.0f
                    }
                }
            }
            // Screen 4: Track & Dynamics (FAFO) -> Throttle %, RPM, Load %
            4 -> {
                when (tick % 2) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0111", 80)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "010C", 80)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                }
            }
            // Screen 5: Engine Tachometer -> High-rate RPM, Load %, Throttle %
            5 -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "010C", 80)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0104", 80)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0111", 80)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                }
            }
            // Screen 6: Fuel & Trip Economy -> Fuel %, Load %
            6 -> {
                when (tick % 2) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "012F", 90)
                        val bytes = parseHexBytes(resp, "412F")
                        if (bytes.isNotEmpty()) liveFuelLevelPct = parseCalibratedFuel(bytes[0])
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0104", 80)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                }
            }
            // Diagnostic Sub-Screens (8-12): Trims (STFT & LTFT), Fuel Rail, AFR, Timing
            else -> {
                when (tick % 5) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0106", 80)
                        val bytes = parseHexBytes(resp, "4106")
                        if (bytes.isNotEmpty()) liveStft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0107", 80)
                        val bytes = parseHexBytes(resp, "4107")
                        if (bytes.isNotEmpty()) liveLtft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0123", 80)
                        val bytes = parseHexBytes(resp, "4123")
                        if (bytes.size >= 2) liveRailPressurePsi = ((((bytes[0] * 256) + bytes[1]) * 10) * 0.145038f).toInt()
                    }
                    3 -> {
                        val resp = sendObdCommand(output, input, "0124", 80)
                        val bytes = parseHexBytes(resp, "4124")
                        if (bytes.size >= 2) liveAfr = (((bytes[0] * 256) + bytes[1]) / 32768.0f) * 14.7f
                    }
                    4 -> {
                        val resp = sendObdCommand(output, input, "010E", 80)
                        val bytes = parseHexBytes(resp, "410E")
                        if (bytes.isNotEmpty()) liveSparkAdvance = (bytes[0] / 2.0f) - 64.0f
                    }
                }
            }
        }
    }

    private fun executeSafetySweep(output: OutputStream, input: java.io.InputStream) {
        // Query TPMS and Ambient Air Temp on BCM (Header 720)
        sendObdCommand(output, input, "ATSH 720", 90)
        val p0 = parseHexBytes(sendObdCommand(output, input, "222A05", 90), "622A05")
        if (p0.isNotEmpty()) {
            flPsi = ((p0[0] * 1373f) / 1000f) * 0.145038f
            if (p0.size >= 2) flTemp = (p0[1] - 40).toFloat()
        }

        val p1 = parseHexBytes(sendObdCommand(output, input, "222A06", 90), "622A06")
        if (p1.isNotEmpty()) {
            frPsi = ((p1[0] * 1373f) / 1000f) * 0.145038f
            if (p1.size >= 2) frTemp = (p1[1] - 40).toFloat()
        }

        val p2 = parseHexBytes(sendObdCommand(output, input, "222A07", 90), "622A07")
        if (p2.isNotEmpty()) {
            rlPsi = ((p2[0] * 1373f) / 1000f) * 0.145038f
            if (p2.size >= 2) rlTemp = (p2[1] - 40).toFloat()
        }

        val p3 = parseHexBytes(sendObdCommand(output, input, "222A08", 90), "622A08")
        if (p3.isNotEmpty()) {
            rrPsi = ((p3[0] * 1373f) / 1000f) * 0.145038f
            if (p3.size >= 2) rrTemp = (p3[1] - 40).toFloat()
        }

        // PRND Selector Position (DID 222A27 on Header 720)
        val prndResp = sendObdCommand(output, input, "222A27", 90)
        val prndBytes = parseHexBytes(prndResp, "622A27")
        if (prndBytes.isNotEmpty()) {
            tcmPrnd = when (prndBytes[0]) {
                1 -> 'P'
                2 -> 'R'
                3 -> 'N'
                4 -> 'D'
                5 -> 'M'
                else -> '-'
            }
        }

        // Restore PCM
        sendObdCommand(output, input, "ATSH 7E0", 90)

        // Check Coolant & Oil Temp alarms & Ambient Air Temp on PCM
        val cool = parseHexBytes(sendObdCommand(output, input, "0105", 80), "4105")
        if (cool.isNotEmpty()) liveCoolantC = cool[0] - 40

        val oil = parseHexBytes(sendObdCommand(output, input, "221310", 200), "621310")
        if (oil.isNotEmpty() && oil[0] > 40) liveOilTempC = oil[0] - 40

        val amb = parseHexBytes(sendObdCommand(output, input, "0146", 80), "4146")
        if (amb.isNotEmpty()) {
            val temp = amb[0] - 40
            if (temp in -40..60) {
                liveAmbientC = temp
            }
        }

        // Battery Voltage on PCM (PID 0142 with ATRV fallback)
        val batBytes = parseHexBytes(sendObdCommand(output, input, "0142", 100), "4142")
        if (batBytes.size >= 2) {
            val v = ((batBytes[0] * 256) + batBytes[1]) / 1000.0f
            if (v > 5f) liveBatVolts = v
        } else {
            val atrvResp = sendObdCommand(output, input, "ATRV", 80)
            val v = atrvResp.replace("V", "").replace("v", "").trim().toFloatOrNull()
            if (v != null && v > 5f) liveBatVolts = v
        }
    }

    private fun pushTelemetrySnapshot(nowMs: Long) {
        // ND2 Transmission PRND & Active Gear Logic (Supports both Automatic 6AT & Manual 6MT)
        val isAuto = context.getSharedPreferences("sys_prefs", Context.MODE_PRIVATE).getBoolean("trans_auto", true)
        liveGear = if (isAuto) {
            when {
                liveRpm == 0 && liveSpeedKmh == 0 -> '-'
                tcmPrnd == 'R' -> 'R'
                tcmPrnd == 'P' -> 'P'
                tcmPrnd == 'N' -> 'N'
                liveSpeedKmh < 2 -> if (liveRpm > 400) 'P' else '-'
                else -> {
                    // Forward Drive Gear Ratio (1..6)
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
        } else {
            when {
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
        }

        // Real Engine Dynamics Output
        liveHp = min(181, ((liveRpm / 7500.0f) * 181.0f * (liveThrottlePct / 100.0f)).toInt())
        liveTorque = min(151, (151.0f * (liveEngineLoadPct / 100.0f)).toInt())

        // Dynamic Deceleration Rate (Braking %)
        val dtSec = max(0.02f, (nowMs - lastSpeedTimeMs) / 1000.0f)
        val dSpeedMps = (liveSpeedKmh - lastSpeedKmh) * (1000.0f / 3600.0f)
        val accelMps2 = dSpeedMps / dtSec
        lastSpeedKmh = liveSpeedKmh
        lastSpeedTimeMs = nowMs

        if (accelMps2 < -0.6f && liveThrottlePct < 5) {
            liveBrakePct = min(100, max(0, ((-accelMps2 / 6.5f) * 100.0f).toInt()))
        } else {
            liveBrakePct = 0
        }

        // Acceleration Timer (0-60 MPH)
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

        // Instantaneous MPG & Trip Integration
        val speedMph = liveSpeedKmh * 0.621371f
        val instantMpg = if (liveSpeedKmh > 0) {
            if (liveThrottlePct == 0 && liveRpm > 1200) {
                60.0f // Deceleration Fuel Cut-Off (DFCO)
            } else {
                val fuelRateGph = 0.22f + 0.075f * (liveRpm / 1000.0f) * (liveEngineLoadPct / 100.0f)
                val mpg = speedMph / max(0.05f, fuelRateGph)
                min(60.0f, max(0.0f, mpg))
            }
        } else 0.0f

        // Trip Integration Accumulator
        if (lastTripCalcTimeMs == 0L) {
            lastTripCalcTimeMs = nowMs
        } else {
            val tripDtSec = max(0.01f, min(2.0f, (nowMs - lastTripCalcTimeMs) / 1000.0f))
            lastTripCalcTimeMs = nowMs

            val deltaMiles = speedMph * (tripDtSec / 3600.0f)
            tripTotalDistanceMiles += deltaMiles

            val fuelRateGph = if (liveSpeedKmh > 0) {
                if (liveThrottlePct == 0 && liveRpm > 1200) {
                    0.0f
                } else {
                    max(0.15f, 0.22f + 0.075f * (liveRpm / 1000.0f) * (liveEngineLoadPct / 100.0f))
                }
            } else if (liveRpm > 400) {
                0.20f * max(1.0f, liveRpm / 700.0f) * max(1.0f, liveEngineLoadPct / 20.0f)
            } else {
                0.0f
            }

            val deltaGallons = fuelRateGph * (tripDtSec / 3600.0f)
            tripTotalGallons += deltaGallons

            if (tripTotalGallons > 0.0005f && tripTotalDistanceMiles > 0.01f) {
                liveTripAvgMpg = min(99.9f, max(0.0f, tripTotalDistanceMiles / tripTotalGallons))
            }
        }

        val rangeMiles = if (liveFuelLevelPct > 0) (liveFuelLevelPct * 4.2f).toInt() else 0

        // Push to C++ UI Engine
        NativeBridge.nativeUpdateFullTelemetry(
            liveRpm, liveSpeedKmh, liveCoolantC, liveOilTempC, liveIntakeC, liveAmbientC,
            liveBatVolts, liveEngineLoadPct, liveThrottlePct, liveFuelLevelPct,
            liveGear, liveBrakePct, liveHp, liveTorque,
            accel0to60, best0to60, timerState,
            instantMpg, liveTripAvgMpg, tripTotalDistanceMiles, rangeMiles,
            liveAfr, liveStft, liveLtft, liveKnockRetard, liveRailPressurePsi,
            liveSparkAdvance, 0.0f, 0.0f,
            if (liveOilTempC > 0) liveOilTempC - 10 else 0, 0, 0.0f,
            flPsi, frPsi, rlPsi, rrPsi,
            flTemp, frTemp, rlTemp, rrTemp,
            0, false, true
        )
    }

    fun stop() {
        running.set(false)
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        workerThread?.interrupt()
        workerThread = null
    }
}
