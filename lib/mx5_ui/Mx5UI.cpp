#include "Mx5UI.h"
#include "../mx5_config/DtcDatabase.h"

#if defined(ARDUINO) && !defined(PLATFORM_NATIVE)
#include "../waveshare_display/Waveshare35B.h"
#endif

#include "lv_font_mono_48.h"
#include "lv_font_mono_96.h"
#include "lv_font_mono_120.h"

#include <Config.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern "C" {
    extern const lv_image_dsc_t mx5_rf_cal_dsc;
    extern const lv_image_dsc_t mx5_rf_side_left_dsc;
    extern const lv_image_dsc_t mx5_rf_side_right_dsc;
}

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#define M5_PI M_PI

// ---------------------------------------------------------------------------
// Palette: Option 2 - Mazda Connect (CMU) OEM Sync
// ---------------------------------------------------------------------------
static const lv_color_t C_BG        = lv_color_hex(0x0C0D0F);   // Deep Anti-Glare Charcoal/Black
static const lv_color_t C_PANEL     = lv_color_hex(0x15161A);   // Smoked Obsidian Glass (Solid)
static const lv_color_t C_PANEL_BRD = lv_color_hex(0x282C35);   // Hairline Slate/Silver Border
static const lv_color_t C_ACCENT    = lv_color_hex(0xC41230);   // Signature Mazda Soul Red
static const lv_color_t C_ACCENT_DM = lv_color_hex(0x660A18);   // Deep Soul Red (Chip base)
static const lv_color_t C_CHROME    = lv_color_hex(0xDCE0E8);   // Bright Satin Aluminum (High Contrast)
static const lv_color_t C_TEXT      = lv_color_hex(0xFFFFFF);   // Pure Crisp White
static const lv_color_t C_DIM       = lv_color_hex(0xB6BCC8);   // High-Contrast Titanium Silver (74% Luminance)
static const lv_color_t C_SPEED     = lv_color_hex(0xFFFFFF);   // Hero Speed White
static const lv_color_t C_RPM_HI    = lv_color_hex(0xE82848);   // Tachometer Redline
static const lv_color_t C_WARN      = lv_color_hex(0xE86028);   // Warning Coral/Amber
static const lv_color_t C_DANGER    = lv_color_hex(0xD32F2F);   // Critical Danger Red
static const lv_color_t C_OK        = lv_color_hex(0x22C55E);   // Connected Green

// Dial dot colors
static const lv_color_t C_DOT_UNLIT = lv_color_hex(0x1E222A);   // Deep Dark Steel (Solid, clean)
static const lv_color_t C_DOT_LIT   = lv_color_hex(0xFFFFFF);   // Pure Crisp Instrument White

// ---------------------------------------------------------------------------
// US-unit conversions
// ---------------------------------------------------------------------------
static uint16_t speedU(uint8_t kmh) {
#if MX5_UNITS_US
    return (uint16_t)lroundf(kmh * 0.621371f);   // km/h -> mph
#else
    return kmh;
#endif
}

static uint16_t tempU(uint8_t celsius) {
#if MX5_UNITS_US
    return (uint16_t)lroundf(celsius * 9.0f / 5.0f + 32.0f);   // C -> F
#else
    return celsius;
#endif
}

static uint16_t pressureU(float bar) {
#if MX5_UNITS_US
    return (uint16_t)lroundf(bar * 14.50377f);   // bar -> psi
#else
    return (uint16_t)lroundf(bar * 1000.0f);
#endif
}

static float pressU(float bar) {
#if MX5_UNITS_US
    return bar * 14.50377f;                      // bar -> psi float
#else
    return bar * 1000.0f;
#endif
}

// ---------------------------------------------------------------------------
// Shared style helpers
// ---------------------------------------------------------------------------
static void setTextFont(lv_obj_t* obj) {
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
}

// Configures an object to never scroll and bubble touch gestures upward
static void lockNoScroll(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(obj, LV_DIR_NONE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(obj, LV_OBJ_FLAG_GESTURE_BUBBLE);
}

// ---------------------------------------------------------------------------
// Background graphics layer
// ---------------------------------------------------------------------------
static lv_obj_t* addBackgroundLayer(lv_obj_t* parent) {
    lv_obj_t* bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, C_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_pos(bg, 0, 0);
    lockNoScroll(bg);

    // Signature Mazda Soul Red accent rule across the top
    lv_obj_t* topLine = lv_obj_create(bg);
    lv_obj_remove_style_all(topLine);
    lv_obj_set_size(topLine, lv_pct(100), 2);
    lv_obj_set_pos(topLine, 0, 0);
    lv_obj_set_style_bg_color(topLine, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(topLine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(topLine, 0, 0);
    lockNoScroll(topLine);

    lv_obj_move_to_index(bg, 0);
    return bg;
}

// Shared page title in the top-right
static lv_obj_t* addPageTitle(lv_obj_t* parent, const char* txt) {
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, txt);
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 8);
    lockNoScroll(title);
    return title;
}

// ---------------------------------------------------------------------------
// Public Navigation API with Debounced Swipe Handling
// ---------------------------------------------------------------------------

static const char* getScreenTitle(uint8_t index) {
    switch (index) {
        case Mx5UI::SCREEN_SPEED:            return "HERO SPEEDOMETER";
        case Mx5UI::SCREEN_TPMS:             return "TIRE MONITOR (TPMS)";
        case Mx5UI::SCREEN_RPM:              return "ENGINE TACHOMETER";
        case Mx5UI::SCREEN_TEMPS:            return "TEMPS & PRESSURES";
        case Mx5UI::SCREEN_TRACK:            return "TRACK & DYNAMICS";
        case Mx5UI::SCREEN_TRIP:             return "TRIP & FUEL ECONOMY";
        case Mx5UI::SCREEN_DIAG:             return "DIAGNOSTICS & DTC";
        case Mx5UI::SCREEN_MENU:             return "MENU HUB";
        case Mx5UI::SCREEN_DIAG_SUB_FUEL:    return "FUEL TRIMS & HPFP";
        case Mx5UI::SCREEN_DIAG_SUB_CYL:     return "CYLINDER MISFIRE";
        case Mx5UI::SCREEN_DIAG_SUB_CHASSIS: return "CHASSIS & DYNAMICS";
        case Mx5UI::SCREEN_DIAG_SUB_SMOG:    return "I/M SMOG READINESS";
        case Mx5UI::SCREEN_DIAG_SUB_LOGS:    return "INCIDENT BLACKBOX";
        case Mx5UI::SCREEN_SETTINGS:         return "DISPLAY SETTINGS";
        case Mx5UI::SCREEN_BLE_CONFIG:       return "BLUETOOTH ADAPTER";
        case Mx5UI::SCREEN_WIZARD:           return "SETUP WIZARD";
        case Mx5UI::SCREEN_WHEEL_MAP:        return "TPMS CALIBRATION";
        default: return "MX-5 DASHBOARD";
    }
}

lv_obj_t* Mx5UI::buildTransitionScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    // Top Destination Badge Pill
    transBadge_ = lv_obj_create(scr);
    lv_obj_remove_style_all(transBadge_);
    lv_obj_set_size(transBadge_, 280, 34);
    lv_obj_set_pos(transBadge_, 100, 20);
    lv_obj_set_style_bg_color(transBadge_, C_PANEL, 0);
    lv_obj_set_style_bg_opa(transBadge_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(transBadge_, 17, 0);
    lv_obj_set_style_border_color(transBadge_, C_ACCENT, 0);
    lv_obj_set_style_border_width(transBadge_, 1, 0);
    lockNoScroll(transBadge_);

    transTitleLbl_ = lv_label_create(transBadge_);
    lv_obj_set_style_text_font(transTitleLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(transTitleLbl_, C_SPEED, 0);
    lv_label_set_text(transTitleLbl_, "ENGINE TACHOMETER");
    lv_obj_center(transTitleLbl_);
    lockNoScroll(transTitleLbl_);

    // Center Car Image (260x145)
    transCarImg_ = lv_image_create(scr);
    lv_image_set_src(transCarImg_, &mx5_rf_side_left_dsc);
    lv_obj_set_pos(transCarImg_, 110, 75);
    lockNoScroll(transCarImg_);

    // Amber DRL Headlight dot
    transDrlDot_ = lv_obj_create(scr);
    lv_obj_remove_style_all(transDrlDot_);
    lv_obj_set_size(transDrlDot_, 10, 10);
    lv_obj_set_style_radius(transDrlDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(transDrlDot_, lv_color_hex(0xFF9800), 0); // Amber
    lv_obj_set_style_bg_opa(transDrlDot_, LV_OPA_COVER, 0);
    lv_obj_set_pos(transDrlDot_, 132, 145);
    lockNoScroll(transDrlDot_);

    // Soul Red Taillight dot
    transTailDot_ = lv_obj_create(scr);
    lv_obj_remove_style_all(transTailDot_);
    lv_obj_set_size(transTailDot_, 8, 8);
    lv_obj_set_style_radius(transTailDot_, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(transTailDot_, lv_color_hex(0xD12229), 0); // Soul Red
    lv_obj_set_style_bg_opa(transTailDot_, LV_OPA_COVER, 0);
    lv_obj_set_pos(transTailDot_, 352, 137);
    lockNoScroll(transTailDot_);

    // Bottom Speed Pill
    lv_obj_t* spdPill = lv_obj_create(scr);
    lv_obj_remove_style_all(spdPill);
    lv_obj_set_size(spdPill, 160, 32);
    lv_obj_set_pos(spdPill, 160, 245);
    lv_obj_set_style_bg_color(spdPill, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(spdPill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(spdPill, 16, 0);
    lv_obj_set_style_border_color(spdPill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(spdPill, 1, 0);
    lockNoScroll(spdPill);

    transSpeedLbl_ = lv_label_create(spdPill);
    lv_obj_set_style_text_font(transSpeedLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(transSpeedLbl_, C_SPEED, 0);
    lv_label_set_text(transSpeedLbl_, "-- MPH");
    lv_obj_center(transSpeedLbl_);
    lockNoScroll(transSpeedLbl_);

    return scr;
}

void Mx5UI::onTransitionTimer(lv_timer_t* t) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_timer_get_user_data(t));
    if (!ui || ui->pendingTargetScreen_ >= SCREEN_COUNT) return;

    uint8_t target = ui->pendingTargetScreen_;
    ui->pendingTargetScreen_ = 255;
    ui->currentScreen_ = target;
    if (target != SCREEN_MENU) {
        ui->lastContentScreen_ = target;
    }

    lv_obj_scroll_to(ui->screens_[target], 0, 0, LV_ANIM_OFF);
    lv_screen_load_anim(ui->screens_[target], ui->transitionForward_ ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, 120, 0, false);
    ui->update();
}

void Mx5UI::setScreenWithTransition(uint8_t targetIndex, bool forward) {
    if (targetIndex >= SCREEN_COUNT || !screens_[targetIndex]) return;
    if (!transitionScr_) {
        setScreen(targetIndex);
        return;
    }

    pendingTargetScreen_ = targetIndex;
    transitionForward_ = forward;
    obd_.setActiveScreen(targetIndex);

    // Update destination title
    if (transTitleLbl_) {
        lv_label_set_text(transTitleLbl_, getScreenTitle(targetIndex));
    }
    // Update car orientation and lighting dots
    if (transCarImg_) {
        lv_image_set_src(transCarImg_, forward ? &mx5_rf_side_left_dsc : &mx5_rf_side_right_dsc);
    }
    if (transDrlDot_) {
        lv_obj_set_pos(transDrlDot_, forward ? 132 : 348, 145);
    }
    if (transTailDot_) {
        lv_obj_set_pos(transTailDot_, forward ? 352 : 128, 137);
    }
    if (transSpeedLbl_) {
        lv_label_set_text_fmt(transSpeedLbl_, "%u %s", (unitsUs_ ? speedU(data_local_.speedKmh) : data_local_.speedKmh), (unitsUs_ ? "MPH" : "KM/H"));
    }

    // Slide transition interstitial
    lv_screen_load_anim(transitionScr_, forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT, 120, 0, false);

    // Hold for 260ms then slide into target screen
    lv_timer_t* timer = lv_timer_create(onTransitionTimer, 260, this);
    lv_timer_set_repeat_count(timer, 1);
}

void Mx5UI::setScreen(uint8_t index) {
    if (index >= SCREEN_COUNT || !screens_[index]) return;
    currentScreen_ = index;
    if (index != SCREEN_MENU) {
        lastContentScreen_ = index;
    }

    obd_.setActiveScreen(index);

    // Reset scroll offsets so all screens remain perfectly centered
    lv_obj_scroll_to(screens_[currentScreen_], 0, 0, LV_ANIM_OFF);
    lv_screen_load_anim(screens_[currentScreen_], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    update();
}

void Mx5UI::nextScreen() {
    if (currentScreen_ == SCREEN_MENU) {
        setScreen(lastContentScreen_);
        return;
    }
    if (currentScreen_ >= SCREEN_DIAG_SUB_FUEL && currentScreen_ <= SCREEN_DIAG_SUB_LOGS) {
        // Cycle forward among the 5 diagnostic sub-screens: 8 -> 9 -> 10 -> 11 -> 12 -> 8
        uint8_t nextSub = SCREEN_DIAG_SUB_FUEL + ((currentScreen_ - SCREEN_DIAG_SUB_FUEL + 1) % 5);
        setScreenWithTransition(nextSub, true);
        return;
    }
    uint8_t next = (currentScreen_ + 1) % CONTENT_SCREEN_COUNT;
    setScreenWithTransition(next, true);
}

void Mx5UI::prevScreen() {
    if (currentScreen_ == SCREEN_MENU) {
        setScreen(lastContentScreen_);
        return;
    }
    if (currentScreen_ >= SCREEN_DIAG_SUB_FUEL && currentScreen_ <= SCREEN_DIAG_SUB_LOGS) {
        // Cycle backward among the 5 diagnostic sub-screens: 8 <- 9 <- 10 <- 11 <- 12 <- 8
        uint8_t prevSub = SCREEN_DIAG_SUB_FUEL + ((currentScreen_ - SCREEN_DIAG_SUB_FUEL + 4) % 5);
        setScreenWithTransition(prevSub, false);
        return;
    }
    uint8_t prev = (currentScreen_ == 0) ? (CONTENT_SCREEN_COUNT - 1) : (currentScreen_ - 1);
    setScreenWithTransition(prev, false);
}

void Mx5UI::toggleMenu() {
    if (currentScreen_ == SCREEN_MENU) {
        setScreen(lastContentScreen_);
    } else if (currentScreen_ >= SCREEN_DIAG_SUB_FUEL && currentScreen_ <= SCREEN_DIAG_SUB_LOGS) {
        setScreen(SCREEN_DIAG);
    } else if (currentScreen_ >= SCREEN_BLE_CONFIG && currentScreen_ <= SCREEN_WHEEL_MAP) {
        setScreen(SCREEN_SETTINGS);
    } else {
        lastContentScreen_ = currentScreen_;
        setScreen(SCREEN_MENU);
    }
}

static uint32_t lastSwipeTime = 0;

static void triggerSwipe(Mx5UI* ui, int dir) {
    uint32_t now = lv_tick_get();
    if (now - lastSwipeTime < 500) return;
    lastSwipeTime = now;

    if (dir == 1) {
        ui->nextScreen();
    } else if (dir == -1) {
        ui->prevScreen();
    } else if (dir == 0) {
        ui->toggleMenu();
    }
}

static void onScreenEvent(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;

    static int16_t startX = 0;
    static int16_t startY = 0;
    static bool isDragging = false;
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED) {
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            startX = p.x;
            startY = p.y;
            isDragging = true;
        }
    } else if (code == LV_EVENT_RELEASED && isDragging) {
        isDragging = false;
        lv_indev_t* indev = lv_indev_active();
        if (indev) {
            lv_point_t p;
            lv_indev_get_point(indev, &p);
            int16_t dx = p.x - startX;
            int16_t dy = p.y - startY;

            if (abs(dy) > 55 && abs(dy) > abs(dx) * 1.3f) {
                triggerSwipe(ui, 0); // Vertical -> Menu Hub
            } else if (abs(dx) > 55 && abs(dx) > abs(dy) * 1.3f) {
                triggerSwipe(ui, (dx < 0) ? 1 : -1); // Horizontal -> Next / Prev
            }
        }
    } else if (code == LV_EVENT_GESTURE) {
        lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
        if (dir == LV_DIR_LEFT) {
            triggerSwipe(ui, 1);
        } else if (dir == LV_DIR_RIGHT) {
            triggerSwipe(ui, -1);
        } else if (dir == LV_DIR_TOP || dir == LV_DIR_BOTTOM) {
            triggerSwipe(ui, 0);
        }
        isDragging = false;
    }
}

void Mx5UI::begin() {
    screens_[SCREEN_SPEED] = buildSpeedScreen();
    screens_[SCREEN_TPMS]  = buildTpmsScreen();
    screens_[SCREEN_RPM]   = buildRpmScreen();
    screens_[SCREEN_TEMPS] = buildEngineScreen();
    screens_[SCREEN_TRACK] = buildTrackScreen();
    screens_[SCREEN_TRIP]  = buildTripScreen();
    screens_[SCREEN_DIAG]  = buildDiagnosticsScreen();
    screens_[SCREEN_MENU]  = buildMenuScreen();

    // Diagnostic Sub-Dashboards
    screens_[SCREEN_DIAG_SUB_FUEL]    = buildDiagSubFuel();
    screens_[SCREEN_DIAG_SUB_CYL]     = buildDiagSubCyl();
    screens_[SCREEN_DIAG_SUB_CHASSIS] = buildDiagSubChassis();
    screens_[SCREEN_DIAG_SUB_SMOG]    = buildDiagSubSmog();
    screens_[SCREEN_DIAG_SUB_LOGS]    = buildDiagSubLogs();
    screens_[SCREEN_SETTINGS]         = buildSettingsScreen();
    screens_[SCREEN_BLE_CONFIG]       = buildBleConfigScreen();
    screens_[SCREEN_WIZARD]           = buildWizardScreen();
    screens_[SCREEN_WHEEL_MAP]        = buildWheelMapScreen();
    transitionScr_                    = buildTransitionScreen();

    // Attach robust gesture & drag listener to all active screens
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
        if (!screens_[i]) continue;
        lockNoScroll(screens_[i]);
        lv_obj_add_event_cb(screens_[i], onScreenEvent, LV_EVENT_PRESSED, this);
        lv_obj_add_event_cb(screens_[i], onScreenEvent, LV_EVENT_RELEASED, this);
        lv_obj_add_event_cb(screens_[i], onScreenEvent, LV_EVENT_GESTURE, this);
    }

    // Speedometer is the default home screen
    setScreen(SCREEN_SPEED);
}

void Mx5UI::update() {
    obd_.snapshot(data_local_);

    // Shared sticky speed chip
    updateSpeedChip();

    switch (currentScreen_) {
        case SCREEN_SPEED: updateSpeedScreen();       break;
        case SCREEN_TPMS:  updateTpmsScreen();        break;
        case SCREEN_RPM:   updateRpmScreen();         break;
        case SCREEN_TEMPS: updateEngineScreen();      break;
        case SCREEN_TRACK: updateTrackScreen();       break;
        case SCREEN_TRIP:  updateTripScreen();        break;
        case SCREEN_DIAG:  updateDiagnosticsScreen(); break;
        case SCREEN_MENU:  updateMenuScreen();        break;

        case SCREEN_DIAG_SUB_FUEL:    updateDiagSubFuel();    break;
        case SCREEN_DIAG_SUB_CYL:     updateDiagSubCyl();     break;
        case SCREEN_DIAG_SUB_CHASSIS: updateDiagSubChassis(); break;
        case SCREEN_DIAG_SUB_SMOG:    updateDiagSubSmog();    break;
        case SCREEN_BLE_CONFIG:       updateBleConfigScreen(); break;
        default: break;
    }
}

void Mx5UI::onGesture(lv_event_t* e) {
    (void)e;
}

void Mx5UI::onMenuIconClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;

    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uint32_t targetIndex = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    if (targetIndex < SCREEN_COUNT) {
        ui->setScreen((uint8_t)targetIndex);
    }
}

// ---------------------------------------------------------------------------
// Shared sticky speed chip (always top-left, screens 1..7)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::addSpeedChip(lv_obj_t* parent) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 92, 30);
    lv_obj_set_pos(chip, 8, 6);
    lv_obj_set_style_bg_color(chip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 8, 0);
    lv_obj_set_style_border_color(chip, C_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lockNoScroll(chip);

    lv_obj_t* lbl = lv_label_create(chip);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, C_SPEED, 0);
    lv_label_set_text_fmt(lbl, "--");
    lv_obj_center(lbl);
    lockNoScroll(lbl);

    return lbl;
}

// ---------------------------------------------------------------------------
// Segmented (dotted) arc meter - Mazda cluster dial style
// ---------------------------------------------------------------------------
Mx5UI::SegArc Mx5UI::buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX,
                                    int16_t posY, const char* cap, uint8_t segCount) {
    SegArc seg;
    seg.count = segCount;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, size, size);
    lv_obj_set_pos(wrap, posX, posY);
    lv_obj_set_style_bg_color(wrap, C_PANEL, 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);          // Solid opaque obsidian glass
    lv_obj_set_style_radius(wrap, 16, 0);
    lv_obj_set_style_border_color(wrap, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(wrap, 1, 0);
    lockNoScroll(wrap);
    seg.wrap = wrap;

    const float cx = size * 0.5f;
    const float cy = size * 0.5f;
    const float r = size * 0.5f - 10.0f;
    uint8_t dotSize = (size >= 140) ? 8 : ((size >= 88) ? 6 : 5);

    for (uint8_t i = 0; i < segCount; i++) {
        float angDeg = 135.0f + (float)i * 270.0f / (float)(segCount - 1);
        float rad = angDeg * M5_PI / 180.0f;
        lv_obj_t* dot = lv_obj_create(wrap);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_set_pos(dot, (int16_t)(cx + r * cosf(rad) - (dotSize / 2)),
                       (int16_t)(cy + r * sinf(rad) - (dotSize / 2)));
        lv_obj_set_style_bg_color(dot, C_DOT_UNLIT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lockNoScroll(dot);
        seg.dots[i] = dot;
    }

    lv_obj_t* val = lv_label_create(wrap);
    const lv_font_t* font = (size >= 170) ? &lv_font_montserrat_28 :
                            ((size >= 130) ? &lv_font_montserrat_24 :
                            ((size >= 88)  ? &lv_font_montserrat_20 :
                            ((size >= 68)  ? &lv_font_montserrat_18 : &lv_font_montserrat_14)));
    lv_obj_set_style_text_font(val, font, 0);
    lv_obj_set_style_text_color(val, C_SPEED, 0);
    lv_label_set_text_fmt(val, "--");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, (size >= 170) ? -4 : ((size >= 88) ? -2 : -1));
    lockNoScroll(val);
    seg.val = val;

    lv_obj_t* capLbl = lv_label_create(wrap);
    lv_obj_set_style_text_font(capLbl, (size >= 170) ? &lv_font_montserrat_14 : ((size >= 88) ? &lv_font_montserrat_12 : &lv_font_montserrat_10), 0);
    lv_obj_set_style_text_color(capLbl, C_CHROME, 0);
    lv_label_set_text(capLbl, cap);
    lv_obj_align(capLbl, LV_ALIGN_BOTTOM_MID, 0, (size >= 170) ? -6 : ((size >= 88) ? -4 : -2));
    lockNoScroll(capLbl);

    return seg;
}

void Mx5UI::updateDottedValue(SegArc& seg, float frac) {
    if (seg.count == 0) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    uint8_t filled = (uint8_t)roundf(frac * (float)(seg.count - 1));
    for (uint8_t i = 0; i < seg.count; i++) {
        bool lit = (i <= filled);
        lv_obj_set_style_bg_color(seg.dots[i], lit ? C_DOT_LIT : C_DOT_UNLIT, 0);
        lv_obj_set_style_bg_opa(seg.dots[i], LV_OPA_COVER, 0);
    }
}

void Mx5UI::warnTopDots(SegArc& seg) {
    uint8_t topCount = seg.count > 6 ? 4 : 2;
    for (uint8_t i = seg.count - topCount; i < seg.count; i++) {
        lv_obj_set_style_bg_color(seg.dots[i], C_RPM_HI, 0);
        lv_obj_set_style_bg_opa(seg.dots[i], LV_OPA_COVER, 0);
    }
}

// ---------------------------------------------------------------------------
// Gear selector box (positioned at bottom-right of speed card)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::addGearFrame(lv_obj_t* parent, int16_t x, int16_t y) {
    lv_obj_t* box = lv_obj_create(parent);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 44, 48);
    lv_obj_set_pos(box, x - 6, y - 2);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 10, 0);
    lv_obj_set_style_border_color(box, C_ACCENT, 0);
    lv_obj_set_style_border_width(box, 2, 0);
    lockNoScroll(box);

    lv_obj_t* gearLbl = lv_label_create(box);
    lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gearLbl, C_SPEED, 0);
    lv_label_set_text(gearLbl, "-");
    lv_obj_center(gearLbl);
    lockNoScroll(gearLbl);

    return gearLbl;
}

void Mx5UI::updateSpeedChip() {
    lv_obj_t* label = speedChipLabel_[currentScreen_];
    if (!label) return;
    lv_label_set_text_fmt(label, "%3d %s", speedU(data_local_.speedKmh),
                          MX5_UNITS_US ? "MPH" : "km/h");
}

// ---------------------------------------------------------------------------
// Screen 0 - Speed (Default Driving Screen)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildSpeedScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_SPEED] = nullptr;

    // Hero speed card (280x270 @ 16, 24)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 280, 270);
    lv_obj_set_pos(card, 16, 24);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    // Hero Speed Readout (Extra Large 120px bold monospace digits)
    speedBigLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedBigLabel_, &lv_font_mono_120, 0);
    lv_obj_set_style_text_color(speedBigLabel_, C_SPEED, 0);
    lv_label_set_text(speedBigLabel_, "0");
    lv_obj_align(speedBigLabel_, LV_ALIGN_CENTER, 0, -22);
    lockNoScroll(speedBigLabel_);

    // MPH text (placed lower, offset 0, 80)
    speedUnitLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedUnitLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(speedUnitLabel_, C_CHROME, 0);
    lv_label_set_text(speedUnitLabel_, MX5_UNITS_US ? "MPH" : "km/h");
    lv_obj_align(speedUnitLabel_, LV_ALIGN_CENTER, 0, 80);
    lockNoScroll(speedUnitLabel_);

    // AT Gear indicator moved back to the bottom right of the card
    gearLbl_ = addGearFrame(card, 226, 210);

    // Right-side normal container (304, 24, w: 166, h: 270)
    speedNormalRightContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedNormalRightContainer_);
    lv_obj_set_size(speedNormalRightContainer_, 166, 270);
    lv_obj_set_pos(speedNormalRightContainer_, 304, 24);
    lockNoScroll(speedNormalRightContainer_);

    // Right-side circular arc gauges inside container
    rpmSeg_     = buildDottedArc(speedNormalRightContainer_, 156, 6, 6, "RPM", 18);
    fuelSeg_    = buildDottedArc(speedNormalRightContainer_, 76, 6, 174, "FUEL", 10);
    ambientSeg_ = buildDottedArc(speedNormalRightContainer_, 76, 86, 174, "AMB", 10);

    // Right-side dynamic warning container
    speedWarningContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedWarningContainer_);
    lv_obj_set_size(speedWarningContainer_, 166, 270);
    lv_obj_set_pos(speedWarningContainer_, 304, 24);
    lv_obj_add_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(speedWarningContainer_);

    // Warning banner across top
    speedWarnBanner_ = lv_obj_create(speedWarningContainer_);
    lv_obj_remove_style_all(speedWarnBanner_);
    lv_obj_set_size(speedWarnBanner_, 166, 34);
    lv_obj_set_pos(speedWarnBanner_, 0, 0);
    lv_obj_set_style_bg_color(speedWarnBanner_, C_DANGER, 0);
    lv_obj_set_style_bg_opa(speedWarnBanner_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(speedWarnBanner_, 8, 0);
    lv_obj_add_event_cb(speedWarnBanner_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnBanner_);

    speedWarnTitle_ = lv_label_create(speedWarnBanner_);
    lv_obj_set_style_text_font(speedWarnTitle_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(speedWarnTitle_, C_TEXT, 0);
    lv_label_set_text(speedWarnTitle_, "[!] ALERT • TAP TO CLEAR");
    lv_obj_center(speedWarnTitle_);
    lv_obj_add_event_cb(speedWarnTitle_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnTitle_);

    // Warning content card
    speedWarnCard_ = lv_obj_create(speedWarningContainer_);
    lv_obj_remove_style_all(speedWarnCard_);
    lv_obj_set_size(speedWarnCard_, 166, 226);
    lv_obj_set_pos(speedWarnCard_, 0, 42);
    lv_obj_set_style_bg_color(speedWarnCard_, lv_color_hex(0x191014), 0);
    lv_obj_set_style_bg_opa(speedWarnCard_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(speedWarnCard_, 12, 0);
    lv_obj_set_style_border_color(speedWarnCard_, C_ACCENT, 0);
    lv_obj_set_style_border_width(speedWarnCard_, 1, 0);
    lockNoScroll(speedWarnCard_);

    speedWarnMainVal_ = lv_label_create(speedWarnCard_);
    lv_obj_set_style_text_font(speedWarnMainVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(speedWarnMainVal_, C_ACCENT, 0);
    lv_label_set_text(speedWarnMainVal_, "CRITICAL ALERT");
    lv_obj_align(speedWarnMainVal_, LV_ALIGN_TOP_MID, 0, 16);
    lockNoScroll(speedWarnMainVal_);

    speedWarnSubVal_ = lv_label_create(speedWarnCard_);
    lv_obj_set_style_text_font(speedWarnSubVal_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(speedWarnSubVal_, C_TEXT, 0);
    lv_label_set_text(speedWarnSubVal_, "Check vehicle telemetry");
    lv_obj_align(speedWarnSubVal_, LV_ALIGN_CENTER, 0, 18);
    lockNoScroll(speedWarnSubVal_);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 1 - TPMS (4 Tire Pressure & Temp Cards + Center Vehicle Silhouette)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildTpmsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TPMS] = addSpeedChip(scr);
    addPageTitle(scr, "TIRES");

    const char* titles[4] = {"FL", "FR", "RL", "RR"};
    // Left col: FL(12, 46), RL(12, 180). Right col: FR(308, 46), RR(308, 180)
    const int16_t xs[4] = {12, 308, 12, 308};
    const int16_t ys[4] = {46, 46, 180, 180};
    const int16_t cardW = 160;
    const int16_t cardH = 124;

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, cardW, cardH);
        lv_obj_set_pos(card, xs[i], ys[i]);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lockNoScroll(card);
        tpmsCard_[i] = card;

        lv_obj_t* tag = lv_label_create(card);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, titles[i]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 14, 10);
        lockNoScroll(tag);

        // Status indicator dot (12x12 with 1px halo)
        lv_obj_t* dot = lv_obj_create(card);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_pos(dot, cardW - 24, 12);
        lv_obj_set_style_bg_color(dot, C_OK, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x0C0D0F), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lockNoScroll(dot);
        tpmsDot_[i] = dot;

        tpmsLabel_[i] = lv_label_create(card);
        lv_obj_set_style_text_font(tpmsLabel_[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(tpmsLabel_[i], C_SPEED, 0);
        lv_label_set_text(tpmsLabel_[i], "--");
        lv_obj_align(tpmsLabel_[i], LV_ALIGN_LEFT_MID, 14, -4);
        lockNoScroll(tpmsLabel_[i]);

        lv_obj_t* uLbl = lv_label_create(card);
        lv_obj_set_style_text_font(uLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(uLbl, C_DIM, 0);
        lv_label_set_text(uLbl, MX5_UNITS_US ? "PSI" : "bar");
        lv_obj_align(uLbl, LV_ALIGN_LEFT_MID, 82, 0);
        lockNoScroll(uLbl);

        tpmsTemp_[i] = lv_label_create(card);
        lv_obj_set_style_text_font(tpmsTemp_[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tpmsTemp_[i], C_DIM, 0);
        lv_label_set_text(tpmsTemp_[i], "-- °F");
        lv_obj_align(tpmsTemp_[i], LV_ALIGN_BOTTOM_LEFT, 14, -10);
        lockNoScroll(tpmsTemp_[i]);
    }

    // Center Vehicle Silhouette Card (120x258 @ 180, 46)
    lv_obj_t* carCard = lv_obj_create(scr);
    lv_obj_remove_style_all(carCard);
    lv_obj_set_size(carCard, 120, 258);
    lv_obj_set_pos(carCard, 180, 46);
    lv_obj_set_style_bg_color(carCard, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(carCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(carCard, 14, 0);
    lv_obj_set_style_border_color(carCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(carCard, 1, 0);
    lockNoScroll(carCard);

    // Real Mazda MX-5 RF Top-Down Image (99x180 px)
    lv_obj_t* carImg = lv_image_create(carCard);
    lv_image_set_src(carImg, &mx5_rf_cal_dsc);
    lv_obj_align(carImg, LV_ALIGN_CENTER, 0, -8);
    lockNoScroll(carImg);

    lv_obj_t* cTxt = lv_label_create(carCard);
    lv_obj_set_style_text_font(cTxt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cTxt, C_DIM, 0);
    lv_label_set_text(cTxt, "MX-5 RF");
    lv_obj_align(cTxt, LV_ALIGN_BOTTOM_MID, 0, -6);
    lockNoScroll(cTxt);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 2 - RPM / Engine (5-Dial Multi-Gauge Cluster)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildRpmScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_RPM] = addSpeedChip(scr);
    addPageTitle(scr, "ENGINE");

    // Center Big Tachometer Dial (180x180)
    rpmBigSeg_ = buildDottedArc(scr, 180, 150, 52, "TACHOMETER", 22);

    // Left Multi-Dials (96x96)
    loadSeg_     = buildDottedArc(scr, 96, 24, 52, "LOAD", 10);
    throttleSeg_ = buildDottedArc(scr, 96, 24, 168, "THROTTLE", 10);

    // Right Multi-Dials (96x96)
    fuelArcSeg_ = buildDottedArc(scr, 96, 360, 52, "FUEL", 10);
    batArcSeg_  = buildDottedArc(scr, 96, 360, 168, "BATTERY", 10);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 3 - Temperatures / Fluids (4-Dial Instrument Cluster)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildEngineScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TEMPS] = addSpeedChip(scr);
    addPageTitle(scr, "TEMPERATURES");

    // Top Row (y: 42, ends at 158)
    coolantSeg_ = buildDottedArc(scr, 116, 76, 42, "COOLANT", 14);
    oilSeg_     = buildDottedArc(scr, 116, 288, 42, "OIL TEMP", 14);

    // Bottom Row (y: 174, ends at 290)
    intakeSeg_  = buildDottedArc(scr, 116, 76, 174, "INTAKE AIR", 14);
    batterySeg_ = buildDottedArc(scr, 116, 288, 174, "BATTERY", 14);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 4 - Track & Dynamics (Option A: 0-60 Timer, HP & Torque, Throttle/Brake)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildTrackScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TRACK] = addSpeedChip(scr);
    addPageTitle(scr, "TRACK & DYNAMICS");

    // Left Hero Card: 0-60 Acceleration Timer (180x240 @ 18, 48)
    lv_obj_t* timerCard = lv_obj_create(scr);
    lv_obj_remove_style_all(timerCard);
    lv_obj_set_size(timerCard, 180, 240);
    lv_obj_set_pos(timerCard, 18, 48);
    lv_obj_set_style_bg_color(timerCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(timerCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(timerCard, 16, 0);
    lv_obj_set_style_border_color(timerCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(timerCard, 1, 0);
    lockNoScroll(timerCard);

    lv_obj_t* tTag = lv_label_create(timerCard);
    lv_obj_set_style_text_font(tTag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(tTag, C_CHROME, 0);
    lv_label_set_text(tTag, "0 - 60 MPH TIMER");
    lv_obj_align(tTag, LV_ALIGN_TOP_MID, 0, 12);
    lockNoScroll(tTag);

    // Big timer digits (font 40 for 3-foot glance)
    trackTimerLbl_ = lv_label_create(timerCard);
    lv_obj_set_style_text_font(trackTimerLbl_, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(trackTimerLbl_, C_SPEED, 0);
    lv_label_set_text(trackTimerLbl_, "0.00 s");
    lv_obj_align(trackTimerLbl_, LV_ALIGN_CENTER, 0, -20);
    lockNoScroll(trackTimerLbl_);

    // Status state badge (Pill)
    trackStateBadge_ = lv_obj_create(timerCard);
    lv_obj_remove_style_all(trackStateBadge_);
    lv_obj_set_size(trackStateBadge_, 110, 26);
    lv_obj_align(trackStateBadge_, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_style_bg_color(trackStateBadge_, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(trackStateBadge_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackStateBadge_, 13, 0);
    lv_obj_set_style_border_color(trackStateBadge_, C_ACCENT, 0);
    lv_obj_set_style_border_width(trackStateBadge_, 1, 0);
    lockNoScroll(trackStateBadge_);

    lv_obj_t* badgeLbl = lv_label_create(trackStateBadge_);
    lv_obj_set_style_text_font(badgeLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(badgeLbl, C_TEXT, 0);
    lv_label_set_text(badgeLbl, "READY / ARMED");
    lv_obj_center(badgeLbl);
    lockNoScroll(badgeLbl);

    trackBestLbl_ = lv_label_create(timerCard);
    lv_obj_set_style_text_font(trackBestLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(trackBestLbl_, C_DIM, 0);
    lv_label_set_text(trackBestLbl_, "BEST: 5.70 s");
    lv_obj_align(trackBestLbl_, LV_ALIGN_BOTTOM_MID, 0, -12);
    lockNoScroll(trackBestLbl_);

    // Middle Dials: Live HP & Torque Output (116x116 @ 210, 48 & 210, 172)
    hpSeg_     = buildDottedArc(scr, 116, 210, 48, "HORSEPOWER", 14);
    torqueSeg_ = buildDottedArc(scr, 116, 210, 172, "TORQUE", 14);

    // Right Card: Throttle vs Brake Pedal Response (124x240 @ 338, 48)
    lv_obj_t* pedalCard = lv_obj_create(scr);
    lv_obj_remove_style_all(pedalCard);
    lv_obj_set_size(pedalCard, 124, 240);
    lv_obj_set_pos(pedalCard, 338, 48);
    lv_obj_set_style_bg_color(pedalCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(pedalCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pedalCard, 16, 0);
    lv_obj_set_style_border_color(pedalCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(pedalCard, 1, 0);
    lockNoScroll(pedalCard);

    lv_obj_t* pTag = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(pTag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pTag, C_CHROME, 0);
    lv_label_set_text(pTag, "PEDAL INPUTS");
    lv_obj_align(pTag, LV_ALIGN_TOP_MID, 0, 8);
    lockNoScroll(pTag);

    // Throttle Column (Left: x=16, w=38)
    lv_obj_t* thrLbl = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(thrLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(thrLbl, C_DIM, 0);
    lv_label_set_text(thrLbl, "THR");
    lv_obj_set_pos(thrLbl, 22, 28);
    lockNoScroll(thrLbl);

    trackThrottleBar_ = lv_bar_create(pedalCard);
    lv_obj_remove_style_all(trackThrottleBar_);
    lv_obj_set_size(trackThrottleBar_, 38, 134);
    lv_obj_set_pos(trackThrottleBar_, 16, 48);
    lv_obj_set_style_bg_color(trackThrottleBar_, C_DOT_UNLIT, 0);
    lv_obj_set_style_bg_opa(trackThrottleBar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackThrottleBar_, 6, 0);
    lv_obj_set_style_bg_color(trackThrottleBar_, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(trackThrottleBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(trackThrottleBar_, 6, LV_PART_INDICATOR);
    lv_bar_set_range(trackThrottleBar_, 0, 100);
    lockNoScroll(trackThrottleBar_);

    trackThrottleVal_ = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(trackThrottleVal_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(trackThrottleVal_, C_SPEED, 0);
    lv_label_set_text(trackThrottleVal_, "0%");
    lv_obj_align_to(trackThrottleVal_, trackThrottleBar_, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lockNoScroll(trackThrottleVal_);

    // Brake Column (Right: x=70, w=38)
    lv_obj_t* brkLbl = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(brkLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(brkLbl, C_DIM, 0);
    lv_label_set_text(brkLbl, "BRK");
    lv_obj_set_pos(brkLbl, 76, 28);
    lockNoScroll(brkLbl);

    trackBrakeBar_ = lv_bar_create(pedalCard);
    lv_obj_remove_style_all(trackBrakeBar_);
    lv_obj_set_size(trackBrakeBar_, 38, 134);
    lv_obj_set_pos(trackBrakeBar_, 70, 48);
    lv_obj_set_style_bg_color(trackBrakeBar_, C_DOT_UNLIT, 0);
    lv_obj_set_style_bg_opa(trackBrakeBar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackBrakeBar_, 6, 0);
    lv_obj_set_style_bg_color(trackBrakeBar_, C_WARN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(trackBrakeBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(trackBrakeBar_, 6, LV_PART_INDICATOR);
    lv_bar_set_range(trackBrakeBar_, 0, 100);
    lockNoScroll(trackBrakeBar_);

    trackBrakeVal_ = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(trackBrakeVal_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(trackBrakeVal_, C_SPEED, 0);
    lv_label_set_text(trackBrakeVal_, "0%");
    lv_obj_align_to(trackBrakeVal_, trackBrakeBar_, LV_ALIGN_OUT_BOTTOM_MID, 0, 8);
    lockNoScroll(trackBrakeVal_);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 5 - Trip & Fuel Economy (Option B: Instant MPG, Avg MPG, Range, Dist)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildTripScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TRIP] = addSpeedChip(scr);
    addPageTitle(scr, "FUEL & TRIP");

    // Left Hero Instant MPG Card (260x246 @ 16, 44 - spans full height matching right cards)
    lv_obj_t* heroCard = lv_obj_create(scr);
    lv_obj_remove_style_all(heroCard);
    lv_obj_set_size(heroCard, 260, 246);
    lv_obj_set_pos(heroCard, 16, 44);
    lv_obj_set_style_bg_color(heroCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(heroCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(heroCard, 16, 0);
    lv_obj_set_style_border_color(heroCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(heroCard, 1, 0);
    lockNoScroll(heroCard);

    lv_obj_t* heroTag = lv_label_create(heroCard);
    lv_obj_set_style_text_font(heroTag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(heroTag, C_CHROME, 0);
    lv_label_set_text(heroTag, "INSTANT FUEL ECONOMY");
    lv_obj_align(heroTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(heroTag);

    // Dotted arc segments inside hero card (center cx: 130, cy: 128, radius: 82)
    const uint8_t segCount = 22;
    instantMpgSeg_.count = segCount;
    instantMpgSeg_.wrap = heroCard;
    const float cx = 130.0f;
    const float cy = 128.0f;
    const float r = 82.0f;
    const uint8_t dotSize = 7;

    for (uint8_t i = 0; i < segCount; i++) {
        float angDeg = 135.0f + (float)i * 270.0f / (float)(segCount - 1);
        float rad = angDeg * M5_PI / 180.0f;
        lv_obj_t* dot = lv_obj_create(heroCard);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, dotSize, dotSize);
        lv_obj_set_pos(dot, (int16_t)(cx + r * cosf(rad) - (dotSize / 2)),
                       (int16_t)(cy + r * sinf(rad) - (dotSize / 2)));
        lv_obj_set_style_bg_color(dot, C_DOT_UNLIT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lockNoScroll(dot);
        instantMpgSeg_.dots[i] = dot;
    }

    // Large bold Instant MPG digits (48px Montserrat)
    lv_obj_t* mpgVal = lv_label_create(heroCard);
    lv_obj_set_style_text_font(mpgVal, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(mpgVal, C_SPEED, 0);
    lv_label_set_text(mpgVal, "0.0");
    lv_obj_align(mpgVal, LV_ALIGN_CENTER, 0, -8);
    lockNoScroll(mpgVal);
    instantMpgSeg_.val = mpgVal;

    lv_obj_t* mpgUnit = lv_label_create(heroCard);
    lv_obj_set_style_text_font(mpgUnit, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(mpgUnit, C_CHROME, 0);
    lv_label_set_text(mpgUnit, "INSTANT MPG");
    lv_obj_align(mpgUnit, LV_ALIGN_BOTTOM_MID, 0, -14);
    lockNoScroll(mpgUnit);

    // Right 4-card matrix (86x118 each @ 288, 382; y: 44, 172 - spans y: 44..290)
    struct TripCardDef {
        int16_t x; int16_t y; const char* title; const char* unit;
    };
    TripCardDef defs[4] = {
        {288, 44,  "TRIP AVG", "MPG"},
        {382, 44,  "RANGE",    "MILES"},
        {288, 172, "DIST",     "MILES"},
        {382, 172, "FUEL",     "LEVEL"}
    };

    lv_obj_t** valPtrs[4] = {&tripAvgVal_, &tripRangeVal_, &tripDistVal_, &tripFuelVal_};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 86, 118);
        lv_obj_set_pos(card, defs[i].x, defs[i].y);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lockNoScroll(card);

        lv_obj_t* tag = lv_label_create(card);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, defs[i].title);
        lv_obj_align(tag, LV_ALIGN_TOP_MID, 0, 10);
        lockNoScroll(tag);

        lv_obj_t* val = lv_label_create(card);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(val, C_SPEED, 0);
        lv_label_set_text(val, "--");
        lv_obj_align(val, LV_ALIGN_CENTER, 0, 2);
        lockNoScroll(val);
        *valPtrs[i] = val;

        lv_obj_t* u = lv_label_create(card);
        lv_obj_set_style_text_font(u, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(u, C_DIM, 0);
        lv_label_set_text(u, defs[i].unit);
        lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -10);
        lockNoScroll(u);
    }

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 6 - Diagnostics & DTC Fault Code Scanner (Option D)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagnosticsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_DIAG] = addSpeedChip(scr);
    addPageTitle(scr, "DIAGNOSTICS & DTC");

    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 440, 246);
    lv_obj_set_pos(card, 20, 44);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    // Header status banner
    diagStatus_ = lv_label_create(card);
    lv_obj_set_style_text_font(diagStatus_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagStatus_, C_OK, 0);
    lv_label_set_text(diagStatus_, "vLinker MS BLE • Connected");
    lv_obj_align(diagStatus_, LV_ALIGN_TOP_LEFT, 16, 10);
    lockNoScroll(diagStatus_);

    diagBattery_ = lv_label_create(card);
    setTextFont(diagBattery_);
    lv_obj_set_style_text_color(diagBattery_, C_DIM, 0);
    lv_label_set_text_fmt(diagBattery_, "Battery: 14.2 V");
    lv_obj_align(diagBattery_, LV_ALIGN_TOP_RIGHT, -16, 10);
    lockNoScroll(diagBattery_);

    // DTC Status Box (348x76 @ 16, 36) - Interactive tap opens repair guide
    diagDtcBox_ = lv_obj_create(card);
    lv_obj_remove_style_all(diagDtcBox_);
    lv_obj_set_size(diagDtcBox_, 348, 76);
    lv_obj_set_pos(diagDtcBox_, 16, 36);
    lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(diagDtcBox_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(diagDtcBox_, 10, 0);
    lv_obj_set_style_border_color(diagDtcBox_, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(diagDtcBox_, 1, 0);
    lv_obj_add_event_cb(diagDtcBox_, onDtcActionClick, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(diagDtcBox_, (void*)(uintptr_t)1); // 1 = guide
    lockNoScroll(diagDtcBox_);

    diagDtcIndexLbl_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagDtcIndexLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(diagDtcIndexLbl_, C_ACCENT, 0);
    lv_label_set_text(diagDtcIndexLbl_, "");
    lv_obj_align(diagDtcIndexLbl_, LV_ALIGN_TOP_LEFT, 12, 8);
    lv_obj_remove_flag(diagDtcIndexLbl_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagDtcIndexLbl_);

    diagDtcLbl_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagDtcLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagDtcLbl_, C_OK, 0);
    lv_label_set_text(diagDtcLbl_, "OK: NO FAULT CODES STORED (ECU NORMAL)");
    lv_obj_align(diagDtcLbl_, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_remove_flag(diagDtcLbl_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagDtcLbl_);

    diagHint_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagHint_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(diagHint_, C_DIM, 0);
    lv_label_set_text(diagHint_, "All powertrain and chassis modules reporting zero faults");
    lv_obj_align(diagHint_, LV_ALIGN_BOTTOM_LEFT, 12, -6);
    lv_obj_remove_flag(diagHint_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagHint_);

    // Up / Down cycle buttons (Separate widgets on card at x: 372, completely disjoint from diagDtcBox_)
    diagDtcPrevBtn_ = lv_obj_create(card);
    lv_obj_remove_style_all(diagDtcPrevBtn_);
    lv_obj_set_size(diagDtcPrevBtn_, 52, 35);
    lv_obj_set_pos(diagDtcPrevBtn_, 372, 36);
    lv_obj_set_style_bg_color(diagDtcPrevBtn_, C_PANEL, 0);
    lv_obj_set_style_bg_opa(diagDtcPrevBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(diagDtcPrevBtn_, 8, 0);
    lv_obj_set_style_border_color(diagDtcPrevBtn_, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(diagDtcPrevBtn_, 1, 0);
    lv_obj_set_user_data(diagDtcPrevBtn_, (void*)(intptr_t)-1);
    lv_obj_add_event_cb(diagDtcPrevBtn_, onDtcCycleClick, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(diagDtcPrevBtn_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(diagDtcPrevBtn_);

    lv_obj_t* prevLbl = lv_label_create(diagDtcPrevBtn_);
    lv_obj_set_style_text_font(prevLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(prevLbl, C_TEXT, 0);
    lv_label_set_text(prevLbl, "UP");
    lv_obj_center(prevLbl);
    lv_obj_remove_flag(prevLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(prevLbl);

    diagDtcNextBtn_ = lv_obj_create(card);
    lv_obj_remove_style_all(diagDtcNextBtn_);
    lv_obj_set_size(diagDtcNextBtn_, 52, 35);
    lv_obj_set_pos(diagDtcNextBtn_, 372, 77);
    lv_obj_set_style_bg_color(diagDtcNextBtn_, C_PANEL, 0);
    lv_obj_set_style_bg_opa(diagDtcNextBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(diagDtcNextBtn_, 8, 0);
    lv_obj_set_style_border_color(diagDtcNextBtn_, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(diagDtcNextBtn_, 1, 0);
    lv_obj_set_user_data(diagDtcNextBtn_, (void*)(intptr_t)1);
    lv_obj_add_event_cb(diagDtcNextBtn_, onDtcCycleClick, LV_EVENT_CLICKED, this);
    lv_obj_add_flag(diagDtcNextBtn_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(diagDtcNextBtn_);

    lv_obj_t* nextLbl = lv_label_create(diagDtcNextBtn_);
    lv_obj_set_style_text_font(nextLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(nextLbl, C_TEXT, 0);
    lv_label_set_text(nextLbl, "DN");
    lv_obj_center(nextLbl);
    lv_obj_remove_flag(nextLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(nextLbl);

    // Action buttons row (Scan DTCs & Clear DTCs & Repair Guide)
    lv_obj_t* scanBtn = lv_obj_create(card);
    lv_obj_remove_style_all(scanBtn);
    lv_obj_set_size(scanBtn, 126, 34);
    lv_obj_set_pos(scanBtn, 16, 120);
    lv_obj_set_style_bg_color(scanBtn, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(scanBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scanBtn, 8, 0);
    lv_obj_set_style_border_color(scanBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(scanBtn, 1, 0);
    lv_obj_set_user_data(scanBtn, (void*)(uintptr_t)2); // 2 = scan
    lv_obj_add_event_cb(scanBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(scanBtn);

    lv_obj_t* scanLbl = lv_label_create(scanBtn);
    lv_obj_set_style_text_font(scanLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scanLbl, C_TEXT, 0);
    lv_label_set_text(scanLbl, "READ DTCs");
    lv_obj_center(scanLbl);
    lockNoScroll(scanLbl);

    lv_obj_t* clearBtn = lv_obj_create(card);
    lv_obj_remove_style_all(clearBtn);
    lv_obj_set_size(clearBtn, 126, 34);
    lv_obj_set_pos(clearBtn, 150, 120);
    lv_obj_set_style_bg_color(clearBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(clearBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(clearBtn, 8, 0);
    lv_obj_set_style_border_color(clearBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(clearBtn, 1, 0);
    lv_obj_set_user_data(clearBtn, (void*)(uintptr_t)3); // 3 = clear
    lv_obj_add_event_cb(clearBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(clearBtn);

    lv_obj_t* clearLbl = lv_label_create(clearBtn);
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(clearLbl, C_DIM, 0);
    lv_label_set_text(clearLbl, "CLEAR CODES");
    lv_obj_center(clearLbl);
    lockNoScroll(clearLbl);

    lv_obj_t* guideBtn = lv_obj_create(card);
    lv_obj_remove_style_all(guideBtn);
    lv_obj_set_size(guideBtn, 140, 34);
    lv_obj_set_pos(guideBtn, 284, 120);
    lv_obj_set_style_bg_color(guideBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(guideBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(guideBtn, 8, 0);
    lv_obj_set_style_border_color(guideBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(guideBtn, 1, 0);
    lv_obj_set_user_data(guideBtn, (void*)(uintptr_t)1); // 1 = guide
    lv_obj_add_event_cb(guideBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(guideBtn);

    lv_obj_t* guideLbl = lv_label_create(guideBtn);
    lv_obj_set_style_text_font(guideLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(guideLbl, C_CHROME, 0);
    lv_label_set_text(guideLbl, "REPAIR GUIDE");
    lv_obj_center(guideLbl);
    lockNoScroll(guideLbl);

    // 5 Advanced Diagnostic Sub-Dashboard Launchers (Row at y: 164)
    struct DiagSubBtnDef {
        int16_t x;
        const char* title;
        const char* subtitle;
        uint32_t targetScreen;
    };
    DiagSubBtnDef subDefs[5] = {
        {12,  "FUEL",     "TRIMS & HPFP", SCREEN_DIAG_SUB_FUEL},
        {96,  "MISFIRE",  "CYL & TIMING", SCREEN_DIAG_SUB_CYL},
        {180, "CHASSIS",  "SPEEDS & SAS", SCREEN_DIAG_SUB_CHASSIS},
        {264, "I/M SMOG", "MONITORS",     SCREEN_DIAG_SUB_SMOG},
        {348, "LOGS",     "BLACK BOX",    SCREEN_DIAG_SUB_LOGS}
    };

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t* subBtn = lv_obj_create(card);
        lv_obj_remove_style_all(subBtn);
        lv_obj_set_size(subBtn, 78, 44);
        lv_obj_set_pos(subBtn, subDefs[i].x, 164);
        lv_obj_set_style_bg_color(subBtn, lv_color_hex(0x191A20), 0);
        lv_obj_set_style_bg_opa(subBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(subBtn, 8, 0);
        lv_obj_set_style_border_color(subBtn, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(subBtn, 1, 0);
        lv_obj_set_user_data(subBtn, (void*)(uintptr_t)subDefs[i].targetScreen);
        lv_obj_add_event_cb(subBtn, onSubScreenNavClick, LV_EVENT_CLICKED, this);
        lockNoScroll(subBtn);

        lv_obj_t* titleLbl = lv_label_create(subBtn);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(titleLbl, C_TEXT, 0);
        lv_label_set_text(titleLbl, subDefs[i].title);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_MID, 0, 6);
        lockNoScroll(titleLbl);

        lv_obj_t* subLbl = lv_label_create(subBtn);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(subLbl, C_DIM, 0);
        lv_label_set_text(subLbl, subDefs[i].subtitle);
        lv_obj_align(subLbl, LV_ALIGN_BOTTOM_MID, 0, -6);
        lockNoScroll(subLbl);
    }

    diagError_ = lv_label_create(card);
    setTextFont(diagError_);
    lv_obj_set_style_text_color(diagError_, C_DIM, 0);
    lv_label_set_text_fmt(diagError_, "Bus Status: Healthy");
    lv_obj_align(diagError_, LV_ALIGN_BOTTOM_LEFT, 16, -6);
    lockNoScroll(diagError_);

    lv_obj_t* swipeHint = lv_label_create(card);
    setTextFont(swipeHint);
    lv_obj_set_style_text_color(swipeHint, C_CHROME, 0);
    lv_label_set_text(swipeHint, "Swipe UP/DOWN for Menu");
    lv_obj_align(swipeHint, LV_ALIGN_BOTTOM_RIGHT, -16, -6);
    lockNoScroll(swipeHint);

    // Attach DTC Repair Guide Modal on top of screen 6
    buildDtcRepairModal(scr);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 7 - CMU Home Hub (7 Circular Touch Pods - Accessed via Vertical Swipe)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildMenuScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_MENU] = addSpeedChip(scr);

    // Header title
    lv_obj_t* header = lv_label_create(scr);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, C_CHROME, 0);
    lv_label_set_text(header, "MAZDA CONNECT • MENU HUB");
    lv_obj_align(header, LV_ALIGN_TOP_MID, 20, 10);
    lockNoScroll(header);

    // 7 Circular Pods matching CMU Home carousel
    struct PodConfig {
        const char* emblem;
        const char* title;
        uint32_t targetScreen;
    };
    PodConfig pods[CONTENT_SCREEN_COUNT] = {
        {"MPH",   "SPEED",  SCREEN_SPEED},
        {"PSI",   "TIRES",  SCREEN_TPMS},
        {"RPM",   "ENGINE", SCREEN_RPM},
        {"°F",    "TEMPS",  SCREEN_TEMPS},
        {"0-60",  "TRACK",  SCREEN_TRACK},
        {"MPG",   "TRIP",   SCREEN_TRIP},
        {"OBD",   "DIAG",   SCREEN_DIAG}
    };

    const int16_t podW = 54;
    const int16_t startX = 14;
    const int16_t gap = 12;
    const int16_t podY = 90;

    for (uint8_t i = 0; i < CONTENT_SCREEN_COUNT; i++) {
        int16_t x = startX + i * (podW + gap);

        // Circular Touch Button
        lv_obj_t* btn = lv_obj_create(scr);
        lv_obj_remove_style_all(btn);
        lv_obj_set_size(btn, podW, podW);
        lv_obj_set_pos(btn, x, podY);
        lv_obj_set_style_bg_color(btn, C_PANEL, 0);
        lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(btn, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(btn, (i == 0) ? C_ACCENT : C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(btn, (i == 0) ? 2 : 1, 0);
        lockNoScroll(btn);

        // Store target screen index in user data for click handler
        lv_obj_set_user_data(btn, (void*)(uintptr_t)pods[i].targetScreen);
        lv_obj_add_event_cb(btn, onMenuIconClick, LV_EVENT_CLICKED, this);

        // Center Emblem text
        lv_obj_t* emblem = lv_label_create(btn);
        lv_obj_set_style_text_font(emblem, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(emblem, (i == 0) ? C_ACCENT : C_SPEED, 0);
        lv_label_set_text(emblem, pods[i].emblem);
        lv_obj_center(emblem);
        lockNoScroll(emblem);

        // Title text below pod
        lv_obj_t* lbl = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, (i == 0) ? C_TEXT : C_DIM, 0);
        lv_label_set_text(lbl, pods[i].title);
        lv_obj_set_pos(lbl, x + (podW / 2) - 25, podY + podW + 10);
        lv_obj_set_width(lbl, 50);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lockNoScroll(lbl);

        menuPods_[i] = btn;
    }

    // Status pill at bottom left
    lv_obj_t* pill = lv_obj_create(scr);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 276, 38);
    lv_obj_set_pos(pill, 20, 238);
    lv_obj_set_style_bg_color(pill, C_PANEL, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 12, 0);
    lv_obj_set_style_border_color(pill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lockNoScroll(pill);

    menuStatusLbl_ = lv_label_create(pill);
    lv_obj_set_style_text_font(menuStatusLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(menuStatusLbl_, C_CHROME, 0);
    lv_label_set_text(menuStatusLbl_, "Touch pod to jump • Swipe UP/DOWN");
    lv_obj_center(menuStatusLbl_);
    lockNoScroll(menuStatusLbl_);

    // Settings Button at bottom right
    lv_obj_t* setBtn = lv_obj_create(scr);
    lv_obj_remove_style_all(setBtn);
    lv_obj_set_size(setBtn, 154, 38);
    lv_obj_set_pos(setBtn, 306, 238);
    lv_obj_set_style_bg_color(setBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(setBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(setBtn, 12, 0);
    lv_obj_set_style_border_color(setBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(setBtn, 1, 0);
    lv_obj_set_user_data(setBtn, (void*)(uintptr_t)SCREEN_SETTINGS);
    lv_obj_add_event_cb(setBtn, onMenuIconClick, LV_EVENT_CLICKED, this);
    lockNoScroll(setBtn);

    lv_obj_t* setLbl = lv_label_create(setBtn);
    lv_obj_set_style_text_font(setLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(setLbl, C_TEXT, 0);
    lv_label_set_text(setLbl, "SETTINGS");
    lv_obj_center(setLbl);
    lv_obj_remove_flag(setLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(setLbl);

    return scr;
}

// ---------------------------------------------------------------------------
// Live update pass (runs continuously for ALL screens each frame)
// ---------------------------------------------------------------------------

void Mx5UI::updateSpeedScreen() {
    lv_label_set_text_fmt(speedBigLabel_, "%u", speedU(data_local_.speedKmh));

    char gearBuf[2];
    gearBuf[0] = data_local_.gear;
    gearBuf[1] = '\0';
    lv_label_set_text(gearLbl_, gearBuf);

    // Check critical fault triggers
    bool hasTireWarn = false;
    uint8_t lowTireIdx = 0;
    float lowestPsi = 99.0f;
    for (uint8_t i = 0; i < 4; i++) {
        float psi = pressU(data_local_.tirePressure[i]);
        if (data_local_.tireKnown[i] && psi > 4.0f && psi < 26.0f) {
            hasTireWarn = true;
            if (psi < lowestPsi) {
                lowestPsi = psi;
                lowTireIdx = i;
            }
        }
    }

    bool hasCoolantWarn = (data_local_.coolantC >= 108); // >= 226 F
    bool hasOilWarn = (data_local_.oilTempC >= 122);     // >= 252 F
    bool hasBatWarn = (data_local_.batteryVolts > 0.0f && data_local_.batteryVolts < 11.6f);
    bool hasDtcWarn = (data_local_.dtcCount > 0);

    bool shouldWarn = (hasTireWarn || hasCoolantWarn || hasOilWarn || hasBatWarn || hasDtcWarn) && !warningMutedForDrive_;

    if (shouldWarn) {
        if (speedNormalRightContainer_) lv_obj_add_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
        if (speedWarningContainer_) lv_obj_remove_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);

        if (hasTireWarn) {
            const char* tireNames[4] = {"FL", "FR", "RL", "RR"};
            lv_label_set_text(speedWarnTitle_, "[!] TIRE ALERT • TAP TO CLEAR");
            lv_label_set_text_fmt(speedWarnMainVal_, "%s: %.1f PSI LOW", tireNames[lowTireIdx], lowestPsi);
            lv_label_set_text(speedWarnSubVal_, "Nominal Cold: 29.0 PSI\nInspect tire for puncture");
        } else if (hasCoolantWarn || hasOilWarn) {
            lv_label_set_text(speedWarnTitle_, "[!] OVERHEAT • TAP TO CLEAR");
            lv_label_set_text_fmt(speedWarnMainVal_, "ECT: %d F | OIL: %d F",
                                  tempU(data_local_.coolantC), tempU(data_local_.oilTempC));
            lv_label_set_text(speedWarnSubVal_, "Reduce engine load\nPull over safely if rising");
        } else if (hasBatWarn) {
            lv_label_set_text(speedWarnTitle_, "[!] BATTERY • TAP TO CLEAR");
            lv_label_set_text_fmt(speedWarnMainVal_, "%.1f VOLTS LOW", data_local_.batteryVolts);
            lv_label_set_text(speedWarnSubVal_, "Charging system low\nCheck alternator / belt");
        } else if (hasDtcWarn) {
            lv_label_set_text(speedWarnTitle_, "[!] CHECK ENGINE • TAP");
            const char* code = data_local_.dtcCodes[0];
            lv_label_set_text_fmt(speedWarnMainVal_, "DTC: %s", (code && code[0]) ? code : "FAULT");
            lv_label_set_text(speedWarnSubVal_, "Active fault code\nTap menu to inspect OBD");
        }
    } else {
        if (speedWarningContainer_) lv_obj_add_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
        if (speedNormalRightContainer_) lv_obj_remove_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);

        updateDottedValue(rpmSeg_, data_local_.rpm / 8000.0f);
        if (data_local_.rpm > 6500) warnTopDots(rpmSeg_);
        lv_label_set_text_fmt(rpmSeg_.val, "%d rpm", data_local_.rpm);

        updateDottedValue(fuelSeg_, data_local_.fuelLevelPct / 100.0f);
        lv_label_set_text_fmt(fuelSeg_.val, "%d%%", data_local_.fuelLevelPct);

        updateDottedValue(ambientSeg_, (float)data_local_.ambientC / 50.0f);
        lv_label_set_text_fmt(ambientSeg_.val, "%d %s", tempU(data_local_.ambientC),
                              MX5_UNITS_US ? "°F" : "°C");
    }
}

void Mx5UI::updateMenuScreen() {
    // Highlight the pod corresponding to the active dashboard screen
    for (uint8_t i = 0; i < CONTENT_SCREEN_COUNT; i++) {
        if (menuPods_[i]) {
            bool active = (i == lastContentScreen_);
            lv_obj_set_style_border_color(menuPods_[i], active ? C_ACCENT : C_PANEL_BRD, 0);
            lv_obj_set_style_border_width(menuPods_[i], active ? 2 : 1, 0);
        }
    }

    if (menuStatusLbl_) {
        if (data_local_.connected) {
            lv_label_set_text_fmt(menuStatusLbl_, "vLinker MS BLE Connected • Battery %.1f V",
                                  data_local_.batteryVolts);
            lv_obj_set_style_text_color(menuStatusLbl_, C_OK, 0);
        } else {
            lv_label_set_text(menuStatusLbl_, "Touch icon to jump • Swipe UP/DOWN to return");
            lv_obj_set_style_text_color(menuStatusLbl_, C_CHROME, 0);
        }
    }
}

void Mx5UI::updateRpmScreen() {
    // Center Tachometer
    updateDottedValue(rpmBigSeg_, data_local_.rpm / 8000.0f);
    if (data_local_.rpm > 6500) warnTopDots(rpmBigSeg_);
    lv_label_set_text_fmt(rpmBigSeg_.val, "%d rpm", data_local_.rpm);

    // Mini Dials
    updateDottedValue(loadSeg_, data_local_.engineLoadPct / 100.0f);
    lv_label_set_text_fmt(loadSeg_.val, "%d%%", data_local_.engineLoadPct);

    updateDottedValue(throttleSeg_, data_local_.throttlePct / 100.0f);
    lv_label_set_text_fmt(throttleSeg_.val, "%d%%", data_local_.throttlePct);

    updateDottedValue(fuelArcSeg_, data_local_.fuelLevelPct / 100.0f);
    lv_label_set_text_fmt(fuelArcSeg_.val, "%d%%", data_local_.fuelLevelPct);

    float batFrac = (data_local_.batteryVolts - 10.0f) / 5.0f; // 10V..15V
    updateDottedValue(batArcSeg_, batFrac);
    lv_label_set_text_fmt(batArcSeg_.val, "%.1f V", data_local_.batteryVolts);
}

void Mx5UI::updateEngineScreen() {
    // Coolant (0..120°C -> 0..1)
    float coolFrac = (float)data_local_.coolantC / 120.0f;
    updateDottedValue(coolantSeg_, coolFrac);
    if (data_local_.coolantC > 105) warnTopDots(coolantSeg_);
    lv_label_set_text_fmt(coolantSeg_.val, "%d %s", tempU(data_local_.coolantC),
                          MX5_UNITS_US ? "°F" : "°C");

    // Oil Temp (0..150°C -> 0..1)
    float oilFrac = (float)data_local_.oilTempC / 150.0f;
    updateDottedValue(oilSeg_, oilFrac);
    if (data_local_.oilTempC > 130) warnTopDots(oilSeg_);
    lv_label_set_text_fmt(oilSeg_.val, "%d %s", tempU(data_local_.oilTempC),
                          MX5_UNITS_US ? "°F" : "°C");

    // Intake Air (0..60°C -> 0..1)
    float intakeFrac = (float)data_local_.intakeAirC / 60.0f;
    updateDottedValue(intakeSeg_, intakeFrac);
    lv_label_set_text_fmt(intakeSeg_.val, "%d %s", tempU(data_local_.intakeAirC),
                          MX5_UNITS_US ? "°F" : "°C");

    // Battery Voltage (10V..15V -> 0..1)
    float batFrac = (data_local_.batteryVolts - 10.0f) / 5.0f;
    updateDottedValue(batterySeg_, batFrac);
    lv_label_set_text_fmt(batterySeg_.val, "%.1f V", data_local_.batteryVolts);
}

void Mx5UI::updateTrackScreen() {
    // Update 0-60 timer readout & badge
    lv_label_set_text_fmt(trackTimerLbl_, "%.2f s", data_local_.accel0to60TimeSec);
    lv_label_set_text_fmt(trackBestLbl_, "BEST: %.2f s", data_local_.best0to60TimeSec);

    // HP & Torque live dials
    updateDottedValue(hpSeg_, (float)data_local_.estHorsepower / 200.0f);
    lv_label_set_text_fmt(hpSeg_.val, "%d hp", data_local_.estHorsepower);

    updateDottedValue(torqueSeg_, (float)data_local_.estTorqueFtLb / 200.0f);
    lv_label_set_text_fmt(torqueSeg_.val, "%d lb-ft", data_local_.estTorqueFtLb);

    // Throttle & Brake pedal response bars
    lv_bar_set_value(trackThrottleBar_, data_local_.throttlePct, LV_ANIM_OFF);
    lv_label_set_text_fmt(trackThrottleVal_, "%d%%", data_local_.throttlePct);

    lv_bar_set_value(trackBrakeBar_, data_local_.brakePressurePct, LV_ANIM_OFF);
    lv_label_set_text_fmt(trackBrakeVal_, "%d%%", data_local_.brakePressurePct);
}

void Mx5UI::updateTripScreen() {
    // Instant MPG dial (0..60 MPG)
    updateDottedValue(instantMpgSeg_, data_local_.instantMpg / 60.0f);
    lv_label_set_text_fmt(instantMpgSeg_.val, "%.1f", data_local_.instantMpg);

    lv_label_set_text_fmt(tripAvgVal_, "%.1f", data_local_.tripAvgMpg);
    lv_label_set_text_fmt(tripRangeVal_, "%d", data_local_.rangeMiles);
    lv_label_set_text_fmt(tripDistVal_, "%.1f", data_local_.tripDistanceMiles);
    lv_label_set_text_fmt(tripFuelVal_, "%d%%", data_local_.fuelLevelPct);
}

void Mx5UI::updateTpmsScreen() {
    if (!MX5_TPMS_ENABLED) {
        for (uint8_t i = 0; i < 4; i++) {
            lv_label_set_text(tpmsLabel_[i], "--");
            lv_label_set_text(tpmsTemp_[i], "-- °F");
            if (tpmsDot_[i]) lv_obj_set_style_bg_color(tpmsDot_[i], C_DIM, 0);
        }
        return;
    }
    for (uint8_t i = 0; i < 4; i++) {
        float psi = pressU(data_local_.tirePressure[i]);
        if (data_local_.tireKnown[i] && psi > 4.0f) {
            lv_label_set_text_fmt(tpmsLabel_[i], "%.1f", psi);
            lv_label_set_text_fmt(tpmsTemp_[i], "%d °F", tempU((uint8_t)lroundf(data_local_.tireTemp[i])));
            if (tpmsDot_[i]) {
                if (psi < 22.0f) {
                    lv_obj_set_style_bg_color(tpmsDot_[i], C_DANGER, 0);
                } else if (psi < 26.0f) {
                    lv_obj_set_style_bg_color(tpmsDot_[i], C_WARN, 0);
                } else {
                    lv_obj_set_style_bg_color(tpmsDot_[i], C_OK, 0);
                }
            }
            if (tpmsCard_[i]) {
                if (psi < 26.0f) {
                    lv_obj_set_style_border_color(tpmsCard_[i], C_ACCENT, 0);
                    lv_obj_set_style_bg_color(tpmsCard_[i], lv_color_hex(0x281014), 0);
                } else {
                    lv_obj_set_style_border_color(tpmsCard_[i], C_PANEL_BRD, 0);
                    lv_obj_set_style_bg_color(tpmsCard_[i], C_PANEL, 0);
                }
            }
            if (tpmsLabel_[i]) {
                lv_obj_set_style_text_color(tpmsLabel_[i], (psi < 26.0f) ? C_ACCENT : C_SPEED, 0);
            }
        } else {
            lv_label_set_text(tpmsLabel_[i], "--");
            lv_label_set_text(tpmsTemp_[i], "-- °F");
            if (tpmsDot_[i]) lv_obj_set_style_bg_color(tpmsDot_[i], C_DIM, 0);
            if (tpmsCard_[i]) {
                lv_obj_set_style_border_color(tpmsCard_[i], C_PANEL_BRD, 0);
                lv_obj_set_style_bg_color(tpmsCard_[i], C_PANEL, 0);
            }
            if (tpmsLabel_[i]) {
                lv_obj_set_style_text_color(tpmsLabel_[i], C_SPEED, 0);
            }
        }
    }
}

void Mx5UI::updateDiagnosticsScreen() {
    if (data_local_.connected) {
        lv_obj_set_style_text_color(diagStatus_, C_OK, 0);
        lv_label_set_text(diagStatus_, "vLinker MS BLE • Connected");
    } else {
        lv_obj_set_style_text_color(diagStatus_, C_WARN, 0);
        lv_label_set_text(diagStatus_, "Connecting to vLinker BLE...");
    }
    lv_label_set_text_fmt(diagBattery_, "Battery: %.1f V", data_local_.batteryVolts);
    lv_label_set_text_fmt(diagError_, data_local_.canError ? "CAN Status: Bus Warnings" : "CAN Status: Healthy");

    if (data_local_.dtcCount == 0) {
        currentDtcIndex_ = 0;
        if (diagDtcLbl_) {
            lv_obj_set_style_text_color(diagDtcLbl_, C_OK, 0);
            lv_label_set_text(diagDtcLbl_, "OK: NO FAULT CODES STORED (ECU NORMAL)");
        }
        if (diagHint_) {
            lv_obj_set_style_text_color(diagHint_, C_DIM, 0);
            lv_label_set_text(diagHint_, "All powertrain and chassis modules reporting zero faults");
        }
        if (diagDtcBox_) {
            lv_obj_set_style_border_color(diagDtcBox_, C_PANEL_BRD, 0);
            lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x0E1013), 0);
        }
        if (diagDtcIndexLbl_) lv_label_set_text(diagDtcIndexLbl_, "");
        if (diagDtcPrevBtn_) lv_obj_add_flag(diagDtcPrevBtn_, LV_OBJ_FLAG_HIDDEN);
        if (diagDtcNextBtn_) lv_obj_add_flag(diagDtcNextBtn_, LV_OBJ_FLAG_HIDDEN);
    } else {
        if (currentDtcIndex_ >= data_local_.dtcCount) {
            currentDtcIndex_ = 0;
        }
        const char* code = data_local_.dtcCodes[currentDtcIndex_];
        const char* desc = data_local_.dtcDesc[currentDtcIndex_];
        if (diagDtcLbl_) {
            lv_obj_set_style_text_color(diagDtcLbl_, C_ACCENT, 0);
            lv_label_set_text_fmt(diagDtcLbl_, "FAULT: %s - %s",
                                  (code && strlen(code) > 0) ? code : "P0171",
                                  (desc && strlen(desc) > 0) ? desc : "Diagnostic Fault");
        }
        if (diagHint_) {
            lv_obj_set_style_text_color(diagHint_, C_WARN, 0);
            lv_label_set_text(diagHint_, "Tap code box to open Technical Repair Guide");
        }
        if (diagDtcBox_) {
            lv_obj_set_style_border_color(diagDtcBox_, C_ACCENT, 0);
            lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x281014), 0);
        }

        if (diagDtcIndexLbl_) {
            lv_label_set_text_fmt(diagDtcIndexLbl_, "FAULT %d OF %d • STORED", currentDtcIndex_ + 1, data_local_.dtcCount);
        }

        if (data_local_.dtcCount > 1) {
            if (diagDtcPrevBtn_) lv_obj_remove_flag(diagDtcPrevBtn_, LV_OBJ_FLAG_HIDDEN);
            if (diagDtcNextBtn_) lv_obj_remove_flag(diagDtcNextBtn_, LV_OBJ_FLAG_HIDDEN);
        } else {
            if (diagDtcPrevBtn_) lv_obj_add_flag(diagDtcPrevBtn_, LV_OBJ_FLAG_HIDDEN);
            if (diagDtcNextBtn_) lv_obj_add_flag(diagDtcNextBtn_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------------------
// Diagnostic Sub-Screens Builders & Helpers
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex) {
    addBackgroundLayer(parent);

    // Back Button [ < BACK ] at top left (pos: 14, 8, size: 76, 28)
    lv_obj_t* backBtn = lv_obj_create(parent);
    lv_obj_remove_style_all(backBtn);
    lv_obj_set_size(backBtn, 76, 28);
    lv_obj_set_pos(backBtn, 14, 8);
    lv_obj_set_style_bg_color(backBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(backBtn, 6, 0);
    lv_obj_set_style_border_color(backBtn, C_ACCENT, 0);
    uint8_t backTarget = (subScreenIndex >= SCREEN_BLE_CONFIG && subScreenIndex <= SCREEN_WHEEL_MAP) ? SCREEN_SETTINGS : SCREEN_DIAG;
    lv_obj_set_user_data(backBtn, (void*)(uintptr_t)backTarget);
    lv_obj_add_event_cb(backBtn, onSubScreenNavClick, LV_EVENT_CLICKED, this);
    lockNoScroll(backBtn);

    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(backLbl, C_TEXT, 0);
    lv_label_set_text(backLbl, "< BACK");
    lv_obj_center(backLbl);
    lockNoScroll(backLbl);

    // Title label in center/left
    lv_obj_t* titleLbl = lv_label_create(parent);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(titleLbl, C_CHROME, 0);
    lv_label_set_text(titleLbl, title);
    lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 98, 14);
    lockNoScroll(titleLbl);

    // Speed Chip at top right (pos: 388, 8, size: 78, 28)
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 78, 28);
    lv_obj_set_pos(chip, 388, 8);
    lv_obj_set_style_bg_color(chip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 6, 0);
    lv_obj_set_style_border_color(chip, C_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lockNoScroll(chip);

    lv_obj_t* spdLbl = lv_label_create(chip);
    lv_obj_set_style_text_font(spdLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(spdLbl, C_TEXT, 0);
    lv_label_set_text(spdLbl, "0 MPH");
    lv_obj_center(spdLbl);
    lockNoScroll(spdLbl);

    if (subScreenIndex < SCREEN_COUNT) {
        speedChipLabel_[subScreenIndex] = spdLbl;
    }

    return backBtn;
}

void Mx5UI::buildDtcRepairModal(lv_obj_t* parent) {
    dtcModalCard_ = lv_obj_create(parent);
    lv_obj_remove_style_all(dtcModalCard_);
    lv_obj_set_size(dtcModalCard_, 456, 286);
    lv_obj_set_pos(dtcModalCard_, 12, 16);
    lv_obj_set_style_bg_color(dtcModalCard_, lv_color_hex(0x0A0B0E), 0);
    lv_obj_set_style_bg_opa(dtcModalCard_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dtcModalCard_, 14, 0);
    lv_obj_set_style_border_color(dtcModalCard_, C_ACCENT, 0);
    lv_obj_set_style_border_width(dtcModalCard_, 2, 0);
    lv_obj_add_flag(dtcModalCard_, LV_OBJ_FLAG_HIDDEN); // Hidden by default
    lockNoScroll(dtcModalCard_);

    // Title
    dtcModalTitle_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalTitle_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(dtcModalTitle_, C_TEXT, 0);
    lv_label_set_text(dtcModalTitle_, "DTC REPAIR GUIDE: P0171 - System Too Lean");
    lv_obj_align(dtcModalTitle_, LV_ALIGN_TOP_LEFT, 16, 12);
    lockNoScroll(dtcModalTitle_);

    // Category
    dtcModalCategory_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalCategory_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dtcModalCategory_, C_ACCENT, 0);
    lv_label_set_text(dtcModalCategory_, "CATEGORY: Fuel & Air Metering System");
    lv_obj_align(dtcModalCategory_, LV_ALIGN_TOP_LEFT, 16, 32);
    lockNoScroll(dtcModalCategory_);

    // Meaning
    dtcModalMeaning_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalMeaning_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dtcModalMeaning_, C_SPEED, 0);
    lv_obj_set_width(dtcModalMeaning_, 424);
    lv_label_set_long_mode(dtcModalMeaning_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dtcModalMeaning_, "ECU DIAGNOSIS: Excess oxygen detected in exhaust; fuel trims maxed out.");
    lv_obj_align(dtcModalMeaning_, LV_ALIGN_TOP_LEFT, 16, 50);
    lockNoScroll(dtcModalMeaning_);

    // Checklist
    dtcModalChecklist_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalChecklist_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dtcModalChecklist_, C_CHROME, 0);
    lv_obj_set_width(dtcModalChecklist_, 424);
    lv_label_set_long_mode(dtcModalChecklist_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dtcModalChecklist_, "WHAT TO INSPECT:\n1. Check intake accordion boot for tears.\n2. Inspect PCV hose for vacuum leaks.\n3. Test fuel pressure.");
    lv_obj_align(dtcModalChecklist_, LV_ALIGN_TOP_LEFT, 16, 96);
    lockNoScroll(dtcModalChecklist_);

    // Recommended Fix
    dtcModalRepair_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalRepair_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dtcModalRepair_, C_OK, 0);
    lv_obj_set_width(dtcModalRepair_, 424);
    lv_label_set_long_mode(dtcModalRepair_, LV_LABEL_LONG_WRAP);
    lv_label_set_text(dtcModalRepair_, "RECOMMENDED FIX:\nClean MAF with CRC MAF spray, replace cracked PCV hose or intake boot.");
    lv_obj_align(dtcModalRepair_, LV_ALIGN_TOP_LEFT, 16, 196);
    lockNoScroll(dtcModalRepair_);

    // Close button
    lv_obj_t* closeBtn = lv_obj_create(dtcModalCard_);
    lv_obj_remove_style_all(closeBtn);
    lv_obj_set_size(closeBtn, 150, 32);
    lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_MID, 0, -8);
    lv_obj_set_style_bg_color(closeBtn, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(closeBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(closeBtn, 8, 0);
    lv_obj_set_style_border_color(closeBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(closeBtn, 1, 0);
    lv_obj_add_event_cb(closeBtn, onDtcModalCloseClick, LV_EVENT_CLICKED, this);
    lockNoScroll(closeBtn);

    lv_obj_t* closeLbl = lv_label_create(closeBtn);
    lv_obj_set_style_text_font(closeLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(closeLbl, C_TEXT, 0);
    lv_label_set_text(closeLbl, "CLOSE");
    lv_obj_center(closeLbl);
    lockNoScroll(closeLbl);
    dtcModalCloseLbl_ = closeLbl;
}

void Mx5UI::openDtcGuide(uint8_t index) {
    if (!dtcModalCard_) return;

    if (data_local_.dtcCount == 0) {
        lv_obj_set_style_border_color(dtcModalCard_, C_OK, 0);
        if (dtcModalTitle_) {
            lv_obj_set_style_text_color(dtcModalTitle_, C_OK, 0);
            lv_label_set_text(dtcModalTitle_, "VEHICLE HEALTH: ALL SYSTEMS NORMAL");
        }
        if (dtcModalCategory_) {
            lv_obj_set_style_text_color(dtcModalCategory_, C_OK, 0);
            lv_label_set_text(dtcModalCategory_, "STATUS: All Modules Nominal • Zero DTCs");
        }
        if (dtcModalMeaning_) {
            lv_obj_set_style_text_color(dtcModalMeaning_, C_TEXT, 0);
            lv_label_set_text(dtcModalMeaning_, "Zero active or pending Diagnostic Trouble Codes reported across all modules.");
        }
        if (dtcModalChecklist_) {
            lv_obj_set_style_text_color(dtcModalChecklist_, C_DIM, 0);
            lv_label_set_text(dtcModalChecklist_,
                "SYSTEM SUMMARY:\n"
                "• SkyActiv-G 2.0L Engine Control Module: Nominal / 0 Codes\n"
                "• Dynamic Stability Control & ABS Controller: Nominal / 0 Codes\n"
                "• High-Speed CAN Bus Communications: Active & Healthy\n"
                "• On-Board Emissions Readiness Monitors: Complete & Passed");
        }
        if (dtcModalRepair_) {
            lv_obj_set_style_text_color(dtcModalRepair_, C_DIM, 0);
            lv_label_set_text(dtcModalRepair_, "ACTION: No repairs required. Powertrain operating within factory parameters.");
        }
        if (dtcModalCloseLbl_) {
            lv_label_set_text(dtcModalCloseLbl_, "CLOSE");
            lv_obj_t* btnParent = lv_obj_get_parent(dtcModalCloseLbl_);
            if (btnParent) lv_obj_set_style_bg_color(btnParent, C_OK, 0);
        }
    } else {
        if (index >= data_local_.dtcCount) index = 0;
        const char* targetCode = data_local_.dtcCodes[index];
        DtcInfo info = DtcDatabase::lookup(targetCode);

        lv_obj_set_style_border_color(dtcModalCard_, C_ACCENT, 0);
        if (dtcModalTitle_) {
            lv_obj_set_style_text_color(dtcModalTitle_, C_ACCENT, 0);
            char titleBuf[128];
            snprintf(titleBuf, sizeof(titleBuf), "DTC %s: %s", info.code, info.title);
            lv_label_set_text(dtcModalTitle_, titleBuf);
        }
        if (dtcModalCategory_) {
            lv_obj_set_style_text_color(dtcModalCategory_, C_ACCENT, 0);
            char catBuf[64];
            snprintf(catBuf, sizeof(catBuf), "CATEGORY: %s", info.category);
            lv_label_set_text(dtcModalCategory_, catBuf);
        }
        if (dtcModalMeaning_) {
            lv_obj_set_style_text_color(dtcModalMeaning_, C_SPEED, 0);
            char meanBuf[256];
            snprintf(meanBuf, sizeof(meanBuf), "ECU DIAGNOSIS: %s", info.meaning);
            lv_label_set_text(dtcModalMeaning_, meanBuf);
        }
        if (dtcModalChecklist_) {
            lv_obj_set_style_text_color(dtcModalChecklist_, C_CHROME, 0);
            char checkBuf[384];
            snprintf(checkBuf, sizeof(checkBuf), "WHAT TO INSPECT:\n%s", info.inspection);
            lv_label_set_text(dtcModalChecklist_, checkBuf);
        }
        if (dtcModalRepair_) {
            lv_obj_set_style_text_color(dtcModalRepair_, C_OK, 0);
            char fixBuf[256];
            snprintf(fixBuf, sizeof(fixBuf), "RECOMMENDED FIX:\n%s", info.repair);
            lv_label_set_text(dtcModalRepair_, fixBuf);
        }
        if (dtcModalCloseLbl_) {
            lv_label_set_text(dtcModalCloseLbl_, "CLOSE GUIDE");
            lv_obj_t* btnParent = lv_obj_get_parent(dtcModalCloseLbl_);
            if (btnParent) lv_obj_set_style_bg_color(btnParent, C_ACCENT_DM, 0);
        }
    }

    lv_obj_remove_flag(dtcModalCard_, LV_OBJ_FLAG_HIDDEN);
}

void Mx5UI::showDtcRepairGuide(const char* code) {
    openDtcGuide(currentDtcIndex_);
}

void Mx5UI::hideDtcRepairGuide() {
    if (dtcModalCard_) {
        lv_obj_add_flag(dtcModalCard_, LV_OBJ_FLAG_HIDDEN);
    }
}

// ---------------------------------------------------------------------------
// Sub-Screen 8 - Fuel Trims & High-Pressure Fuel Rail
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagSubFuel() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "FUEL TRIMS & HPFP DIRECT INJECTION", SCREEN_DIAG_SUB_FUEL);

    // Left Dials: STFT & LTFT (116x116 @ 18, 44 & 144, 44)
    stftSeg_ = buildDottedArc(scr, 116, 18, 44, "SHORT TRIM", 14);
    ltftSeg_ = buildDottedArc(scr, 116, 144, 44, "LONG TRIM", 14);

    // Right Card: Wideband AFR & Lambda (194x116 @ 268, 44)
    lv_obj_t* afrCard = lv_obj_create(scr);
    lv_obj_remove_style_all(afrCard);
    lv_obj_set_size(afrCard, 194, 116);
    lv_obj_set_pos(afrCard, 268, 44);
    lv_obj_set_style_bg_color(afrCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(afrCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(afrCard, 16, 0);
    lv_obj_set_style_border_color(afrCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(afrCard, 1, 0);
    lockNoScroll(afrCard);

    lv_obj_t* afrTag = lv_label_create(afrCard);
    lv_obj_set_style_text_font(afrTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(afrTag, C_CHROME, 0);
    lv_label_set_text(afrTag, "WIDEBAND AIR/FUEL");
    lv_obj_align(afrTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(afrTag);

    diagAfrVal_ = lv_label_create(afrCard);
    lv_obj_set_style_text_font(diagAfrVal_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(diagAfrVal_, C_SPEED, 0);
    lv_label_set_text(diagAfrVal_, "14.7 : 1");
    lv_obj_align(diagAfrVal_, LV_ALIGN_LEFT_MID, 14, 0);
    lockNoScroll(diagAfrVal_);

    lv_obj_t* afrSub = lv_label_create(afrCard);
    lv_obj_set_style_text_font(afrSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(afrSub, C_DIM, 0);
    lv_label_set_text(afrSub, "Target: 14.70 (Stoich AFR)");
    lv_obj_align(afrSub, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(afrSub);

    // Bottom Left Card: DI High-Pressure Fuel Rail (242x118 @ 18, 168)
    lv_obj_t* hpfpCard = lv_obj_create(scr);
    lv_obj_remove_style_all(hpfpCard);
    lv_obj_set_size(hpfpCard, 242, 118);
    lv_obj_set_pos(hpfpCard, 18, 168);
    lv_obj_set_style_bg_color(hpfpCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(hpfpCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hpfpCard, 16, 0);
    lv_obj_set_style_border_color(hpfpCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(hpfpCard, 1, 0);
    lockNoScroll(hpfpCard);

    lv_obj_t* hpfpTag = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(hpfpTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hpfpTag, C_CHROME, 0);
    lv_label_set_text(hpfpTag, "DI HIGH-PRESSURE RAIL (HPFP)");
    lv_obj_align(hpfpTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(hpfpTag);

    diagHpfpVal_ = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(diagHpfpVal_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(diagHpfpVal_, C_SPEED, 0);
    lv_label_set_text(diagHpfpVal_, "1,850 PSI");
    lv_obj_align(diagHpfpVal_, LV_ALIGN_LEFT_MID, 14, 0);
    lockNoScroll(diagHpfpVal_);

    lv_obj_t* hpfpSub = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(hpfpSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hpfpSub, C_DIM, 0);
    lv_label_set_text(hpfpSub, "Skyactiv-G 2.0L Direct Injection");
    lv_obj_align(hpfpSub, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(hpfpSub);

    // Bottom Right Card: EVAP Vapor System (194x118 @ 268, 168)
    lv_obj_t* evapCard = lv_obj_create(scr);
    lv_obj_remove_style_all(evapCard);
    lv_obj_set_size(evapCard, 194, 118);
    lv_obj_set_pos(evapCard, 268, 168);
    lv_obj_set_style_bg_color(evapCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(evapCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(evapCard, 16, 0);
    lv_obj_set_style_border_color(evapCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(evapCard, 1, 0);
    lockNoScroll(evapCard);

    lv_obj_t* evapTag = lv_label_create(evapCard);
    lv_obj_set_style_text_font(evapTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(evapTag, C_CHROME, 0);
    lv_label_set_text(evapTag, "EVAP TANK VAPOR");
    lv_obj_align(evapTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(evapTag);

    diagEvapVal_ = lv_label_create(evapCard);
    lv_obj_set_style_text_font(diagEvapVal_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(diagEvapVal_, C_SPEED, 0);
    lv_label_set_text(diagEvapVal_, "+12 Pa");
    lv_obj_align(diagEvapVal_, LV_ALIGN_LEFT_MID, 14, 0);
    lockNoScroll(diagEvapVal_);

    lv_obj_t* evapSub = lv_label_create(evapCard);
    lv_obj_set_style_text_font(evapSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(evapSub, C_DIM, 0);
    lv_label_set_text(evapSub, "Purge Valve: 18% Duty");
    lv_obj_align(evapSub, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(evapSub);

    return scr;
}

void Mx5UI::updateDiagSubFuel() {
    // STFT (-25%..+25% -> 0..1, center 0.5)
    float stftFrac = (data_local_.shortTermFuelTrimPct + 25.0f) / 50.0f;
    updateDottedValue(stftSeg_, stftFrac);
    lv_label_set_text_fmt(stftSeg_.val, "%+.1f%%", data_local_.shortTermFuelTrimPct);

    // LTFT (-25%..+25% -> 0..1, center 0.5)
    float ltftFrac = (data_local_.longTermFuelTrimPct + 25.0f) / 50.0f;
    updateDottedValue(ltftSeg_, ltftFrac);
    lv_label_set_text_fmt(ltftSeg_.val, "%+.1f%%", data_local_.longTermFuelTrimPct);

    // AFR
    lv_label_set_text_fmt(diagAfrVal_, "%.1f : 1", data_local_.airFuelRatio);

    // HPFP Rail Pressure
    lv_label_set_text_fmt(diagHpfpVal_, "%d PSI", data_local_.fuelRailPressurePsi);

    // EVAP
    lv_label_set_text_fmt(diagEvapVal_, "%+d Pa", data_local_.evapVaporPa);
}

// ---------------------------------------------------------------------------
// Sub-Screen 9 - Cylinder Misfires & Ignition Timing (Mode $06)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagSubCyl() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "CYLINDER MISFIRE & IGNITION TIMING", SCREEN_DIAG_SUB_CYL);

    // Left Card: 4-Cylinder Misfire Monitor (228x242 @ 18, 44)
    lv_obj_t* misCard = lv_obj_create(scr);
    lv_obj_remove_style_all(misCard);
    lv_obj_set_size(misCard, 228, 242);
    lv_obj_set_pos(misCard, 18, 44);
    lv_obj_set_style_bg_color(misCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(misCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(misCard, 16, 0);
    lv_obj_set_style_border_color(misCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(misCard, 1, 0);
    lockNoScroll(misCard);

    lv_obj_t* misTag = lv_label_create(misCard);
    lv_obj_set_style_text_font(misTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(misTag, C_CHROME, 0);
    lv_label_set_text(misTag, "CYLINDER MISFIRE (MODE $06)");
    lv_obj_align(misTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(misTag);

    for (uint8_t c = 0; c < 4; c++) {
        int16_t rowY = 36 + c * 48;

        lv_obj_t* cylLbl = lv_label_create(misCard);
        lv_obj_set_style_text_font(cylLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cylLbl, C_TEXT, 0);
        lv_label_set_text_fmt(cylLbl, "CYLINDER %d", c + 1);
        lv_obj_set_pos(cylLbl, 14, rowY);
        lockNoScroll(cylLbl);

        misfireCountLbl_[c] = lv_label_create(misCard);
        lv_obj_set_style_text_font(misfireCountLbl_[c], &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(misfireCountLbl_[c], C_OK, 0);
        lv_label_set_text(misfireCountLbl_[c], "0 misfires");
        lv_obj_align(misfireCountLbl_[c], LV_ALIGN_TOP_RIGHT, -14, rowY);
        lockNoScroll(misfireCountLbl_[c]);

        misfireBar_[c] = lv_bar_create(misCard);
        lv_obj_remove_style_all(misfireBar_[c]);
        lv_obj_set_size(misfireBar_[c], 200, 6);
        lv_obj_set_pos(misfireBar_[c], 14, rowY + 20);
        lv_obj_set_style_bg_color(misfireBar_[c], C_PANEL_BRD, 0);
        lv_obj_set_style_bg_opa(misfireBar_[c], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(misfireBar_[c], 3, 0);
        lv_obj_set_style_bg_color(misfireBar_[c], C_OK, LV_PART_INDICATOR);
        lv_obj_set_style_bg_opa(misfireBar_[c], LV_OPA_COVER, LV_PART_INDICATOR);
        lv_bar_set_range(misfireBar_[c], 0, 50);
        lv_bar_set_value(misfireBar_[c], 0, LV_ANIM_OFF);
        lockNoScroll(misfireBar_[c]);
    }

    // Right Top Card: Spark Timing & Knock Retard (208x116 @ 254, 44)
    lv_obj_t* sparkCard = lv_obj_create(scr);
    lv_obj_remove_style_all(sparkCard);
    lv_obj_set_size(sparkCard, 208, 116);
    lv_obj_set_pos(sparkCard, 254, 44);
    lv_obj_set_style_bg_color(sparkCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(sparkCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sparkCard, 16, 0);
    lv_obj_set_style_border_color(sparkCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(sparkCard, 1, 0);
    lockNoScroll(sparkCard);

    lv_obj_t* spkTag = lv_label_create(sparkCard);
    lv_obj_set_style_text_font(spkTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(spkTag, C_CHROME, 0);
    lv_label_set_text(spkTag, "SPARK ADVANCE / KNOCK");
    lv_obj_align(spkTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(spkTag);

    diagSparkVal_ = lv_label_create(sparkCard);
    lv_obj_set_style_text_font(diagSparkVal_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(diagSparkVal_, C_SPEED, 0);
    lv_label_set_text(diagSparkVal_, "16.5° BTDC");
    lv_obj_align(diagSparkVal_, LV_ALIGN_LEFT_MID, 14, -6);
    lockNoScroll(diagSparkVal_);

    diagKnockVal_ = lv_label_create(sparkCard);
    lv_obj_set_style_text_font(diagKnockVal_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(diagKnockVal_, C_OK, 0);
    lv_label_set_text(diagKnockVal_, "Knock Retard: 0.0° (NORMAL)");
    lv_obj_align(diagKnockVal_, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(diagKnockVal_);

    // Right Bottom Card: Dual VVT Camshaft Angles (208x118 @ 254, 168)
    lv_obj_t* vvtCard = lv_obj_create(scr);
    lv_obj_remove_style_all(vvtCard);
    lv_obj_set_size(vvtCard, 208, 118);
    lv_obj_set_pos(vvtCard, 254, 168);
    lv_obj_set_style_bg_color(vvtCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(vvtCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(vvtCard, 16, 0);
    lv_obj_set_style_border_color(vvtCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(vvtCard, 1, 0);
    lockNoScroll(vvtCard);

    lv_obj_t* vvtTag = lv_label_create(vvtCard);
    lv_obj_set_style_text_font(vvtTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(vvtTag, C_CHROME, 0);
    lv_label_set_text(vvtTag, "DUAL VVT CAM PHASING");
    lv_obj_align(vvtTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(vvtTag);

    diagVvtInVal_ = lv_label_create(vvtCard);
    lv_obj_set_style_text_font(diagVvtInVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagVvtInVal_, C_SPEED, 0);
    lv_label_set_text(diagVvtInVal_, "Intake VVT: +18.0°");
    lv_obj_align(diagVvtInVal_, LV_ALIGN_LEFT_MID, 14, -6);
    lockNoScroll(diagVvtInVal_);

    diagVvtExVal_ = lv_label_create(vvtCard);
    lv_obj_set_style_text_font(diagVvtExVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagVvtExVal_, C_SPEED, 0);
    lv_label_set_text(diagVvtExVal_, "Exhaust VVT: +6.5°");
    lv_obj_align(diagVvtExVal_, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(diagVvtExVal_);

    return scr;
}

void Mx5UI::updateDiagSubCyl() {
    for (uint8_t c = 0; c < 4; c++) {
        uint16_t cnt = data_local_.cylMisfireCount[c];
        lv_label_set_text_fmt(misfireCountLbl_[c], "%d misfires", cnt);
        lv_obj_set_style_text_color(misfireCountLbl_[c], cnt == 0 ? C_OK : C_WARN, 0);
        lv_bar_set_value(misfireBar_[c], cnt, LV_ANIM_OFF);
        lv_obj_set_style_bg_color(misfireBar_[c], cnt == 0 ? C_OK : C_WARN, LV_PART_INDICATOR);
    }

    lv_label_set_text_fmt(diagSparkVal_, "%.1f° BTDC", data_local_.sparkAdvanceDeg);
    if (data_local_.knockRetardDeg > 0.1f) {
        lv_obj_set_style_text_color(diagKnockVal_, C_WARN, 0);
        lv_label_set_text_fmt(diagKnockVal_, "Knock Retard: -%.1f° (PULLING)", data_local_.knockRetardDeg);
    } else {
        lv_obj_set_style_text_color(diagKnockVal_, C_OK, 0);
        lv_label_set_text(diagKnockVal_, "Knock Retard: 0.0° (NORMAL)");
    }

    lv_label_set_text_fmt(diagVvtInVal_, "Intake VVT: %+.1f°", data_local_.vvtIntakeDeg);
    lv_label_set_text_fmt(diagVvtExVal_, "Exhaust VVT: %+.1f°", data_local_.vvtExhaustDeg);
}

// ---------------------------------------------------------------------------
// Sub-Screen 10 - Chassis, ABS & Transmission
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagSubChassis() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "CHASSIS, SPEEDS & TRANSMISSION", SCREEN_DIAG_SUB_CHASSIS);

    // Left Card: 4-Wheel Speed Matrix (228x242 @ 18, 44)
    lv_obj_t* wssCard = lv_obj_create(scr);
    lv_obj_remove_style_all(wssCard);
    lv_obj_set_size(wssCard, 228, 242);
    lv_obj_set_pos(wssCard, 18, 44);
    lv_obj_set_style_bg_color(wssCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(wssCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wssCard, 16, 0);
    lv_obj_set_style_border_color(wssCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(wssCard, 1, 0);
    lockNoScroll(wssCard);

    lv_obj_t* wssTag = lv_label_create(wssCard);
    lv_obj_set_style_text_font(wssTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wssTag, C_CHROME, 0);
    lv_label_set_text(wssTag, "4-WHEEL SPEED SENSORS (ABS)");
    lv_obj_align(wssTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(wssTag);

    const char* posTags[4] = {"FRONT-LEFT", "FRONT-RIGHT", "REAR-LEFT", "REAR-RIGHT"};
    int16_t posGrid[4][2] = {{14, 38}, {120, 38}, {14, 134}, {120, 134}};

    for (uint8_t w = 0; w < 4; w++) {
        lv_obj_t* pod = lv_obj_create(wssCard);
        lv_obj_remove_style_all(pod);
        lv_obj_set_size(pod, 94, 82);
        lv_obj_set_pos(pod, posGrid[w][0], posGrid[w][1]);
        lv_obj_set_style_bg_color(pod, lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(pod, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pod, 10, 0);
        lv_obj_set_style_border_color(pod, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(pod, 1, 0);
        lockNoScroll(pod);

        lv_obj_t* tag = lv_label_create(pod);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(tag, C_DIM, 0);
        lv_label_set_text(tag, posTags[w]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 8, 6);
        lockNoScroll(tag);

        wheelSpeedLbl_[w] = lv_label_create(pod);
        lv_obj_set_style_text_font(wheelSpeedLbl_[w], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(wheelSpeedLbl_[w], C_SPEED, 0);
        lv_label_set_text(wheelSpeedLbl_[w], "--");
        lv_obj_align(wheelSpeedLbl_[w], LV_ALIGN_CENTER, 0, 4);
        lockNoScroll(wheelSpeedLbl_[w]);
    }

    // Right Top Card: Steering Angle Sensor (SAS) (208x116 @ 254, 44)
    lv_obj_t* sasCard = lv_obj_create(scr);
    lv_obj_remove_style_all(sasCard);
    lv_obj_set_size(sasCard, 208, 116);
    lv_obj_set_pos(sasCard, 254, 44);
    lv_obj_set_style_bg_color(sasCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(sasCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sasCard, 16, 0);
    lv_obj_set_style_border_color(sasCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(sasCard, 1, 0);
    lockNoScroll(sasCard);

    lv_obj_t* sasTag = lv_label_create(sasCard);
    lv_obj_set_style_text_font(sasTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sasTag, C_CHROME, 0);
    lv_label_set_text(sasTag, "STEERING ANGLE SENSOR (SAS)");
    lv_obj_align(sasTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(sasTag);

    diagSasVal_ = lv_label_create(sasCard);
    lv_obj_set_style_text_font(diagSasVal_, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(diagSasVal_, C_SPEED, 0);
    lv_label_set_text(diagSasVal_, "+0.0°");
    lv_obj_align(diagSasVal_, LV_ALIGN_LEFT_MID, 14, 0);
    lockNoScroll(diagSasVal_);

    lv_obj_t* sasSub = lv_label_create(sasCard);
    lv_obj_set_style_text_font(sasSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sasSub, C_DIM, 0);
    lv_label_set_text(sasSub, "Center Alignment: Valid");
    lv_obj_align(sasSub, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(sasSub);

    // Right Bottom Card: 6AT Transmission Health (208x118 @ 254, 168)
    lv_obj_t* transCard = lv_obj_create(scr);
    lv_obj_remove_style_all(transCard);
    lv_obj_set_size(transCard, 208, 118);
    lv_obj_set_pos(transCard, 254, 168);
    lv_obj_set_style_bg_color(transCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(transCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(transCard, 16, 0);
    lv_obj_set_style_border_color(transCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(transCard, 1, 0);
    lockNoScroll(transCard);

    lv_obj_t* transTag = lv_label_create(transCard);
    lv_obj_set_style_text_font(transTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(transTag, C_CHROME, 0);
    lv_label_set_text(transTag, "6AT TRANSMISSION HEALTH");
    lv_obj_align(transTag, LV_ALIGN_TOP_LEFT, 14, 10);
    lockNoScroll(transTag);

    diagTransTempVal_ = lv_label_create(transCard);
    lv_obj_set_style_text_font(diagTransTempVal_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(diagTransTempVal_, C_SPEED, 0);
    lv_label_set_text(diagTransTempVal_, "ATF Temp: 172 °F");
    lv_obj_align(diagTransTempVal_, LV_ALIGN_LEFT_MID, 14, -6);
    lockNoScroll(diagTransTempVal_);

    diagTccSlipVal_ = lv_label_create(transCard);
    lv_obj_set_style_text_font(diagTccSlipVal_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(diagTccSlipVal_, C_OK, 0);
    lv_label_set_text(diagTccSlipVal_, "TCC Slip: 0 RPM (LOCKED)");
    lv_obj_align(diagTccSlipVal_, LV_ALIGN_BOTTOM_LEFT, 14, -10);
    lockNoScroll(diagTccSlipVal_);

    return scr;
}

void Mx5UI::updateDiagSubChassis() {
    const char* spdUnit = MX5_UNITS_US ? "MPH" : "km/h";
    for (uint8_t w = 0; w < 4; w++) {
        uint8_t s = speedU((uint8_t)lroundf(data_local_.wheelSpeedKmh[w]));
        lv_label_set_text_fmt(wheelSpeedLbl_[w], "%d %s", s, spdUnit);
    }

    lv_label_set_text_fmt(diagSasVal_, "%+.1f°", data_local_.steeringAngleDeg);
    lv_label_set_text_fmt(diagTransTempVal_, "ATF Temp: %d %s",
                          tempU(data_local_.transFluidTempC),
                          MX5_UNITS_US ? "°F" : "°C");
    lv_label_set_text_fmt(diagTccSlipVal_, "TCC Slip: %d RPM %s",
                          data_local_.tccSlipRpm,
                          data_local_.tccSlipRpm < 20 ? "(LOCKED)" : "(SLIPPING)");
}

// ---------------------------------------------------------------------------
// Sub-Screen 11 - I/M Smog Readiness Monitors
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagSubSmog() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "I/M SMOG READINESS MONITORS", SCREEN_DIAG_SUB_SMOG);

    // Summary Card at top (444x40 @ 18, 44)
    lv_obj_t* sumCard = lv_obj_create(scr);
    lv_obj_remove_style_all(sumCard);
    lv_obj_set_size(sumCard, 444, 40);
    lv_obj_set_pos(sumCard, 18, 44);
    lv_obj_set_style_bg_color(sumCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(sumCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sumCard, 10, 0);
    lv_obj_set_style_border_color(sumCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(sumCard, 1, 0);
    lockNoScroll(sumCard);

    smogSummaryLbl_ = lv_label_create(sumCard);
    lv_obj_set_style_text_font(smogSummaryLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(smogSummaryLbl_, C_OK, 0);
    lv_label_set_text(smogSummaryLbl_, "ALL 8 MONITORS COMPLETE • SMOG READY");
    lv_obj_center(smogSummaryLbl_);
    lockNoScroll(smogSummaryLbl_);

    // 8 Monitor Pods (4 columns x 2 rows @ y: 92 and y: 190)
    struct SmogPodDef {
        const char* code;
        const char* name;
    };
    SmogPodDef smogDefs[8] = {
        {"MIS",  "MISFIRE"},
        {"FUEL", "FUEL SYS"},
        {"CCM",  "COMPONENTS"},
        {"CAT",  "CATALYST"},
        {"EVAP", "EVAPORATIVE"},
        {"O2S",  "O2 SENSOR"},
        {"HTR",  "O2 HEATER"},
        {"VVT",  "VVT / EGR"}
    };

    for (uint8_t m = 0; m < 8; m++) {
        uint8_t col = m % 4;
        uint8_t row = m / 4;
        int16_t x = 18 + col * (104 + 9);
        int16_t y = 92 + row * (92 + 6);

        lv_obj_t* pod = lv_obj_create(scr);
        lv_obj_remove_style_all(pod);
        lv_obj_set_size(pod, 104, 92);
        lv_obj_set_pos(pod, x, y);
        lv_obj_set_style_bg_color(pod, C_PANEL, 0);
        lv_obj_set_style_bg_opa(pod, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(pod, 12, 0);
        lv_obj_set_style_border_color(pod, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(pod, 1, 0);
        lockNoScroll(pod);

        // Status LED dot
        smogPodDot_[m] = lv_obj_create(pod);
        lv_obj_remove_style_all(smogPodDot_[m]);
        lv_obj_set_size(smogPodDot_[m], 10, 10);
        lv_obj_set_pos(smogPodDot_[m], 10, 10);
        lv_obj_set_style_bg_color(smogPodDot_[m], C_OK, 0);
        lv_obj_set_style_bg_opa(smogPodDot_[m], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(smogPodDot_[m], LV_RADIUS_CIRCLE, 0);
        lockNoScroll(smogPodDot_[m]);

        lv_obj_t* codeLbl = lv_label_create(pod);
        lv_obj_set_style_text_font(codeLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(codeLbl, C_TEXT, 0);
        lv_label_set_text(codeLbl, smogDefs[m].code);
        lv_obj_align(codeLbl, LV_ALIGN_TOP_RIGHT, -10, 8);
        lockNoScroll(codeLbl);

        lv_obj_t* nameLbl = lv_label_create(pod);
        lv_obj_set_style_text_font(nameLbl, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(nameLbl, C_CHROME, 0);
        lv_label_set_text(nameLbl, smogDefs[m].name);
        lv_obj_align(nameLbl, LV_ALIGN_LEFT_MID, 10, 4);
        lockNoScroll(nameLbl);

        smogPodLbl_[m] = lv_label_create(pod);
        lv_obj_set_style_text_font(smogPodLbl_[m], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(smogPodLbl_[m], C_OK, 0);
        lv_label_set_text(smogPodLbl_[m], "READY");
        lv_obj_align(smogPodLbl_[m], LV_ALIGN_BOTTOM_LEFT, 10, -8);
        lockNoScroll(smogPodLbl_[m]);
    }

    return scr;
}

void Mx5UI::updateDiagSubSmog() {
    bool ready[8] = {
        data_local_.imMisfireReady,
        data_local_.imFuelReady,
        data_local_.imCompReady,
        data_local_.imCatReady,
        data_local_.imEvapReady,
        data_local_.imO2Ready,
        data_local_.imO2HeaterReady,
        data_local_.imEgrVvtReady
    };

    uint8_t readyCount = 0;
    for (uint8_t m = 0; m < 8; m++) {
        if (ready[m]) readyCount++;
        lv_obj_set_style_bg_color(smogPodDot_[m], ready[m] ? C_OK : C_WARN, 0);
        lv_obj_set_style_text_color(smogPodLbl_[m], ready[m] ? C_OK : C_WARN, 0);
        lv_label_set_text(smogPodLbl_[m], ready[m] ? "READY" : "NOT READY");
    }

    if (readyCount == 8) {
        lv_obj_set_style_text_color(smogSummaryLbl_, C_OK, 0);
        lv_label_set_text(smogSummaryLbl_, "ALL 8 MONITORS COMPLETE • SMOG READY");
    } else {
        lv_obj_set_style_text_color(smogSummaryLbl_, C_WARN, 0);
        lv_label_set_text_fmt(smogSummaryLbl_, "DRIVE CYCLE INCOMPLETE: %d / 8 READY", readyCount);
    }
}

// ---------------------------------------------------------------------------
// Event Callbacks
// ---------------------------------------------------------------------------

void Mx5UI::onSubScreenNavClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_target(e);
    uintptr_t targetScreen = (uintptr_t)lv_obj_get_user_data(targetObj);
    ui->setScreen((uint8_t)targetScreen);
}

void Mx5UI::onDtcModalCloseClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (ui) ui->hideDtcRepairGuide();
}

void Mx5UI::onDtcCycleClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui || ui->data_local_.dtcCount <= 1) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    intptr_t delta = (intptr_t)lv_obj_get_user_data(targetObj);

    if (delta < 0) {
        ui->currentDtcIndex_ = (ui->currentDtcIndex_ == 0) ? (ui->data_local_.dtcCount - 1) : (ui->currentDtcIndex_ - 1);
    } else {
        ui->currentDtcIndex_ = (ui->currentDtcIndex_ + 1) % ui->data_local_.dtcCount;
    }
    ui->updateDiagnosticsScreen();
}

void Mx5UI::onDtcActionClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action == 1) {
        // Open DTC Repair Guide or Vehicle Health Inspection
        ui->openDtcGuide(ui->currentDtcIndex_);
    } else if (action == 2) {
        // Read DTCs action
        if (ui->diagDtcLbl_) {
            lv_label_set_text(ui->diagDtcLbl_, "Querying ECU Mode 03 / 07 DTCs...");
        }
    } else if (action == 3) {
        // Clear DTCs action
        ui->data_local_.dtcCount = 0;
        ui->currentDtcIndex_ = 0;
        memset(ui->data_local_.dtcCodes, 0, sizeof(ui->data_local_.dtcCodes));
        memset(ui->data_local_.dtcDesc, 0, sizeof(ui->data_local_.dtcDesc));
        ui->updateDiagnosticsScreen();
    }
}

// ---------------------------------------------------------------------------
// Sub-Screen 12 - Incident Logs & Telemetry History Charts
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildDiagSubLogs() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "INCIDENT LOGS & TELEMETRY CHARTS", SCREEN_DIAG_SUB_LOGS);

    // Left Panel: Incident List & SD Status (140x260 @ 14, 44)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 140, 260);
    lv_obj_set_pos(leftCard, 14, 44);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 14, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    lv_obj_t* incTag = lv_label_create(leftCard);
    lv_obj_set_style_text_font(incTag, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(incTag, C_CHROME, 0);
    lv_label_set_text(incTag, "RECORDED INCIDENTS");
    lv_obj_align(incTag, LV_ALIGN_TOP_LEFT, 10, 10);
    lockNoScroll(incTag);

    sdCardStatusLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(sdCardStatusLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(sdCardStatusLbl_, C_OK, 0);
    lv_label_set_text(sdCardStatusLbl_, "SD: READY (32 GB)");
    lv_obj_align(sdCardStatusLbl_, LV_ALIGN_TOP_LEFT, 10, 26);
    lockNoScroll(sdCardStatusLbl_);

    const char* incNames[3] = {"1. P0171 LEAN", "2. KNOCK RETARD", "3. CYL 1 MISFIRE"};
    const char* incTimes[3] = {"09/02 11:24", "09/02 11:38", "09/02 11:42"};

    for (uint8_t i = 0; i < 3; i++) {
        incidentItems_[i] = lv_obj_create(leftCard);
        lv_obj_remove_style_all(incidentItems_[i]);
        lv_obj_set_size(incidentItems_[i], 122, 46);
        lv_obj_set_pos(incidentItems_[i], 9, 48 + i * 52);
        lv_obj_set_style_bg_color(incidentItems_[i], (i == 0) ? C_ACCENT_DM : lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(incidentItems_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(incidentItems_[i], 8, 0);
        lv_obj_set_style_border_color(incidentItems_[i], (i == 0) ? C_ACCENT : C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(incidentItems_[i], 1, 0);
        lv_obj_set_user_data(incidentItems_[i], (void*)(uintptr_t)i);
        lv_obj_add_event_cb(incidentItems_[i], onIncidentSelectClick, LV_EVENT_CLICKED, this);
        lockNoScroll(incidentItems_[i]);

        lv_obj_t* tLbl = lv_label_create(incidentItems_[i]);
        lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(tLbl, C_TEXT, 0);
        lv_label_set_text(tLbl, incNames[i]);
        lv_obj_align(tLbl, LV_ALIGN_TOP_LEFT, 6, 6);
        lv_obj_remove_flag(tLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(tLbl);

        lv_obj_t* dLbl = lv_label_create(incidentItems_[i]);
        lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(dLbl, C_DIM, 0);
        lv_label_set_text(dLbl, incTimes[i]);
        lv_obj_align(dLbl, LV_ALIGN_BOTTOM_LEFT, 6, -6);
        lv_obj_remove_flag(dLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(dLbl);
    }

    // Format SD button at bottom of left panel
    sdFormatBtn_ = lv_obj_create(leftCard);
    lv_obj_remove_style_all(sdFormatBtn_);
    lv_obj_set_size(sdFormatBtn_, 122, 32);
    lv_obj_set_pos(sdFormatBtn_, 9, 214);
    lv_obj_set_style_bg_color(sdFormatBtn_, lv_color_hex(0x191A20), 0);
    lv_obj_set_style_bg_opa(sdFormatBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(sdFormatBtn_, 6, 0);
    lv_obj_set_style_border_color(sdFormatBtn_, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(sdFormatBtn_, 1, 0);
    lv_obj_set_user_data(sdFormatBtn_, (void*)(uintptr_t)2); // 2 = request format modal
    lv_obj_add_event_cb(sdFormatBtn_, onSdFormatClick, LV_EVENT_CLICKED, this);
    lockNoScroll(sdFormatBtn_);

    lv_obj_t* fmtLbl = lv_label_create(sdFormatBtn_);
    lv_obj_set_style_text_font(fmtLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fmtLbl, C_WARN, 0);
    lv_label_set_text(fmtLbl, "FORMAT SD");
    lv_obj_center(fmtLbl);
    lv_obj_remove_flag(fmtLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(fmtLbl);

    // Right Panel: Telemetry Line Chart & Metrics (308x260 @ 158, 44)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 308, 260);
    lv_obj_set_pos(rightCard, 158, 44);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 14, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    incidentTitleLbl_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(incidentTitleLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(incidentTitleLbl_, C_TEXT, 0);
    lv_label_set_text(incidentTitleLbl_, "P0171 LEAN EXHAUST SPIKE");
    lv_obj_set_pos(incidentTitleLbl_, 12, 10);
    lockNoScroll(incidentTitleLbl_);

    incidentReasonLbl_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(incidentReasonLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(incidentReasonLbl_, C_DIM, 0);
    lv_label_set_text(incidentReasonLbl_, "DTC P0171 (Bank 1) • Time: 09/02 11:24");
    lv_obj_set_pos(incidentReasonLbl_, 12, 28);
    lockNoScroll(incidentReasonLbl_);

    // LVGL Line Chart (284x124 @ 12, 48)
    logChart_ = lv_chart_create(rightCard);
    lv_obj_set_size(logChart_, 284, 124);
    lv_obj_set_pos(logChart_, 12, 48);
    lv_obj_set_style_bg_color(logChart_, lv_color_hex(0x0E1013), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(logChart_, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(logChart_, 8, LV_PART_MAIN);
    lv_obj_set_style_border_color(logChart_, C_PANEL_BRD, LV_PART_MAIN);
    lv_obj_set_style_border_width(logChart_, 1, LV_PART_MAIN);
    lv_obj_set_style_line_width(logChart_, 3, LV_PART_ITEMS);
    lv_obj_set_style_line_opa(logChart_, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_line_width(logChart_, 3, 0);
    lv_obj_set_style_line_opa(logChart_, LV_OPA_COVER, 0);
    lv_chart_set_type(logChart_, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(logChart_, 50);
    lv_chart_set_axis_range(logChart_, LV_CHART_AXIS_PRIMARY_Y, -30, 30);
    lv_chart_set_div_line_count(logChart_, 3, 5);
    lv_obj_set_style_line_color(logChart_, lv_color_hex(0x20242D), LV_PART_MAIN);
    lockNoScroll(logChart_);

    chartSeriesKnock_ = lv_chart_add_series(logChart_, C_ACCENT, LV_CHART_AXIS_PRIMARY_Y);
    chartSeriesStft_  = lv_chart_add_series(logChart_, C_WARN, LV_CHART_AXIS_PRIMARY_Y);
    chartSeriesAfr_   = lv_chart_add_series(logChart_, C_SPEED, LV_CHART_AXIS_PRIMARY_Y);

    lv_chart_set_all_values(logChart_, chartSeriesKnock_, 0);
    lv_chart_set_all_values(logChart_, chartSeriesStft_, 0);
    lv_chart_set_all_values(logChart_, chartSeriesAfr_, 0);

    // Legend & Stats Chips at bottom
    lv_obj_t* legWrap = lv_obj_create(rightCard);
    lv_obj_remove_style_all(legWrap);
    lv_obj_set_size(legWrap, 284, 16);
    lv_obj_set_pos(legWrap, 12, 178);
    lockNoScroll(legWrap);

    // Helper for legend dots
    auto addLegItem = [](lv_obj_t* wrap, lv_color_t c, const char* txt, int16_t x) {
        lv_obj_t* dot = lv_obj_create(wrap);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 6, 6);
        lv_obj_set_pos(dot, x, 5);
        lv_obj_set_style_bg_color(dot, c, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, 3, 0);
        lockNoScroll(dot);

        lv_obj_t* lbl = lv_label_create(wrap);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(lbl, c, 0);
        lv_label_set_text(lbl, txt);
        lv_obj_set_pos(lbl, x + 10, 0);
        lockNoScroll(lbl);
    };

    addLegItem(legWrap, C_ACCENT, "Knock", 4);
    addLegItem(legWrap, C_WARN,   "STFT %", 72);
    addLegItem(legWrap, C_SPEED,  "AFR Delta", 144);

    lv_obj_t* timeRange = lv_label_create(legWrap);
    lv_obj_set_style_text_font(timeRange, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(timeRange, C_DIM, 0);
    lv_label_set_text(timeRange, "-15s .. 0s .. +15s");
    lv_obj_align(timeRange, LV_ALIGN_RIGHT_MID, -4, 0);

    // Summary Metric Chips
    lv_obj_t* statWrap = lv_obj_create(rightCard);
    lv_obj_remove_style_all(statWrap);
    lv_obj_set_size(statWrap, 284, 52);
    lv_obj_set_pos(statWrap, 12, 198);
    lockNoScroll(statWrap);

    chartStatKnock_ = lv_label_create(statWrap);
    lv_obj_set_style_text_font(chartStatKnock_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chartStatKnock_, C_TEXT, 0);
    lv_label_set_text(chartStatKnock_, "Peak Knock: 0.0°");
    lv_obj_set_pos(chartStatKnock_, 4, 4);

    chartStatStft_ = lv_label_create(statWrap);
    lv_obj_set_style_text_font(chartStatStft_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chartStatStft_, C_TEXT, 0);
    lv_label_set_text(chartStatStft_, "Max STFT: +24.6%");
    lv_obj_set_pos(chartStatStft_, 148, 4);

    chartStatAfr_ = lv_label_create(statWrap);
    lv_obj_set_style_text_font(chartStatAfr_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chartStatAfr_, C_TEXT, 0);
    lv_label_set_text(chartStatAfr_, "AFR Range: 14.2 .. 17.8");
    lv_obj_set_pos(chartStatAfr_, 4, 28);

    chartStatRpm_ = lv_label_create(statWrap);
    lv_obj_set_style_text_font(chartStatRpm_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(chartStatRpm_, C_TEXT, 0);
    lv_label_set_text(chartStatRpm_, "Peak RPM: 3,450");
    lv_obj_set_pos(chartStatRpm_, 148, 28);

    renderIncidentChart(0);
    return scr;
}

void Mx5UI::renderIncidentChart(uint8_t incidentIndex) {
    selectedIncident_ = incidentIndex;
    const IncidentMeta* meta = SdLogger::instance().getIncidentMeta(incidentIndex);
    if (!meta) return;

    // Update left buttons selection highlight
    for (uint8_t i = 0; i < 3; i++) {
        if (incidentItems_[i]) {
            bool sel = (i == incidentIndex);
            lv_obj_set_style_bg_color(incidentItems_[i], sel ? C_ACCENT_DM : lv_color_hex(0x0E1013), 0);
            lv_obj_set_style_border_color(incidentItems_[i], sel ? C_ACCENT : C_PANEL_BRD, 0);
        }
    }

    if (incidentTitleLbl_) lv_label_set_text(incidentTitleLbl_, meta->title);
    if (incidentReasonLbl_) {
        lv_label_set_text_fmt(incidentReasonLbl_, "%s • Time: %s", meta->triggerReason, meta->dateStr);
    }

    IncidentDataPoint pts[50];
    uint16_t actual = 0;
    if (SdLogger::instance().loadIncidentData(incidentIndex, pts, 50, &actual)) {
        for (uint16_t i = 0; i < actual; i++) {
            int32_t vk = (int32_t)(pts[i].knockRetard * 5.0f);
            int32_t vs = (int32_t)(pts[i].stft);
            int32_t va = (int32_t)((pts[i].afr - 14.7f) * 8.0f);
            if (chartSeriesKnock_) lv_chart_set_series_value_by_id(logChart_, chartSeriesKnock_, i, vk);
            if (chartSeriesStft_)  lv_chart_set_series_value_by_id(logChart_, chartSeriesStft_,  i, vs);
            if (chartSeriesAfr_)   lv_chart_set_series_value_by_id(logChart_, chartSeriesAfr_,   i, va);
        }
        if (chartSeriesKnock_) lv_chart_set_x_start_point(logChart_, chartSeriesKnock_, 0);
        if (chartSeriesStft_)  lv_chart_set_x_start_point(logChart_, chartSeriesStft_,  0);
        if (chartSeriesAfr_)   lv_chart_set_x_start_point(logChart_, chartSeriesAfr_,   0);
        if (logChart_) lv_chart_refresh(logChart_);
    }

    if (chartStatKnock_) lv_label_set_text_fmt(chartStatKnock_, "Peak Knock: %.1f°", meta->peakKnock);
    if (chartStatStft_)  lv_label_set_text_fmt(chartStatStft_, "Max STFT: %+.1f%%", meta->maxStft);
    if (chartStatAfr_)   lv_label_set_text_fmt(chartStatAfr_, "AFR Range: %.1f .. %.1f", meta->minAfr, meta->maxAfr);
    if (chartStatRpm_)   lv_label_set_text_fmt(chartStatRpm_, "Peak RPM: %d", (int)meta->peakRpm);
}

void Mx5UI::updateDiagSubLogs() {
    if (sdCardStatusLbl_) {
        if (SdLogger::instance().isCardInserted()) {
            lv_obj_set_style_text_color(sdCardStatusLbl_, C_OK, 0);
            lv_label_set_text(sdCardStatusLbl_, "SD: READY (32 GB)");
        } else {
            lv_obj_set_style_text_color(sdCardStatusLbl_, C_WARN, 0);
            lv_label_set_text(sdCardStatusLbl_, "SD: NOT DETECTED");
        }
    }

    // Refresh chart on active view
    static uint8_t lastRendered = 255;
    if (lastRendered != selectedIncident_) {
        lastRendered = selectedIncident_;
        renderIncidentChart(selectedIncident_);
    }
}

// ---------------------------------------------------------------------------
// SD Card Format Confirmation Modal
// ---------------------------------------------------------------------------
void Mx5UI::buildSdFormatModal(lv_obj_t* parent) {
    sdModalCard_ = lv_obj_create(parent);
    lv_obj_remove_style_all(sdModalCard_);
    lv_obj_set_size(sdModalCard_, 480, 320);
    lv_obj_set_pos(sdModalCard_, 0, 0);
    lv_obj_set_style_bg_color(sdModalCard_, lv_color_hex(0x050608), 0);
    lv_obj_set_style_bg_opa(sdModalCard_, 225, 0);
    lv_obj_add_flag(sdModalCard_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(sdModalCard_);

    lv_obj_t* box = lv_obj_create(sdModalCard_);
    lv_obj_remove_style_all(box);
    lv_obj_set_size(box, 390, 226);
    lv_obj_center(box);
    lv_obj_set_style_bg_color(box, C_PANEL, 0);
    lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(box, 16, 0);
    lv_obj_set_style_border_color(box, C_ACCENT, 0);
    lv_obj_set_style_border_width(box, 1, 0);
    lockNoScroll(box);

    lv_obj_t* title = lv_label_create(box);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, "FORMAT MICROSD CARD (FAT32)");
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 20, 14);
    lockNoScroll(title);

    sdModalMsg_ = lv_label_create(box);
    lv_obj_set_style_text_font(sdModalMsg_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sdModalMsg_, C_TEXT, 0);
    lv_label_set_text(sdModalMsg_,
        "Re-format SD card to clean FAT32?\n\n"
        "• Native format: FAT32 (up to 32 GB)\n"
        "• For 64GB+ cards, partition to 32GB FAT32 on PC.\n"
        "• Re-initializes /INCIDENTS and /SESSIONS folders.\n"
        "All existing telemetry logs will be erased.");
    lv_obj_set_width(sdModalMsg_, 350);
    lv_obj_align(sdModalMsg_, LV_ALIGN_TOP_LEFT, 20, 42);
    lockNoScroll(sdModalMsg_);

    // Confirm button (user_data = 1)
    lv_obj_t* confirmBtn = lv_obj_create(box);
    lv_obj_remove_style_all(confirmBtn);
    lv_obj_set_size(confirmBtn, 160, 36);
    lv_obj_set_pos(confirmBtn, 20, 168);
    lv_obj_set_style_bg_color(confirmBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(confirmBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(confirmBtn, 8, 0);
    lv_obj_set_user_data(confirmBtn, (void*)(uintptr_t)1);
    lv_obj_add_event_cb(confirmBtn, onSdFormatClick, LV_EVENT_CLICKED, this);
    lockNoScroll(confirmBtn);

    lv_obj_t* cLbl = lv_label_create(confirmBtn);
    lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cLbl, C_TEXT, 0);
    lv_label_set_text(cLbl, "FORMAT CARD NOW");
    lv_obj_center(cLbl);
    lv_obj_remove_flag(cLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(cLbl);

    // Cancel button (user_data = 0)
    lv_obj_t* cancelBtn = lv_obj_create(box);
    lv_obj_remove_style_all(cancelBtn);
    lv_obj_set_size(cancelBtn, 160, 36);
    lv_obj_set_pos(cancelBtn, 210, 168);
    lv_obj_set_style_bg_color(cancelBtn, lv_color_hex(0x222630), 0);
    lv_obj_set_style_bg_opa(cancelBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(cancelBtn, 8, 0);
    lv_obj_set_style_border_color(cancelBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(cancelBtn, 1, 0);
    lv_obj_set_user_data(cancelBtn, (void*)(uintptr_t)0);
    lv_obj_add_event_cb(cancelBtn, onSdFormatClick, LV_EVENT_CLICKED, this);
    lockNoScroll(cancelBtn);

    lv_obj_t* canLbl = lv_label_create(cancelBtn);
    lv_obj_set_style_text_font(canLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(canLbl, C_DIM, 0);
    lv_label_set_text(canLbl, "CANCEL");
    lv_obj_center(canLbl);
    lv_obj_remove_flag(canLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(canLbl);
}

void Mx5UI::showSdFormatModal() {
    if (sdModalCard_) lv_obj_remove_flag(sdModalCard_, LV_OBJ_FLAG_HIDDEN);
}

void Mx5UI::hideSdFormatModal() {
    if (sdModalCard_) lv_obj_add_flag(sdModalCard_, LV_OBJ_FLAG_HIDDEN);
}

void Mx5UI::onSdFormatClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action == 2) {
        // Open confirmation modal
        ui->showSdFormatModal();
    } else if (action == 1) {
        // Execute Format
        SdLogger::instance().formatCard();
        ui->renderIncidentChart(0);
        ui->hideSdFormatModal();
    } else {
        // Cancel
        ui->hideSdFormatModal();
    }
}

void Mx5UI::onIncidentSelectClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t index = (uintptr_t)lv_obj_get_user_data(targetObj);
    ui->renderIncidentChart((uint8_t)index);
}

// ---------------------------------------------------------------------------
// Dynamic Auto-Dimming & Night Mode Layer (Zero-RAM, direct local style update)
// ---------------------------------------------------------------------------

void Mx5UI::setNightMode(bool isNight) {
    data_local_.isNightMode = isNight;
    applyThemeMode(isNight);
}

void Mx5UI::setBacklightDuty(uint8_t duty) {
#ifdef ARDUINO
    static bool ledcInit = false;
    if (!ledcInit) {
        ledcSetup(MX5_LEDC_BACKLIGHT_CH, 5000, 8);
        ledcAttachPin(MX5_PIN_BACKLIGHT, MX5_LEDC_BACKLIGHT_CH);
        ledcInit = true;
    }
    ledcWrite(MX5_LEDC_BACKLIGHT_CH, duty);
#else
    (void)duty;
#endif
}

void Mx5UI::applyThemeMode(bool isNight) {
    currentNightMode_ = isNight;
    targetBacklightDuty_ = isNight ? (uint8_t)(255 * MX5_BACKLIGHT_NIGHT_PCT / 100)
                                   : (uint8_t)(255 * MX5_BACKLIGHT_DAY_PCT / 100);

    if (isNight) {
        // Muted Mazda Night Amber palette (Preserves night vision, zero glare)
        themeText_   = lv_color_hex(0xD86B1A);
        themeSpeed_  = lv_color_hex(0xC44D00);
        themeDim_    = lv_color_hex(0x8A4B22);
        themeDotLit_ = lv_color_hex(0xC44D00);
    } else {
        // Daytime Crisp Pure White palette
        themeText_   = lv_color_hex(0xFFFFFF);
        themeSpeed_  = lv_color_hex(0xFFFFFF);
        themeDim_    = lv_color_hex(0x8E949F);
        themeDotLit_ = lv_color_hex(0xFFFFFF);
    }

    // Direct local style updates - ZERO widget destruction/recreation
    if (speedBigLabel_) lv_obj_set_style_text_color(speedBigLabel_, themeSpeed_, 0);
    if (speedUnitLabel_) lv_obj_set_style_text_color(speedUnitLabel_, themeDim_, 0);
    if (gearLbl_) lv_obj_set_style_text_color(gearLbl_, themeSpeed_, 0);

    // Speed chips across all screens
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
        if (speedChipLabel_[i]) lv_obj_set_style_text_color(speedChipLabel_[i], themeSpeed_, 0);
    }

    // Arc & Dial value readouts
    if (rpmSeg_.val)        lv_obj_set_style_text_color(rpmSeg_.val, themeText_, 0);
    if (fuelSeg_.val)       lv_obj_set_style_text_color(fuelSeg_.val, themeText_, 0);
    if (ambientSeg_.val)    lv_obj_set_style_text_color(ambientSeg_.val, themeText_, 0);
    if (rpmBigSeg_.val)     lv_obj_set_style_text_color(rpmBigSeg_.val, themeText_, 0);
    if (loadSeg_.val)       lv_obj_set_style_text_color(loadSeg_.val, themeText_, 0);
    if (throttleSeg_.val)   lv_obj_set_style_text_color(throttleSeg_.val, themeText_, 0);
    if (fuelArcSeg_.val)    lv_obj_set_style_text_color(fuelArcSeg_.val, themeText_, 0);
    if (batArcSeg_.val)     lv_obj_set_style_text_color(batArcSeg_.val, themeText_, 0);
    if (coolantSeg_.val)    lv_obj_set_style_text_color(coolantSeg_.val, themeText_, 0);
    if (oilSeg_.val)        lv_obj_set_style_text_color(oilSeg_.val, themeText_, 0);
    if (intakeSeg_.val)     lv_obj_set_style_text_color(intakeSeg_.val, themeText_, 0);
    if (batterySeg_.val)    lv_obj_set_style_text_color(batterySeg_.val, themeText_, 0);
    if (instantMpgSeg_.val) lv_obj_set_style_text_color(instantMpgSeg_.val, themeText_, 0);
    if (hpSeg_.val)         lv_obj_set_style_text_color(hpSeg_.val, themeText_, 0);
    if (torqueSeg_.val)     lv_obj_set_style_text_color(torqueSeg_.val, themeText_, 0);
    if (stftSeg_.val)       lv_obj_set_style_text_color(stftSeg_.val, themeText_, 0);
    if (ltftSeg_.val)       lv_obj_set_style_text_color(ltftSeg_.val, themeText_, 0);

    // Trip metrics
    if (tripAvgVal_)   lv_obj_set_style_text_color(tripAvgVal_, themeSpeed_, 0);
    if (tripDistVal_)  lv_obj_set_style_text_color(tripDistVal_, themeSpeed_, 0);
    if (tripRangeVal_) lv_obj_set_style_text_color(tripRangeVal_, themeSpeed_, 0);
    if (tripFuelVal_)  lv_obj_set_style_text_color(tripFuelVal_, themeSpeed_, 0);

    // Track metrics
    if (trackTimerLbl_)    lv_obj_set_style_text_color(trackTimerLbl_, themeSpeed_, 0);
    if (trackThrottleVal_) lv_obj_set_style_text_color(trackThrottleVal_, themeText_, 0);
    if (trackBrakeVal_)    lv_obj_set_style_text_color(trackBrakeVal_, themeText_, 0);

    // Diagnostic metrics
    if (diagAfrVal_)      lv_obj_set_style_text_color(diagAfrVal_, themeSpeed_, 0);
    if (diagHpfpVal_)     lv_obj_set_style_text_color(diagHpfpVal_, themeSpeed_, 0);
    if (diagEvapVal_)     lv_obj_set_style_text_color(diagEvapVal_, themeSpeed_, 0);
    if (diagSparkVal_)    lv_obj_set_style_text_color(diagSparkVal_, themeSpeed_, 0);
    if (diagKnockVal_)    lv_obj_set_style_text_color(diagKnockVal_, themeSpeed_, 0);
    if (diagVvtInVal_)    lv_obj_set_style_text_color(diagVvtInVal_, themeSpeed_, 0);
    if (diagVvtExVal_)    lv_obj_set_style_text_color(diagVvtExVal_, themeSpeed_, 0);
    if (diagSasVal_)      lv_obj_set_style_text_color(diagSasVal_, themeSpeed_, 0);
    if (diagTransTempVal_)lv_obj_set_style_text_color(diagTransTempVal_, themeSpeed_, 0);
    if (diagTccSlipVal_)  lv_obj_set_style_text_color(diagTccSlipVal_, themeSpeed_, 0);

    for (uint8_t i = 0; i < 4; i++) {
        if (tpmsLabel_[i]) lv_obj_set_style_text_color(tpmsLabel_[i], themeText_, 0);
        if (tpmsTemp_[i])  lv_obj_set_style_text_color(tpmsTemp_[i], themeDim_, 0);
        if (wheelSpeedLbl_[i]) lv_obj_set_style_text_color(wheelSpeedLbl_[i], themeSpeed_, 0);
        if (misfireCountLbl_[i]) lv_obj_set_style_text_color(misfireCountLbl_[i], themeText_, 0);
    }
}

void Mx5UI::setThemeMode(ThemeMode mode) {
    themeMode_ = mode;
    if (mode == THEME_DAY) {
        applyThemeMode(false);
    } else if (mode == THEME_NIGHT) {
        applyThemeMode(true);
    } else {
        applyThemeMode(data_local_.isNightMode);
    }
    updateSettingsScreen();
}

void Mx5UI::setRotation(uint8_t rot) {
    currentRotation_ = rot;
#ifdef ARDUINO
    Waveshare35B::setRotation(rot);
#endif
    updateSettingsScreen();
}

void Mx5UI::setBrightness(uint8_t pct) {
    userBrightness_ = pct;
    targetBacklightDuty_ = (uint8_t)(255 * pct / 100);
    setBacklightDuty(targetBacklightDuty_);
    updateSettingsScreen();
}

// ---------------------------------------------------------------------------
// Screen 13 - System Settings & Preferences
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildSettingsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);

    // Back Button [ < BACK ] returns to Menu Hub (Screen 7)
    lv_obj_t* backBtn = lv_obj_create(scr);
    lv_obj_remove_style_all(backBtn);
    lv_obj_set_size(backBtn, 76, 28);
    lv_obj_set_pos(backBtn, 14, 8);
    lv_obj_set_style_bg_color(backBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(backBtn, 6, 0);
    lv_obj_set_style_border_color(backBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(backBtn, 1, 0);
    lv_obj_set_user_data(backBtn, (void*)(uintptr_t)SCREEN_MENU);
    lv_obj_add_event_cb(backBtn, onSubScreenNavClick, LV_EVENT_CLICKED, this);
    lockNoScroll(backBtn);

    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(backLbl, C_TEXT, 0);
    lv_label_set_text(backLbl, "< BACK");
    lv_obj_center(backLbl);
    lockNoScroll(backLbl);

    // Title label in center/left
    lv_obj_t* titleLbl = lv_label_create(scr);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(titleLbl, C_CHROME, 0);
    lv_label_set_text(titleLbl, "SYSTEM SETTINGS & PREFERENCES");
    lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 98, 14);
    lockNoScroll(titleLbl);

    speedChipLabel_[SCREEN_SETTINGS] = addSpeedChip(scr);

    // -----------------------------------------------------------------------
    // Left Card: Display, Rotation, Theme & Brightness (218x246 @ 14, 44)
    // -----------------------------------------------------------------------
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 218, 246);
    lv_obj_set_pos(leftCard, 14, 44);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 14, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    // 1. Orientation Header & Buttons
    lv_obj_t* rotHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(rotHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rotHeader, C_DIM, 0);
    lv_label_set_text(rotHeader, "SCREEN ORIENTATION");
    lv_obj_set_pos(rotHeader, 12, 10);
    lockNoScroll(rotHeader);

    auto makeBtn = [this](lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, const char* txt, uintptr_t id) -> lv_obj_t* {
        lv_obj_t* b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, w, h);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 6, 0);
        lv_obj_set_style_border_color(b, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_user_data(b, (void*)id);
        lv_obj_add_event_cb(b, onSettingsActionClick, LV_EVENT_CLICKED, this);
        lockNoScroll(b);

        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lockNoScroll(l);
        return b;
    };

    btnRotLeft_  = makeBtn(leftCard, 12, 28, 92, 30, "USB LEFT", 101);
    btnRotRight_ = makeBtn(leftCard, 112, 28, 92, 30, "USB RIGHT", 102);

    // 2. Theme & Lighting
    lv_obj_t* themeHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(themeHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(themeHeader, C_DIM, 0);
    lv_label_set_text(themeHeader, "THEME & AUTO-DIMMING");
    lv_obj_set_pos(themeHeader, 12, 68);
    lockNoScroll(themeHeader);

    btnThemeAuto_  = makeBtn(leftCard, 12, 86, 60, 30, "AUTO", 103);
    btnThemeDay_   = makeBtn(leftCard, 78, 86, 60, 30, "DAY", 104);
    btnThemeNight_ = makeBtn(leftCard, 144, 86, 60, 30, "NIGHT", 105);

    // 3. Brightness
    lv_obj_t* briHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(briHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(briHeader, C_DIM, 0);
    lv_label_set_text(briHeader, "DAYLIGHT BRIGHTNESS");
    lv_obj_set_pos(briHeader, 12, 126);
    lockNoScroll(briHeader);

    btnBri25_  = makeBtn(leftCard, 12, 144, 44, 28, "25%", 106);
    btnBri50_  = makeBtn(leftCard, 60, 144, 44, 28, "50%", 107);
    btnBri75_  = makeBtn(leftCard, 108, 144, 44, 28, "75%", 108);
    btnBri100_ = makeBtn(leftCard, 156, 144, 48, 28, "100%", 109);

    // Quick Status Pill in Left Card
    lv_obj_t* leftPill = lv_obj_create(leftCard);
    lv_obj_remove_style_all(leftPill);
    lv_obj_set_size(leftPill, 194, 44);
    lv_obj_set_pos(leftPill, 12, 186);
    lv_obj_set_style_bg_color(leftPill, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(leftPill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftPill, 8, 0);
    lv_obj_set_style_border_color(leftPill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftPill, 1, 0);
    lockNoScroll(leftPill);

    lv_obj_t* lpLbl = lv_label_create(leftPill);
    lv_obj_set_style_text_font(lpLbl, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(lpLbl, C_DIM, 0);
    lv_label_set_text(lpLbl, "Auto-Dim: Senses Mode 01 0x42\nAlternator & lighting load shift");
    lv_obj_align(lpLbl, LV_ALIGN_LEFT_MID, 8, 0);
    lockNoScroll(lpLbl);

    // -----------------------------------------------------------------------
    // Right Card: Units, Datalogger, Privacy & Tools (230x246 @ 236, 44)
    // -----------------------------------------------------------------------
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 230, 246);
    lv_obj_set_pos(rightCard, 236, 44);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 14, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    // 1. Units
    lv_obj_t* unitHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(unitHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(unitHeader, C_DIM, 0);
    lv_label_set_text(unitHeader, "MEASUREMENT UNITS");
    lv_obj_set_pos(unitHeader, 12, 6);
    lockNoScroll(unitHeader);

    btnUnitUs_  = makeBtn(rightCard, 12, 22, 98, 24, "US (MPH/°F)", 110);
    btnUnitMet_ = makeBtn(rightCard, 118, 22, 98, 24, "METRIC (KM/H)", 111);

    // 2. Datalogger
    lv_obj_t* logHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(logHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(logHeader, C_DIM, 0);
    lv_label_set_text(logHeader, "INCIDENT DATALOGGER");
    lv_obj_set_pos(logHeader, 12, 50);
    lockNoScroll(logHeader);

    btnLogAuto_ = makeBtn(rightCard, 12, 66, 98, 24, "AUTO INCIDENT", 112);
    btnLogDis_  = makeBtn(rightCard, 118, 66, 98, 24, "DISABLED", 113);

    // 3. Privacy Masking
    lv_obj_t* maskHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(maskHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(maskHeader, C_DIM, 0);
    lv_label_set_text(maskHeader, "SPEED PRIVACY MASKING");
    lv_obj_set_pos(maskHeader, 12, 94);
    lockNoScroll(maskHeader);

    btnMaskOn_  = makeBtn(rightCard, 12, 110, 98, 24, "MASK: ON", 114);
    btnMaskOff_ = makeBtn(rightCard, 118, 110, 98, 24, "RAW: OFF", 115);

    // 4. Tools & Configuration Links
    lv_obj_t* bleNavBtn = makeBtn(rightCard, 12, 142, 204, 28, "BLUETOOTH OBD SCANNER  >", 116);
    lv_obj_set_style_border_color(bleNavBtn, C_ACCENT, 0);

    lv_obj_t* tpmsNavBtn = makeBtn(rightCard, 12, 176, 204, 28, "TPMS WHEEL CALIBRATION  >", 117);
    lv_obj_set_style_border_color(tpmsNavBtn, C_OK, 0);

    lv_obj_t* wizNavBtn = makeBtn(rightCard, 12, 210, 204, 26, "INITIAL SETUP WIZARD  >", 118);
    lv_obj_set_style_border_color(wizNavBtn, C_CHROME, 0);

    updateSettingsScreen();
    return scr;
}

void Mx5UI::updateSettingsScreen() {
    auto highlight = [](lv_obj_t* b, bool active) {
        if (!b) return;
        lv_obj_set_style_bg_color(b, active ? C_ACCENT : lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_border_color(b, active ? C_ACCENT : C_PANEL_BRD, 0);
    };

    highlight(btnRotLeft_, currentRotation_ == 1);
    highlight(btnRotRight_, currentRotation_ == 3);

    highlight(btnThemeAuto_, themeMode_ == THEME_AUTO);
    highlight(btnThemeDay_, themeMode_ == THEME_DAY);
    highlight(btnThemeNight_, themeMode_ == THEME_NIGHT);

    highlight(btnBri25_, userBrightness_ == 25);
    highlight(btnBri50_, userBrightness_ == 50);
    highlight(btnBri75_, userBrightness_ == 75);
    highlight(btnBri100_, userBrightness_ >= 95);

    highlight(btnUnitUs_, unitsUs_);
    highlight(btnUnitMet_, !unitsUs_);

    highlight(btnLogAuto_, autoLogEnabled_);
    highlight(btnLogDis_, !autoLogEnabled_);

    highlight(btnMaskOn_, speedMaskEnabled_);
    highlight(btnMaskOff_, !speedMaskEnabled_);
}

void Mx5UI::onSettingsActionClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    switch (action) {
        case 101: // Rotation: USB Left (1)
            ui->setRotation(1);
            break;
        case 102: // Rotation: USB Right (3)
            ui->setRotation(3);
            break;
        case 103: // Theme: Auto
            ui->setThemeMode(THEME_AUTO);
            break;
        case 104: // Theme: Day (Forced White)
            ui->setThemeMode(THEME_DAY);
            break;
        case 105: // Theme: Night (Forced Amber)
            ui->setThemeMode(THEME_NIGHT);
            break;
        case 106: // Brightness 25%
            ui->userBrightness_ = 25;
            ui->setBacklightDuty((uint8_t)(255 * 25 / 100));
            break;
        case 107: // Brightness 50%
            ui->userBrightness_ = 50;
            ui->setBacklightDuty((uint8_t)(255 * 50 / 100));
            break;
        case 108: // Brightness 75%
            ui->userBrightness_ = 75;
            ui->setBacklightDuty((uint8_t)(255 * 75 / 100));
            break;
        case 109: // Brightness 100%
            ui->userBrightness_ = 100;
            ui->setBacklightDuty((uint8_t)(255 * 95 / 100));
            break;
        case 110: // Units US
            ui->unitsUs_ = true;
            break;
        case 111: // Units Metric
            ui->unitsUs_ = false;
            break;
        case 112: // Auto Incidents Log
            ui->autoLogEnabled_ = true;
            break;
        case 113: // Datalogger Disabled
            ui->autoLogEnabled_ = false;
            break;
        case 114: // Speed Mask ON
            ui->speedMaskEnabled_ = true;
            break;
        case 115: // Speed Mask OFF
            ui->speedMaskEnabled_ = false;
            break;
        case 116: // BLE Connection Screen
            ui->setScreen(SCREEN_BLE_CONFIG);
            break;
        case 117: // TPMS Wheel Calibration Screen
            ui->setScreen(SCREEN_WHEEL_MAP);
            break;
        case 118: // Initial Setup Wizard Screen
            ui->setScreen(SCREEN_WIZARD);
            break;
        case 119: // Wizard Finish -> Home Screen
            ui->setScreen(SCREEN_SPEED);
            break;
    }
    ui->updateSettingsScreen();
}

// ---------------------------------------------------------------------------
// Screen 14 - BLE Connection & OBD-II Scanner Discovery & Pairing
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildBleConfigScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "OBD-II ADAPTER & BLUETOOTH", SCREEN_BLE_CONFIG);

    // Left Card: Current Paired Scanner Profile (218x246 @ 14, 44)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 218, 246);
    lv_obj_set_pos(leftCard, 14, 44);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 14, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    blePairedTitle_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(blePairedTitle_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(blePairedTitle_, C_DIM, 0);
    lv_label_set_text(blePairedTitle_, "CURRENT PAIRED SCANNER");
    lv_obj_align(blePairedTitle_, LV_ALIGN_TOP_LEFT, 12, 10);
    lockNoScroll(blePairedTitle_);

    blePairedNameLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(blePairedNameLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(blePairedNameLbl_, C_OK, 0);
    lv_label_set_text(blePairedNameLbl_, "vLinker MS 08449");
    lv_obj_align(blePairedNameLbl_, LV_ALIGN_TOP_LEFT, 12, 28);
    lockNoScroll(blePairedNameLbl_);

    blePairedMacLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(blePairedMacLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(blePairedMacLbl_, C_DIM, 0);
    lv_label_set_text(blePairedMacLbl_, "MAC: 64:8C:BB:1A:08:0A");
    lv_obj_align(blePairedMacLbl_, LV_ALIGN_TOP_LEFT, 12, 50);
    lockNoScroll(blePairedMacLbl_);

    blePairedStatusLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(blePairedStatusLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(blePairedStatusLbl_, C_OK, 0);
    lv_label_set_text(blePairedStatusLbl_, "Status: Connected & Streaming");
    lv_obj_align(blePairedStatusLbl_, LV_ALIGN_TOP_LEFT, 12, 72);
    lockNoScroll(blePairedStatusLbl_);

    blePairedRssiLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(blePairedRssiLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(blePairedRssiLbl_, C_DIM, 0);
    lv_label_set_text(blePairedRssiLbl_, "Signal: -58 dBm • ISO 15765-4");
    lv_obj_align(blePairedRssiLbl_, LV_ALIGN_TOP_LEFT, 12, 94);
    lockNoScroll(blePairedRssiLbl_);

    // Action 401: Reconnect button
    lv_obj_t* recBtn = lv_obj_create(leftCard);
    lv_obj_remove_style_all(recBtn);
    lv_obj_set_size(recBtn, 194, 34);
    lv_obj_set_pos(recBtn, 12, 154);
    lv_obj_set_style_bg_color(recBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(recBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(recBtn, 8, 0);
    lv_obj_set_user_data(recBtn, (void*)(uintptr_t)401);
    lv_obj_add_event_cb(recBtn, onBleActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(recBtn);

    lv_obj_t* recLbl = lv_label_create(recBtn);
    lv_obj_set_style_text_font(recLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(recLbl, C_TEXT, 0);
    lv_label_set_text(recLbl, "RECONNECT TO ADAPTER");
    lv_obj_center(recLbl);
    lockNoScroll(recLbl);

    // Action 402: Forget Device button
    lv_obj_t* fgtBtn = lv_obj_create(leftCard);
    lv_obj_remove_style_all(fgtBtn);
    lv_obj_set_size(fgtBtn, 194, 30);
    lv_obj_set_pos(fgtBtn, 12, 198);
    lv_obj_set_style_bg_color(fgtBtn, lv_color_hex(0x13161B), 0);
    lv_obj_set_style_bg_opa(fgtBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fgtBtn, 8, 0);
    lv_obj_set_style_border_color(fgtBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(fgtBtn, 1, 0);
    lv_obj_set_user_data(fgtBtn, (void*)(uintptr_t)402);
    lv_obj_add_event_cb(fgtBtn, onBleActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(fgtBtn);

    lv_obj_t* fgtLbl = lv_label_create(fgtBtn);
    lv_obj_set_style_text_font(fgtLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fgtLbl, C_DIM, 0);
    lv_label_set_text(fgtLbl, "FORGET / UNPAIR SCANNER");
    lv_obj_center(fgtLbl);
    lockNoScroll(fgtLbl);

    // Right Card: Discovered BLE Devices List (230x246 @ 236, 44)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 230, 246);
    lv_obj_set_pos(rightCard, 236, 44);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 14, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    lv_obj_t* rTitle = lv_label_create(rightCard);
    lv_obj_set_style_text_font(rTitle, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rTitle, C_DIM, 0);
    lv_label_set_text(rTitle, "NEARBY BLE DEVICES");
    lv_obj_align(rTitle, LV_ALIGN_TOP_LEFT, 12, 8);
    lockNoScroll(rTitle);

    // Action 403: Rescan button
    bleScanBtn_ = lv_obj_create(rightCard);
    lv_obj_remove_style_all(bleScanBtn_);
    lv_obj_set_size(bleScanBtn_, 64, 22);
    lv_obj_set_pos(bleScanBtn_, 154, 4);
    lv_obj_set_style_bg_color(bleScanBtn_, lv_color_hex(0x191A20), 0);
    lv_obj_set_style_bg_opa(bleScanBtn_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bleScanBtn_, 6, 0);
    lv_obj_set_style_border_color(bleScanBtn_, C_ACCENT, 0);
    lv_obj_set_style_border_width(bleScanBtn_, 1, 0);
    lv_obj_set_user_data(bleScanBtn_, (void*)(uintptr_t)403);
    lv_obj_add_event_cb(bleScanBtn_, onBleActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(bleScanBtn_);

    bleScanBtnLbl_ = lv_label_create(bleScanBtn_);
    lv_obj_set_style_text_font(bleScanBtnLbl_, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(bleScanBtnLbl_, C_TEXT, 0);
    lv_label_set_text(bleScanBtnLbl_, "RESCAN");
    lv_obj_center(bleScanBtnLbl_);
    lockNoScroll(bleScanBtnLbl_);

    bleScanStatusLbl_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(bleScanStatusLbl_, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(bleScanStatusLbl_, C_CHROME, 0);
    lv_label_set_text(bleScanStatusLbl_, "Tap a device to pair & connect:");
    lv_obj_align(bleScanStatusLbl_, LV_ALIGN_TOP_LEFT, 12, 28);
    lockNoScroll(bleScanStatusLbl_);

    // 4 Device Slots (pos: 12, y: 44, 86, 128, 170, size: 206x38)
    const char* defNames[4] = {"vLinker MS 08449", "OBDLink CX BLE", "VEEPEAK OBD-II", "iCar Pro BLE4.0"};
    const char* defMacs[4]  = {"64:8C:BB:1A:08:0A", "F0:B5:D1:22:90:4C", "B8:1F:5E:44:11:02", "DC:06:98:50:31:AA"};
    const int16_t slotYs[4] = {44, 86, 128, 170};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* slot = lv_obj_create(rightCard);
        lv_obj_remove_style_all(slot);
        lv_obj_set_size(slot, 206, 38);
        lv_obj_set_pos(slot, 12, slotYs[i]);
        lv_obj_set_style_bg_color(slot, (i == 0) ? lv_color_hex(0x201214) : lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(slot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(slot, 8, 0);
        lv_obj_set_style_border_color(slot, (i == 0) ? C_ACCENT : C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(slot, (i == 0) ? 2 : 1, 0);
        lv_obj_set_user_data(slot, (void*)(uintptr_t)(500 + i));
        lv_obj_add_event_cb(slot, onBleDeviceSelectClick, LV_EVENT_CLICKED, this);
        lockNoScroll(slot);
        bleDeviceSlot_[i] = slot;

        lv_obj_t* nameL = lv_label_create(slot);
        lv_obj_set_style_text_font(nameL, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(nameL, (i == 0) ? C_ACCENT : C_TEXT, 0);
        lv_label_set_text(nameL, defNames[i]);
        lv_obj_align(nameL, LV_ALIGN_TOP_LEFT, 8, 4);
        lockNoScroll(nameL);
        bleDevNameLbl_[i] = nameL;

        lv_obj_t* macL = lv_label_create(slot);
        lv_obj_set_style_text_font(macL, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(macL, C_DIM, 0);
        lv_label_set_text_fmt(macL, "%s • -%ddBm", defMacs[i], 58 + i * 12);
        lv_obj_align(macL, LV_ALIGN_BOTTOM_LEFT, 8, -4);
        lockNoScroll(macL);
        bleDevMacLbl_[i] = macL;

        lv_obj_t* tagL = lv_label_create(slot);
        lv_obj_set_style_text_font(tagL, &lv_font_montserrat_8, 0);
        lv_obj_set_style_text_color(tagL, (i == 0) ? C_OK : C_DIM, 0);
        lv_label_set_text(tagL, (i == 0) ? "[PAIRED]" : "[OBD]");
        lv_obj_align(tagL, LV_ALIGN_RIGHT_MID, -8, 0);
        lockNoScroll(tagL);
        bleDevTagLbl_[i] = tagL;
    }

    // Bottom Status Pill
    lv_obj_t* btmPill = lv_obj_create(rightCard);
    lv_obj_remove_style_all(btmPill);
    lv_obj_set_size(btmPill, 206, 24);
    lv_obj_set_pos(btmPill, 12, 214);
    lv_obj_set_style_bg_color(btmPill, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(btmPill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btmPill, 6, 0);
    lv_obj_set_style_border_color(btmPill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(btmPill, 1, 0);
    lockNoScroll(btmPill);

    lv_obj_t* bpLbl = lv_label_create(btmPill);
    lv_obj_set_style_text_font(bpLbl, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(bpLbl, C_DIM, 0);
    lv_label_set_text(bpLbl, "Auto-connects to paired adapter on boot");
    lv_obj_center(bpLbl);
    lockNoScroll(bpLbl);

    updateBleConfigScreen();
    return scr;
}

void Mx5UI::updateBleConfigScreen() {
    char pairedMac[20] = "";
    char pairedName[32] = "";
    obd_.getPairedDevice(pairedMac, sizeof(pairedMac), pairedName, sizeof(pairedName));

    if (blePairedNameLbl_) {
        if (strlen(pairedName) > 0) {
            lv_label_set_text(blePairedNameLbl_, pairedName);
            lv_obj_set_style_text_color(blePairedNameLbl_, data_local_.connected ? C_OK : C_WARN, 0);
        } else {
            lv_label_set_text(blePairedNameLbl_, "No Scanner Paired");
            lv_obj_set_style_text_color(blePairedNameLbl_, C_DIM, 0);
        }
    }
    if (blePairedMacLbl_) {
        if (strlen(pairedMac) > 0) {
            lv_label_set_text_fmt(blePairedMacLbl_, "MAC: %s", pairedMac);
        } else {
            lv_label_set_text(blePairedMacLbl_, "Tap a device on right to pair");
        }
    }
    if (blePairedStatusLbl_) {
        lv_label_set_text(blePairedStatusLbl_, data_local_.connected ? "Status: Connected & Streaming" : "Status: Disconnected / Ready");
        lv_obj_set_style_text_color(blePairedStatusLbl_, data_local_.connected ? C_OK : C_WARN, 0);
    }
    if (blePairedRssiLbl_) {
        lv_label_set_text_fmt(blePairedRssiLbl_, "Signal: %d dBm • ISO 15765-4", obd_.getRssi());
    }
}

void Mx5UI::onBleDeviceSelectClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action >= 500 && action <= 503) {
        uint8_t slotIdx = (uint8_t)(action - 500);
        ui->selectedBleDevice_ = slotIdx;

        const char* names[4] = {"vLinker MS 08449", "OBDLink CX BLE", "VEEPEAK OBD-II", "iCar Pro BLE4.0"};
        const char* macs[4]  = {"64:8C:BB:1A:08:0A", "F0:B5:D1:22:90:4C", "B8:1F:5E:44:11:02", "DC:06:98:50:31:AA"};

        ui->obd_.pairDevice(macs[slotIdx], names[slotIdx]);

        for (uint8_t i = 0; i < 4; i++) {
            if (ui->bleDeviceSlot_[i]) {
                lv_obj_set_style_bg_color(ui->bleDeviceSlot_[i], (i == slotIdx) ? lv_color_hex(0x201214) : lv_color_hex(0x0E1013), 0);
                lv_obj_set_style_border_color(ui->bleDeviceSlot_[i], (i == slotIdx) ? C_ACCENT : C_PANEL_BRD, 0);
                lv_obj_set_style_border_width(ui->bleDeviceSlot_[i], (i == slotIdx) ? 2 : 1, 0);
            }
            if (ui->bleDevNameLbl_[i]) {
                lv_obj_set_style_text_color(ui->bleDevNameLbl_[i], (i == slotIdx) ? C_ACCENT : C_TEXT, 0);
            }
            if (ui->bleDevTagLbl_[i]) {
                lv_label_set_text(ui->bleDevTagLbl_[i], (i == slotIdx) ? "[PAIRED]" : "[OBD]");
                lv_obj_set_style_text_color(ui->bleDevTagLbl_[i], (i == slotIdx) ? C_OK : C_DIM, 0);
            }
        }
        if (ui->bleScanStatusLbl_) {
            lv_label_set_text_fmt(ui->bleScanStatusLbl_, "Paired to %s!", names[slotIdx]);
            lv_obj_set_style_text_color(ui->bleScanStatusLbl_, C_OK, 0);
        }
        ui->updateBleConfigScreen();
    }
}

void Mx5UI::onBleActionClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action == 401) { // Reconnect
        ui->obd_.rescan();
        if (ui->bleScanStatusLbl_) {
            lv_label_set_text(ui->bleScanStatusLbl_, "Reconnecting to adapter...");
            lv_obj_set_style_text_color(ui->bleScanStatusLbl_, C_CHROME, 0);
        }
    } else if (action == 402) { // Forget
        ui->obd_.forgetPairedDevice();
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->bleDeviceSlot_[i]) {
                lv_obj_set_style_bg_color(ui->bleDeviceSlot_[i], lv_color_hex(0x0E1013), 0);
                lv_obj_set_style_border_color(ui->bleDeviceSlot_[i], C_PANEL_BRD, 0);
                lv_obj_set_style_border_width(ui->bleDeviceSlot_[i], 1, 0);
            }
            if (ui->bleDevNameLbl_[i]) {
                lv_obj_set_style_text_color(ui->bleDevNameLbl_[i], C_TEXT, 0);
            }
            if (ui->bleDevTagLbl_[i]) {
                lv_label_set_text(ui->bleDevTagLbl_[i], "[OBD]");
                lv_obj_set_style_text_color(ui->bleDevTagLbl_[i], C_DIM, 0);
            }
        }
        if (ui->bleScanStatusLbl_) {
            lv_label_set_text(ui->bleScanStatusLbl_, "Scanner unmapped. Ready to pair.");
            lv_obj_set_style_text_color(ui->bleScanStatusLbl_, C_WARN, 0);
        }
        ui->updateBleConfigScreen();
    } else if (action == 403) { // Rescan
        ui->obd_.startBleScan();
        if (ui->bleScanStatusLbl_) {
            lv_label_set_text(ui->bleScanStatusLbl_, "Scanning nearby BLE devices...");
            lv_obj_set_style_text_color(ui->bleScanStatusLbl_, C_ACCENT, 0);
        }
    }
}

// ---------------------------------------------------------------------------
// Screen 15 - Initial Setup Wizard
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildWizardScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "INITIAL SETUP WIZARD", SCREEN_WIZARD);

    // Main Card (452x246 @ 14, 44)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 452, 246);
    lv_obj_set_pos(card, 14, 44);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    lv_obj_t* wTitle = lv_label_create(card);
    lv_obj_set_style_text_font(wTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wTitle, C_ACCENT, 0);
    lv_label_set_text(wTitle, "WELCOME TO MX-5 ND2 DIGITAL CLUSTER");
    lv_obj_align(wTitle, LV_ALIGN_TOP_LEFT, 16, 12);
    lockNoScroll(wTitle);

    const char* steps[4] = {
        "1. Verify OBD-II BLE Scanner Connection (vLinker MS 08449)",
        "2. Choose Measurement Units (US Imperial MPH/°F vs Metric)",
        "3. Calibrate TPMS Wheel Sensor ID Binding (DIDs 2A05-2A08)",
        "4. Auto-dimming & Continuous Incident Datalogger Ready"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sLbl = lv_label_create(card);
        lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(sLbl, C_TEXT, 0);
        lv_label_set_text(sLbl, steps[i]);
        lv_obj_align(sLbl, LV_ALIGN_TOP_LEFT, 16, 42 + i * 26);
        lockNoScroll(sLbl);
    }

    auto makeWizBtn = [this](lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, const char* txt, uintptr_t id, lv_color_t bgCol) -> lv_obj_t* {
        lv_obj_t* b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, w, h);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_style_bg_color(b, bgCol, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 8, 0);
        lv_obj_set_style_border_color(b, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_user_data(b, (void*)id);
        lv_obj_add_event_cb(b, onSettingsActionClick, LV_EVENT_CLICKED, this);
        lockNoScroll(b);

        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lockNoScroll(l);
        return b;
    };

    makeWizBtn(card, 16, 186, 132, 36, "BLUETOOTH SETUP", 116, lv_color_hex(0x191A20));
    makeWizBtn(card, 158, 186, 132, 36, "CALIBRATE TPMS", 117, lv_color_hex(0x191A20));
    makeWizBtn(card, 300, 186, 136, 36, "FINISH SETUP", 119, C_ACCENT);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 16 - TPMS Wheel Calibration & Sensor Binding
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildWheelMapScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "TPMS WHEEL CALIBRATION", SCREEN_WHEEL_MAP);

    // Left Panel: 4 Wheel Target Cards (240x246 @ 14, 44)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 240, 246);
    lv_obj_set_pos(leftCard, 14, 44);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 14, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    const char* names[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
    const char* defDids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
    const int16_t xs[4] = {10, 124, 10, 124};
    const int16_t ys[4] = {12, 12, 126, 126};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* cell = lv_obj_create(leftCard);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, 106, 108);
        lv_obj_set_pos(cell, xs[i], ys[i]);
        lv_obj_set_style_bg_color(cell, (i == 0) ? lv_color_hex(0x201214) : lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_border_color(cell, (i == 0) ? C_ACCENT : C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(cell, (i == 0) ? 2 : 1, 0);
        lv_obj_set_user_data(cell, (void*)(uintptr_t)(200 + i));
        lv_obj_add_event_cb(cell, onWheelMapActionClick, LV_EVENT_CLICKED, this);
        lockNoScroll(cell);
        wheelCell_[i] = cell;

        lv_obj_t* tag = lv_label_create(cell);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(tag, (i == 0) ? C_ACCENT : C_CHROME, 0);
        lv_label_set_text(tag, names[i]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 8, 8);
        lockNoScroll(tag);
        wheelCellLbl_[i] = tag;

        lv_obj_t* didLbl = lv_label_create(cell);
        lv_obj_set_style_text_font(didLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(didLbl, C_OK, 0);
        lv_label_set_text(didLbl, defDids[i]);
        lv_obj_align(didLbl, LV_ALIGN_LEFT_MID, 8, -4);
        lockNoScroll(didLbl);
        wheelCellDid_[i] = didLbl;

        lv_obj_t* pressLbl = lv_label_create(cell);
        lv_obj_set_style_text_font(pressLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pressLbl, C_SPEED, 0);
        lv_label_set_text(pressLbl, "29.0 PSI");
        lv_obj_align(pressLbl, LV_ALIGN_BOTTOM_LEFT, 8, -8);
        lockNoScroll(pressLbl);
        wheelCellPress_[i] = pressLbl;
    }

    // Right Panel: Instructions & Actions (208x246 @ 258, 44)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 208, 246);
    lv_obj_set_pos(rightCard, 258, 44);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 14, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    lv_obj_t* insTitle = lv_label_create(rightCard);
    lv_obj_set_style_text_font(insTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(insTitle, C_CHROME, 0);
    lv_label_set_text(insTitle, "CALIBRATION GUIDE");
    lv_obj_align(insTitle, LV_ALIGN_TOP_LEFT, 10, 10);
    lockNoScroll(insTitle);

    wheelPrompt_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(wheelPrompt_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wheelPrompt_, C_DIM, 0);
    lv_label_set_text(wheelPrompt_,
        "1. Tap a wheel to target\n"
        "2. Deflate/inflate ~3 PSI\n"
        "3. BCM feed binds DID\n"
        "4. Auto-detect assigns map");
    lv_obj_align(wheelPrompt_, LV_ALIGN_TOP_LEFT, 10, 32);
    lockNoScroll(wheelPrompt_);

    // Action 301: Auto-Detect
    lv_obj_t* autoBtn = lv_obj_create(rightCard);
    lv_obj_remove_style_all(autoBtn);
    lv_obj_set_size(autoBtn, 188, 36);
    lv_obj_set_pos(autoBtn, 10, 148);
    lv_obj_set_style_bg_color(autoBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(autoBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(autoBtn, 8, 0);
    lv_obj_set_user_data(autoBtn, (void*)(uintptr_t)301);
    lv_obj_add_event_cb(autoBtn, onWheelMapActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(autoBtn);

    lv_obj_t* aLbl = lv_label_create(autoBtn);
    lv_obj_set_style_text_font(aLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(aLbl, C_TEXT, 0);
    lv_label_set_text(aLbl, "AUTO-BIND SENSORS");
    lv_obj_center(aLbl);
    lockNoScroll(aLbl);

    // Action 302: Reset
    lv_obj_t* rstBtn = lv_obj_create(rightCard);
    lv_obj_remove_style_all(rstBtn);
    lv_obj_set_size(rstBtn, 188, 30);
    lv_obj_set_pos(rstBtn, 10, 194);
    lv_obj_set_style_bg_color(rstBtn, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(rstBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rstBtn, 8, 0);
    lv_obj_set_style_border_color(rstBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rstBtn, 1, 0);
    lv_obj_set_user_data(rstBtn, (void*)(uintptr_t)302);
    lv_obj_add_event_cb(rstBtn, onWheelMapActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(rstBtn);

    lv_obj_t* rstLbl = lv_label_create(rstBtn);
    lv_obj_set_style_text_font(rstLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rstLbl, C_DIM, 0);
    lv_label_set_text(rstLbl, "RESET FACTORY DIDs");
    lv_obj_center(rstLbl);
    lockNoScroll(rstLbl);

    return scr;
}

void Mx5UI::onWheelMapActionClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action >= 200 && action <= 203) {
        ui->wheelActive_ = (uint8_t)(action - 200);
        const char* names[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCell_[i]) {
                lv_obj_set_style_bg_color(ui->wheelCell_[i], (i == ui->wheelActive_) ? lv_color_hex(0x201214) : lv_color_hex(0x0E1013), 0);
                lv_obj_set_style_border_color(ui->wheelCell_[i], (i == ui->wheelActive_) ? C_ACCENT : C_PANEL_BRD, 0);
                lv_obj_set_style_border_width(ui->wheelCell_[i], (i == ui->wheelActive_) ? 2 : 1, 0);
            }
            if (ui->wheelCellLbl_[i]) {
                lv_obj_set_style_text_color(ui->wheelCellLbl_[i], (i == ui->wheelActive_) ? C_ACCENT : C_CHROME, 0);
            }
        }
        if (ui->wheelPrompt_) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Selected: %s\nDeflate/inflate by ~3 PSI.\nResponding BCM DID binds.", names[ui->wheelActive_]);
            lv_label_set_text(ui->wheelPrompt_, buf);
        }
    } else if (action == 301) {
        const char* dids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCellDid_[i]) {
                lv_label_set_text(ui->wheelCellDid_[i], dids[i]);
                lv_obj_set_style_text_color(ui->wheelCellDid_[i], C_OK, 0);
            }
        }
        if (ui->wheelPrompt_) {
            lv_label_set_text(ui->wheelPrompt_, "Auto-Learn Complete!\nAll 4 wheel DIDs mapped.\nTap < BACK to return.");
        }
    } else if (action == 302) {
        const char* dids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCellDid_[i]) {
                lv_label_set_text(ui->wheelCellDid_[i], dids[i]);
            }
        }
        if (ui->wheelPrompt_) {
            lv_label_set_text(ui->wheelPrompt_, "Reset to factory DIDs.\nTap Auto-Bind to learn.");
        }
    }
}

void Mx5UI::onSpeedWarnBannerClick(lv_event_t* e) {
    Mx5UI* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    ui->warningMutedForDrive_ = true;
    if (ui->speedWarningContainer_) {
        lv_obj_add_flag(ui->speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->speedNormalRightContainer_) {
        lv_obj_remove_flag(ui->speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
    }
}

