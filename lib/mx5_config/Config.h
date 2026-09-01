#pragma once

#include <Arduino.h>
#include <IPAddress.h>

/**
 * mx5_config - All setup knobs that need to change for a given car/dongle.
 */

// ---------------------------------------------------------------------------
// Wi-Fi ELM327 adapter
//
// The adapter creates its own access point (typically a hidden network).
// The ESP32 joins it as a station and opens a raw TCP socket to the adapter's
// ELM327 command server on the port below. Most Wi-Fi ELM327 dongles use
// 192.168.0.10:35000.
// ---------------------------------------------------------------------------
#define MX5_DONGLE_SSID  "V-LINK"                // adapter AP SSID
#define MX5_DONGLE_PASS  "12345678"              // adapter AP password
#define MX5_DONGLE_PORT  35000
#define MX5_DONGLE_IP    IPAddress(192, 168, 0, 10)

// ---------------------------------------------------------------------------
// TPMS (Mode 22 manufacturer PIDs, MS-CAN). The 2022 MX-5 ND2 SkyActiv-G
// exposes tire data from the BCM. Leave DONGLE_ADAPTER_NET_ENABLED="no need".
// Pressure DIDs found: 22 2A05..2A08, temps 22 2A0A..2A0D.
// Wheel-to-DID mapping is NOT guaranteed and must be calibrated: inflate each
// tire to a distinct pressure, drive briefly, then map readings in the
// TPMS_WHEEL_FROM_* tables below.
// ---------------------------------------------------------------------------
#define MX5_TPMS_ENABLED 0                       // 1 once DID map is confirmed
#define MX5_TPMS_INTERVAL_MS 5000

// DID table + which physical wheel each DID reports (calibrate!).
// 0=FL 1=FR 2=RL 3=RR, 255 = unmapped
#define MX5_WHEEL_UNMAPPED ((int8_t)255)
// pressure DIDs
#define MX5_TPMS_DID_PRESSURE_0 "2A05"
#define MX5_TPMS_DID_PRESSURE_1 "2A06"
#define MX5_TPMS_DID_PRESSURE_2 "2A07"
#define MX5_TPMS_DID_PRESSURE_3 "2A08"
static const int8_t MX5_TPMS_WHEEL_FROM_PREDID[4] = {MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED};
// temperature DIDs
#define MX5_TPMS_DID_TEMP_0 "2A0A"
#define MX5_TPMS_DID_TEMP_1 "2A0B"
#define MX5_TPMS_DID_TEMP_2 "2A0C"
#define MX5_TPMS_DID_TEMP_3 "2A0D"
static const int8_t MX5_TPMS_WHEEL_FROM_TEMPDID[4] = {MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED, MX5_WHEEL_UNMAPPED};

// ---------------------------------------------------------------------------
// Mode 01 PID polling cadence (ms). rpm is polled the most often since it is
// the primary driver of the RPM bar / arc.
// ---------------------------------------------------------------------------
#define MX5_POLL_RPM_MS      100
#define MX5_POLL_FAST_MS     300   // speed, coolant
#define MX5_POLL_SLOW_MS     1200  // load, throttle, fuel, intake, battery