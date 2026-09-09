#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Waveshare35B.h>
#include <ObdService.h>
#include <Mx5UI.h>
#include "../lib/mx5_config/UserPrefs.h"

// ---------------------------------------------------------------------------
// mx5-dash
//
// 2022 Mazda MX-5 RF GT on a Waveshare ESP32-S3-Touch-LCD-3.5B.
//
// Sizing/threading:
//   core 1 : display + LVGL (this loop) - rendering/UI
//   core 0 : ObdService FreeRTOS task     - BLE ELM327 polling
//
// The UI task calls snapshot() (critical-section protected) every frame to
// pull vehicle metrics across cores without blocking the OBD task.
// ---------------------------------------------------------------------------

static Waveshare35B display;
static ObdService obd;
static Mx5UI ui(obd);

void setup() {
    // TEMP DIAGNOSTIC: how much internal RAM exists before anything runs?
    Serial.begin(115200);
    Serial.printf("[main] heap at setup start: int=%u dma=%u\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_DMA));
    delay(200);

    Serial.println("[main] booting...");

    // Load all user preferences from NVS flash (falls back to Config.h defaults)
    UserPrefs::loadAll();

    // OBD service: spins up its own task on core 0.
    // Started BEFORE display/LVGL init: on ESP32-S3 the UI buffers consume
    // ~250KB of internal RAM, shrinking the heap so far that NimBLE init plus
    // the 8KB OBD task stack can no longer coexist (task creation returned
    // pdFAIL / NimBLEDevice::init() would hang). Starting early keeps ~310KB
    // free so BLE + task both fit comfortably.
    char blePrefix[32];
    UserPrefs::getBlePrefix(blePrefix, sizeof(blePrefix));
    obd.setBlePrefix(blePrefix);
    obd.setBleScanTimeout(UserPrefs::getBleScanTimeout());
    obd.start();

    // Display + touch + LVGL live entirely on core 1 (this core).
    if (!display.begin()) {
        Serial.println("[main] FATAL: display init failed");
    }

    // Apply saved rotation unconditionally (must happen after display.begin()).
    // Mirroring the Settings screen's setRotation() call exactly so the panel
    // always lands on the user's chosen orientation, even when it equals the
    // compiled default.
    uint8_t savedRot = UserPrefs::getRotation();
    display.setRotation(savedRot);
    Serial.printf("[main] applied rotation %d\n", savedRot);

    // UI screens are built once up front.
    ui.begin();

    // Apply saved theme & brightness after UI is built
    uint8_t themeMode = UserPrefs::getThemeMode();
    ui.setThemeMode((Mx5UI::ThemeMode)themeMode);

    uint8_t savedBri = UserPrefs::getBrightness();
    ui.setBrightness(savedBri);

    // First-time boot: run the 4-step setup wizard before the driving dashboard.
    if (!UserPrefs::isConfigured()) {
        obd.freeze(true);   // hold PID polling while in initial setup wizard
        ui.runSetupWizard();
    } else {
        obd.freeze(false);  // live PID polling immediately on normal boot
    }

    Serial.println("[main] ready");
}

void loop() {
    // lv_timer_handler() every 5ms -> smooth ~60fps rendering.
    display.loop();

    // Pull the latest vehicle metrics (non-blocking, cross-core safe).
    ui.update();
}