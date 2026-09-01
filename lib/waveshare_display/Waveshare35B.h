#pragma once

#include <Arduino.h>
#include <lvgl.h>

/**
 * Waveshare ESP32-S3-Touch-LCD-3.5B board support.
 *
 * Wraps the Arduino_GFX low-level display driver and the AXS5106L capacitive
 * touch controller, exposing an LVGL display + input device pair so the rest
 * of the app only ever talks to LVGL.
 *
 * Pin mapping (from the Waveshare schematic/wiki):
 *   Display (QSPI): CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4
 *   Touch + I2C  : SDA=8, SCL=7
 *   Backlight    : 6   (enabled via the TCA9554 port expander)
 *   TCA9554      : I2C addr 0x20, pin 1 gates display peripheral power
 *   Touch IC     : AXS5106L at I2C addr 0x3B
 */

class Waveshare35B {
public:
    static constexpr uint16_t WIDTH = 320;
    static constexpr uint16_t HEIGHT = 480;

    bool begin();
    void loop(); // advances LVGL

private:
    void initPower();
    void initTouch();
    lv_display_t* createDisplay();
    lv_indev_t* createInput();

    uint32_t millis_cb();
    static void my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map);
    static void my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data);
};
