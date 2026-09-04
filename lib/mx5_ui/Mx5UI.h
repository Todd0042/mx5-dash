#pragma once

#include <lvgl.h>

#include "../mx5_config/Config.h"
#include "../mx5_config/ObdSource.h"
#include "../mx5_config/SdLogger.h"

/**
 * Mx5UI
 *
 * Owns the LVGL screen stack for the MX-5 display:
 *
 *   0. Speed (Default) - 120px speed + edge gauges + AT gear indicator
 *   1. TPMS            - 4-tire pressure + temperature cards
 *   2. RPM / Engine    - 5 circular dial cluster (Tachometer + Load, Throttle, Fuel, Battery)
 *   3. Temperatures    - 4 circular dial cluster (Coolant, Oil Temp, Intake Air, Battery)
 *   4. Track & Dyn     - 0-60 MPH acceleration timer, Throttle vs Brake bars, HP & Torque
 *   5. Trip & Economy  - Instant MPG arc, Trip Avg MPG, Range, Distance
 *   6. Diagnostics/DTC - System health, active DTC fault codes reader & clear action
 *   7. Menu Hub        - CMU-style 7 circular touch pods (quick jump to any screen)
 *
 * All screens stay 100% active and updated in the background for zero-lag transitions.
 */

class Mx5UI {
public:
    static constexpr uint16_t WIDTH = 480;
    static constexpr uint16_t HEIGHT = 320;

    static constexpr uint8_t SCREEN_SPEED = 0;
    static constexpr uint8_t SCREEN_TPMS  = 1;
    static constexpr uint8_t SCREEN_TEMPS = 2;
    static constexpr uint8_t SCREEN_DIAG  = 3;
    static constexpr uint8_t SCREEN_TRACK = 4;
    static constexpr uint8_t SCREEN_RPM   = 5;
    static constexpr uint8_t SCREEN_TRIP  = 6;
    static constexpr uint8_t SCREEN_MENU  = 7;

    // Advanced Diagnostic Sub-Dashboards
    static constexpr uint8_t SCREEN_DIAG_SUB_FUEL    = 8;
    static constexpr uint8_t SCREEN_DIAG_SUB_CYL     = 9;
    static constexpr uint8_t SCREEN_DIAG_SUB_CHASSIS = 10;
    static constexpr uint8_t SCREEN_DIAG_SUB_SMOG    = 11;
    static constexpr uint8_t SCREEN_DIAG_SUB_LOGS    = 12;

    // Settings Screen (Accessed from Menu Hub)
    static constexpr uint8_t SCREEN_SETTINGS         = 13;

    // BLE Connection Configuration
    static constexpr uint8_t SCREEN_BLE_CONFIG       = 14;

    // Setup Wizard & Wheel Map
    static constexpr uint8_t SCREEN_WIZARD           = 15;
    static constexpr uint8_t SCREEN_WHEEL_MAP        = 16;

    static constexpr uint8_t SCREEN_COUNT            = 17;
    static constexpr uint8_t CONTENT_SCREEN_COUNT    = 7; // 0..6 for horizontal carousel

    enum ThemeMode { THEME_AUTO = 0, THEME_DAY = 1, THEME_NIGHT = 2 };

    explicit Mx5UI(ObdSource& obd) : obd_(obd) {}

    void begin();   // build all screens and load the default one
    void update();  // refresh live values across all screens each frame

    void nextScreen();
    void prevScreen();
    void toggleMenu();
    void setScreen(uint8_t index);
    void showDtcRepairGuide(const char* code);
    void hideDtcRepairGuide();
    void showSdFormatModal();
    void hideSdFormatModal();
    void setNightMode(bool isNight);
    bool isNightMode() const { return currentNightMode_; }
    void setThemeMode(ThemeMode mode);
    ThemeMode getThemeMode() const { return themeMode_; }
    void setRotation(uint8_t rot);
    void setBrightness(uint8_t pct);
    void runSetupWizard() { setScreen(SCREEN_WIZARD); }
    void runTpmsRecalibration() { setScreen(SCREEN_WHEEL_MAP); }
    void openDtcGuide(uint8_t index = 0);

private:
    // ---- screen builders ----
    lv_obj_t* buildSpeedScreen();
    lv_obj_t* buildTpmsScreen();
    lv_obj_t* buildRpmScreen();
    lv_obj_t* buildEngineScreen();
    lv_obj_t* buildTrackScreen();
    lv_obj_t* buildTripScreen();
    lv_obj_t* buildDiagnosticsScreen();
    lv_obj_t* buildMenuScreen();
    lv_obj_t* buildSettingsScreen();
    lv_obj_t* buildTransitionScreen();
    void setScreenWithTransition(uint8_t targetIndex, bool forward);

    // ---- diagnostic sub-screen builders ----
    lv_obj_t* buildDiagSubFuel();
    lv_obj_t* buildDiagSubCyl();
    lv_obj_t* buildDiagSubChassis();
    lv_obj_t* buildDiagSubSmog();
    lv_obj_t* buildDiagSubLogs();
    lv_obj_t* buildBleConfigScreen();
    lv_obj_t* buildWizardScreen();
    lv_obj_t* buildWheelMapScreen();
    void buildDtcRepairModal(lv_obj_t* parent);
    void buildSdFormatModal(lv_obj_t* parent);
    void renderIncidentChart(uint8_t incidentIndex);

    // ---- shared decoration ----
    lv_obj_t* addSpeedChip(lv_obj_t* parent);   // always-on top-left chip
    lv_obj_t* addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex);

    // segmented (dotted) arc meter - Mazda digital readout style
    struct SegArc {
        lv_obj_t* wrap = nullptr;
        lv_obj_t* dots[24] = {};
        uint8_t count = 0;
        lv_obj_t* val = nullptr;
    };
    SegArc buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX, int16_t posY,
                          const char* cap, uint8_t segCount);
    void updateDottedValue(SegArc& seg, float frac);   // 0..1 -> lights segments
    void warnTopDots(SegArc& seg);                     // redlines the top segments
    lv_obj_t* addGearFrame(lv_obj_t* parent, int16_t x, int16_t y);  // gear cell

    // ---- live update helpers ----
    void updateSpeedChip();
    void updateSpeedScreen();
    void updateTpmsScreen();
    void updateRpmScreen();
    void updateEngineScreen();
    void updateTrackScreen();
    void updateTripScreen();
    void updateDiagnosticsScreen();
    void updateMenuScreen();
    void updateDiagSubFuel();
    void updateDiagSubCyl();
    void updateDiagSubChassis();
    void updateDiagSubSmog();
    void updateDiagSubLogs();
    void updateBleConfigScreen();

    static void onGesture(lv_event_t* e);
    static void onMenuIconClick(lv_event_t* e);
    static void onDtcActionClick(lv_event_t* e);
    static void onDtcCycleClick(lv_event_t* e);
    static void onSubScreenNavClick(lv_event_t* e);
    static void onDtcModalCloseClick(lv_event_t* e);
    static void onSdFormatClick(lv_event_t* e);
    static void onIncidentSelectClick(lv_event_t* e);
    static void onSpeedWarnBannerClick(lv_event_t* e);
    static void onWheelMapActionClick(lv_event_t* e);
    static void onBleDeviceSelectClick(lv_event_t* e);
    static void onBleActionClick(lv_event_t* e);
    static void onTransitionTimer(lv_timer_t* t);

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

    // shared widgets - one speed chip per screen (screen 0 has none)
    lv_obj_t* speedChipLabel_[SCREEN_COUNT] = {};

    // screen 0: speed widgets
    lv_obj_t* speedBigLabel_ = nullptr;
    lv_obj_t* speedUnitLabel_ = nullptr;
    lv_obj_t* gearLbl_ = nullptr;
    SegArc rpmSeg_, fuelSeg_, ambientSeg_;

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

    // screen 2: rpm/engine widgets (5 dials)
    SegArc rpmBigSeg_;
    SegArc loadSeg_, throttleSeg_, fuelArcSeg_, batArcSeg_;

    // screen 3: temps widgets (4 dials)
    SegArc coolantSeg_, intakeSeg_, batterySeg_, oilSeg_;

    // screen 4: track & dynamics widgets
    lv_obj_t* trackTimerLbl_ = nullptr;
    lv_obj_t* trackBestLbl_ = nullptr;
    lv_obj_t* trackStateBadge_ = nullptr;
    lv_obj_t* trackThrottleBar_ = nullptr;
    lv_obj_t* trackBrakeBar_ = nullptr;
    lv_obj_t* trackThrottleVal_ = nullptr;
    lv_obj_t* trackBrakeVal_ = nullptr;
    SegArc hpSeg_, torqueSeg_;

    // screen 5: trip & economy widgets
    SegArc instantMpgSeg_;
    lv_obj_t* tripAvgVal_ = nullptr;
    lv_obj_t* tripDistVal_ = nullptr;
    lv_obj_t* tripRangeVal_ = nullptr;
    lv_obj_t* tripFuelVal_ = nullptr;

    // screen 6: diagnostics & DTC widgets
    lv_obj_t* diagStatus_ = nullptr;
    lv_obj_t* diagBattery_ = nullptr;
    lv_obj_t* diagError_ = nullptr;
    lv_obj_t* diagDtcBox_ = nullptr;
    lv_obj_t* diagDtcLbl_ = nullptr;
    lv_obj_t* diagDtcIndexLbl_ = nullptr;
    lv_obj_t* diagDtcPrevBtn_ = nullptr;
    lv_obj_t* diagDtcNextBtn_ = nullptr;
    lv_obj_t* diagHint_ = nullptr;
    uint8_t currentDtcIndex_ = 0;

    // screen 7: menu screen widgets
    lv_obj_t* menuStatusLbl_ = nullptr;
    lv_obj_t* menuPods_[CONTENT_SCREEN_COUNT] = {};

    // sub-screen 8: fuel trims & HPFP
    SegArc stftSeg_, ltftSeg_;
    lv_obj_t* diagAfrVal_ = nullptr;
    lv_obj_t* diagHpfpVal_ = nullptr;
    lv_obj_t* diagEvapVal_ = nullptr;

    // sub-screen 9: cylinder misfire & ignition
    lv_obj_t* misfireCountLbl_[4] = {};
    lv_obj_t* misfireBar_[4] = {};
    lv_obj_t* diagSparkVal_ = nullptr;
    lv_obj_t* diagKnockVal_ = nullptr;
    lv_obj_t* diagVvtInVal_ = nullptr;
    lv_obj_t* diagVvtExVal_ = nullptr;

    // sub-screen 10: chassis, sensors & transmission
    lv_obj_t* wheelSpeedLbl_[4] = {};
    lv_obj_t* diagSasVal_ = nullptr;
    lv_obj_t* diagTransTempVal_ = nullptr;
    lv_obj_t* diagTccSlipVal_ = nullptr;

    // sub-screen 11: I/M smog readiness monitors
    lv_obj_t* smogSummaryLbl_ = nullptr;
    lv_obj_t* smogPodDot_[8] = {};
    lv_obj_t* smogPodLbl_[8] = {};

    // sub-screen 12: incident logs & history charts
    lv_obj_t* sdCardStatusLbl_ = nullptr;
    lv_obj_t* sdFormatBtn_ = nullptr;
    lv_obj_t* incidentItems_[6] = {};
    lv_obj_t* incidentTitleLbl_ = nullptr;
    lv_obj_t* incidentReasonLbl_ = nullptr;
    uint8_t selectedIncident_ = 0;
    lv_obj_t* logChart_ = nullptr;
    lv_chart_series_t* chartSeriesKnock_ = nullptr;
    lv_chart_series_t* chartSeriesStft_ = nullptr;
    lv_chart_series_t* chartSeriesAfr_ = nullptr;
    lv_obj_t* chartStatKnock_ = nullptr;
    lv_obj_t* chartStatStft_ = nullptr;
    lv_obj_t* chartStatAfr_ = nullptr;
    lv_obj_t* chartStatRpm_ = nullptr;

    // SD Card Format Confirmation Modal
    lv_obj_t* sdModalCard_ = nullptr;
    lv_obj_t* sdModalMsg_ = nullptr;

    // DTC Repair Guide Modal (global overlay across screens)
    lv_obj_t* dtcModalCard_ = nullptr;
    lv_obj_t* dtcModalTitle_ = nullptr;
    lv_obj_t* dtcModalCategory_ = nullptr;
    lv_obj_t* dtcModalMeaning_ = nullptr;
    lv_obj_t* dtcModalChecklist_ = nullptr;
    lv_obj_t* dtcModalRepair_ = nullptr;
    lv_obj_t* dtcModalCloseLbl_ = nullptr;

    // Dynamic Auto-Dimming & Night Mode
    bool currentNightMode_ = false;
    uint8_t currentBacklightDuty_ = 242; // Current PWM duty (0..255)
    uint8_t targetBacklightDuty_ = 242;  // Target PWM duty (0..255)
    lv_color_t themeText_   = lv_color_hex(0xFFFFFF);
    lv_color_t themeSpeed_  = lv_color_hex(0xFFFFFF);
    lv_color_t themeDim_    = lv_color_hex(0x8E949F);
    lv_color_t themeDotLit_ = lv_color_hex(0xFFFFFF);

    void applyThemeMode(bool isNight);
    void setBacklightDuty(uint8_t duty);

    // Settings Screen widgets & callbacks
    void updateSettingsScreen();
    static void onSettingsActionClick(lv_event_t* e);

    // Settings state
    ThemeMode themeMode_ = THEME_AUTO;
    uint8_t currentRotation_ = MX5_LCD_ROTATION; // 1 = USB Left, 3 = USB Right
    bool unitsUs_ = MX5_UNITS_US;
    bool autoLogEnabled_ = true;
    bool speedMaskEnabled_ = true;
    bool transAuto_ = true;
    uint8_t userBrightness_ = 95;

    // Settings UI widgets
    lv_obj_t* btnRotLeft_ = nullptr;
    lv_obj_t* btnRotRight_ = nullptr;
    lv_obj_t* btnThemeAuto_ = nullptr;
    lv_obj_t* btnThemeDay_ = nullptr;
    lv_obj_t* btnThemeNight_ = nullptr;
    lv_obj_t* btnBri25_ = nullptr;
    lv_obj_t* btnBri50_ = nullptr;
    lv_obj_t* btnBri75_ = nullptr;
    lv_obj_t* btnBri100_ = nullptr;
    lv_obj_t* btnTransAuto_ = nullptr;
    lv_obj_t* btnTransManual_ = nullptr;
    lv_obj_t* btnUnitUs_ = nullptr;
    lv_obj_t* btnUnitMet_ = nullptr;
    lv_obj_t* btnLogAuto_ = nullptr;
    lv_obj_t* btnLogDis_ = nullptr;
    lv_obj_t* btnMaskOn_ = nullptr;
    lv_obj_t* btnMaskOff_ = nullptr;
    lv_obj_t* settingsStatusLbl_ = nullptr;

    // Wheel Map calibration UI
    lv_obj_t* wheelCell_[4] = {};
    lv_obj_t* wheelCellLbl_[4] = {};
    lv_obj_t* wheelCellDid_[4] = {};
    lv_obj_t* wheelCellPress_[4] = {};
    lv_obj_t* wheelPrompt_ = nullptr;
    uint8_t wheelActive_ = 0;

    // Screen 14 - Bluetooth BLE Discovery & Pairing UI
    lv_obj_t* blePairedTitle_ = nullptr;
    lv_obj_t* blePairedNameLbl_ = nullptr;
    lv_obj_t* blePairedMacLbl_ = nullptr;
    lv_obj_t* blePairedRssiLbl_ = nullptr;
    lv_obj_t* blePairedStatusLbl_ = nullptr;
    lv_obj_t* bleScanStatusLbl_ = nullptr;
    lv_obj_t* bleScanBtn_ = nullptr;
    lv_obj_t* bleScanBtnLbl_ = nullptr;
    lv_obj_t* bleDeviceSlot_[4] = {};
    lv_obj_t* bleDevNameLbl_[4] = {};
    lv_obj_t* bleDevMacLbl_[4] = {};
    lv_obj_t* bleDevTagLbl_[4] = {};
    uint8_t selectedBleDevice_ = 0;

    lv_obj_t* screens_[SCREEN_COUNT] = {};
    uint8_t currentScreen_ = 0;
    uint8_t lastContentScreen_ = 0;
    uint32_t lastReflash_ = 0;
};
