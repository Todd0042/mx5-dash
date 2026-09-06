#include "ObdService.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "BleElm.h"
#include "../mx5_config/Config.h"
#include "../mx5_config/UserPrefs.h"

#include <cstring>
#include <cstdio>

// ---------------------------------------------------------------------------
// pimpl - all BLE/FreeRTOS-specific state lives here so ObdService.h stays
// ESP32-free (the desktop preview includes the header but not this TU).
// ---------------------------------------------------------------------------

struct ObdService::Impl {
    BleElm elm;
    VehicleData data;

    portMUX_TYPE mux = portMUX_INITIALIZER_UNLOCKED;
    TaskHandle_t task = nullptr;

    uint32_t lastRpmMs = 0;
    uint32_t lastSeqMs = 0;      // gate for the medium/slow rotation
    uint8_t  slowIdx = 0;        // current PID in the rotation
    uint32_t lastTpmsMs = 0;
    uint32_t lastOilMs = 0;
    uint32_t pollErrors = 0;     // consecutive failed reads

    uint32_t accelStartMs = 0;
    uint8_t  prevSpeedKmh = 0;
    uint32_t prevSpeedMs = 0;
    uint32_t lastDimCheckMs = 0;
    uint32_t lastSafetySweepMs = 0;
    float    tripTotalDistanceMiles = 0.0f;
    float    tripTotalGallons = 0.0f;
    uint32_t lastTripCalcMs = 0;
    char     tcmDirectGear = '-';
    volatile uint8_t activeScreen_ = 0;

    // Setup wizard / calibration state (updated under mux)
    volatile bool frozen_ = false;            // task suspended (wizard up)
    volatile bool calibrating_ = false;       // wheel-mapping in progress
    volatile uint8_t activeWheel_ = 0;        // 0..3 corner being bound
    char capturedDid_[8] = "";                // DID that fired during current read
    bool bound_[4] = {false, false, false, false}; // corners already mapped this session
    uint16_t lastDidVal_[4] = {0, 0, 0, 0};   // previous raw value per DID
};

// ---------------------------------------------------------------------------
// FreeRTOS task setup
// ---------------------------------------------------------------------------

void ObdService::start() {
    Impl* p = new Impl();
    impl_ = p;
    p->elm.begin(MX5_BLE_DEVICE_PREFIX);
    xTaskCreatePinnedToCore(taskMain, "obd", 8192, this, 1, &p->task, 0);
}

void ObdService::taskMain(void* arg) {
    static_cast<ObdService*>(arg)->loopTask();
}

// Estimates the active gear from the RPM / speed ratio and TCM PRND position.
// Supports both Automatic 6AT (PRND state + 6-speed ratios) and Manual 6MT.
static char estimateGear(uint16_t rpm, uint8_t speedKmh, bool isAuto, char tcmPrnd, char tcmDirectGear = '-') {
    if (rpm == 0 && speedKmh == 0) return '-';   // no signal yet
    if (isAuto) {
        if (tcmPrnd == 'R') return 'R';
        if (tcmPrnd == 'P') return 'P';
        if (tcmPrnd == 'N') return 'N';
        if (speedKmh < 2) return (rpm > 400) ? 'P' : '-';
        if (tcmDirectGear >= '1' && tcmDirectGear <= '6') return tcmDirectGear;

        // SkyActiv-Drive RC6A-EL 6AT Physical Gear Ratios (2.866 Final Drive)
        float r = (float)rpm / (float)(speedKmh > 0 ? speedKmh : 1);
        if (r < 16.0f) return '6';
        if (r < 21.0f) return '5';
        if (r < 29.5f) return '4';
        if (r < 42.5f) return '3';
        if (r < 68.0f) return '2';
        return '1';
    } else {
        if (speedKmh < 3) return (rpm > 400) ? 'N' : '-';

        // SkyActiv-MT 6MT Physical Gear Ratios (2.866 Final Drive)
        float r = (float)rpm / (float)(speedKmh > 0 ? speedKmh : 1);
        if (r < 28.2f) return '6';
        if (r < 35.5f) return '5';
        if (r < 44.7f) return '4';
        if (r < 62.0f) return '3';
        if (r < 98.0f) return '2';
        return '1';
    }
}

void ObdService::loopTask() {
    Impl* p = impl_;
    for (;;) {
        p->elm.loop();                     // keep BLE + ELM handshake healthy
        connected_ = p->elm.isInitialized();
        uint32_t now = millis();

        // Frozen (setup wizard on screen): stop normal PID polling, keep BLE up.
        if (p->frozen_) {
            vTaskDelay(20 / portTICK_PERIOD_MS);
            continue;
        }

        // Calibration mode: poll raw Mode 22 DIDs instead of normal telemetry.
        if (p->calibrating_) {
            pollCalibration(*p, now);
            vTaskDelay(5 / portTICK_PERIOD_MS);
            continue;
        }

        if (connected_) {
            pollTick(*p, now);
            if (MX5_TPMS_ENABLED) pollTpms(*p, now);

            // Compute dynamic derived metrics across frames
            portENTER_CRITICAL(&p->mux);
            p->data.connected = true;
            p->data.lastUpdateMs = now;
            p->data.isAutomatic = UserPrefs::getTransAuto();
            p->data.gear = estimateGear(p->data.rpm, p->data.speedKmh, p->data.isAutomatic, p->data.tcmPrnd, p->tcmDirectGear);

            // 1. Live HP & Torque estimations for Skyactiv-G 2.0L (ND2)
            float loadRatio = (float)p->data.engineLoadPct / 100.0f;
            float rpmRatio = (float)p->data.rpm / 7500.0f;
            if (rpmRatio > 1.0f) rpmRatio = 1.0f;
            p->data.estTorqueFtLb = (uint16_t)(151.0f * loadRatio * (0.55f + 0.45f * sinf(rpmRatio * 3.14159f)));
            if (p->data.rpm > 0) {
                p->data.estHorsepower = (uint16_t)(((uint32_t)p->data.rpm * p->data.estTorqueFtLb) / 5252);
            } else {
                p->data.estHorsepower = 0;
            }

            // 2. 0-60 Auto-Timer State Machine
            if (p->data.speedKmh == 0) {
                p->data.accelTimerState = 0; // Armed/Ready
                p->data.accel0to60TimeSec = 0.0f;
            } else if (p->data.accelTimerState == 0 && p->data.speedKmh > 2 && p->data.throttlePct > 15) {
                p->data.accelTimerState = 1; // Timing
                p->accelStartMs = now;
            } else if (p->data.accelTimerState == 1) {
                p->data.accel0to60TimeSec = (float)(now - p->accelStartMs) / 1000.0f;
                if (p->data.speedKmh >= 97) { // 60.27 mph
                    p->data.accelTimerState = 2; // Finished
                    if (p->data.accel0to60TimeSec < p->data.best0to60TimeSec || p->data.best0to60TimeSec == 0.0f) {
                        p->data.best0to60TimeSec = p->data.accel0to60TimeSec;
                    }
                }
            }

            // 3. Instantaneous MPG & Trip Economy Integration
            float speedMph = (float)p->data.speedKmh * 0.621371f;
            if (p->data.speedKmh > 0) {
                if (p->data.throttlePct == 0 && p->data.rpm > 1200) {
                    p->data.instantMpg = 60.0f; // Fuel cutoff / engine braking
                } else {
                    float fuelRateGph = 0.22f + 0.075f * ((float)p->data.rpm / 1000.0f) * ((float)p->data.engineLoadPct / 100.0f);
                    float mpg = speedMph / (fuelRateGph > 0.05f ? fuelRateGph : 0.05f);
                    p->data.instantMpg = (mpg > 60.0f) ? 60.0f : mpg;
                }
            } else {
                p->data.instantMpg = 0.0f;
            }

            // Trip Accumulator (Distance & Average MPG)
            if (p->lastTripCalcMs == 0) {
                p->lastTripCalcMs = now;
            } else if (now > p->lastTripCalcMs) {
                float tripDtSec = (float)(now - p->lastTripCalcMs) / 1000.0f;
                if (tripDtSec >= 0.02f && tripDtSec <= 2.0f) {
                    p->lastTripCalcMs = now;
                    float dMiles = speedMph * (tripDtSec / 3600.0f);
                    p->tripTotalDistanceMiles += dMiles;
                    p->data.tripDistanceMiles = p->tripTotalDistanceMiles;

                    float fuelRateGph = 0.0f;
                    if (p->data.speedKmh > 0) {
                        if (p->data.throttlePct == 0 && p->data.rpm > 1200) {
                            fuelRateGph = 0.0f;
                        } else {
                            fuelRateGph = 0.22f + 0.075f * ((float)p->data.rpm / 1000.0f) * ((float)p->data.engineLoadPct / 100.0f);
                        }
                    } else if (p->data.rpm > 400) {
                        fuelRateGph = 0.20f * ((float)p->data.rpm / 700.0f) * ((float)p->data.engineLoadPct / 20.0f);
                    }
                    p->tripTotalGallons += fuelRateGph * (tripDtSec / 3600.0f);

                    if (p->tripTotalGallons > 0.0005f && p->tripTotalDistanceMiles > 0.01f) {
                        float avg = p->tripTotalDistanceMiles / p->tripTotalGallons;
                        p->data.tripAvgMpg = (avg > 99.9f) ? 99.9f : avg;
                    }
                }
            }

            p->data.rangeMiles = (p->data.fuelLevelPct > 0) ? (uint16_t)(p->data.fuelLevelPct * 4.2f) : 0;

            // 4. Deceleration / Brake Pressure Indicator
            if (p->prevSpeedMs > 0 && now > p->prevSpeedMs) {
                float dtSec = (float)(now - p->prevSpeedMs) / 1000.0f;
                if (dtSec > 0.05f) {
                    float decel = (float)((int16_t)p->prevSpeedKmh - (int16_t)p->data.speedKmh) / dtSec; // km/h per sec
                    if (decel > 2.0f && p->data.throttlePct == 0) {
                        uint8_t brk = (uint8_t)(decel * 4.5f);
                        p->data.brakePressurePct = (brk > 100) ? 100 : brk;
                    } else {
                        p->data.brakePressurePct = 0;
                    }
                    p->prevSpeedKmh = p->data.speedKmh;
                    p->prevSpeedMs = now;
                }
            } else {
                p->prevSpeedKmh = p->data.speedKmh;
                p->prevSpeedMs = now;
            }

            // 5. Dynamic Auto-Dimming & Night Mode Trigger (Relaxed 3000ms interval on Core 0)
            if (MX5_AUTO_DIM_ENABLED && (now - p->lastDimCheckMs >= MX5_DIM_CHECK_INTERVAL_MS)) {
                p->lastDimCheckMs = now;
                // Senses electrical load shift (headlights/parking lights on) or ambient conditions
                bool night = (p->data.batteryVolts > 0.0f && p->data.batteryVolts < MX5_NIGHT_VOLTS_THRESHOLD);
                p->data.isNightMode = night;
                p->data.backlightPct = night ? MX5_BACKLIGHT_NIGHT_PCT : MX5_BACKLIGHT_DAY_PCT;
            }

            portEXIT_CRITICAL(&p->mux);
        } else {
            portENTER_CRITICAL(&p->mux);
            p->data.connected = false;
            portEXIT_CRITICAL(&p->mux);
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);   // ~5 ms between ticks
    }
}

void ObdService::snapshot(VehicleData& out) {
    if (!impl_) { out = VehicleData(); return; }
    portENTER_CRITICAL(&impl_->mux);
    out = impl_->data;
    portEXIT_CRITICAL(&impl_->mux);
}

// ---------------------------------------------------------------------------
// Raw ELM327 queries + hex decoding
// ---------------------------------------------------------------------------

static bool hexVal(char c, uint8_t& v) {
    if (c >= '0' && c <= '9') { v = (uint8_t)(c - '0'); return true; }
    if (c >= 'A' && c <= 'F') { v = (uint8_t)(c - 'A' + 10); return true; }
    if (c >= 'a' && c <= 'f') { v = (uint8_t)(c - 'a' + 10); return true; }
    return false;
}

// Finds the "41<pid>" echo in a trimmed response ("410D48") and copies N
// following bytes. Works for Mode 01 PIDs as long as headers/spaces/echo are
// off (ATE0 ATS0 ATH0) - which BleElm ensures during init.
static bool parsePidBytes(const char* resp, const char* pidHex, uint8_t* out, uint8_t n) {
    char needle[5];
    needle[0] = '4';
    needle[1] = '1';
    needle[2] = pidHex[0];
    needle[3] = pidHex[1];
    needle[4] = '\0';

    const char* p = strstr(resp, needle);
    if (!p) return false;
    p += 4;

    for (uint8_t i = 0; i < n; i++) {
        uint8_t hi = 0, lo = 0;
        if (!hexVal(p[i * 2], hi) || !hexVal(p[i * 2 + 1], lo)) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

static bool parseMode22Bytes(const char* resp, const char* didHex, uint8_t* out, uint8_t n) {
    char needle[7];
    needle[0] = '6';
    needle[1] = '2';
    size_t dlen = strlen(didHex);
    if (dlen > 4) dlen = 4;
    for (size_t i = 0; i < dlen; i++) {
        needle[2 + i] = didHex[i];
    }
    needle[2 + dlen] = '\0';

    const char* p = strstr(resp, needle);
    if (!p) return false;
    p += 2 + dlen;

    for (uint8_t i = 0; i < n; i++) {
        uint8_t hi = 0, lo = 0;
        if (!hexVal(p[i * 2], hi) || !hexVal(p[i * 2 + 1], lo)) return false;
        out[i] = (uint8_t)((hi << 4) | lo);
    }
    return true;
}

bool ObdService::readUint8(Impl& i, const char* cmd, const char* pidHex, uint8_t& out) {
    char resp[BleElm::MAX_RESPONSE];
    if (!i.elm.sendQuery(cmd, resp, sizeof(resp), 400)) { i.pollErrors++; return false; }
    if (!parsePidBytes(resp, pidHex, &out, 1)) { i.pollErrors++; return false; }
    i.pollErrors = 0;
    return true;
}

bool ObdService::readUint16(Impl& i, const char* cmd, const char* pidHex, uint16_t& out) {
    char resp[BleElm::MAX_RESPONSE];
    uint8_t bytes[2];
    if (!i.elm.sendQuery(cmd, resp, sizeof(resp), 400)) { i.pollErrors++; return false; }
    if (!parsePidBytes(resp, pidHex, bytes, 2)) { i.pollErrors++; return false; }
    out = (uint16_t)((bytes[0] << 8) | bytes[1]);
    i.pollErrors = 0;
    return true;
}

static inline uint8_t parseCalibratedFuel(uint8_t raw) {
    if (raw <= 8) return 0;
    uint32_t val = ((uint32_t)(raw - 8) * 100) / 216;
    return (val > 100) ? 100 : (uint8_t)val;
}

// ---------------------------------------------------------------------------
// PID polling - one per cadence expiry, sequenced so only one ELM command is
// in flight at a time (the dongle is single-command, prompt-terminated).
// ---------------------------------------------------------------------------

void ObdService::setActiveScreen(uint8_t screenIndex) {
    if (!impl_) return;
    portENTER_CRITICAL(&impl_->mux);
    impl_->activeScreen_ = screenIndex;
    portEXIT_CRITICAL(&impl_->mux);
}

void ObdService::pollTick(Impl& i, uint32_t now) {
    uint16_t tmp = 0;
    uint8_t b = 0;

    // 1. ALWAYS Poll High-Priority Speed (010D)
    if (now - i.lastSeqMs >= MX5_POLL_FAST_MS) {
        i.lastSeqMs = now;
        if (readUint8(i, "010D", "0D", b)) {
            portENTER_CRITICAL(&i.mux);
            i.data.speedKmh = b;
            portEXIT_CRITICAL(&i.mux);
        }
    }

    uint8_t screen = i.activeScreen_;
    i.slowIdx = (i.slowIdx + 1) % 16;

    // 2. 25-Second Safety Sweep (TPMS + Critical Overheat + Battery Volts + Ambient Temp when not on Screen 1)
    if (now - i.lastSafetySweepMs > 25000 && screen != 1) {
        i.lastSafetySweepMs = now;
        pollTpms(i, now);
        if (readUint8(i, "0105", "05", b)) {
            portENTER_CRITICAL(&i.mux);
            i.data.coolantC = b > 40 ? b - 40 : 0;
            portEXIT_CRITICAL(&i.mux);
        }
        char resp[BleElm::MAX_RESPONSE];
        if (i.elm.sendQuery("221310", resp, sizeof(resp), 400)) {
            uint8_t ob[1];
            if (parseMode22Bytes(resp, "1310", ob, 1)) {
                portENTER_CRITICAL(&i.mux);
                i.data.oilTempC = (ob[0] > 40) ? (ob[0] - 40) : 0;
                portEXIT_CRITICAL(&i.mux);
            }
        }
        uint16_t batTmp = 0;
        if (readUint16(i, "0142", "42", batTmp)) {
            portENTER_CRITICAL(&i.mux);
            i.data.batteryVolts = (float)batTmp / 1000.0f;
            portEXIT_CRITICAL(&i.mux);
        }
    } else {
        // 3. View-Driven Command Queue based on active screen
        switch (screen) {
            // Screen 0: Hero Speedometer -> RPM, Fuel %, TCM Direct Gear
            case 0:
                switch (i.slowIdx % 4) {
                    case 0:
                    case 2:
                        if (readUint16(i, "010C", "0C", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.rpm = tmp / 4;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint8(i, "012F", "2F", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.fuelLevelPct = parseCalibratedFuel(b);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 3:
                        if (i.data.isAutomatic) {
                            char resp[BleElm::MAX_RESPONSE];
                            i.elm.sendQuery("ATSH 7E1", resp, sizeof(resp), 150);
                            if (i.elm.sendQuery("221E12", resp, sizeof(resp), 250)) {
                                uint8_t gb[1];
                                if (parseMode22Bytes(resp, "1E12", gb, 1) && gb[0] >= 1 && gb[0] <= 6) {
                                    i.tcmDirectGear = (char)('0' + gb[0]);
                                }
                            }
                            i.elm.sendQuery("ATSH 7E0", resp, sizeof(resp), 150);
                        } else {
                            if (readUint16(i, "010C", "0C", tmp)) {
                                portENTER_CRITICAL(&i.mux);
                                i.data.rpm = tmp / 4;
                                portEXIT_CRITICAL(&i.mux);
                            }
                        }
                        break;
                }
                break;

            // Screen 1: TPMS -> 4-Corner Pressure & Temp
            case 1:
                pollTpms(i, now);
                break;

            // Screen 2: Temperatures -> Coolant, Oil Temp, Intake Air, Battery
            case 2:
                switch (i.slowIdx % 4) {
                    case 0:
                        if (readUint8(i, "0105", "05", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.coolantC = b > 40 ? b - 40 : 0;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1: {
                        char resp[BleElm::MAX_RESPONSE];
                        if (i.elm.sendQuery("221310", resp, sizeof(resp), 400)) {
                            uint8_t ob[1];
                            if (parseMode22Bytes(resp, "1310", ob, 1)) {
                                portENTER_CRITICAL(&i.mux);
                                i.data.oilTempC = (ob[0] > 40) ? (ob[0] - 40) : 0;
                                portEXIT_CRITICAL(&i.mux);
                            }
                        }
                        break;
                    }
                    case 2:
                        if (readUint8(i, "010F", "0F", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.intakeAirC = b > 40 ? b - 40 : 0;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 3:
                        if (readUint16(i, "0142", "42", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.batteryVolts = (float)tmp / 1000.0f;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;

            // Screen 3: Diagnostic Hub -> Trims (STFT/LTFT), FRP, AFR, Spark Timing
            case 3:
                switch (i.slowIdx % 5) {
                    case 0:
                        if (readUint8(i, "0106", "06", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.shortTermFuelTrimPct = ((float)b - 128.0f) * (100.0f / 128.0f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint8(i, "0107", "07", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.longTermFuelTrimPct = ((float)b - 128.0f) * (100.0f / 128.0f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 2:
                        if (readUint16(i, "0123", "23", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.fuelRailPressurePsi = (uint16_t)((float)tmp * 10.0f * 0.145038f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 3:
                        if (readUint16(i, "0124", "24", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.airFuelRatio = ((float)tmp / 32768.0f) * 14.7f;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 4:
                        if (readUint8(i, "010E", "0E", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.sparkAdvanceDeg = ((float)b / 2.0f) - 64.0f;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;

            // Screen 4: Track & Dynamics (FAFO) -> Calibrated Throttle %, RPM, Load %
            case 4:
                switch (i.slowIdx % 3) {
                    case 0:
                        if (readUint8(i, "0111", "11", b)) {
                            uint8_t raw = (uint8_t)(((uint16_t)b * 100) / 255);
                            uint8_t eff = (raw <= 13) ? 0 : (uint8_t)((((uint16_t)(raw - 13)) * 100) / 87);
                            if (eff > 100) eff = 100;
                            portENTER_CRITICAL(&i.mux);
                            i.data.throttlePct = eff;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint16(i, "010C", "0C", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.rpm = tmp / 4;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 2:
                        if (readUint8(i, "0104", "04", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.engineLoadPct = (uint8_t)(((uint16_t)b * 100) / 255);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;

            // Screen 5: Engine Tachometer -> Fast RPM, Load %, Calibrated Throttle %
            case 5:
                switch (i.slowIdx % 3) {
                    case 0:
                        if (readUint16(i, "010C", "0C", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.rpm = tmp / 4;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint8(i, "0104", "04", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.engineLoadPct = (uint8_t)(((uint16_t)b * 100) / 255);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 2:
                        if (readUint8(i, "0111", "11", b)) {
                            uint8_t raw = (uint8_t)(((uint16_t)b * 100) / 255);
                            uint8_t eff = (raw <= 13) ? 0 : (uint8_t)((((uint16_t)(raw - 13)) * 100) / 87);
                            if (eff > 100) eff = 100;
                            portENTER_CRITICAL(&i.mux);
                            i.data.throttlePct = eff;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;

            // Screen 6: Fuel & Trip Economy -> Fuel %, Load %
            case 6:
                switch (i.slowIdx % 2) {
                    case 0:
                        if (readUint8(i, "012F", "2F", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.fuelLevelPct = parseCalibratedFuel(b);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint8(i, "0104", "04", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.engineLoadPct = (uint8_t)(((uint16_t)b * 100) / 255);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;

            // Diagnostic Sub-Screens (8-12): Trims (STFT/LTFT), FRP, AFR, Spark Timing
            default:
                switch (i.slowIdx % 5) {
                    case 0:
                        if (readUint8(i, "0106", "06", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.shortTermFuelTrimPct = ((float)b - 128.0f) * (100.0f / 128.0f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 1:
                        if (readUint8(i, "0107", "07", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.longTermFuelTrimPct = ((float)b - 128.0f) * (100.0f / 128.0f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 2:
                        if (readUint16(i, "0123", "23", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.fuelRailPressurePsi = (uint16_t)((float)tmp * 10.0f * 0.145038f);
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 3:
                        if (readUint16(i, "0124", "24", tmp)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.airFuelRatio = ((float)tmp / 32768.0f) * 14.7f;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                    case 4:
                        if (readUint8(i, "010E", "0E", b)) {
                            portENTER_CRITICAL(&i.mux);
                            i.data.sparkAdvanceDeg = ((float)b / 2.0f) - 64.0f;
                            portEXIT_CRITICAL(&i.mux);
                        }
                        break;
                }
                break;
        }
    }

    // surface consecutive poll failures to the diagnostics screen
    {
        portENTER_CRITICAL(&i.mux);
        i.data.canError = i.pollErrors > 3;
        portEXIT_CRITICAL(&i.mux);
    }
}

// ---------------------------------------------------------------------------
// BLE config forwarding to BleElm (public API)
// ---------------------------------------------------------------------------

void ObdService::setBlePrefix(const char* prefix) {
    if (impl_) impl_->elm.setNamePrefix(prefix);
}

void ObdService::setBleScanTimeout(uint16_t ms) {
    if (impl_) impl_->elm.setRetryDelay(ms);
}

void ObdService::rescan() {
    if (impl_) impl_->elm.rescan();
}

void ObdService::disconnect() {
    if (impl_) impl_->elm.forceDisconnect();
}

const char* ObdService::getBlePrefix() const {
    return impl_ ? impl_->elm.namePrefix() : "vLinker";
}

uint16_t ObdService::getBleScanTimeout() const {
    return impl_ ? (uint16_t)impl_->elm.retryDelay() : 5000;
}

bool ObdService::isScanning() const {
    return impl_ ? impl_->elm.isScanning() : false;
}

const char* ObdService::getConnectedDeviceName() const {
    return impl_ ? impl_->elm.connectedDeviceName() : "";
}

int8_t ObdService::getRssi() const {
    return impl_ ? impl_->elm.rssi() : 0;
}

void ObdService::startBleScan() {
    if (impl_) impl_->elm.startScan(5);
}

void ObdService::stopBleScan() {
    if (impl_) impl_->elm.stopScan();
}

uint8_t ObdService::getDiscoveredDeviceCount() const {
    return impl_ ? impl_->elm.getDiscoveredCount() : 0;
}

bool ObdService::getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const {
    return impl_ ? impl_->elm.getDiscoveredDevice(index, out) : false;
}

void ObdService::pairDevice(const char* mac, const char* name) {
    if (impl_) impl_->elm.pairDevice(mac, name);
}

void ObdService::forgetPairedDevice() {
    if (impl_) impl_->elm.forgetPairedDevice();
}

void ObdService::getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const {
    if (impl_) impl_->elm.getPairedDevice(macBuf, macLen, nameBuf, nameLen);
}

// ---------------------------------------------------------------------------
// TPMS - Mode 22 manufacturer DIDs (MS-CAN)
// ---------------------------------------------------------------------------
void ObdService::pollTpms(Impl& i, uint32_t now) {
    if (now - i.lastTpmsMs < MX5_TPMS_INTERVAL_MS) return;
    i.lastTpmsMs = now;

    // Switch to Instrument Cluster / BCM header 720
    char resp[BleElm::MAX_RESPONSE];
    i.elm.sendQuery("ATSH 720", resp, sizeof(resp), 150);

    const char* dids[4] = {"2A05", "2A06", "2A07", "2A08"};
    for (uint8_t k = 0; k < 4; k++) {
        char cmd[16];
        snprintf(cmd, sizeof(cmd), "22%s", dids[k]);
        if (i.elm.sendQuery(cmd, resp, sizeof(resp), 250)) {
            uint8_t ob[2];
            if (parseMode22Bytes(resp, dids[k], ob, 2)) {
                portENTER_CRITICAL(&i.mux);
                float psi = (((float)ob[0] * 1373.0f) / 1000.0f) * 0.145038f;
                i.data.tirePressure[k] = psi / 14.5038f;
                i.data.tireTemp[k] = (float)ob[1] - 40.0f;
                i.data.tireKnown[k] = true;
                portEXIT_CRITICAL(&i.mux);
            }
        }
    }

    // Query PRND Selector Position DID 222A27 while on Header 720
    if (i.elm.sendQuery("222A27", resp, sizeof(resp), 250)) {
        uint8_t ob[1];
        if (parseMode22Bytes(resp, "2A27", ob, 1)) {
            char prnd = '-';
            switch (ob[0]) {
                case 1: prnd = 'P'; break;
                case 2: prnd = 'R'; break;
                case 3: prnd = 'N'; break;
                case 4: prnd = 'D'; break;
                case 5: prnd = 'M'; break;
                default: prnd = '-'; break;
            }
            portENTER_CRITICAL(&i.mux);
            i.data.tcmPrnd = prnd;
            portEXIT_CRITICAL(&i.mux);
        }
    }

    // Restore PCM header 7E0
    i.elm.sendQuery("ATSH 7E0", resp, sizeof(resp), 150);

    // Query Ambient Air Temp (Mode 01 PID 46 on PCM 7E0)
    uint8_t amb = 0;
    if (readUint8(i, "0146", "46", amb)) {
        int16_t temp = (int16_t)amb - 40;
        if (temp >= -40 && temp <= 60) {
            portENTER_CRITICAL(&i.mux);
            i.data.ambientC = (uint8_t)(temp > 0 ? temp : 0);
            portEXIT_CRITICAL(&i.mux);
        }
    }
}

// ---------------------------------------------------------------------------
// Wheel-mapping calibration - poll the four candidate Mode 22 DIDs
// (222A05..222A08) and watch for a value change on an unmapped candidate.
// The user sets the currently-active corner's tire to a distinct pressure, so
// when that corner's DID changes between reads we bind it to the active wheel.
// ---------------------------------------------------------------------------
void ObdService::pollCalibration(Impl& i, uint32_t now) {
    static const char* dids[4] = {"2A05", "2A06", "2A07", "2A08"};
    static uint32_t lastCalMs = 0;

    if (now - lastCalMs < 400) return;   // one DID per ~400ms, rotated
    lastCalMs = now;

    uint8_t active = i.activeWheel_;
    char resp[BleElm::MAX_RESPONSE];
    char cmd[16];
    uint16_t val = 0;

    // Round-robin over all candidate DIDs so we catch whichever changes while
    // the user messes with the active tire.
    static uint8_t scanIdx = 0;
    for (uint8_t k = 0; k < 4; k++) {
        uint8_t did = (scanIdx + k) % 4;
        if (i.bound_[did]) continue;   // skip DIDs already mapped this session

        snprintf(cmd, sizeof(cmd), "222A%s", dids[did]);
        if (!i.elm.sendQuery(cmd, resp, sizeof(resp), 250)) continue;

        // Parse "62 2A0X <2-byte value>" payload appended to the DID
        uint8_t bytes[2];
        char needle[5];
        needle[0] = '6'; needle[1] = '2'; needle[2] = '2'; needle[3] = 'A'; needle[4] = '\0';
        const char* p = strstr(resp, needle);
        if (!p) continue;
        p += 4;  // skip "622A"
        // now p points at DID bytes "05".. plus the 2 value bytes
        bool ok = hexVal(p[0], bytes[0]) && hexVal(p[1], bytes[1]);
        // We don't strictly need to decode the DID; we just need to know a
        // change happened on this candidate during the active wheel's window.
        (void)ok;
        // Extract the 2-byte value (skip DID's 2 hex chars = 1 byte follows the
        // "622A" marker only if the 3rd byte is the DID... for robust capture we
        // take bytes at position 4..5 (value low word) as the change signature.
        uint8_t vHi=0, vLo=0;
        if (p[2] && p[3]) { hexVal(p[2], vHi); hexVal(p[3], vLo); }
        val = (uint16_t)((vHi << 8) | vLo);

        // If this candidate's value changed since last read AND it isn't already
        // bound, treat it as the active wheel's DID.
        if (val != i.lastDidVal_[did] && i.lastDidVal_[did] != 0) {
            portENTER_CRITICAL(&i.mux);
            strncpy(i.capturedDid_, dids[did], sizeof(i.capturedDid_) - 1);
            i.capturedDid_[sizeof(i.capturedDid_) - 1] = '\0';
            i.bound_[did] = true;
            portEXIT_CRITICAL(&i.mux);
            Serial.printf("[obd] wheel %u bind candidate DID %s (val %u)\n",
                          active, dids[did], val);
        }
        i.lastDidVal_[did] = val;
        break;   // one query per tick
    }
    scanIdx = (scanIdx + 1) % 4;

    // Feed captured value into the TPMS slot for the active wheel so the UI can
    // show live pressure while mapping.
    if (val != 0) {
        portENTER_CRITICAL(&i.mux);
        i.data.tirePressure[active] = (float)val / 1000.0f;   // rough kPa->... placeholder
        i.data.tireKnown[active] = true;
        portEXIT_CRITICAL(&i.mux);
    }
}

// ---------------------------------------------------------------------------
// Setup wizard / TPMS calibration public API
// ---------------------------------------------------------------------------
void ObdService::freeze(bool frozen) {
    if (!impl_) return;
    portENTER_CRITICAL(&impl_->mux);
    impl_->frozen_ = frozen;
    if (frozen) impl_->calibrating_ = false;
    portEXIT_CRITICAL(&impl_->mux);
}

void ObdService::startCalibration(uint8_t activeWheel) {
    if (!impl_) return;
    portENTER_CRITICAL(&impl_->mux);
    impl_->activeWheel_ = activeWheel;
    impl_->calibrating_ = true;
    impl_->frozen_ = false;
    impl_->capturedDid_[0] = '\0';
    portEXIT_CRITICAL(&impl_->mux);
}

void ObdService::stopCalibration() {
    if (!impl_) return;
    portENTER_CRITICAL(&impl_->mux);
    impl_->calibrating_ = false;
    impl_->capturedDid_[0] = '\0';
    portEXIT_CRITICAL(&impl_->mux);
}

void ObdService::calibrationMarkBound(uint8_t wheel) {
    if (!impl_ || wheel >= 4) return;
    portENTER_CRITICAL(&impl_->mux);
    impl_->bound_[wheel] = true;
    impl_->capturedDid_[0] = '\0';
    portEXIT_CRITICAL(&impl_->mux);
}

bool ObdService::didCaptured(char* didBuf, size_t len) const {
    if (!impl_ || !didBuf || len == 0) return false;
    bool got = false;
    portENTER_CRITICAL(&impl_->mux);
    if (impl_->capturedDid_[0] != '\0') {
        strncpy(didBuf, impl_->capturedDid_, len - 1);
        didBuf[len - 1] = '\0';
        impl_->capturedDid_[0] = '\0';   // consume the event
        got = true;
    }
    portEXIT_CRITICAL(&impl_->mux);
    return got;
}

bool ObdService::calibrationActive() const {
    if (!impl_) return false;
    bool active = false;
    portENTER_CRITICAL(&impl_->mux);
    active = impl_->calibrating_;
    portEXIT_CRITICAL(&impl_->mux);
    return active;
}
