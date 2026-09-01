#include "ObdService.h"

// ---------------------------------------------------------------------------
// FreeRTOS task setup
// ---------------------------------------------------------------------------

void ObdService::start() {
    elm_.begin(MX5_DONGLE_SSID, MX5_DONGLE_PASS, MX5_DONGLE_IP, MX5_DONGLE_PORT);
    xTaskCreatePinnedToCore(taskMain, "obd", 8192, this, 1, &taskHandle_, 0);
}

void ObdService::taskMain(void* arg) {
    static_cast<ObdService*>(arg)->loopTask();
}

// Estimates the active gear from the RPM / speed ratio. Calibrated against the
// ND2 6-speed manual gearbox + 3.454 final drive on 205/45R17 rubber:
//   1st ~157 rpm per km/h, 2nd ~92, 3rd ~63, 4th ~49, 5th ~40, 6th ~31.
static char estimateGear(uint16_t rpm, uint8_t speedKmh) {
    if (rpm == 0 && speedKmh == 0) return '-';   // no signal yet
    if (speedKmh == 0) return 'N';               // stationary, engine ticking
    float r = (float)rpm / (float)speedKmh;
    if (r < 35.0f) return '6';
    if (r < 45.0f) return '5';
    if (r < 56.0f) return '4';
    if (r < 77.0f) return '3';
    if (r < 125.0f) return '2';
    return '1';
}

void ObdService::loopTask() {
    for (;;) {
        elm_.loop();                     // keep WiFi + TCP + handshake healthy
        connected_ = elm_.isInitialized();
        {
            portENTER_CRITICAL(&mux_);
            data_.connected = connected_;
            if (connected_) data_.lastUpdateMs = millis();
            data_.gear = estimateGear(data_.rpm, data_.speedKmh);
            portEXIT_CRITICAL(&mux_);
        }

        if (connected_) {
            pollTick(millis());
            if (MX5_TPMS_ENABLED) pollTpms(millis());
        }

        vTaskDelay(5 / portTICK_PERIOD_MS);   // ~5 ms between ticks
    }
}

void ObdService::snapshot(VehicleData& out) {
    portENTER_CRITICAL(&mux_);
    out = data_;
    portEXIT_CRITICAL(&mux_);
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
// off (ATE0 ATS0 ATH0) - which WifiElm ensures during init.
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

bool ObdService::readUint8(const char* cmd, const char* pidHex, uint8_t& out) {
    char resp[WifiElm::MAX_RESPONSE];
    if (!elm_.sendQuery(cmd, resp, sizeof(resp), 400)) { pollErrors_++; return false; }
    if (!parsePidBytes(resp, pidHex, &out, 1)) { pollErrors_++; return false; }
    pollErrors_ = 0;
    return true;
}

bool ObdService::readUint16(const char* cmd, const char* pidHex, uint16_t& out) {
    char resp[WifiElm::MAX_RESPONSE];
    uint8_t bytes[2];
    if (!elm_.sendQuery(cmd, resp, sizeof(resp), 400)) { pollErrors_++; return false; }
    if (!parsePidBytes(resp, pidHex, bytes, 2)) { pollErrors_++; return false; }
    out = (uint16_t)((bytes[0] << 8) | bytes[1]);
    pollErrors_ = 0;
    return true;
}

// ---------------------------------------------------------------------------
// PID polling - one per cadence expiry, sequenced so only one ELM command is
// in flight at a time (the dongle is single-command, prompt-terminated).
// ---------------------------------------------------------------------------

void ObdService::pollTick(uint32_t now) {
    uint16_t tmp = 0;
    uint8_t b = 0;

    // rpm - fastest
    if (now - lastRpmMs_ >= MX5_POLL_RPM_MS) {
        lastRpmMs_ = now;
        if (readUint16("010C", "0C", tmp)) {
            tmp /= 4;
            portENTER_CRITICAL(&mux_);
            data_.rpm = tmp;
            portEXIT_CRITICAL(&mux_);
        }
    }

    // medium/slow group, rotated one PID per tick
    switch (slowIdx_) {
        case 0:
            if (now - lastSeqMs_ >= MX5_POLL_FAST_MS) {
                if (readUint8("010D", "0D", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.speedKmh = b;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 1:
            if (now - lastSeqMs_ >= MX5_POLL_FAST_MS) {
                if (readUint8("0105", "05", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.coolantC = b > 40 ? b - 40 : 0;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 2:
            if (now - lastSeqMs_ >= MX5_POLL_SLOW_MS) {
                if (readUint8("0104", "04", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.engineLoadPct = b;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 3:
            if (now - lastSeqMs_ >= MX5_POLL_SLOW_MS) {
                if (readUint8("0111", "11", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.throttlePct = b;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 4:
            if (now - lastSeqMs_ >= MX5_POLL_SLOW_MS) {
                if (readUint8("012F", "2F", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.fuelLevelPct = b;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 5:
            if (now - lastSeqMs_ >= MX5_POLL_SLOW_MS) {
                if (readUint8("010F", "0F", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.intakeAirC = b > 40 ? b - 40 : 0;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        case 6:
            if (now - lastSeqMs_ >= MX5_POLL_SLOW_MS) {
                if (readUint8("0142", "42", b)) {
                    portENTER_CRITICAL(&mux_);
                    data_.batteryVolts = b * 0.1f;
                    portEXIT_CRITICAL(&mux_);
                }
                lastSeqMs_ = now;
            }
            break;
        default:
            slowIdx_ = 0;
            lastSeqMs_ = 0;   // avoids a burst storm when the counter wraps
            break;
    }

    // advance the rotation
    if (unsigned(slowIdx_) < 7) slowIdx_++;

    // optional Mode 22 oil temp (DID 1310) - best effort, may be unsupported
    if (now - lastOilMs_ >= 3000) {
        lastOilMs_ = now;
        char resp[WifiElm::MAX_RESPONSE];
        if (elm_.sendQuery("221310", resp, sizeof(resp), 500)) {
            uint8_t ob[3];
            if (parsePidBytes(resp, "13", ob, 3)) {
                portENTER_CRITICAL(&mux_);
                data_.oilTempC = ob[2];
                portEXIT_CRITICAL(&mux_);
            }
        }
    }

    // surface consecutive poll failures to the diagnostics screen
    {
        portENTER_CRITICAL(&mux_);
        data_.canError = pollErrors_ > 3;
        portEXIT_CRITICAL(&mux_);
    }
}

// ---------------------------------------------------------------------------
// TPMS - Mode 22 manufacturer DIDs (MS-CAN)
// ---------------------------------------------------------------------------
// On the 2022 MX-5 ND2 the tire data lives in BCM DIDs 22 2A05..2A08
// (pressure) and 22 2A0A..2A0D (temperature), readable over MS-CAN with a
// "MS"-capable adapter. Response sample (ATH1 required to see the source):
//   "481622622A05xxxx"   <- 29-bit hdr + "622A05" + payload
//
// Units + wheel order are NOT standardized; calibration is mandatory:
//   1. Inflate each tire to a distinct pressure (e.g. 2.0/2.1/2.2/2.3 bar).
//   2. Drive a few hundred meters so the BCM refreshes its sensor data.
//   3. Read every 222A0x DID, note which reports each + the scale factor,
//      and encode the mapping in Config.h (MX5_TPMS_WHEEL_FROM_* tables).
// Then set MX5_TPMS_ENABLED 1 and fill in parsing below.
void ObdService::pollTpms(uint32_t now) {
    if (now - lastTpmsMs_ < MX5_TPMS_INTERVAL_MS) return;
    lastTpmsMs_ = now;

    // const char* pressureDids[4] = {"2A05", "2A06", "2A07", "2A08"};
    // const char* tempDids[4]    = {"2A0A", "2A0B", "2A0C", "2A0D"};

    // TODO(calibration): sendQuery("222A05", ...) with ATH1 temporarily on,
    // parsePidBytes-like match on "62" + DID, scale + map per Config tables,
    // and write into data_.tirePressure[]/tireTemp[]/tireKnown[].
}