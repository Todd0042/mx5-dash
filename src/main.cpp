#include <Arduino.h>

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

#include <Waveshare35B.h>
#include <ObdService.h>
#include <Mx5UI.h>

// ---------------------------------------------------------------------------
// mx5-dash
//
// 2022 Mazda MX-5 RF GT on a Waveshare ESP32-S3-Touch-LCD-3.5B.
//
// Sizing/threading:
//   core 1 : display + LVGL (this loop) - rendering/UI
//   core 0 : ObdService FreeRTOS task     - Wi-Fi ELM327 polling
//
// The UI task calls snapshot() (critical-section protected) every frame to
// pull vehicle metrics across cores without blocking the OBD task.
// ---------------------------------------------------------------------------

static Waveshare35B display;
static ObdService obd;
static Mx5UI ui(obd);

void setup() {
    Serial.begin(115200);
    delay(200);

    Serial.println("[main] booting...");

    // Display + touch + LVGL live entirely on core 1 (this core).
    if (!display.begin()) {
        Serial.println("[main] FATAL: display init failed");
    }

    // UI screens are built once up front.
    ui.begin();

    // OBD service: spins up its own task on core 0.
    obd.start();

    Serial.println("[main] ready");
}

void loop() {
    // lv_timer_handler() every 5ms -> smooth ~60fps rendering.
    display.loop();

    // Pull the latest vehicle metrics (non-blocking, cross-core safe).
    ui.update();
}