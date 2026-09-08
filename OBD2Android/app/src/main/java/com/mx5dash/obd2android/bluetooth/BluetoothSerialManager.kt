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
    private var liveEvapVaporPa = 0

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
    private var lastBackgroundQueryMs = 0L
    private var backgroundStep = 0
    private var tcmPrnd = '-'
    private var tcmDirectGear = '-'

    // Trip Integration Accumulator
    private var tripTotalDistanceMiles = 0.0f
    private var tripTotalGallons = 0.0f
    private var liveTripAvgMpg = 0.0f
    private var lastTripCalcTimeMs = 0L
    private var lastLogMs = 0L

    private var isReceiverRegistered = false
    private val btStateReceiver = object : android.content.BroadcastReceiver() {
        @SuppressLint("MissingPermission")
        override fun onReceive(c: Context?, intent: android.content.Intent?) {
            val action = intent?.action ?: return
            if (action == BluetoothDevice.ACTION_ACL_DISCONNECTED ||
                action == BluetoothDevice.ACTION_ACL_DISCONNECT_REQUESTED) {
                val dev: BluetoothDevice? = if (android.os.Build.VERSION.SDK_INT >= android.os.Build.VERSION_CODES.TIRAMISU) {
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE, BluetoothDevice::class.java)
                } else {
                    @Suppress("DEPRECATION")
                    intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE)
                }
                val activeDev = socket?.remoteDevice
                if (dev != null && activeDev != null && dev.address == activeDev.address) {
                    Log.w(TAG, "Active OBD-II adapter ACL disconnected: ${dev.name} (${dev.address}). Reconnecting...")
                    reconnectNow()
                }
            } else if (action == BluetoothAdapter.ACTION_STATE_CHANGED) {
                val state = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, BluetoothAdapter.ERROR)
                if (state == BluetoothAdapter.STATE_TURNING_OFF || state == BluetoothAdapter.STATE_OFF) {
                    Log.w(TAG, "Bluetooth radio disabled by OS. Reconnecting...")
                    reconnectNow()
                }
            }
        }
    }

    fun reconnectNow() {
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        workerThread?.interrupt()
    }

    @SuppressLint("MissingPermission")
    fun start() {
        if (running.get()) return
        running.set(true)

        if (!isReceiverRegistered) {
            val filter = android.content.IntentFilter().apply {
                addAction(BluetoothDevice.ACTION_ACL_DISCONNECTED)
                addAction(BluetoothDevice.ACTION_ACL_DISCONNECT_REQUESTED)
                addAction(BluetoothAdapter.ACTION_STATE_CHANGED)
            }
            try {
                context.registerReceiver(btStateReceiver, filter)
                isReceiverRegistered = true
            } catch (e: Exception) {
                Log.w(TAG, "Failed to register BT receiver: ${e.message}")
            }
        }

        workerThread = Thread({
            dataLogger.startSession()

            while (running.get()) {
                try {
                    val device = findTargetDevice()
                    if (device == null) {
                        notifyConnectionState(false, "SEARCHING FOR OBD-II SCANNER…")
                        pushDisconnectedSnapshot()
                        Thread.sleep(2000)
                        continue
                    }

                    notifyConnectionState(false, "CONNECTING TO ${device.name}…")
                    pushDisconnectedSnapshot()
                    Log.i(TAG, "Attempting connection to ${device.name} (${device.address})")

                    val adapter = BluetoothAdapter.getDefaultAdapter()
                    if (adapter != null && adapter.isEnabled) {
                        try { adapter.cancelDiscovery() } catch (_: Exception) {}
                    }

                    var sock: BluetoothSocket? = null
                    try {
                        sock = device.createRfcommSocketToServiceRecord(SPP_UUID)
                        socket = sock
                        sock.connect()
                    } catch (e: Exception) {
                        Log.w(TAG, "Standard SPP connection failed (${e.message}), attempting RFCOMM channel 1 fallback...")
                        try { sock?.close() } catch (_: Exception) {}
                        val m = device.javaClass.getMethod("createRfcommSocket", Int::class.javaPrimitiveType)
                        val fallbackSock = m.invoke(device, 1) as BluetoothSocket
                        socket = fallbackSock
                        fallbackSock.connect()
                        sock = fallbackSock
                    }

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
        val backoffMs = min(5000L, 1000L * min(consecutiveFailures, 5))
        Log.e(TAG, "Serial connection error. Silent backoff delay: ${backoffMs}ms (attempt $consecutiveFailures)...")
        notifyConnectionState(false, "OBD-II DISCONNECTED • RECONNECTING…")
        pushDisconnectedSnapshot()
        telemetrySimulator.start()

        if (running.get()) {
            try {
                Thread.sleep(backoffMs)
            } catch (_: InterruptedException) {
                // Thread interrupted for immediate retry
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
     * Robust OBD command sender with guaranteed '>' prompt synchronization.
     * Prevents serial stream desynchronization by strictly awaiting the ELM327 prompt.
     */
    private fun sendObdCommand(
        output: OutputStream,
        input: java.io.InputStream,
        cmd: String,
        timeoutMs: Long = 350L
    ): String {
        // Drain any stale bytes that arrived while idle
        var drained = 0
        while (input.available() > 0) {
            input.read()
            drained++
        }
        if (drained > 0) {
            Log.v(TAG, "Drained $drained residual bytes before '$cmd'")
        }

        val sendStart = System.currentTimeMillis()
        output.write("$cmd\r".toByteArray(Charsets.US_ASCII))
        output.flush()

        val sb = StringBuilder()
        val byteBuf = ByteArray(64)
        var sawPrompt = false

        while (System.currentTimeMillis() - sendStart < timeoutMs) {
            val avail = input.available()
            if (avail > 0) {
                val toRead = min(avail, byteBuf.size)
                val readCount = input.read(byteBuf, 0, toRead)
                if (readCount < 0) {
                    throw java.io.IOException("RFCOMM socket stream closed by remote host")
                }
                if (readCount > 0) {
                    for (i in 0 until readCount) {
                        val ch = byteBuf[i].toInt().toChar()
                        if (ch == '>') {
                            sawPrompt = true
                            break
                        }
                        if (ch != '\r' && ch != '\n' && ch != '\u0000') {
                            sb.append(ch)
                        }
                    }
                    if (sawPrompt) break
                }
            } else {
                Thread.sleep(2)
            }
        }

        val duration = System.currentTimeMillis() - sendStart
        val result = sb.toString().trim()

        if (!sawPrompt) {
            Log.w(TAG, "OBD TIMEOUT on '$cmd' (${duration}ms) - partial: '$result'. Resyncing with CR...")
            try {
                // Recovery: send CR to cancel pending command in ELM327 and wait for prompt
                output.write("\r".toByteArray(Charsets.US_ASCII))
                output.flush()
                val resyncStart = System.currentTimeMillis()
                while (System.currentTimeMillis() - resyncStart < 300L) {
                    if (input.available() > 0) {
                        val c = input.read()
                        if (c == '>'.code) {
                            Log.i(TAG, "ELM327 prompt '>' recovered after timeout on '$cmd'")
                            break
                        }
                    } else {
                        Thread.sleep(2)
                    }
                }
            } catch (e: Exception) {
                Log.w(TAG, "Prompt recovery failed: ${e.message}")
            }
            return "" // Return empty so partial response is NEVER mistaken for next command
        }

        Log.d(TAG, "OBD: '$cmd' (${duration}ms) => '$result'")
        return result
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

    private fun decodeTcmGear(prndBytes: List<Int>) {
        if (prndBytes.isEmpty()) return
        val b = prndBytes[0]
        when {
            b == 0x60 -> {
                tcmPrnd = 'R'
                tcmDirectGear = 'R'
            }
            b == 0x70 -> {
                tcmPrnd = 'P'
                tcmDirectGear = 'P'
            }
            b == 0x50 -> {
                tcmPrnd = 'N'
                tcmDirectGear = 'N'
            }
            b in 1..6 -> {
                tcmPrnd = 'D'
                tcmDirectGear = ('0' + b).toChar()
            }
            (b and 0xF0) != 0 && (b and 0x0F) in 1..6 -> {
                tcmPrnd = 'D'
                tcmDirectGear = ('0' + (b and 0x0F)).toChar()
            }
            else -> {
                Log.d(TAG, "TCM 221E12 raw byte: 0x${Integer.toHexString(b)}")
            }
        }
    }

    private fun runPollingLoop(sock: BluetoothSocket) {
        val input = sock.inputStream
        val output = sock.outputStream

        // Initialize ELM327 protocol for Mazda SkyActiv CAN (ISO 15765-4 CAN 11/500k)
        sendObdCommand(output, input, "ATZ", 1200)
        try { Thread.sleep(150) } catch (_: InterruptedException) {}
        sendObdCommand(output, input, "ATE0", 300)   // Echo off
        sendObdCommand(output, input, "ATL0", 200)   // Linefeeds off
        sendObdCommand(output, input, "ATH0", 200)   // Headers off
        sendObdCommand(output, input, "ATS0", 200)   // Spaces off
        sendObdCommand(output, input, "ATAT2", 200)  // Fast adaptive timing
        sendObdCommand(output, input, "ATST32", 200) // 200ms timeout for Mode 22 DIDs
        sendObdCommand(output, input, "ATSP6", 300)  // Direct ISO 15765-4 CAN 11/500k (Fast Mazda CAN)
        sendObdCommand(output, input, "ATSH 7E0", 200) // Explicitly set PCM Header 7E0

        // Transmission Auto-Detection: Probe TCM (Header 7E1) to auto-detect 6AT (Automatic) vs 6MT (Manual)
        try {
            sendObdCommand(output, input, "ATSH 7E1", 200)
            val tcmResp = sendObdCommand(output, input, "221E12", 300)
            val hasTcm = parseHexBytes(tcmResp, "621E12").isNotEmpty() ||
                         (!tcmResp.contains("NO DATA", ignoreCase = true) &&
                          !tcmResp.contains("ERROR", ignoreCase = true) &&
                          !tcmResp.contains("UNABLE", ignoreCase = true) &&
                          tcmResp.trim().length >= 6)
            Log.i(TAG, "Transmission auto-detection probe: resp='$tcmResp', detectedAuto=$hasTcm")
            context.getSharedPreferences("sys_prefs", Context.MODE_PRIVATE)
                .edit()
                .putBoolean("trans_auto", hasTcm)
                .apply()
        } catch (e: Exception) {
            Log.w(TAG, "Transmission probe exception", e)
        } finally {
            sendObdCommand(output, input, "ATSH 7E0", 200)
        }

        var screenTick = 0
        var consecutiveTimeouts = 0
        var consecutiveNoData = 0
        var lastValidResponseMs = System.currentTimeMillis()
        var isEcuAwake = true

        while (running.get() && sock.isConnected) {
            val nowMs = System.currentTimeMillis()

            // ================================================================
            // 1. ALWAYS POLL HIGH-PRIORITY: Vehicle Speed (Mode 010D)
            // ================================================================
            val spdResp = sendObdCommand(output, input, "010D", 350)
            val spdBytes = parseHexBytes(spdResp, "410D")

            if (spdBytes.isNotEmpty()) {
                liveSpeedKmh = spdBytes[0]
                consecutiveTimeouts = 0
                consecutiveNoData = 0
                lastValidResponseMs = nowMs
                isEcuAwake = true
            } else if (spdResp.isNotEmpty()) {
                consecutiveTimeouts = 0
                if (spdResp.contains("NO DATA", ignoreCase = true) ||
                    spdResp.contains("UNABLE", ignoreCase = true) ||
                    spdResp.contains("BUS INIT", ignoreCase = true) ||
                    spdResp.contains("ERROR", ignoreCase = true) ||
                    spdResp.contains("STOPPED", ignoreCase = true)) {
                    consecutiveNoData++
                    Log.w(TAG, "Speed 010D non-data response: '$spdResp' (count: $consecutiveNoData)")
                    if (consecutiveNoData >= 15) {
                        isEcuAwake = false
                        pushTelemetrySnapshot(nowMs)
                        Thread.sleep(500)
                        continue
                    }
                }
            } else {
                consecutiveTimeouts++
                Log.w(TAG, "Speed 010D timeout (consecutiveTimeouts: $consecutiveTimeouts)")
            }

            // Short yield to avoid hogging CPU while maintaining responsive ~20Hz telemetry
            Thread.sleep(20)

            // Watchdog check: If 20 consecutive PID timeouts occurred (>7s of total silence), verify adapter health
            if (consecutiveTimeouts >= 20) {
                Log.w(TAG, "Watchdog triggered after 20 timeouts. Checking adapter ATRV...")
                val voltResp = sendObdCommand(output, input, "ATRV", 400)
                if (voltResp.isNotEmpty() && (voltResp.contains("V", ignoreCase = true) || voltResp.any { it.isDigit() })) {
                    // Dongle is alive! Reset counter
                    consecutiveTimeouts = 0
                    lastValidResponseMs = nowMs
                    val v = voltResp.replace("V", "").replace("v", "").trim().toFloatOrNull()
                    if (v != null && v > 5f) liveBatVolts = v
                    Log.i(TAG, "Adapter alive via ATRV ($voltResp)")
                } else {
                    // Dongle itself is unresponsive (e.g. out of range or unpowered)
                    Log.e(TAG, "OBD adapter unresponsive to ATRV watchdog pulse (resp: '$voltResp'). Link lost, reconnecting.")
                    throw java.io.IOException("OBD adapter link lost")
                }
            }

            // ================================================================
            // 2. VIEW-DRIVEN SCREEN QUEUE & INTERLEAVED BACKGROUND ROTATION
            // ================================================================
            val activeScreen = if (targetTransitionScreen >= 0) targetTransitionScreen else currentActiveScreen
            screenTick = (screenTick + 1) % 16

            // Interleaved background check: Polls ONE background metric every 2.5s
            // Never blocks speed updates (takes at most 45-120ms before speed is polled again)
            if (nowMs - lastBackgroundQueryMs > 2500L) {
                lastBackgroundQueryMs = nowMs
                executeNextBackgroundQuery(output, input, activeScreen, backgroundStep)
                backgroundStep = (backgroundStep + 1) % 10
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
        val isAuto = context.getSharedPreferences("sys_prefs", Context.MODE_PRIVATE).getBoolean("trans_auto", true)
        when (screen) {
            // Screen 0: Hero Speedometer -> RPM, TCM Direct Gear / Fuel %, Load %
            0 -> {
                when (tick % 4) {
                    0, 2 -> {
                        val resp = sendObdCommand(output, input, "010C", 300)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    1 -> {
                        if (isAuto) {
                            try {
                                sendObdCommand(output, input, "ATSH 7E1", 150)
                                val prndResp = sendObdCommand(output, input, "221E12", 250)
                                val prndBytes = parseHexBytes(prndResp, "621E12")
                                decodeTcmGear(prndBytes)
                            } finally {
                                sendObdCommand(output, input, "ATSH 7E0", 150)
                            }
                        } else {
                            val resp = sendObdCommand(output, input, "012F", 300)
                            val bytes = parseHexBytes(resp, "412F")
                            if (bytes.isNotEmpty()) liveFuelLevelPct = parseCalibratedFuel(bytes[0])
                        }
                    }
                    3 -> {
                        if (isAuto && (tick % 16) == 7) {
                            val resp = sendObdCommand(output, input, "012F", 300)
                            val bytes = parseHexBytes(resp, "412F")
                            if (bytes.isNotEmpty()) liveFuelLevelPct = parseCalibratedFuel(bytes[0])
                        } else {
                            val resp = sendObdCommand(output, input, "0104", 300)
                            val bytes = parseHexBytes(resp, "4104")
                            if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                        }
                    }
                }
            }
            // Screen 1: TPMS -> 4-Corner Pressure & Temp + PRND on TCM 7E1, then Ambient Temp on PCM 7E0
            1 -> {
                try {
                    sendObdCommand(output, input, "ATSH 720", 150)
                    val p0 = parseHexBytes(sendObdCommand(output, input, "222A05", 250), "622A05")
                    if (p0.isNotEmpty()) {
                        flPsi = ((p0[0] * 1373f) / 1000f) * 0.145038f
                        if (p0.size >= 2) flTemp = (p0[1] - 40).toFloat()
                    }

                    val p1 = parseHexBytes(sendObdCommand(output, input, "222A06", 250), "622A06")
                    if (p1.isNotEmpty()) {
                        frPsi = ((p1[0] * 1373f) / 1000f) * 0.145038f
                        if (p1.size >= 2) frTemp = (p1[1] - 40).toFloat()
                    }

                    val p2 = parseHexBytes(sendObdCommand(output, input, "222A07", 250), "622A07")
                    if (p2.isNotEmpty()) {
                        rlPsi = ((p2[0] * 1373f) / 1000f) * 0.145038f
                        if (p2.size >= 2) rlTemp = (p2[1] - 40).toFloat()
                    }

                    val p3 = parseHexBytes(sendObdCommand(output, input, "222A08", 250), "622A08")
                    if (p3.isNotEmpty()) {
                        rrPsi = ((p3[0] * 1373f) / 1000f) * 0.145038f
                        if (p3.size >= 2) rrTemp = (p3[1] - 40).toFloat()
                    }
                } finally {
                    sendObdCommand(output, input, "ATSH 7E0", 150)
                }

                // PRND & Commanded Gear (DID 221E12 on TCM Header 7E1 - only on Automatic)
                if (isAuto) {
                    try {
                        sendObdCommand(output, input, "ATSH 7E1", 150)
                        val prndResp = sendObdCommand(output, input, "221E12", 250)
                        val prndBytes = parseHexBytes(prndResp, "621E12")
                        decodeTcmGear(prndBytes)
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 150)
                    }
                }

                // Ambient Air Temp (Mode 01 PID 46 on PCM 7E0)
                val ambResp = sendObdCommand(output, input, "0146", 200)
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
                        val resp = sendObdCommand(output, input, "0105", 200)
                        val bytes = parseHexBytes(resp, "4105")
                        if (bytes.isNotEmpty()) liveCoolantC = bytes[0] - 40
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "221310", 250)
                        val bytes = parseHexBytes(resp, "621310")
                        if (bytes.size >= 2) {
                            val tempC = (((bytes[0] * 256) + bytes[1]) / 100.0f) - 40.0f
                            if (tempC in 0.0f..160.0f) liveOilTempC = tempC.roundToInt()
                        } else if (bytes.isNotEmpty() && bytes[0] > 40) {
                            liveOilTempC = bytes[0] - 40
                        }
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "010F", 200)
                        val bytes = parseHexBytes(resp, "410F")
                        if (bytes.isNotEmpty()) liveIntakeC = bytes[0] - 40
                    }
                    3 -> {
                        // Query Mode 01 PID 42 (ECU Control Module Supply Voltage) with ATRV fallback
                        val resp = sendObdCommand(output, input, "0142", 200)
                        val bytes = parseHexBytes(resp, "4142")
                        if (bytes.size >= 2) {
                            val v = ((bytes[0] * 256) + bytes[1]) / 1000.0f
                            if (v > 5f) liveBatVolts = v
                        } else {
                            val atrvResp = sendObdCommand(output, input, "ATRV", 200)
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
                        val resp = sendObdCommand(output, input, "0106", 200)
                        val bytes = parseHexBytes(resp, "4106")
                        if (bytes.isNotEmpty()) liveStft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0107", 200)
                        val bytes = parseHexBytes(resp, "4107")
                        if (bytes.isNotEmpty()) liveLtft = ((bytes[0] - 128) * 100f) / 128f
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0123", 200)
                        val bytes = parseHexBytes(resp, "4123")
                        if (bytes.size >= 2) liveRailPressurePsi = ((((bytes[0] * 256) + bytes[1]) * 10) * 0.145038f).toInt()
                    }
                    3 -> {
                        val resp = sendObdCommand(output, input, "0124", 200)
                        val bytes = parseHexBytes(resp, "4124")
                        if (bytes.size >= 2) liveAfr = (((bytes[0] * 256) + bytes[1]) / 32768.0f) * 14.7f
                    }
                    4 -> {
                        val resp = sendObdCommand(output, input, "010E", 200)
                        val bytes = parseHexBytes(resp, "410E")
                        if (bytes.isNotEmpty()) liveSparkAdvance = (bytes[0] / 2.0f) - 64.0f
                    }
                }
            }
            // Screen 4: Track & Dynamics (FAFO) -> Throttle %, RPM, Load %
            4 -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0111", 200)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "010C", 200)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                }
            }
            // Screen 5: Engine Tachometer -> High-rate RPM, Load %, Throttle %
            5 -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "010C", 200)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0111", 200)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                }
            }
            // Screen 6: Fuel & Trip Economy -> Fuel %, Load %
            6 -> {
                when (tick % 2) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "012F", 200)
                        val bytes = parseHexBytes(resp, "412F")
                        if (bytes.isNotEmpty()) liveFuelLevelPct = parseCalibratedFuel(bytes[0])
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                }
            }
            // Screen 8: Fuel Trims & HPFP Direct Injection
            8 -> {
                when (tick % 6) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0106", 200)
                        val bytes = parseHexBytes(resp, "4106")
                        if (bytes.isNotEmpty()) {
                            liveStft = ((bytes[0] - 128) * 100f) / 128f
                        }
                        Log.i(TAG, "Screen8 STFT (0106): '$resp' -> ${liveStft}%")
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0107", 200)
                        val bytes = parseHexBytes(resp, "4107")
                        if (bytes.isNotEmpty()) {
                            liveLtft = ((bytes[0] - 128) * 100f) / 128f
                        }
                        Log.i(TAG, "Screen8 LTFT (0107): '$resp' -> ${liveLtft}%")
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0123", 200)
                        val bytes = parseHexBytes(resp, "4123")
                        if (bytes.size >= 2) {
                            liveRailPressurePsi = ((((bytes[0] * 256) + bytes[1]) * 10) * 0.145038f).toInt()
                        } else {
                            val resp2 = sendObdCommand(output, input, "0122", 200)
                            val bytes2 = parseHexBytes(resp2, "4122")
                            if (bytes2.size >= 2) {
                                liveRailPressurePsi = ((((bytes2[0] * 256) + bytes2[1]) * 0.079f) * 0.145038f).toInt()
                            }
                        }
                        Log.i(TAG, "Screen8 HPFP Rail (0123): '$resp' -> ${liveRailPressurePsi} PSI")
                    }
                    3 -> {
                        val resp = sendObdCommand(output, input, "0124", 200)
                        val bytes = parseHexBytes(resp, "4124")
                        if (bytes.size >= 2) {
                            liveAfr = (((bytes[0] * 256) + bytes[1]) / 32768.0f) * 14.7f
                        } else {
                            val resp2 = sendObdCommand(output, input, "0134", 200)
                            val bytes2 = parseHexBytes(resp2, "4134")
                            if (bytes2.size >= 2) {
                                liveAfr = (((bytes2[0] * 256) + bytes2[1]) / 32768.0f) * 14.7f
                            }
                        }
                        Log.i(TAG, "Screen8 AFR (0124): '$resp' -> ${liveAfr} : 1")
                    }
                    4 -> {
                        val resp = sendObdCommand(output, input, "010E", 200)
                        val bytes = parseHexBytes(resp, "410E")
                        if (bytes.isNotEmpty()) {
                            liveSparkAdvance = (bytes[0] / 2.0f) - 64.0f
                        }
                        Log.i(TAG, "Screen8 Timing (010E): '$resp' -> ${liveSparkAdvance} deg")
                    }
                    5 -> {
                        val resp = sendObdCommand(output, input, "0132", 200)
                        val bytes = parseHexBytes(resp, "4132")
                        if (bytes.size >= 2) {
                            liveEvapVaporPa = (((bytes[0] * 256) + bytes[1]) / 4) - 8192
                        }
                        Log.i(TAG, "Screen8 EVAP (0132): '$resp' -> ${liveEvapVaporPa} Pa")
                    }
                }
            }
            // Screen 9: Cylinders & Misfire (Mode $06 / Timing)
            9 -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "010E", 200)
                        val bytes = parseHexBytes(resp, "410E")
                        if (bytes.isNotEmpty()) liveSparkAdvance = (bytes[0] / 2.0f) - 64.0f
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "010C", 200)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                }
            }
            // Screen 10: Chassis Dynamics & G-Force
            10 -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "0111", 200)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "010C", 200)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                }
            }
            // Screen 11: I/M Smog Readiness Monitors
            11 -> {
                val resp = sendObdCommand(output, input, "0101", 250)
                Log.i(TAG, "Screen11 Smog (0101): '$resp'")
            }
            // Screen 12: DTC Logs & Black Box
            12 -> {
                val resp = sendObdCommand(output, input, "03", 250)
                Log.i(TAG, "Screen12 Stored DTCs (03): '$resp'")
            }
            // Default / Other Screens
            else -> {
                when (tick % 3) {
                    0 -> {
                        val resp = sendObdCommand(output, input, "010C", 200)
                        val bytes = parseHexBytes(resp, "410C")
                        if (bytes.size >= 2) liveRpm = ((bytes[0] * 256) + bytes[1]) / 4
                    }
                    1 -> {
                        val resp = sendObdCommand(output, input, "0104", 200)
                        val bytes = parseHexBytes(resp, "4104")
                        if (bytes.isNotEmpty()) liveEngineLoadPct = (bytes[0] * 100) / 255
                    }
                    2 -> {
                        val resp = sendObdCommand(output, input, "0111", 200)
                        val bytes = parseHexBytes(resp, "4111")
                        if (bytes.isNotEmpty()) liveThrottlePct = parseCalibratedThrottle(bytes[0])
                    }
                }
            }
        }
    }

    /**
     * Interleaved background query dispatcher.
     * Executes ONE background safety query every ~2.5s instead of running a 2+ second blocking sweep.
     * Guarantees that vehicle speed polling is never delayed by more than 45-120ms.
     */
    private fun executeNextBackgroundQuery(
        output: OutputStream,
        input: java.io.InputStream,
        activeScreen: Int,
        step: Int
    ) {
        when (step) {
            0 -> { // Coolant Temp (0105 on PCM 7E0) - skip if user is on Temps screen (2)
                if (activeScreen != 2) {
                    val resp = sendObdCommand(output, input, "0105", 350)
                    val bytes = parseHexBytes(resp, "4105")
                    if (bytes.isNotEmpty()) liveCoolantC = bytes[0] - 40
                }
            }
            1 -> { // Oil Temp (221310 on PCM 7E0) - skip if user is on Temps screen (2)
                if (activeScreen != 2) {
                    val resp = sendObdCommand(output, input, "221310", 350)
                    val bytes = parseHexBytes(resp, "621310")
                    if (bytes.size >= 2) {
                        val tempC = (((bytes[0] * 256) + bytes[1]) / 100.0f) - 40.0f
                        if (tempC in 0.0f..160.0f) liveOilTempC = tempC.roundToInt()
                    } else if (bytes.isNotEmpty() && bytes[0] > 40) {
                        liveOilTempC = bytes[0] - 40
                    }
                }
            }
            2 -> { // Ambient Temp (0146 on PCM 7E0) - skip if on TPMS (1) or Temps (2)
                if (activeScreen != 1 && activeScreen != 2) {
                    val resp = sendObdCommand(output, input, "0146", 350)
                    val bytes = parseHexBytes(resp, "4146")
                    if (bytes.isNotEmpty()) {
                        val temp = bytes[0] - 40
                        if (temp in -40..60) liveAmbientC = temp
                    }
                }
            }
            3 -> { // Battery Voltage (0142 on PCM 7E0) - skip if on Temps (2)
                if (activeScreen != 2) {
                    val resp = sendObdCommand(output, input, "0142", 350)
                    val bytes = parseHexBytes(resp, "4142")
                    if (bytes.size >= 2) {
                        val v = ((bytes[0] * 256) + bytes[1]) / 1000.0f
                        if (v > 5f) liveBatVolts = v
                    } else {
                        val atrv = sendObdCommand(output, input, "ATRV", 250)
                        val v = atrv.replace("V", "").replace("v", "").trim().toFloatOrNull()
                        if (v != null && v > 5f) liveBatVolts = v
                    }
                }
            }
            4 -> { // Intake Air Temp (010F on PCM 7E0) - skip if on Temps (2)
                if (activeScreen != 2) {
                    val resp = sendObdCommand(output, input, "010F", 350)
                    val bytes = parseHexBytes(resp, "410F")
                    if (bytes.isNotEmpty()) liveIntakeC = bytes[0] - 40
                }
            }
            5 -> { // TPMS Front-Left (222A05 on BCM 720) - skip if on TPMS screen (1)
                if (activeScreen != 1) {
                    try {
                        sendObdCommand(output, input, "ATSH 720", 200)
                        val resp = sendObdCommand(output, input, "222A05", 350)
                        val bytes = parseHexBytes(resp, "622A05")
                        if (bytes.isNotEmpty()) {
                            flPsi = ((bytes[0] * 1373f) / 1000f) * 0.145038f
                            if (bytes.size >= 2) flTemp = (bytes[1] - 40).toFloat()
                        }
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 200)
                    }
                }
            }
            6 -> { // TPMS Front-Right (222A06 on BCM 720) - skip if on TPMS screen (1)
                if (activeScreen != 1) {
                    try {
                        sendObdCommand(output, input, "ATSH 720", 200)
                        val resp = sendObdCommand(output, input, "222A06", 350)
                        val bytes = parseHexBytes(resp, "622A06")
                        if (bytes.isNotEmpty()) {
                            frPsi = ((bytes[0] * 1373f) / 1000f) * 0.145038f
                            if (bytes.size >= 2) frTemp = (bytes[1] - 40).toFloat()
                        }
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 200)
                    }
                }
            }
            7 -> { // TPMS Rear-Left (222A07 on BCM 720) - skip if on TPMS screen (1)
                if (activeScreen != 1) {
                    try {
                        sendObdCommand(output, input, "ATSH 720", 200)
                        val resp = sendObdCommand(output, input, "222A07", 350)
                        val bytes = parseHexBytes(resp, "622A07")
                        if (bytes.isNotEmpty()) {
                            rlPsi = ((bytes[0] * 1373f) / 1000f) * 0.145038f
                            if (bytes.size >= 2) rlTemp = (bytes[1] - 40).toFloat()
                        }
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 200)
                    }
                }
            }
            8 -> { // TPMS Rear-Right (222A08 on BCM 720) - skip if on TPMS screen (1)
                if (activeScreen != 1) {
                    try {
                        sendObdCommand(output, input, "ATSH 720", 200)
                        val resp = sendObdCommand(output, input, "222A08", 350)
                        val bytes = parseHexBytes(resp, "622A08")
                        if (bytes.isNotEmpty()) {
                            rrPsi = ((bytes[0] * 1373f) / 1000f) * 0.145038f
                            if (bytes.size >= 2) rrTemp = (bytes[1] - 40).toFloat()
                        }
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 200)
                    }
                }
            }
            9 -> { // TCM Gear & PRND (221E12 on TCM 7E1 - only on Automatic)
                val isAuto = context.getSharedPreferences("sys_prefs", Context.MODE_PRIVATE).getBoolean("trans_auto", true)
                if (isAuto && activeScreen != 0 && activeScreen != 1) {
                    try {
                        sendObdCommand(output, input, "ATSH 7E1", 150)
                        val prndResp = sendObdCommand(output, input, "221E12", 250)
                        val prndBytes = parseHexBytes(prndResp, "621E12")
                        decodeTcmGear(prndBytes)
                    } finally {
                        sendObdCommand(output, input, "ATSH 7E0", 150)
                    }
                }
            }
        }
    }

    private fun pushTelemetrySnapshot(nowMs: Long) {
        // ND2 Transmission PRND & Active Gear Logic (Supports both Automatic 6AT & Manual 6MT)
        val isAuto = context.getSharedPreferences("sys_prefs", Context.MODE_PRIVATE).getBoolean("trans_auto", true)
        liveGear = if (isAuto) {
            when {
                liveRpm == 0 && liveSpeedKmh == 0 -> '-'
                tcmPrnd == 'R' || tcmDirectGear == 'R' -> 'R'
                tcmPrnd == 'P' || tcmDirectGear == 'P' -> 'P'
                tcmPrnd == 'N' || tcmDirectGear == 'N' -> 'N'
                tcmDirectGear in '1'..'6' -> tcmDirectGear
                liveSpeedKmh < 2 -> if (liveRpm > 400) 'P' else '-'
                else -> {
                    // SkyActiv-Drive RC6A-EL 6AT Physical Gear Ratios (2.866 Final Drive)
                    val r = liveRpm.toFloat() / max(1f, liveSpeedKmh.toFloat())
                    when {
                        r < 16.0f -> '6'
                        r < 21.0f -> '5'
                        r < 29.5f -> '4'
                        r < 42.5f -> '3'
                        r < 68.0f -> '2'
                        else -> '1'
                    }
                }
            }
        } else {
            when {
                liveRpm == 0 && liveSpeedKmh == 0 -> '-'
                tcmPrnd == 'R' || tcmDirectGear == 'R' -> 'R'
                liveSpeedKmh < 3 -> if (liveRpm > 400) 'N' else '-'
                else -> {
                    // SkyActiv-MT 6MT Physical Gear Ratios (2.866 Final Drive)
                    val r = liveRpm.toFloat() / max(1f, liveSpeedKmh.toFloat())
                    when {
                        r < 28.2f -> '6'
                        r < 35.5f -> '5'
                        r < 44.7f -> '4'
                        r < 62.0f -> '3'
                        r < 98.0f -> '2'
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

        if (nowMs - lastLogMs > 2000L) {
            lastLogMs = nowMs
            Log.i(TAG, "Stream[screen=$currentActiveScreen]: RPM=$liveRpm, Speed=$liveSpeedKmh km/h, Bat=${liveBatVolts}V, ECT=${liveCoolantC}C, Oil=${liveOilTempC}C, Gear=$liveGear, Fuel=${liveFuelLevelPct}%")
        }
    }

    private fun pushDisconnectedSnapshot() {
        val rangeMiles = if (liveFuelLevelPct > 0) (liveFuelLevelPct * 4.2f).toInt() else 0
        NativeBridge.nativeUpdateFullTelemetry(
            liveRpm, liveSpeedKmh, liveCoolantC, liveOilTempC, liveIntakeC, liveAmbientC,
            liveBatVolts, liveEngineLoadPct, liveThrottlePct, liveFuelLevelPct,
            liveGear, 0, liveHp, liveTorque,
            accel0to60, best0to60, timerState,
            0.0f, liveTripAvgMpg, tripTotalDistanceMiles, rangeMiles,
            liveAfr, liveStft, liveLtft, liveKnockRetard, liveRailPressurePsi,
            liveSparkAdvance, 0.0f, 0.0f,
            if (liveOilTempC > 0) liveOilTempC - 10 else 0, 0, 0.0f,
            flPsi, frPsi, rlPsi, rrPsi,
            flTemp, frTemp, rlTemp, rrTemp,
            0, false, false
        )
    }

    fun stop() {
        running.set(false)
        if (isReceiverRegistered) {
            try {
                context.unregisterReceiver(btStateReceiver)
            } catch (_: Exception) {}
            isReceiverRegistered = false
        }
        try { socket?.close() } catch (_: Exception) {}
        socket = null
        workerThread?.interrupt()
        workerThread = null
    }
}
