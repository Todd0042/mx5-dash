#pragma once

#ifdef ARDUINO
#include <Arduino.h>
#include <IPAddress.h>
#endif

/**
 * mx5_config - All setup knobs that need to change for a given car/dongle.
 */

// ---------------------------------------------------------------------------
// Hardware & Display Settings (ESP32-S3 + Waveshare 3.5" 480x320)
// ---------------------------------------------------------------------------
#define MX5_LCD_ROTATION 1                       // 1 = 90 deg CW (USB Left), 3 = 270 deg CW (USB Right)
#define MX5_DISPLAY_SELFTEST 0                   // 1 = blink R/G/B during display.begin() (bring-up diag only)
#define MX5_UNITS_US     1                       // 1 = US (MPH / °F / PSI), 0 = Metric (km/h / °C / bar)
#define MX5_HOR_RES      480
#define MX5_VER_RES      320
#define MX5_BACKLIGHT_DAY_PCT   95               // Default day brightness %
#define MX5_BACKLIGHT_NIGHT_PCT 25               // Default night brightness %
#define MX5_AUTO_DIM_ENABLED    1
#define MX5_DIM_CHECK_INTERVAL_MS 3000
#define MX5_NIGHT_VOLTS_THRESHOLD 12.8f
#define MX5_PIN_BACKLIGHT       6
#define MX5_LEDC_BACKLIGHT_CH   0

// ---------------------------------------------------------------------------
// BLE ELM327 adapter (Vgate vLinker MS / MC+ / BLE OBD2)
//
// The ESP32-S3 scans for the BLE advertisement matching the device name prefix
// (e.g. "vLinker", "V-LINK", "OBDII") and auto-connects to its GATT serial
// service. Once connected, it communicates via ELM327 ASCII protocol.
// ---------------------------------------------------------------------------
#define MX5_BLE_DEVICE_PREFIX "vLinker"          // Matches "vLinker MS ...", "vLinker MC ...", etc.

// ---------------------------------------------------------------------------
// TPMS (Mode 22 manufacturer PIDs, MS-CAN). The 2022 MX-5 ND2 SkyActiv-G
// exposes tire data from the BCM. Leave DONGLE_ADAPTER_NET_ENABLED="no need".
// Pressure DIDs found: 22 2A05..2A08, temps 22 2A0A..2A0D.
// Wheel-to-DID mapping is NOT guaranteed and must be calibrated: inflate each
// tire to a distinct pressure, drive briefly, then map readings in the
// TPMS_WHEEL_FROM_* tables below.
// ---------------------------------------------------------------------------
#define MX5_TPMS_ENABLED 1                       // 1 once DID map is confirmed
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