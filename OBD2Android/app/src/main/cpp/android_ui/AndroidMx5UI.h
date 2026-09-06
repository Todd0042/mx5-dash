#pragma once

#include <stdint.h>
#include <lvgl.h>

#include "VehicleData.h"
#include "ObdSource.h"

/**
 * AndroidMx5UI
 *
 * Dedicated Widescreen UI Engine for Android (Google Pixel 11 Pro - 2410x1080 / 800x360 logical).
 * Maintains complete separation from the ESP32-S3 microcontroller hardware codebase.
 */

class AndroidMx5UI {
public:
    static constexpr uint16_t WIDTH = 800;
    static constexpr uint16_t HEIGHT = 360;

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

    // Settings Screen
    static constexpr uint8_t SCREEN_SETTINGS         = 13;

    // BLE Connection Configuration
    static constexpr uint8_t SCREEN_BLE_CONFIG       = 14;

    // Setup Wizard & Wheel Map
    static constexpr uint8_t SCREEN_WIZARD           = 15;
    static constexpr uint8_t SCREEN_WHEEL_MAP        = 16;

    static constexpr uint8_t SCREEN_COUNT            = 17;
    static constexpr uint8_t CONTENT_SCREEN_COUNT    = 7;

    enum ThemeMode {
        THEME_AUTO = 0,
        THEME_DAY = 1,
        THEME_NIGHT = 2
    };

    explicit AndroidMx5UI(ObdSource& obd) : obd_(obd) {}

    void begin();
    void update();

    void nextScreen();
    void prevScreen();
    void setScreen(uint8_t index);
    void toggleMenu();
    uint8_t currentScreen() const { return currentScreen_; }

    void setThemeMode(ThemeMode mode);
    void setNightMode(bool isNight);
    void setRotation(uint8_t rot);
    void setBrightness(uint8_t pct);

private:
    lv_obj_t* buildSpeedScreen();
    lv_obj_t* buildTpmsScreen();
    lv_obj_t* buildRpmScreen();
    lv_obj_t* buildEngineScreen();
    lv_obj_t* buildTrackScreen();
    lv_obj_t* buildTripScreen();
    lv_obj_t* buildDiagnosticsScreen();
    lv_obj_t* buildMenuScreen();
    lv_obj_t* buildSettingsScreen();
    lv_obj_t* buildBleConfigScreen();
    lv_obj_t* buildSetupWizard();
    lv_obj_t* buildWheelMapScreen();

    lv_obj_t* buildDiagSubFuel();
    lv_obj_t* buildDiagSubCyl();
    lv_obj_t* buildDiagSubChassis();
    lv_obj_t* buildDiagSubSmog();
    lv_obj_t* buildDiagSubLogs();
    void buildDtcRepairModal(lv_obj_t* parent);
    void buildSdFormatModal(lv_obj_t* parent);
    void renderIncidentChart(uint8_t incidentIndex);

    lv_obj_t* addSpeedChip(lv_obj_t* parent);
    lv_obj_t* addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex);

    struct SegArc {
        lv_obj_t* wrap = nullptr;
        lv_obj_t* dots[32] = {};
        uint8_t count = 0;
        lv_obj_t* val = nullptr;
    };
    SegArc buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX, int16_t posY,
                          const char* cap, uint8_t segCount);
    void updateDottedValue(SegArc& seg, float frac);
    void warnTopDots(SegArc& seg);
    lv_obj_t* addGearFrame(lv_obj_t* parent, int16_t x, int16_t y);

    void updateSpeedChip();
    void updateSpeedScreen();
    void updateTpmsScreen();
    void updateRpmScreen();
    void updateEngineScreen();
    void updateTrackScreen();
    void updateTripScreen();
    void updateDiagnosticsScreen();
    void updateMenuScreen();
    void updateSettingsScreen();
    void updateBleConfigScreen();
    void updateSetupWizard();
    void updateWheelMapScreen();

    void updateDiagSubFuel();
    void updateDiagSubCyl();
    void updateDiagSubChassis();
    void updateDiagSubSmog();
    void updateDiagSubLogs();
    void openDtcGuide(uint8_t index = 0);

    static void onScreenEvent(lv_event_t* e);
    static void onMenuIconClick(lv_event_t* e);
    static void onDtcActionClick(lv_event_t* e);
    static void onDtcCycleClick(lv_event_t* e);
    static void onSubScreenNavClick(lv_event_t* e);
    static void onDtcModalCloseClick(lv_event_t* e);
    static void onSdFormatClick(lv_event_t* e);
    static void onIncidentSelectClick(lv_event_t* e);
    static void onSettingsActionClick(lv_event_t* e);
    static void onBleConfigActionClick(lv_event_t* e);
    static void onWizardActionClick(lv_event_t* e);
    static void onWheelMapActionClick(lv_event_t* e);
    static void onSpeedWarnBannerClick(lv_event_t* e);

    void applyThemeMode(bool isNight);

    VehicleData data_local_;
    ObdSource& obd_;

    lv_obj_t* speedChipLabel_[SCREEN_COUNT] = {};

    // screen 0: speed
    lv_obj_t* speedBigLabel_ = nullptr;
    lv_obj_t* speedUnitLabel_ = nullptr;
    lv_obj_t* gearLbl_ = nullptr;
    SegArc rpmSeg_, fuelSeg_, ambientSeg_;

    // screen 0: dynamic warning panel
    lv_obj_t* speedNormalRightContainer_ = nullptr;
    lv_obj_t* speedWarningContainer_ = nullptr;
    lv_obj_t* speedWarnBanner_ = nullptr;
    lv_obj_t* speedWarnTitle_ = nullptr;
    lv_obj_t* speedWarnDetail_ = nullptr;
    lv_obj_t* speedWarnSubDetail_ = nullptr;
    lv_obj_t* speedWarnTpmsPod_[4] = {};
    lv_obj_t* speedWarnTpmsVal_[4] = {};
    lv_obj_t* speedWarnCarImg_ = nullptr;
    lv_obj_t* speedWarnTempsCard_ = nullptr;
    lv_obj_t* speedWarnCoolantVal_ = nullptr;
    lv_obj_t* speedWarnOilVal_ = nullptr;
    uint8_t currentWarningType_ = 0; // 0=none, 1=tpms, 2=coolant, 3=oil, 4=bat, 5=dtc
    bool warningMutedForDrive_ = false;

    // screen 1: tpms
    lv_obj_t* tpmsCard_[4] = {};
    lv_obj_t* tpmsDot_[4] = {};
    lv_obj_t* tpmsLabel_[4] = {};
    lv_obj_t* tpmsTemp_[4] = {};

    // screen 2: rpm
    SegArc rpmBigSeg_, loadSeg_, throttleSeg_, fuelArcSeg_, batArcSeg_;

    // screen 3: temps
    SegArc coolantSeg_, intakeSeg_, batterySeg_, oilSeg_;

    // screen 4: track
    lv_obj_t* trackTimerLbl_ = nullptr;
    lv_obj_t* trackBestLbl_ = nullptr;
    lv_obj_t* trackStateBadge_ = nullptr;
    lv_obj_t* trackThrottleBar_ = nullptr;
    lv_obj_t* trackLoadBar_ = nullptr;
    lv_obj_t* trackThrottleVal_ = nullptr;
    lv_obj_t* trackLoadVal_ = nullptr;
    SegArc hpSeg_, torqueSeg_;

    // screen 5: trip
    SegArc instantMpgSeg_;
    lv_obj_t* tripAvgVal_ = nullptr;
    lv_obj_t* tripDistVal_ = nullptr;
    lv_obj_t* tripRangeVal_ = nullptr;
    lv_obj_t* tripFuelVal_ = nullptr;

    // screen 6: diag
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

    // screen 7: menu
    lv_obj_t* menuStatusLbl_ = nullptr;
    lv_obj_t* menuPods_[CONTENT_SCREEN_COUNT] = {};

    // sub-screens 8-12
    SegArc stftSeg_, ltftSeg_;
    lv_obj_t* diagAfrVal_ = nullptr;
    lv_obj_t* diagHpfpVal_ = nullptr;
    lv_obj_t* diagEvapVal_ = nullptr;

    lv_obj_t* misfireCountLbl_[4] = {};
    lv_obj_t* misfireBar_[4] = {};
    lv_obj_t* diagSparkVal_ = nullptr;
    lv_obj_t* diagKnockVal_ = nullptr;
    lv_obj_t* diagVvtInVal_ = nullptr;
    lv_obj_t* diagVvtExVal_ = nullptr;

    lv_obj_t* wheelSpeedLbl_[4] = {};
    lv_obj_t* diagSasVal_ = nullptr;
    lv_obj_t* diagTransTempVal_ = nullptr;
    lv_obj_t* diagTccSlipVal_ = nullptr;

    lv_obj_t* smogSummaryLbl_ = nullptr;
    lv_obj_t* smogPodDot_[8] = {};
    lv_obj_t* smogPodLbl_[8] = {};

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

    lv_obj_t* sdModalCard_ = nullptr;
    lv_obj_t* sdModalMsg_ = nullptr;

    lv_obj_t* dtcModalCard_ = nullptr;
    lv_obj_t* dtcModalTitle_ = nullptr;
    lv_obj_t* dtcModalCategory_ = nullptr;
    lv_obj_t* dtcModalMeaning_ = nullptr;
    lv_obj_t* dtcModalChecklist_ = nullptr;
    lv_obj_t* dtcModalRepair_ = nullptr;
    lv_obj_t* dtcModalCloseLbl_ = nullptr;

    // settings widgets
    ThemeMode themeMode_ = THEME_AUTO;
    uint8_t currentRotation_ = 1;
    bool unitsUs_ = true;
    bool autoLogEnabled_ = true;
    bool speedMaskEnabled_ = true;
    bool transAuto_ = true;
    uint8_t userBrightness_ = 95;

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

    // BLE screen
    uint16_t bleScanTimeoutMs_ = 5000;
    char blePrefixBuf_[32] = "vLinker";
    lv_obj_t* bleConnStatusLbl_ = nullptr;
    lv_obj_t* bleDevNameLbl_ = nullptr;
    lv_obj_t* bleRssiLbl_ = nullptr;
    lv_obj_t* bleAdapterLbl_ = nullptr;
    lv_obj_t* bleScanBtn_ = nullptr;
    lv_obj_t* bleDisconnectBtn_ = nullptr;
    lv_obj_t* btnPrefixVlinker_ = nullptr;
    lv_obj_t* btnPrefixVLINK_ = nullptr;
    lv_obj_t* btnPrefixOBD_ = nullptr;
    lv_obj_t* btnPrefixCustom_ = nullptr;
    lv_obj_t* btnScan5_ = nullptr;
    lv_obj_t* btnScan10_ = nullptr;
    lv_obj_t* btnScan15_ = nullptr;

    // Wizard
    uint8_t wizardStep_ = 0;
    lv_obj_t* wizardStepPanel_[4] = {};
    lv_obj_t* wizardStepTitle_ = nullptr;
    lv_obj_t* wizardStepIndicator_ = nullptr;
    lv_obj_t* wizardNextBtn_ = nullptr;
    lv_obj_t* wizardBackBtn_ = nullptr;
    lv_obj_t* wizUnitUs_ = nullptr;
    lv_obj_t* wizUnitMet_ = nullptr;
    lv_obj_t* wizRotLeft_ = nullptr;
    lv_obj_t* wizRotRight_ = nullptr;
    lv_obj_t* wizPrefixBtn_[4] = {};
    lv_obj_t* wizScanStatus_ = nullptr;
    lv_obj_t* wizSkipBtn_ = nullptr;
    lv_obj_t* wizStartCalBtn_ = nullptr;

    // Wheel Map
    uint8_t wheelActive_ = 0;
    bool wheelComplete_ = false;
    lv_obj_t* wheelTitle_ = nullptr;
    lv_obj_t* wheelPrompt_ = nullptr;
    lv_obj_t* wheelCell_[4] = {};
    lv_obj_t* wheelCellLbl_[4] = {};
    lv_obj_t* wheelCellDid_[4] = {};
    lv_obj_t* wheelCellPress_[4] = {};
    lv_obj_t* wheelSuccessCard_ = nullptr;
    lv_obj_t* wheelSuccessLbl_ = nullptr;
    uint32_t wheelBlinkMs_ = 0;
    bool wheelBlinkOn_ = false;

    lv_obj_t* screens_[SCREEN_COUNT] = {};
    uint8_t currentScreen_ = 0;
    uint8_t lastContentScreen_ = 0;

    bool currentNightMode_ = false;
    lv_color_t themeText_   = lv_color_hex(0xFFFFFF);
    lv_color_t themeSpeed_  = lv_color_hex(0xFFFFFF);
    lv_color_t themeDim_    = lv_color_hex(0x8E949F);
    lv_color_t themeDotLit_ = lv_color_hex(0xFFFFFF);
};
