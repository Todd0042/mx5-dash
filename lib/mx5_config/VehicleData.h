#pragma once

#include <stdint.h>

/**
 * VehicleData - telemetry snapshot shared between the OBD task (core 0) and
 * the UI task (core 1). Pure host types only (no Arduino/ESP32), so the same
 * struct is used verbatim by the device build and the desktop LVGL preview.
 */
struct VehicleData {
    // Standard OBD (Mode 01) constants - polled continuously
    uint16_t rpm = 0;
    uint8_t speedKmh = 0;          // 01 0D
    uint8_t coolantC = 0;          // 01 05 (minus 40)
    uint8_t intakeAirC = 0;        // 01 0F (minus 40)
    uint8_t ambientC = 0;          // 01 46 (minus 40) - may be unsupported
    uint8_t throttlePct = 0;       // 01 11
    uint8_t engineLoadPct = 0;     // 01 04
    float batteryVolts = 0.0f;     // 01 42

    // SkyActiv "engine oil temperature" - Mode 22 DID 1310; may be unsupported
    uint8_t oilTempC = 0;
    uint8_t fuelLevelPct = 0;      // 01 2F

    // Active gear position indicator ('P','N','D','M' or '1'-'6')
    char gear = '-';

    // TPMS (Mode 22 manufacturer)
    float tirePressure[4] = {0, 0, 0, 0}; // bar
    float tireTemp[4] = {0, 0, 0, 0};     // deg C
    bool tireKnown[4] = {false, false, false, false};

    // Track & Dynamics Telemetry (Option A)
    uint8_t brakePressurePct = 0;  // 0..100%
    uint16_t estHorsepower = 0;    // Live calculated HP
    uint16_t estTorqueFtLb = 0;    // Live calculated Torque (lb-ft)
    float accel0to60TimeSec = 0.0f;
    float best0to60TimeSec = 5.7f; // ND2 factory baseline (~5.7s)
    uint8_t accelTimerState = 0;   // 0=Armed/Ready, 1=Timing, 2=Finished

    // Trip & Fuel Economy Telemetry (Option B)
    float instantMpg = 0.0f;       // Instantaneous MPG (0..60)
    float tripAvgMpg = 32.4f;      // Trip Average MPG
    float tripDistanceMiles = 0.0f;// Trip Distance
    uint16_t rangeMiles = 280;     // Estimated Range

    // Diagnostics & DTC Scanner (Option D)
    uint8_t dtcCount = 0;
    char dtcCodes[8][8] = {};
    char dtcDesc[8][32] = {};

    // Advanced Diagnostics Telemetry
    // 1. Fuel Trims & High-Pressure Fuel Rail
    float shortTermFuelTrimPct = 0.0f; // -25.0% .. +25.0% (Mode 01 06)
    float longTermFuelTrimPct = 0.0f;  // -25.0% .. +25.0% (Mode 01 07)
    float airFuelRatio = 14.7f;        // 10.0 .. 20.0 (Mode 01 24 / 34)
    uint16_t fuelRailPressurePsi = 580;// 0 .. 2900 PSI (DI Rail Pressure)
    int16_t evapVaporPa = 0;           // Tank vapor pressure Pa
    uint8_t evapPurgePct = 0;          // Purge duty 0..100%

    // 2. Cylinder Health & Mode $06 Misfires
    uint16_t cylMisfireCount[4] = {0, 0, 0, 0}; // Cyl 1, 2, 3, 4 misfires
    float sparkAdvanceDeg = 12.0f;     // Ignition timing advance (° BTDC)
    float knockRetardDeg = 0.0f;       // Knock retard (°)
    float vvtIntakeDeg = 15.0f;        // Intake VVT advance (°)
    float vvtExhaustDeg = 8.0f;        // Exhaust VVT advance (°)

    // 3. Chassis, ABS & Transmission
    float wheelSpeedKmh[4] = {0, 0, 0, 0}; // FL, FR, RL, RR
    float steeringAngleDeg = 0.0f;     // Steering wheel angle (-540° .. +540°)
    uint8_t transFluidTempC = 75;      // 6AT Transmission Fluid Temp °C
    uint16_t tccSlipRpm = 0;           // Torque converter slip RPM

    // 4. I/M Smog Readiness Monitors
    bool imMisfireReady = true;
    bool imFuelReady = true;
    bool imCompReady = true;
    bool imCatReady = true;
    bool imEvapReady = true;
    bool imO2Ready = true;
    bool imO2HeaterReady = true;
    bool imEgrVvtReady = true;

    // Dynamic Auto-Dimming & Night Mode
    bool isNightMode = false;
    uint8_t backlightPct = 95;     // 15% in night mode, 90-100% daytime

    bool connected = false;        // TCP + ELM handshake up
    bool canError = false;         // last read returned a NO DATA / error
    uint32_t lastUpdateMs = 0;
};