package com.mx5dash.obd2android.bridge

object NativeBridge {
    init {
        System.loadLibrary("mx5dash")
    }

    external fun nativeInit(prefBridge: PreferencesBridge)
    external fun nativeRender(outPixels: IntArray, len: Int): Boolean
    external fun nativeTouch(action: Int, x: Int, y: Int)
    external fun nativeNext()
    external fun nativePrev()
    external fun nativeToggleMenu()
    external fun nativeSetScreen(index: Int)
    external fun nativeSetThemeMode(mode: Int)
    external fun nativeSetNightMode(isNight: Boolean)

    external fun nativeUpdateFullTelemetry(
        rpm: Int, speedKmh: Int, coolantC: Int, oilTempC: Int, intakeC: Int, ambientC: Int,
        batVolts: Float, loadPct: Int, throttlePct: Int, fuelPct: Int,
        gear: Char, brakePct: Int, hp: Int, torque: Int,
        accel0to60: Float, best0to60: Float, accelTimerState: Int,
        instantMpg: Float, tripAvgMpg: Float, tripDistance: Float, rangeMiles: Int,
        afr: Float, stft: Float, ltft: Float, knockRetard: Float, railPressurePsi: Int,
        sparkAdvance: Float, vvtIntake: Float, vvtExhaust: Float,
        transFluidTempC: Int, tccSlipRpm: Int, steeringAngle: Float,
        flPsi: Float, frPsi: Float, rlPsi: Float, rrPsi: Float,
        flTemp: Float, frTemp: Float, rlTemp: Float, rrTemp: Float,
        dtcCount: Int, isNight: Boolean, connected: Boolean
    )
}
