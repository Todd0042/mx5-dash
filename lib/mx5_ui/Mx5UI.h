#pragma once

#include <lvgl.h>

#include "../mx5_config/Config.h"
#include "../mx5_config/ObdSource.h"
#include "../mx5_config/SdLogger.h"

/**
 * Mx5UI
 *
 * Simplified ESP32 UI Stack for 3.5" 480x320 display:
 *   0. Speed (Default) - High-visibility mono speed + gear + RPM/Fuel/Temp arcs + warnings
 *   1. TPMS            - 4-tire pressure + temperature cards
 *   2. Setup Wizard    - OBD BLE pairing & initial setup sequence
 */

class Mx5UI {
public:
    static constexpr uint16_t WIDTH = 480;
    static constexpr uint16_t HEIGHT = 320;

    static constexpr uint8_t SCREEN_SPEED  = 0;
    static constexpr uint8_t SCREEN_TPMS   = 1;
    static constexpr uint8_t SCREEN_WIZARD = 2;

    static constexpr uint8_t SCREEN_COUNT         = 3;
    static constexpr uint8_t CONTENT_SCREEN_COUNT = 2; // 0 (Speed), 1 (TPMS)

    enum ThemeMode { THEME_AUTO = 0, THEME_DAY = 1, THEME_NIGHT = 2 };

    explicit Mx5UI(ObdSource& obd) : obd_(obd) {}

    void begin();   // build all screens and load the default one
    void update();  // refresh live values across active screens each frame

    void nextScreen();
    void prevScreen();
    void toggleMenu();
    void setScreen(uint8_t index);
    uint8_t getCurrentScreen() const { return currentScreen_; }
    void setNightMode(bool isNight);
    bool isNightMode() const { return currentNightMode_; }
    void setThemeMode(ThemeMode mode);
    ThemeMode getThemeMode() const { return themeMode_; }
    void setRotation(uint8_t rot);
    void setBrightness(uint8_t pct);
    void runSetupWizard() { setScreen(SCREEN_WIZARD); }
    void runTpmsRecalibration() { setScreen(SCREEN_TPMS); }

private:
    // ---- screen builders ----
    lv_obj_t* buildSpeedScreen();
    lv_obj_t* buildTpmsScreen();
    lv_obj_t* buildWizardScreen();
    void setScreenWithTransition(uint8_t targetIndex, bool forward);

    // ---- shared decoration ----
    lv_obj_t* addSpeedChip(lv_obj_t* parent);
    lv_obj_t* addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex);

    // segmented (dotted) arc meter - Mazda digital readout style
    struct SegArc {
        lv_obj_t* wrap = nullptr;
        lv_obj_t* dots[24] = {};
        uint8_t count = 0;
        uint16_t pct10[24] = {};     // per-dot fuel threshold (percent*10); unused for RPM arc
        lv_obj_t* val = nullptr;
        lv_obj_t* sub = nullptr;     // optional second readout line (fuel "M x gal")
    };
    SegArc buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX, int16_t posY,
                          const char* cap, uint8_t segCount);
    SegArc buildFuelGauge(lv_obj_t* parent, uint16_t width, uint16_t height,
                          int16_t posX, int16_t posY);
    void updateDottedValue(SegArc& seg, float frac);   // 0..1 -> lights segments
    void updateFuelGauge(SegArc& seg, uint8_t pct);    // fuel % -> lights U-trough dots
    void warnTopDots(SegArc& seg);                     // redlines top segments
    lv_obj_t* addGearFrame(lv_obj_t* parent, int16_t x, int16_t y);

    // ---- live update helpers ----
    void updateSpeedChip();
    void updateSpeedScreen();
    void updateTpmsScreen();
    void updateWizardScreen();

    static void onGesture(lv_event_t* e);
    static void onMenuIconClick(lv_event_t* e);
    static void onSpeedWarnBannerClick(lv_event_t* e);
    static void onSettingsActionClick(lv_event_t* e);

    VehicleData data_local_;      // snapshot of shared traffic pulled each update()
    ObdSource& obd_;

    // Screen Transition Interstitial
    lv_obj_t* transitionScr_ = nullptr;
    lv_obj_t* transBadge_ = nullptr;
    lv_obj_t* transTitleLbl_ = nullptr;
    lv_obj_t* transCarImg_ = nullptr;
    lv_obj_t* transDrlDot_ = nullptr;
    lv_obj_t* transTailDot_ = nullptr;
    lv_obj_t* transSpeedLbl_ = nullptr;
    uint8_t pendingTargetScreen_ = 255;
    bool transitionForward_ = true;

    // shared widgets
    lv_obj_t* speedChipLabel_[SCREEN_COUNT] = {};

    // screen 0: speed widgets
    lv_obj_t* speedBigLabel_ = nullptr;
    lv_obj_t* speedUnitLabel_ = nullptr;
    lv_obj_t* gearLbl_ = nullptr;
    SegArc rpmSeg_, fuelSeg_;

    // Screen 0 Dynamic Alert Overlay
    bool warningMutedForDrive_ = false;
    lv_obj_t* speedNormalRightContainer_ = nullptr;
    lv_obj_t* speedWarningContainer_ = nullptr;
    lv_obj_t* speedWarnBanner_ = nullptr;
    lv_obj_t* speedWarnTitle_ = nullptr;
    lv_obj_t* speedWarnCard_ = nullptr;
    lv_obj_t* speedWarnMainVal_ = nullptr;
    lv_obj_t* speedWarnSubVal_ = nullptr;

    // screen 1: tpms widgets
    lv_obj_t* tpmsLabel_[4] = {};
    lv_obj_t* tpmsTemp_[4] = {};
    lv_obj_t* tpmsCard_[4] = {};
    lv_obj_t* tpmsDot_[4] = {};

    // global top connection status pill
    lv_obj_t* connStatusPill_ = nullptr;
    lv_obj_t* connStatusLbl_ = nullptr;
    uint32_t connConnectedSinceMs_ = 0;
    bool wasConnected_ = false;

    // Dynamic Auto-Dimming & Night Mode
    bool currentNightMode_ = false;
    uint8_t currentBacklightDuty_ = 242;
    uint8_t targetBacklightDuty_ = 242;
    lv_color_t themeText_   = lv_color_hex(0xFFFFFF);
    lv_color_t themeSpeed_  = lv_color_hex(0xFFFFFF);
    lv_color_t themeDim_    = lv_color_hex(0x8E949F);
    lv_color_t themeDotLit_ = lv_color_hex(0xFFFFFF);

    void applyThemeMode(bool isNight);
    void setBacklightDuty(uint8_t duty);

    // Settings state
    ThemeMode themeMode_ = THEME_AUTO;
    uint8_t currentRotation_ = MX5_LCD_ROTATION;
    bool unitsUs_ = MX5_UNITS_US;
    bool transAuto_ = true;
    uint8_t userBrightness_ = 95;

    // Initial Setup Wizard State
    uint8_t wizardStep_ = 0;
    uint32_t wizAutoAdvanceMs_ = 0;
    bool wizAutoAdvancing_ = false;
    lv_obj_t* wizConnStatusLbl_ = nullptr;
    lv_obj_t* wizConnSubLbl_ = nullptr;
    lv_obj_t* wizStepCard_[4] = {};
    lv_obj_t* wizStepBadge_[4] = {};

    lv_obj_t* screens_[SCREEN_COUNT] = {};
    uint8_t currentScreen_ = 0;
    uint8_t lastContentScreen_ = 0;
    uint32_t lastReflash_ = 0;
};

