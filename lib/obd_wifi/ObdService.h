#pragma once

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include "WifiElm.h"
#include "../mx5_config/Config.h"

/**
 * ObdService
 *
 * Vehicle telemetry service. Owns the Wi-Fi ELM327 transport and polls the
 * Mode 01 PIDs round-robin at staggered cadences, decoding each raw response
 * into the shared VehicleData struct.
 *
 * This is NOT ELMduino: ELMduino is replaced by a ~150 line direct ELM327
 * implementation that speaks the raw ASCII protocol over the TCP socket.
 *
 * The service runs in its own FreeRTOS task pinned to core 0, so blocking
 * reads (short timeouts) never stall the LVGL UI task on core 1. VehicleData
 * is read and written under a critical section.
 *
 *   PID        Query   Value           Cadence
 *   rpm        010C    16-bit /4       100 ms
 *   speed      010D    1 byte          300 ms
 *   coolant    0105    byte - 40       300 ms
 *   load       0104    byte %          1200 ms
 *   throttle   0111    byte %          1200 ms
 *   fuel       012F    byte %          1200 ms
 *   intake air 010F    byte - 40       1200 ms
 *   battery    0142    byte * 0.1 V    1200 ms
 *   oil temp   221310  (best effort)   3000 ms
 *   TPMS       222Axx  manufacturer DIDs, MS-CAN, see Config.h
 */

struct VehicleData {
    // Standard OBD (Mode 01) constants - polled continuously
    uint16_t rpm = 0;
    uint8_t speedKmh = 0;          // 01 0D
    uint8_t coolantC = 0;          // 01 05 (minus 40)
    uint8_t intakeAirC = 0;        // 01 0F (minus 40)
    uint8_t throttlePct = 0;       // 01 11
    uint8_t engineLoadPct = 0;     // 01 04
    float batteryVolts = 0.0f;     // 01 42

    // SkyActiv "engine oil temperature" - Mode 22 DID 1310; may be unsupported
    uint8_t oilTempC = 0;
    uint8_t fuelLevelPct = 0;      // 01 2F

    // Active gear position indicator. OBD-II does not expose the selector,
    // so this is ESTIMATED from the speed / rpm ratio against ND2 6MT ratios
    // (see ObdService.cpp). Char class follows the cluster convention: '-'
    // (no signal), 'P','N','D','M' or '1'-'6' depending on the source.
    char gear = '-';

    // TPMS (Mode 22 manufacturer) - may be empty until calibrated
    float tirePressure[4] = {0, 0, 0, 0}; // bar
    float tireTemp[4] = {0, 0, 0, 0};     // deg C
    bool tireKnown[4] = {false, false, false, false};

    bool connected = false;        // TCP + ELM handshake up
    bool canError = false;         // last read returned a NO DATA / error
    uint32_t lastUpdateMs = 0;
};

class ObdService {
public:
    // start() begins WiFi association and spawns the OBD task. Call once in setup().
    void start();

    // Blocking copy of the latest telemetry (critical-section protected).
    void snapshot(VehicleData& out);

    bool connected() const { return connected_; }

private:
    static void taskMain(void* arg);
    void loopTask();               // runs on core 0

    bool initWifi();
    void pollTick(uint32_t now);   // rotates through PID cadences

    // raw ELM327 query -> filled VehicleData (already trimmed, "410D48" etc.)
    bool readUint8(const char* cmd, const char* pidHex, uint8_t& out);
    bool readUint16(const char* cmd, const char* pidHex, uint16_t& out);

    void pollTpms(uint32_t now);   // Mode 22 (see Config.h; off until calibrated)

    WifiElm elm_;
    VehicleData data_;
    volatile bool connected_ = false;
    TaskHandle_t taskHandle_ = nullptr;
    portMUX_TYPE mux_ = portMUX_INITIALIZER_UNLOCKED;

    uint32_t lastRpmMs_ = 0;
    uint32_t lastSeqMs_ = 0;      // gate for the medium/slow rotation
    uint8_t  slowIdx_ = 0;        // current PID in the rotation
    uint32_t lastTpmsMs_ = 0;
    uint32_t lastOilMs_ = 0;
    uint32_t pollErrors_ = 0;     // consecutive failed reads
};