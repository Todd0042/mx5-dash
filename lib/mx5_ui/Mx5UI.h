#pragma once

#include <Arduino.h>
#include <lvgl.h>

#include "../obd_wifi/ObdService.h"

/**
 * Mx5UI
 *
 * Owns the LVGL screen stack for the MX-5 display. Five swipeable screens:
 *
 *   0. Speed      - big centered speed + edge gauges (default screen)
 *   1. RPM        - RPM gauge, engine load, throttle
 *   2. Engine     - coolant, intake air, battery, oil temp
 *   3. TPMS       - 4-tire pressure + temperature
 *   4. Diagnostics- connection status, adapter info, DTC controls
 *
 * The current vehicle speed is always drawn in a compact chip at the top-left
 * on every screen. On the Speed screen the same value is additionally shown
 * large in the center.
 *
 * Swiping left/right on the capacitive panel changes screens
 * (LVGL LV_EVENT_GESTURE).
 */

class Mx5UI {
public:
    static constexpr uint16_t WIDTH = 320;
    static constexpr uint16_t HEIGHT = 480;

    explicit Mx5UI(ObdService& obd) : obd_(obd) {}

    void begin();   // build all screens and load the default one
    void update();  // refresh live values each frame

private:
    // ---- screen builders ----
    lv_obj_t* buildSpeedScreen();
    lv_obj_t* buildRpmScreen();
    lv_obj_t* buildEngineScreen();
    lv_obj_t* buildTpmsScreen();
    lv_obj_t* buildDiagnosticsScreen();

    // ---- shared decoration ----
    lv_obj_t* addSpeedChip(lv_obj_t* parent);   // always-on top-left chip
    void addEdgeCard(lv_obj_t* parent, lv_align_t align,
                     int8_t x, int8_t y, uint16_t w, uint16_t h,
                     lv_color_t accent);
    lv_obj_t* addMiniGauge(lv_obj_t* parent, const char* cap, uint8_t idx);
    lv_obj_t* addMetricRow(lv_obj_t* parent, const char* cap, const char* unit, int16_t y);

    // segmented (dotted) arc meter - Mazda digital readout style
    struct SegArc {
        lv_obj_t* dots[24] = {};
        uint8_t count = 0;
        lv_obj_t* val = nullptr;
    };
    SegArc buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX, int16_t posY,
                          const char* cap, uint8_t segCount);
    void updateDottedValue(SegArc& seg, float frac);   // 0..1 -> lights segments
    void warnTopDots(SegArc& seg);                     // redlines the top segments
    lv_obj_t* addGearFrame(lv_obj_t* parent);          // square white-bordered gear cell

    // ---- live update helpers ----
    void updateSpeedChip();
    void updateSpeedScreen();
    void updateRpmScreen();
    void updateEngineScreen();
    void updateTpmsScreen();
    void updateDiagnosticsScreen();

    static void onGesture(lv_event_t* e);

    VehicleData data_local_;      // snapshot of shared traffic pulled each update()
    ObdService& obd_;

    // shared widgets
    lv_obj_t* speedChipLabel_ = nullptr;

    // speed screen widgets
    lv_obj_t* speedBigLabel_ = nullptr;
    lv_obj_t* speedUnitLabel_ = nullptr;
    lv_obj_t* gearLbl_ = nullptr;             // P/R/N/D/M active gear char
    SegArc rpmSeg_, fuelSeg_, coolantSeg_;

    // rpm screen widgets
    SegArc rpmBigSeg_;
    lv_obj_t* rpmArcVal_ = nullptr;
    lv_obj_t* loadArcVal_ = nullptr;
    lv_obj_t* throttleArcVal_ = nullptr;

    // engine screen widgets
    lv_obj_t* engCoolantVal_ = nullptr;
    lv_obj_t* engIntakeVal_ = nullptr;
    lv_obj_t* engBatteryVal_ = nullptr;
    lv_obj_t* engOilVal_ = nullptr;

    // tpms screen widgets
    lv_obj_t* tpmsLabel_[4] = {};
    lv_obj_t* tpmsTemp_[4] = {};
    lv_obj_t* tpmsRed_[4] = {};

    // diagnostics widgets
    lv_obj_t* diagStatus_ = nullptr;
    lv_obj_t* diagBattery_ = nullptr;
    lv_obj_t* diagError_ = nullptr;
    lv_obj_t* diagHint_ = nullptr;

    lv_obj_t* screens_[5] = {};
    uint8_t currentScreen_ = 0;
    uint32_t lastReflash_ = 0;
};