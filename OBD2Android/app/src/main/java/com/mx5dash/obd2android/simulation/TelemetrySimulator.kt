package com.mx5dash.obd2android.simulation

import android.util.Log
import com.mx5dash.obd2android.bridge.NativeBridge
import com.mx5dash.obd2android.storage.DataLogger
import java.util.concurrent.atomic.AtomicBoolean
import kotlin.math.*

class TelemetrySimulator(private val dataLogger: DataLogger) {

    companion object {
        private const val TAG = "TelemetrySimulator"
        private val GEAR_RATIOS = floatArrayOf(157.0f, 92.0f, 63.0f, 49.0f, 40.0f, 31.0f)
    }

    private val isRunning = AtomicBoolean(false)
    private var simThread: Thread? = null

    private fun ease(x: Float): Float = x * x * (3.0f - 2.0f * x)

    fun start() {
        if (isRunning.get()) return
        isRunning.set(true)

        simThread = Thread({
            var t = 0f
            Log.i(TAG, "Started MX-5 Dynamic Telemetry Simulation")

            while (isRunning.get()) {
                t += 0.05f // ~20 Hz tick
                val cycle = t % 60.0f // 60-second drive cycle loop

                // Speed profile (km/h): accelerates up to 215 km/h (134 MPH)
                val spd = when {
                    cycle < 12.0f -> 215.0f * ease(cycle / 12.0f)
                    cycle < 22.0f -> 215.0f - 85.0f * ease((cycle - 12.0f) / 10.0f)
                    cycle < 30.0f -> 130.0f + 65.0f * ease((cycle - 22.0f) / 8.0f)
                    cycle < 42.0f -> 195.0f - 140.0f * ease((cycle - 30.0f) / 12.0f)
                    else -> 55.0f * (1.0f - ease((cycle - 42.0f) / 18.0f))
                }

                // ND2 6MT: Gear calculation
                val gi = when {
                    spd < 12f -> 0
                    spd < 32f -> 1
                    spd < 50f -> 2
                    spd < 75f -> 3
                    spd < 105f -> 4
                    else -> 5
                }

                val speedKmh = (spd + 0.5f).toInt()
                val rpm = (spd * GEAR_RATIOS[gi] + (if (spd < 1.0f) 780.0f else 0.0f) + 0.5f).toInt().coerceIn(0, 7500)

                val gear = when {
                    rpm == 0 && speedKmh == 0 -> '-'
                    speedKmh == 0 -> 'N'
                    else -> {
                        val r = rpm.toFloat() / max(1f, speedKmh.toFloat())
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

                val coolantC = (83.0f + 4.0f * sin(t / 3.1f)).toInt()
                val intakeC = (29.0f + 7.0f * sin(t / 5.3f)).toInt()
                val ambientC = (24.0f + 3.0f * sin(t / 7.1f)).toInt()
                val throttlePct = (14.0f + 80.0f * (0.5f + 0.5f * sin(t / 2.3f))).toInt().coerceIn(0, 100)
                val engineLoadPct = (22.0f + 70.0f * (0.5f + 0.5f * sin(t / 1.9f))).toInt().coerceIn(0, 100)
                val batVolts = 14.1f + 0.35f * sin(t / 6.7f)
                val oilTempC = (91.0f + 5.0f * sin(t / 9.1f)).toInt()
                val fuelLevelPct = max(64.0f - t / 40.0f, 15.0f).toInt()

                val braking = (cycle in 12.0f..16.0f) || (cycle in 30.0f..36.0f)
                val brakePct = if (braking) (65.0f * sin((cycle - 12.0f) * 0.78f)).toInt().coerceIn(0, 100) else 0

                val hp = min(181, ((rpm / 7500.0f) * 181.0f * (throttlePct / 100.0f)).toInt())
                val torque = min(151, (151.0f * (engineLoadPct / 100.0f)).toInt())

                val accel0to60 = if (cycle < 5.42f) cycle else 5.42f
                val accelTimerState = if (cycle < 5.42f) 1 else 2

                val instantMpg = if (speedKmh > 5) min(60.0f, (32.0f + 14.0f * cos(t / 3.0f)).toFloat()) else 0.0f
                val tripAvgMpg = 32.8f
                val tripDistance = 14.2f + t / 40.0f
                val rangeMiles = (fuelLevelPct * 4.2f).toInt()

                val afr = if (throttlePct > 80) 12.5f else 14.7f + 0.2f * sin(t / 1.5f)
                val stft = 3.2f + 2.5f * sin(t / 2.0f)
                val ltft = 5.8f
                val knockRetard = 0.0f
                val railPressurePsi = 1200 + engineLoadPct * 12

                val sparkAdvance = 14.0f + 12.0f * (rpm / 7500.0f)
                val vvtIntake = 12.0f + 16.0f * (rpm / 7500.0f)
                val vvtExhaust = 4.0f + 8.0f * (rpm / 7500.0f)
                val transFluidTempC = 78
                val tccSlipRpm = if (speedKmh > 20) 0 else 45
                val steeringAngle = 2.5f * sin(t / 4.0f)

                // Simulate dynamic low tire pressure event on Front Left tire (21.8 PSI)
                val flPsi = if (cycle < 25.0f) 21.8f else 32.0f + 0.8f * sin(t / 10.0f)
                val frPsi = 32.0f + 0.8f * sin(t / 10.0f + 1.0f)
                val rlPsi = 32.0f + 0.8f * sin(t / 10.0f + 2.0f)
                val rrPsi = 32.0f + 0.8f * sin(t / 10.0f + 3.0f)

                val flTemp = 23.0f + 2.0f * sin(t / 12.0f)
                val frTemp = 23.0f + 2.0f * sin(t / 12.0f + 1.0f)
                val rlTemp = 23.0f + 2.0f * sin(t / 12.0f + 2.0f)
                val rrTemp = 23.0f + 2.0f * sin(t / 12.0f + 3.0f)

                if (abs(t % 2.0f) < 0.06f) {
                    Log.d(TAG, "Sim tick: t=${"%.1f".format(t)}s spd=${speedKmh}km/h rpm=$rpm gear=$gear")
                }

                NativeBridge.nativeUpdateFullTelemetry(
                    rpm, speedKmh, coolantC, oilTempC, intakeC, ambientC,
                    batVolts, engineLoadPct, throttlePct, fuelLevelPct,
                    gear, brakePct, hp, torque,
                    accel0to60, 5.28f, accelTimerState,
                    instantMpg, tripAvgMpg, tripDistance, rangeMiles,
                    afr, stft, ltft, knockRetard, railPressurePsi,
                    sparkAdvance, vvtIntake, vvtExhaust,
                    transFluidTempC, tccSlipRpm, steeringAngle,
                    flPsi, frPsi, rlPsi, rrPsi,
                    flTemp, frTemp, rlTemp, rrTemp,
                    0, false, true
                )

                try {
                    Thread.sleep(50) // 20 Hz
                } catch (_: InterruptedException) {
                    break
                }
            }
        }, "TelemetrySimulatorThread").apply {
            isDaemon = true
            start()
        }
    }

    fun stop() {
        isRunning.set(false)
        simThread?.interrupt()
        simThread = null
    }
}
