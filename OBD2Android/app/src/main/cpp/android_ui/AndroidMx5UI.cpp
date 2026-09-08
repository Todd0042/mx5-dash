#include "AndroidMx5UI.h"

#include <math.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "lvgl.h"
#include "lvgl_v9compat.h"
#include "DtcDatabase.h"
#include "UserPrefs.h"
#include "SdLogger.h"

#include "lv_font_mono_120.h"
#include "lv_font_mono_48.h"
#include "lv_font_mono_96.h"

#ifndef M5_PI
#define M5_PI 3.14159265358979323846f
#endif

extern "C" {
    extern const lv_image_dsc_t mx5_rf_tpms_dsc;
    extern const lv_image_dsc_t mx5_rf_cal_dsc;
}

// ---------------------------------------------------------------------------
// Signature Mazda Palette
// ---------------------------------------------------------------------------
static const lv_color_t C_BG        = lv_color_hex(0x0C0D0F);
static const lv_color_t C_PANEL     = lv_color_hex(0x14161B);
static const lv_color_t C_PANEL_BRD = lv_color_hex(0x2A2D36);
static const lv_color_t C_ACCENT    = lv_color_hex(0xD12229); // Mazda Soul Red Crystal
static const lv_color_t C_ACCENT_DM = lv_color_hex(0x4A1014);
static const lv_color_t C_SPEED     = lv_color_hex(0xFFFFFF);
static const lv_color_t C_TEXT      = lv_color_hex(0xFFFFFF);
static const lv_color_t C_CHROME    = lv_color_hex(0xD8DCE3);
static const lv_color_t C_DIM       = lv_color_hex(0x8E949F);
static const lv_color_t C_DOT_UNLIT = lv_color_hex(0x22252C);
static const lv_color_t C_OK        = lv_color_hex(0x10B981);
static const lv_color_t C_WARN      = lv_color_hex(0xF59E0B);
static const lv_color_t C_DANGER    = lv_color_hex(0xEF4444);

// ---------------------------------------------------------------------------
// Helpers & Conversions
// ---------------------------------------------------------------------------
static inline uint16_t speedU(uint8_t kmh) {
    return (uint16_t)(kmh * 0.621371f + 0.5f);
}

static inline int16_t tempU(int16_t degC) {
    return (int16_t)(degC * 1.8f + 32.5f);
}

static inline float pressU(float bar) {
    return bar * 14.5038f;
}

static void lockNoScroll(lv_obj_t* obj) {
    if (!obj) return;
    lv_obj_clear_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scrollbar_mode(obj, LV_SCROLLBAR_MODE_OFF);
}

static void setTextFont(lv_obj_t* label) {
    lv_obj_set_style_text_font(label, &lv_font_montserrat_12, 0);
}

static lv_obj_t* addBackgroundLayer(lv_obj_t* parent) {
    lv_obj_t* bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, C_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_pos(bg, 0, 0);
    lockNoScroll(bg);

    lv_obj_t* topLine = lv_obj_create(bg);
    lv_obj_remove_style_all(topLine);
    lv_obj_set_size(topLine, lv_pct(100), 2);
    lv_obj_set_pos(topLine, 0, 0);
    lv_obj_set_style_bg_color(topLine, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(topLine, LV_OPA_COVER, 0);
    lockNoScroll(topLine);

    lv_obj_move_to_index(bg, 0);
    return bg;
}

static lv_obj_t* addPageTitle(lv_obj_t* parent, const char* txt) {
    lv_obj_t* title = lv_label_create(parent);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, txt);
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -20, 12);
    lockNoScroll(title);
    return title;
}

// ---------------------------------------------------------------------------
// Navigation & Swipe Gestures (Single source of truth with 500ms debounce)
// ---------------------------------------------------------------------------
static uint32_t lastSwipeTime = 0;

static void triggerSwipe(AndroidMx5UI* ui, int dir) {
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

void AndroidMx5UI::onScreenEvent(lv_event_t*) {
    // On Android, high-level swipe gestures and animated transitions are handled by Mx5RenderView.kt
}

void AndroidMx5UI::setScreen(uint8_t index) {
    if (index >= SCREEN_COUNT) return;
    currentScreen_ = index;
    if (index != SCREEN_MENU) {
        lastContentScreen_ = index;
    }
    if (screens_[index]) {
        lv_screen_load(screens_[index]);
    }
}

void AndroidMx5UI::nextScreen() {
    if (currentScreen_ == SCREEN_MENU) {
        setScreen(lastContentScreen_);
        return;
    }
    if (currentScreen_ >= SCREEN_DIAG_SUB_FUEL && currentScreen_ <= SCREEN_DIAG_SUB_LOGS) {
        // Cycle forward among the 5 diagnostic sub-screens: 8 -> 9 -> 10 -> 11 -> 12 -> 8
        uint8_t nextSub = SCREEN_DIAG_SUB_FUEL + ((currentScreen_ - SCREEN_DIAG_SUB_FUEL + 1) % 5);
        setScreen(nextSub);
        return;
    }
    if (currentScreen_ >= CONTENT_SCREEN_COUNT) {
        setScreen(0);
        return;
    }
    setScreen((currentScreen_ + 1) % CONTENT_SCREEN_COUNT);
}

void AndroidMx5UI::prevScreen() {
    if (currentScreen_ == SCREEN_MENU) {
        setScreen(lastContentScreen_);
        return;
    }
    if (currentScreen_ >= SCREEN_DIAG_SUB_FUEL && currentScreen_ <= SCREEN_DIAG_SUB_LOGS) {
        // Cycle backward among the 5 diagnostic sub-screens: 8 <- 9 <- 10 <- 11 <- 12 <- 8
        uint8_t prevSub = SCREEN_DIAG_SUB_FUEL + ((currentScreen_ - SCREEN_DIAG_SUB_FUEL + 4) % 5);
        setScreen(prevSub);
        return;
    }
    if (currentScreen_ >= CONTENT_SCREEN_COUNT) {
        setScreen(0);
        return;
    }
    setScreen((currentScreen_ + CONTENT_SCREEN_COUNT - 1) % CONTENT_SCREEN_COUNT);
}

void AndroidMx5UI::toggleMenu() {
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

// ---------------------------------------------------------------------------
// Shared Decoration Helpers
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::addSpeedChip(lv_obj_t* parent) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 84, 30);
    lv_obj_set_pos(chip, 14, 8);
    lv_obj_set_style_bg_color(chip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 8, 0);
    lv_obj_set_style_border_color(chip, C_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lockNoScroll(chip);

    lv_obj_t* spdLbl = lv_label_create(chip);
    lv_obj_set_style_text_font(spdLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spdLbl, C_SPEED, 0);
    lv_label_set_text(spdLbl, "0 MPH");
    lv_obj_center(spdLbl);
    lockNoScroll(spdLbl);

    return spdLbl;
}

lv_obj_t* AndroidMx5UI::addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex) {
    addBackgroundLayer(parent);

    lv_obj_t* backBtn = lv_obj_create(parent);
    lv_obj_remove_style_all(backBtn);
    lv_obj_set_size(backBtn, 84, 30);
    lv_obj_set_pos(backBtn, 14, 8);
    lv_obj_set_style_bg_color(backBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(backBtn, 8, 0);
    lv_obj_set_style_border_color(backBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(backBtn, 1, 0);
    uint8_t backTarget = (subScreenIndex >= SCREEN_BLE_CONFIG && subScreenIndex <= SCREEN_WHEEL_MAP) ? SCREEN_SETTINGS : SCREEN_DIAG;
    lv_obj_set_user_data(backBtn, (void*)(uintptr_t)backTarget);
    lv_obj_add_event_cb(backBtn, onSubScreenNavClick, LV_EVENT_CLICKED, this);
    lockNoScroll(backBtn);

    lv_obj_t* backLbl = lv_label_create(backBtn);
    lv_obj_set_style_text_font(backLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(backLbl, C_TEXT, 0);
    lv_label_set_text(backLbl, "< BACK");
    lv_obj_center(backLbl);
    lv_obj_remove_flag(backLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(backLbl);

    lv_obj_t* titleLbl = lv_label_create(parent);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, C_CHROME, 0);
    lv_label_set_text(titleLbl, title);
    lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 110, 14);
    lockNoScroll(titleLbl);

    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 84, 30);
    lv_obj_align(chip, LV_ALIGN_TOP_RIGHT, -14, 8);
    lv_obj_set_style_bg_color(chip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 8, 0);
    lv_obj_set_style_border_color(chip, C_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lockNoScroll(chip);

    lv_obj_t* spdLbl = lv_label_create(chip);
    lv_obj_set_style_text_font(spdLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(spdLbl, C_TEXT, 0);
    lv_label_set_text(spdLbl, "0 MPH");
    lv_obj_center(spdLbl);
    lockNoScroll(spdLbl);

    return spdLbl;
}

AndroidMx5UI::SegArc AndroidMx5UI::buildDottedArc(
    lv_obj_t* parent, uint16_t size, int16_t posX, int16_t posY,
    const char* cap, uint8_t segCount
) {
    SegArc arc;
    arc.count = segCount;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, size, size);
    lv_obj_set_pos(wrap, posX, posY);
    lockNoScroll(wrap);
    arc.wrap = wrap;

    float cx = size / 2.0f;
    float cy = size / 2.0f;
    float r = (size / 2.0f) - 10.0f;
    uint8_t dotSize = (size >= 180) ? 8 : ((size >= 120) ? 6 : 5);

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
        arc.dots[i] = dot;
    }

    arc.val = lv_label_create(wrap);
    const lv_font_t* font = (size >= 180) ? &lv_font_montserrat_28 :
                            ((size >= 120) ? &lv_font_montserrat_24 :
                            ((size >= 90)  ? &lv_font_montserrat_20 : &lv_font_montserrat_16));
    lv_obj_set_style_text_font(arc.val, font, 0);
    lv_obj_set_style_text_color(arc.val, C_TEXT, 0);
    lv_label_set_text(arc.val, "--");
    lv_obj_align(arc.val, LV_ALIGN_CENTER, 0, (size >= 180) ? -12 : -6);
    lockNoScroll(arc.val);

    lv_obj_t* cLbl = lv_label_create(wrap);
    lv_obj_set_style_text_font(cLbl, (size >= 120) ? &lv_font_montserrat_14 : &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cLbl, C_CHROME, 0);
    lv_label_set_text(cLbl, cap);
    lv_obj_align(cLbl, LV_ALIGN_BOTTOM_MID, 0, (size >= 180) ? -20 : -10);
    lockNoScroll(cLbl);

    return arc;
}

void AndroidMx5UI::updateDottedValue(SegArc& seg, float frac) {
    if (!seg.wrap) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;

    uint8_t litCount = (uint8_t)(frac * seg.count + 0.5f);
    for (uint8_t i = 0; i < seg.count; i++) {
        if (i < litCount) {
            lv_obj_set_style_bg_color(seg.dots[i], themeDotLit_, 0);
        } else {
            lv_obj_set_style_bg_color(seg.dots[i], C_DOT_UNLIT, 0);
        }
    }
}

void AndroidMx5UI::warnTopDots(SegArc& seg) {
    if (!seg.wrap) return;
    uint8_t topCount = (seg.count > 16) ? 4 : 2;
    for (uint8_t i = seg.count - topCount; i < seg.count; i++) {
        lv_obj_set_style_bg_color(seg.dots[i], C_ACCENT, 0);
    }
}

lv_obj_t* AndroidMx5UI::addGearFrame(lv_obj_t* parent, int16_t x, int16_t y) {
    lv_obj_t* frame = lv_obj_create(parent);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 46, 50);
    lv_obj_set_pos(frame, x, y);
    lv_obj_set_style_bg_color(frame, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(frame, C_ACCENT, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_radius(frame, 10, 0);
    lockNoScroll(frame);

    lv_obj_t* gearLbl = lv_label_create(frame);
    lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(gearLbl, C_SPEED, 0);
    lv_label_set_text(gearLbl, "-");
    lv_obj_center(gearLbl);
    lockNoScroll(gearLbl);

    return gearLbl;
}

// ---------------------------------------------------------------------------
// Screen 0 - Speed (Default Widescreen Cockpit Cluster)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildSpeedScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_SPEED] = nullptr;

    // Left Panel: Hero Speed Card (290x290 @ 24, 35)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 290, 290);
    lv_obj_set_pos(card, 24, 35);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 18, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    speedBigLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedBigLabel_, &lv_font_mono_120, 0);
    lv_obj_set_style_text_color(speedBigLabel_, C_SPEED, 0);
    lv_label_set_text(speedBigLabel_, "0");
    lv_obj_align(speedBigLabel_, LV_ALIGN_CENTER, 0, -22);
    lockNoScroll(speedBigLabel_);

    speedUnitLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedUnitLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(speedUnitLabel_, C_CHROME, 0);
    lv_label_set_text(speedUnitLabel_, "MPH");
    lv_obj_align(speedUnitLabel_, LV_ALIGN_CENTER, 0, 80);
    lockNoScroll(speedUnitLabel_);

    gearLbl_ = addGearFrame(card, 236, 230);

    // Right Normal Dials Container (450x290 @ 325, 35)
    speedNormalRightContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedNormalRightContainer_);
    lv_obj_set_size(speedNormalRightContainer_, 450, 290);
    lv_obj_set_pos(speedNormalRightContainer_, 325, 35);
    lockNoScroll(speedNormalRightContainer_);

    // Center Panel: Tachometer Arc Gauge inside normal container (220x220 @ 10, 35)
    rpmSeg_     = buildDottedArc(speedNormalRightContainer_, 220, 10, 35, "RPM", 22);

    // Right Panel: Auxiliary Dials (112x112 @ 250, 91 & 360, 91)
    fuelSeg_    = buildDottedArc(speedNormalRightContainer_, 112, 250, 91, "FUEL", 10);
    ambientSeg_ = buildDottedArc(speedNormalRightContainer_, 112, 360, 91, "AMB", 10);

    // =========================================================================
    // Right Dynamic Warning Container (450x290 @ 325, 35) - Active on Alert
    // =========================================================================
    speedWarningContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedWarningContainer_);
    lv_obj_set_size(speedWarningContainer_, 450, 290);
    lv_obj_set_pos(speedWarningContainer_, 325, 35);
    lv_obj_add_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(speedWarningContainer_);

    // 1. Top Alert Banner (450x42 @ 0, 0)
    speedWarnBanner_ = lv_obj_create(speedWarningContainer_);
    lv_obj_remove_style_all(speedWarnBanner_);
    lv_obj_set_size(speedWarnBanner_, 450, 42);
    lv_obj_set_pos(speedWarnBanner_, 0, 0);
    lv_obj_set_style_bg_color(speedWarnBanner_, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(speedWarnBanner_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(speedWarnBanner_, 12, 0);
    lv_obj_add_flag(speedWarnBanner_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(speedWarnBanner_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnBanner_);

    speedWarnTitle_ = lv_label_create(speedWarnBanner_);
    lv_obj_set_style_text_font(speedWarnTitle_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(speedWarnTitle_, C_TEXT, 0);
    lv_label_set_text(speedWarnTitle_, "CRITICAL TIRE PRESSURE DROP • TAP TO DISMISS");
    lv_obj_center(speedWarnTitle_);
    lv_obj_add_flag(speedWarnTitle_, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(speedWarnTitle_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnTitle_);

    // 2. Alert Content Card (450x240 @ 0, 48)
    lv_obj_t* bCard = lv_obj_create(speedWarningContainer_);
    lv_obj_remove_style_all(bCard);
    lv_obj_set_size(bCard, 450, 240);
    lv_obj_set_pos(bCard, 0, 48);
    lv_obj_set_style_bg_color(bCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(bCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bCard, 14, 0);
    lv_obj_set_style_border_color(bCard, C_ACCENT, 0);
    lv_obj_set_style_border_width(bCard, 1, 0);
    lockNoScroll(bCard);

    // Center Top-Down MX-5 RF Visual
    speedWarnCarImg_ = lv_image_create(bCard);
    lv_image_set_src(speedWarnCarImg_, &mx5_rf_cal_dsc);
    lv_obj_align(speedWarnCarImg_, LV_ALIGN_CENTER, 0, -10);
    lockNoScroll(speedWarnCarImg_);

    // 4 Corner Tire Warning Pods
    const char* wTireNames[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
    const int16_t wxs[4] = {12, 288, 12, 288};
    const int16_t wys[4] = {12, 12, 114, 114};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* tPod = lv_obj_create(bCard);
        lv_obj_remove_style_all(tPod);
        lv_obj_set_size(tPod, 150, 84);
        lv_obj_set_pos(tPod, wxs[i], wys[i]);
        lv_obj_set_style_bg_color(tPod, lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(tPod, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(tPod, 10, 0);
        lv_obj_set_style_border_color(tPod, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(tPod, 1, 0);
        lockNoScroll(tPod);
        speedWarnTpmsPod_[i] = tPod;

        lv_obj_t* tag = lv_label_create(tPod);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, wTireNames[i]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 10, 6);
        lockNoScroll(tag);

        lv_obj_t* val = lv_label_create(tPod);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(val, C_SPEED, 0);
        lv_label_set_text(val, "-- PSI");
        lv_obj_align(val, LV_ALIGN_BOTTOM_LEFT, 10, -8);
        lockNoScroll(val);
        speedWarnTpmsVal_[i] = val;
    }

    // Bottom Alert Advice Text
    speedWarnSubDetail_ = lv_label_create(bCard);
    lv_obj_set_style_text_font(speedWarnSubDetail_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(speedWarnSubDetail_, C_WARN, 0);
    lv_label_set_text(speedWarnSubDetail_, "Recommended Cold Pressure: 29.0 PSI • Check tire immediately");
    lv_obj_align(speedWarnSubDetail_, LV_ALIGN_BOTTOM_MID, 0, -6);
    lockNoScroll(speedWarnSubDetail_);

    // Overheat & Non-TPMS Alert Card (hidden by default)
    speedWarnTempsCard_ = lv_obj_create(bCard);
    lv_obj_remove_style_all(speedWarnTempsCard_);
    lv_obj_set_size(speedWarnTempsCard_, 426, 175);
    lv_obj_align(speedWarnTempsCard_, LV_ALIGN_TOP_MID, 0, 10);
    lv_obj_set_style_bg_color(speedWarnTempsCard_, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(speedWarnTempsCard_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(speedWarnTempsCard_, 10, 0);
    lv_obj_add_flag(speedWarnTempsCard_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(speedWarnTempsCard_);

    speedWarnCoolantVal_ = lv_label_create(speedWarnTempsCard_);
    lv_obj_set_style_text_font(speedWarnCoolantVal_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(speedWarnCoolantVal_, C_DANGER, 0);
    lv_label_set_text(speedWarnCoolantVal_, "ENGINE COOLANT: --");
    lv_obj_align(speedWarnCoolantVal_, LV_ALIGN_TOP_LEFT, 16, 20);
    lockNoScroll(speedWarnCoolantVal_);

    speedWarnOilVal_ = lv_label_create(speedWarnTempsCard_);
    lv_obj_set_style_text_font(speedWarnOilVal_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(speedWarnOilVal_, C_DANGER, 0);
    lv_label_set_text(speedWarnOilVal_, "ENGINE OIL TEMP: --");
    lv_obj_align(speedWarnOilVal_, LV_ALIGN_TOP_LEFT, 16, 65);
    lockNoScroll(speedWarnOilVal_);

    speedWarnDetail_ = lv_label_create(speedWarnTempsCard_);
    lv_obj_set_style_text_font(speedWarnDetail_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(speedWarnDetail_, C_DIM, 0);
    lv_label_set_text(speedWarnDetail_, "Reduce engine load immediately to avoid overheating.");
    lv_obj_align(speedWarnDetail_, LV_ALIGN_BOTTOM_LEFT, 16, -16);
    lockNoScroll(speedWarnDetail_);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 1 - TPMS (4-Tire Pressure & Temp Cards + Center Silhouette)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildTpmsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TPMS] = addSpeedChip(scr);
    addPageTitle(scr, "TIRES");

    const char* titles[4] = {"FL", "FR", "RL", "RR"};
    const int16_t xs[4] = {24, 526, 24, 526};
    const int16_t ys[4] = {50, 50, 198, 198};
    const int16_t cardW = 250;
    const int16_t cardH = 135;

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, cardW, cardH);
        lv_obj_set_pos(card, xs[i], ys[i]);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lockNoScroll(card);
        tpmsCard_[i] = card;

        lv_obj_t* tag = lv_label_create(card);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, titles[i]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 16, 10);
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
        lv_obj_align(tpmsLabel_[i], LV_ALIGN_LEFT_MID, 16, 6);
        lockNoScroll(tpmsLabel_[i]);

        lv_obj_t* uLbl = lv_label_create(card);
        lv_obj_set_style_text_font(uLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(uLbl, C_DIM, 0);
        lv_label_set_text(uLbl, "PSI");
        lv_obj_align(uLbl, LV_ALIGN_LEFT_MID, 76, 10);
        lockNoScroll(uLbl);

        tpmsTemp_[i] = lv_label_create(card);
        lv_obj_set_style_text_font(tpmsTemp_[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tpmsTemp_[i], C_DIM, 0);
        lv_label_set_text(tpmsTemp_[i], "-- °F");
        lv_obj_align(tpmsTemp_[i], LV_ALIGN_BOTTOM_RIGHT, -16, -10);
        lv_obj_add_event_cb(card, onScreenEvent, LV_EVENT_ALL, this);
    }

    // Center Top-Down Vehicle Silhouette Outline
    lv_obj_t* carCard = lv_obj_create(scr);
    lv_obj_remove_style_all(carCard);
    lv_obj_set_size(carCard, 220, 283);
    lv_obj_set_pos(carCard, 290, 50);
    lv_obj_set_style_bg_color(carCard, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(carCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(carCard, 16, 0);
    lv_obj_set_style_border_color(carCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(carCard, 1, 0);
    lv_obj_add_event_cb(carCard, onScreenEvent, LV_EVENT_ALL, this);
    lockNoScroll(carCard);

    // Real Mazda MX-5 RF Top-Down Image
    lv_obj_t* carImg = lv_image_create(carCard);
    lv_image_set_src(carImg, &mx5_rf_tpms_dsc);
    lv_obj_align(carImg, LV_ALIGN_CENTER, 0, -6);
    lockNoScroll(carImg);

    lv_obj_t* cTxt = lv_label_create(carCard);
    lv_obj_set_style_text_font(cTxt, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cTxt, C_DIM, 0);
    lv_label_set_text(cTxt, "MX-5 RF");
    lv_obj_align(cTxt, LV_ALIGN_BOTTOM_MID, 0, -8);
    lockNoScroll(cTxt);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 2 - RPM / Engine (Widescreen 5-Dial Cluster)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildRpmScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_RPM] = addSpeedChip(scr);
    addPageTitle(scr, "ENGINE");

    // Center Big Tachometer Dial (230x230 @ 285, 65)
    rpmBigSeg_ = buildDottedArc(scr, 230, 285, 65, "TACHOMETER", 24);

    // Left Multi-Dials (110x110)
    loadSeg_     = buildDottedArc(scr, 110, 40, 55, "LOAD", 10);
    throttleSeg_ = buildDottedArc(scr, 110, 40, 195, "THROTTLE", 10);

    // Right Multi-Dials (110x110)
    fuelArcSeg_ = buildDottedArc(scr, 110, 650, 55, "FUEL", 10);
    batArcSeg_  = buildDottedArc(scr, 110, 650, 195, "BATTERY", 10);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 3 - Temperatures (4 Large Fluid Dials)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildEngineScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TEMPS] = addSpeedChip(scr);
    addPageTitle(scr, "TEMPERATURES");

    // 4 large dials in 2x2 grid
    coolantSeg_ = buildDottedArc(scr, 130, 180, 50, "COOLANT", 14);
    oilSeg_     = buildDottedArc(scr, 130, 490, 50, "OIL TEMP", 14);
    intakeSeg_  = buildDottedArc(scr, 130, 180, 195, "INTAKE AIR", 14);
    batterySeg_ = buildDottedArc(scr, 130, 490, 195, "BATTERY", 14);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 4 - Track & Dynamics (0-60 Timer, Dyno Output, Pedal Inputs)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildTrackScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TRACK] = addSpeedChip(scr);
    addPageTitle(scr, "TRACK & DYNAMICS");

    // Left Hero Card: 0-60 Acceleration Timer (236x286 @ 24, 50)
    lv_obj_t* timerCard = lv_obj_create(scr);
    lv_obj_remove_style_all(timerCard);
    lv_obj_set_size(timerCard, 236, 286);
    lv_obj_set_pos(timerCard, 24, 50);
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
    lv_obj_align(tTag, LV_ALIGN_TOP_MID, 0, 14);
    lockNoScroll(tTag);

    trackTimerLbl_ = lv_label_create(timerCard);
    lv_obj_set_style_text_font(trackTimerLbl_, &lv_font_montserrat_40, 0);
    lv_obj_set_style_text_color(trackTimerLbl_, C_SPEED, 0);
    lv_label_set_text(trackTimerLbl_, "0.00 s");
    lv_obj_align(trackTimerLbl_, LV_ALIGN_CENTER, 0, -22);
    lockNoScroll(trackTimerLbl_);

    trackStateBadge_ = lv_obj_create(timerCard);
    lv_obj_remove_style_all(trackStateBadge_);
    lv_obj_set_size(trackStateBadge_, 120, 28);
    lv_obj_align(trackStateBadge_, LV_ALIGN_CENTER, 0, 26);
    lv_obj_set_style_bg_color(trackStateBadge_, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(trackStateBadge_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackStateBadge_, 14, 0);
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
    lv_obj_set_style_text_color(trackBestLbl_, C_SPEED, 0);
    lv_label_set_text(trackBestLbl_, "BEST: -- s");
    lv_obj_align(trackBestLbl_, LV_ALIGN_TOP_LEFT, 24, 75);
    lockNoScroll(trackBestLbl_);

    // Middle Dials: Live HP & Torque Output (130x130 @ 335, 50 & 335, 195)
    hpSeg_     = buildDottedArc(scr, 130, 335, 50, "HORSEPOWER", 14);
    torqueSeg_ = buildDottedArc(scr, 130, 335, 195, "TORQUE", 14);

    // Right Card: Throttle vs Brake Pedal Response (216x286 @ 560, 50)
    lv_obj_t* pedalCard = lv_obj_create(scr);
    lv_obj_remove_style_all(pedalCard);
    lv_obj_set_size(pedalCard, 216, 286);
    lv_obj_set_pos(pedalCard, 560, 50);
    lv_obj_set_style_bg_color(pedalCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(pedalCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pedalCard, 16, 0);
    lv_obj_set_style_border_color(pedalCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(pedalCard, 1, 0);
    lockNoScroll(pedalCard);

    lv_obj_t* pTag = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(pTag, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(pTag, C_CHROME, 0);
    lv_label_set_text(pTag, "POWERTRAIN DEMAND");
    lv_obj_align(pTag, LV_ALIGN_TOP_MID, 0, 10);
    lockNoScroll(pTag);

    // Throttle Column (Left: x=28, w=60)
    lv_obj_t* thrLbl = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(thrLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(thrLbl, C_DIM, 0);
    lv_label_set_text(thrLbl, "THR");
    lv_obj_set_pos(thrLbl, 44, 32);
    lockNoScroll(thrLbl);

    trackThrottleBar_ = lv_bar_create(pedalCard);
    lv_obj_remove_style_all(trackThrottleBar_);
    lv_obj_set_size(trackThrottleBar_, 60, 160);
    lv_obj_set_pos(trackThrottleBar_, 28, 54);
    lv_obj_set_style_bg_color(trackThrottleBar_, C_DOT_UNLIT, 0);
    lv_obj_set_style_bg_opa(trackThrottleBar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackThrottleBar_, 8, 0);
    lv_obj_set_style_bg_color(trackThrottleBar_, C_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(trackThrottleBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(trackThrottleBar_, 8, LV_PART_INDICATOR);
    lv_bar_set_range(trackThrottleBar_, 0, 100);
    lockNoScroll(trackThrottleBar_);

    trackThrottleVal_ = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(trackThrottleVal_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(trackThrottleVal_, C_SPEED, 0);
    lv_label_set_text(trackThrottleVal_, "0%");
    lv_obj_align_to(trackThrottleVal_, trackThrottleBar_, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lockNoScroll(trackThrottleVal_);

    // Engine Load Column (Right: x=128, w=60)
    lv_obj_t* loadLbl = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(loadLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(loadLbl, C_DIM, 0);
    lv_label_set_text(loadLbl, "LOAD");
    lv_obj_set_pos(loadLbl, 138, 32);
    lockNoScroll(loadLbl);

    trackLoadBar_ = lv_bar_create(pedalCard);
    lv_obj_remove_style_all(trackLoadBar_);
    lv_obj_set_size(trackLoadBar_, 60, 160);
    lv_obj_set_pos(trackLoadBar_, 128, 54);
    lv_obj_set_style_bg_color(trackLoadBar_, C_DOT_UNLIT, 0);
    lv_obj_set_style_bg_opa(trackLoadBar_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(trackLoadBar_, 8, 0);
    lv_obj_set_style_bg_color(trackLoadBar_, C_WARN, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(trackLoadBar_, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(trackLoadBar_, 8, LV_PART_INDICATOR);
    lv_bar_set_range(trackLoadBar_, 0, 100);
    lockNoScroll(trackLoadBar_);

    trackLoadVal_ = lv_label_create(pedalCard);
    lv_obj_set_style_text_font(trackLoadVal_, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(trackLoadVal_, C_SPEED, 0);
    lv_label_set_text(trackLoadVal_, "0%");
    lv_obj_align_to(trackLoadVal_, trackLoadBar_, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lockNoScroll(trackLoadVal_);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 5 - Fuel & Trip Economy (Hero Instant MPG + 4 Metrics)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildTripScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_TRIP] = addSpeedChip(scr);
    addPageTitle(scr, "FUEL & TRIP");

    // Left Hero Instant MPG Card (360x286 @ 24, 50)
    lv_obj_t* heroCard = lv_obj_create(scr);
    lv_obj_remove_style_all(heroCard);
    lv_obj_set_size(heroCard, 360, 286);
    lv_obj_set_pos(heroCard, 24, 50);
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
    lv_obj_align(heroTag, LV_ALIGN_TOP_LEFT, 16, 12);
    lockNoScroll(heroTag);

    const uint8_t segCount = 22;
    instantMpgSeg_.count = segCount;
    instantMpgSeg_.wrap = heroCard;
    const float cx = 180.0f;
    const float cy = 150.0f;
    const float r = 100.0f;
    const uint8_t dotSize = 8;

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
    lv_obj_align(mpgUnit, LV_ALIGN_BOTTOM_MID, 0, -16);
    lockNoScroll(mpgUnit);

    // Right 4-card matrix (175x136 each @ 405, 600; y: 50, 200)
    struct TripCardDef {
        int16_t x; int16_t y; const char* title; const char* unit;
    };
    TripCardDef defs[4] = {
        {405, 50,  "TRIP AVG", "MPG"},
        {600, 50,  "RANGE",    "MILES"},
        {405, 200, "DIST",     "MILES"},
        {600, 200, "FUEL",     "LEVEL"}
    };

    lv_obj_t** valPtrs[4] = {&tripAvgVal_, &tripRangeVal_, &tripDistVal_, &tripFuelVal_};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 175, 136);
        lv_obj_set_pos(card, defs[i].x, defs[i].y);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 16, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lockNoScroll(card);

        lv_obj_t* tag = lv_label_create(card);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, defs[i].title);
        lv_obj_align(tag, LV_ALIGN_TOP_MID, 0, 10);
        lockNoScroll(tag);

        lv_obj_t* val = lv_label_create(card);
        lv_obj_set_style_text_font(val, &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(val, C_SPEED, 0);
        lv_label_set_text(val, "--");
        lv_obj_align(val, LV_ALIGN_CENTER, 0, 2);
        lockNoScroll(val);
        *valPtrs[i] = val;

        lv_obj_t* u = lv_label_create(card);
        lv_obj_set_style_text_font(u, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(u, C_DIM, 0);
        lv_label_set_text(u, defs[i].unit);
        lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -10);
        lockNoScroll(u);
    }

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 6 - Diagnostics & DTC Scanner
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildDiagnosticsScreen() {
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
    lv_obj_set_size(card, 752, 286);
    lv_obj_set_pos(card, 24, 50);
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
    lv_obj_align(diagStatus_, LV_ALIGN_TOP_LEFT, 20, 12);
    lockNoScroll(diagStatus_);

    diagBattery_ = lv_label_create(card);
    setTextFont(diagBattery_);
    lv_obj_set_style_text_color(diagBattery_, C_DIM, 0);
    lv_label_set_text(diagBattery_, "Battery: -- V");
    lv_obj_align(diagBattery_, LV_ALIGN_TOP_RIGHT, -20, 12);
    lockNoScroll(diagBattery_);

    // DTC Status Box
    diagDtcBox_ = lv_obj_create(card);
    lv_obj_remove_style_all(diagDtcBox_);
    lv_obj_set_size(diagDtcBox_, 630, 80);
    lv_obj_set_pos(diagDtcBox_, 20, 42);
    lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(diagDtcBox_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(diagDtcBox_, 10, 0);
    lv_obj_set_style_border_color(diagDtcBox_, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(diagDtcBox_, 1, 0);
    lv_obj_add_event_cb(diagDtcBox_, onDtcActionClick, LV_EVENT_CLICKED, this);
    lv_obj_set_user_data(diagDtcBox_, (void*)(uintptr_t)1);
    lockNoScroll(diagDtcBox_);

    diagDtcIndexLbl_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagDtcIndexLbl_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(diagDtcIndexLbl_, C_ACCENT, 0);
    lv_label_set_text(diagDtcIndexLbl_, "");
    lv_obj_align(diagDtcIndexLbl_, LV_ALIGN_TOP_LEFT, 14, 8);
    lv_obj_remove_flag(diagDtcIndexLbl_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagDtcIndexLbl_);

    diagDtcLbl_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagDtcLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagDtcLbl_, C_OK, 0);
    lv_label_set_text(diagDtcLbl_, "OK: NO FAULT CODES STORED (ECU & ABS NORMAL)");
    lv_obj_align(diagDtcLbl_, LV_ALIGN_LEFT_MID, 14, 0);
    lv_obj_remove_flag(diagDtcLbl_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagDtcLbl_);

    diagHint_ = lv_label_create(diagDtcBox_);
    lv_obj_set_style_text_font(diagHint_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(diagHint_, C_DIM, 0);
    lv_label_set_text(diagHint_, "All powertrain and chassis modules reporting zero faults");
    lv_obj_align(diagHint_, LV_ALIGN_BOTTOM_LEFT, 14, -8);
    lv_obj_remove_flag(diagHint_, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(diagHint_);

    // Up / Down cycle buttons
    diagDtcPrevBtn_ = lv_obj_create(card);
    lv_obj_remove_style_all(diagDtcPrevBtn_);
    lv_obj_set_size(diagDtcPrevBtn_, 60, 36);
    lv_obj_set_pos(diagDtcPrevBtn_, 665, 42);
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
    lv_obj_set_size(diagDtcNextBtn_, 60, 36);
    lv_obj_set_pos(diagDtcNextBtn_, 665, 86);
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

    // Action buttons row
    lv_obj_t* scanBtn = lv_obj_create(card);
    lv_obj_remove_style_all(scanBtn);
    lv_obj_set_size(scanBtn, 220, 38);
    lv_obj_set_pos(scanBtn, 20, 136);
    lv_obj_set_style_bg_color(scanBtn, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(scanBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scanBtn, 8, 0);
    lv_obj_set_style_border_color(scanBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(scanBtn, 1, 0);
    lv_obj_set_user_data(scanBtn, (void*)(uintptr_t)2);
    lv_obj_add_event_cb(scanBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(scanBtn);

    lv_obj_t* scanLbl = lv_label_create(scanBtn);
    lv_obj_set_style_text_font(scanLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(scanLbl, C_TEXT, 0);
    lv_label_set_text(scanLbl, "READ DTCs");
    lv_obj_center(scanLbl);
    lv_obj_remove_flag(scanLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(scanLbl);

    lv_obj_t* clearBtn = lv_obj_create(card);
    lv_obj_remove_style_all(clearBtn);
    lv_obj_set_size(clearBtn, 220, 38);
    lv_obj_set_pos(clearBtn, 260, 136);
    lv_obj_set_style_bg_color(clearBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(clearBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(clearBtn, 8, 0);
    lv_obj_set_style_border_color(clearBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(clearBtn, 1, 0);
    lv_obj_set_user_data(clearBtn, (void*)(uintptr_t)3);
    lv_obj_add_event_cb(clearBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(clearBtn);

    lv_obj_t* clearLbl = lv_label_create(clearBtn);
    lv_obj_set_style_text_font(clearLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(clearLbl, C_DIM, 0);
    lv_label_set_text(clearLbl, "CLEAR CODES");
    lv_obj_center(clearLbl);
    lv_obj_remove_flag(clearLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(clearLbl);

    lv_obj_t* guideBtn = lv_obj_create(card);
    lv_obj_remove_style_all(guideBtn);
    lv_obj_set_size(guideBtn, 220, 38);
    lv_obj_set_pos(guideBtn, 500, 136);
    lv_obj_set_style_bg_color(guideBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(guideBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(guideBtn, 8, 0);
    lv_obj_set_style_border_color(guideBtn, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(guideBtn, 1, 0);
    lv_obj_set_user_data(guideBtn, (void*)(uintptr_t)1);
    lv_obj_add_event_cb(guideBtn, onDtcActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(guideBtn);

    lv_obj_t* guideLbl = lv_label_create(guideBtn);
    lv_obj_set_style_text_font(guideLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(guideLbl, C_CHROME, 0);
    lv_label_set_text(guideLbl, "REPAIR GUIDE");
    lv_obj_center(guideLbl);
    lv_obj_remove_flag(guideLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(guideLbl);

    // 5 Advanced Diagnostic Sub-Dashboard Launchers
    struct DiagSubBtnDef {
        int16_t x;
        const char* title;
        const char* subtitle;
        uint32_t targetScreen;
    };
    DiagSubBtnDef subDefs[5] = {
        {20,  "FUEL",     "TRIMS & HPFP", SCREEN_DIAG_SUB_FUEL},
        {165, "MISFIRE",  "CYL & TIMING", SCREEN_DIAG_SUB_CYL},
        {310, "CHASSIS",  "SPEEDS & SAS", SCREEN_DIAG_SUB_CHASSIS},
        {455, "I/M SMOG", "MONITORS",     SCREEN_DIAG_SUB_SMOG},
        {600, "LOGS",     "BLACK BOX",    SCREEN_DIAG_SUB_LOGS}
    };

    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_t* subBtn = lv_obj_create(card);
        lv_obj_remove_style_all(subBtn);
        lv_obj_set_size(subBtn, 132, 48);
        lv_obj_set_pos(subBtn, subDefs[i].x, 190);
        lv_obj_set_style_bg_color(subBtn, lv_color_hex(0x191A20), 0);
        lv_obj_set_style_bg_opa(subBtn, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(subBtn, 8, 0);
        lv_obj_set_style_border_color(subBtn, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(subBtn, 1, 0);
        lv_obj_set_user_data(subBtn, (void*)(uintptr_t)subDefs[i].targetScreen);
        lv_obj_add_event_cb(subBtn, onSubScreenNavClick, LV_EVENT_CLICKED, this);
        lockNoScroll(subBtn);

        lv_obj_t* titleLbl = lv_label_create(subBtn);
        lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(titleLbl, C_TEXT, 0);
        lv_label_set_text(titleLbl, subDefs[i].title);
        lv_obj_align(titleLbl, LV_ALIGN_TOP_MID, 0, 8);
        lv_obj_remove_flag(titleLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(titleLbl);

        lv_obj_t* subLbl = lv_label_create(subBtn);
        lv_obj_set_style_text_font(subLbl, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(subLbl, C_DIM, 0);
        lv_label_set_text(subLbl, subDefs[i].subtitle);
        lv_obj_align(subLbl, LV_ALIGN_BOTTOM_MID, 0, -8);
        lv_obj_remove_flag(subLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(subLbl);
    }

    diagError_ = lv_label_create(card);
    setTextFont(diagError_);
    lv_obj_set_style_text_color(diagError_, C_DIM, 0);
    lv_label_set_text_fmt(diagError_, "Bus Status: Healthy");
    lv_obj_align(diagError_, LV_ALIGN_BOTTOM_LEFT, 20, -8);
    lockNoScroll(diagError_);

    diagHint_ = lv_label_create(card);
    setTextFont(diagHint_);
    lv_obj_set_style_text_color(diagHint_, C_CHROME, 0);
    lv_label_set_text(diagHint_, "Swipe UP/DOWN for Menu");
    lv_obj_align(diagHint_, LV_ALIGN_BOTTOM_RIGHT, -20, -8);
    lockNoScroll(diagHint_);

    buildDtcRepairModal(scr);
    return scr;
}

// ---------------------------------------------------------------------------
// Screen 7 - Mazda Connect Menu Hub
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildMenuScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_MENU] = addSpeedChip(scr);

    lv_obj_t* header = lv_label_create(scr);
    lv_obj_set_style_text_font(header, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(header, C_CHROME, 0);
    lv_label_set_text(header, "MAZDA CONNECT • MENU HUB");
    lv_obj_align(header, LV_ALIGN_TOP_MID, 0, 12);
    lockNoScroll(header);

    struct PodConfig {
        const char* emblem;
        const char* title;
        uint32_t targetScreen;
    };
    PodConfig pods[CONTENT_SCREEN_COUNT] = {
        {"MPH",   "SPEED",  SCREEN_SPEED},
        {"PSI",   "TIRES",  SCREEN_TPMS},
        {"°F",    "TEMPS",  SCREEN_TEMPS},
        {"OBD",   "DIAG",   SCREEN_DIAG},
        {"0-60",  "FAFO",   SCREEN_TRACK},
        {"RPM",   "TACH",   SCREEN_RPM},
        {"MPG",   "TRIP",   SCREEN_TRIP}
    };

    const int16_t podW = 76;
    const int16_t startX = 32;
    const int16_t gap = 30;
    const int16_t podY = 100;

    for (uint8_t i = 0; i < CONTENT_SCREEN_COUNT; i++) {
        int16_t x = startX + i * (podW + gap);

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

        lv_obj_set_user_data(btn, (void*)(uintptr_t)pods[i].targetScreen);
        lv_obj_add_event_cb(btn, onMenuIconClick, LV_EVENT_CLICKED, this);

        lv_obj_t* emblem = lv_label_create(btn);
        lv_obj_set_style_text_font(emblem, &lv_font_montserrat_20, 0);
        lv_obj_set_style_text_color(emblem, (i == 0) ? C_ACCENT : C_SPEED, 0);
        lv_label_set_text(emblem, pods[i].emblem);
        lv_obj_center(emblem);
        lockNoScroll(emblem);

        lv_obj_t* lbl = lv_label_create(scr);
        lv_obj_set_style_text_font(lbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(lbl, (i == 0) ? C_TEXT : C_DIM, 0);
        lv_label_set_text(lbl, pods[i].title);
        lv_obj_set_pos(lbl, x + (podW / 2) - 35, podY + podW + 10);
        lv_obj_set_width(lbl, 70);
        lv_obj_set_style_text_align(lbl, LV_TEXT_ALIGN_CENTER, 0);
        lockNoScroll(lbl);

        menuPods_[i] = btn;
    }

    // Status pill at bottom left (430x48 @ 32, 260)
    lv_obj_t* pill = lv_obj_create(scr);
    lv_obj_remove_style_all(pill);
    lv_obj_set_size(pill, 430, 48);
    lv_obj_set_pos(pill, 32, 260);
    lv_obj_set_style_bg_color(pill, C_PANEL, 0);
    lv_obj_set_style_bg_opa(pill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pill, 14, 0);
    lv_obj_set_style_border_color(pill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(pill, 1, 0);
    lockNoScroll(pill);

    menuStatusLbl_ = lv_label_create(pill);
    lv_obj_set_style_text_font(menuStatusLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(menuStatusLbl_, C_CHROME, 0);
    lv_label_set_text(menuStatusLbl_, "Touch pod to jump • Swipe UP/DOWN");
    lv_obj_center(menuStatusLbl_);
    lockNoScroll(menuStatusLbl_);

    // Settings Button at bottom right (280x48 @ 488, 260)
    lv_obj_t* setBtn = lv_obj_create(scr);
    lv_obj_remove_style_all(setBtn);
    lv_obj_set_size(setBtn, 280, 48);
    lv_obj_set_pos(setBtn, 488, 260);
    lv_obj_set_style_bg_color(setBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(setBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(setBtn, 14, 0);
    lv_obj_set_style_border_color(setBtn, C_ACCENT, 0);
    lv_obj_set_style_border_width(setBtn, 1, 0);
    lv_obj_set_user_data(setBtn, (void*)(uintptr_t)SCREEN_SETTINGS);
    lv_obj_add_event_cb(setBtn, onMenuIconClick, LV_EVENT_CLICKED, this);
    lockNoScroll(setBtn);

    lv_obj_t* setLbl = lv_label_create(setBtn);
    lv_obj_set_style_text_font(setLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(setLbl, C_TEXT, 0);
    lv_label_set_text(setLbl, "SETTINGS");
    lv_obj_center(setLbl);
    lv_obj_remove_flag(setLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(setLbl);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 13 - System Settings & Preferences (Dual 366px Widescreen Panels)
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildSettingsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);

    lv_obj_t* backBtn = lv_obj_create(scr);
    lv_obj_remove_style_all(backBtn);
    lv_obj_set_size(backBtn, 84, 30);
    lv_obj_set_pos(backBtn, 14, 8);
    lv_obj_set_style_bg_color(backBtn, C_PANEL, 0);
    lv_obj_set_style_bg_opa(backBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(backBtn, 8, 0);
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
    lv_obj_remove_flag(backLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(backLbl);

    lv_obj_t* titleLbl = lv_label_create(scr);
    lv_obj_set_style_text_font(titleLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(titleLbl, C_CHROME, 0);
    lv_label_set_text(titleLbl, "SYSTEM SETTINGS & PREFERENCES");
    lv_obj_align(titleLbl, LV_ALIGN_TOP_LEFT, 110, 14);
    lockNoScroll(titleLbl);

    lv_obj_t* setChip = lv_obj_create(scr);
    lv_obj_remove_style_all(setChip);
    lv_obj_set_size(setChip, 84, 30);
    lv_obj_align(setChip, LV_ALIGN_TOP_RIGHT, -14, 8);
    lv_obj_set_style_bg_color(setChip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(setChip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(setChip, 8, 0);
    lv_obj_set_style_border_color(setChip, C_ACCENT, 0);
    lv_obj_set_style_border_width(setChip, 1, 0);
    lockNoScroll(setChip);

    lv_obj_t* setSpdLbl = lv_label_create(setChip);
    lv_obj_set_style_text_font(setSpdLbl, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(setSpdLbl, C_TEXT, 0);
    lv_label_set_text(setSpdLbl, "0 MPH");
    lv_obj_center(setSpdLbl);
    lockNoScroll(setSpdLbl);

    speedChipLabel_[SCREEN_SETTINGS] = setSpdLbl;

    // Left Card (366x290 @ 24, 52)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 366, 290);
    lv_obj_set_pos(leftCard, 24, 52);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 14, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

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
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(l);
        return b;
    };

    // 1. Orientation
    lv_obj_t* rotHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(rotHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rotHeader, C_DIM, 0);
    lv_label_set_text(rotHeader, "SCREEN ORIENTATION");
    lv_obj_set_pos(rotHeader, 12, 10);
    lockNoScroll(rotHeader);

    btnRotLeft_  = makeBtn(leftCard, 12, 28, 165, 30, "USB LEFT", 101);
    btnRotRight_ = makeBtn(leftCard, 187, 28, 165, 30, "USB RIGHT", 102);

    // 2. Theme & Lighting
    lv_obj_t* themeHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(themeHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(themeHeader, C_DIM, 0);
    lv_label_set_text(themeHeader, "THEME & AUTO-DIMMING");
    lv_obj_set_pos(themeHeader, 12, 68);
    lockNoScroll(themeHeader);

    btnThemeAuto_  = makeBtn(leftCard, 12, 86, 108, 30, "AUTO", 103);
    btnThemeDay_   = makeBtn(leftCard, 128, 86, 108, 30, "DAY", 104);
    btnThemeNight_ = makeBtn(leftCard, 244, 86, 108, 30, "NIGHT", 105);

    // 3. Brightness
    lv_obj_t* briHeader = lv_label_create(leftCard);
    lv_obj_set_style_text_font(briHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(briHeader, C_DIM, 0);
    lv_label_set_text(briHeader, "DAYLIGHT BRIGHTNESS");
    lv_obj_set_pos(briHeader, 12, 126);
    lockNoScroll(briHeader);

    btnBri25_  = makeBtn(leftCard, 12, 144, 80, 28, "25%", 106);
    btnBri50_  = makeBtn(leftCard, 98, 144, 80, 28, "50%", 107);
    btnBri75_  = makeBtn(leftCard, 184, 144, 80, 28, "75%", 108);
    btnBri100_ = makeBtn(leftCard, 270, 144, 84, 28, "100%", 109);

    // Status pill
    lv_obj_t* leftPill = lv_obj_create(leftCard);
    lv_obj_remove_style_all(leftPill);
    lv_obj_set_size(leftPill, 342, 36);
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
    lv_label_set_text(lpLbl, "Auto-Dim: Tracks Mode 01 0x42 Alternator & lighting load shift");
    lv_obj_align(lpLbl, LV_ALIGN_LEFT_MID, 8, 0);
    lockNoScroll(lpLbl);

    // Firmware version pill
    lv_obj_t* fwPill = lv_obj_create(leftCard);
    lv_obj_remove_style_all(fwPill);
    lv_obj_set_size(fwPill, 342, 20);
    lv_obj_set_pos(fwPill, 12, 232);
    lv_obj_set_style_bg_color(fwPill, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(fwPill, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(fwPill, 8, 0);
    lv_obj_set_style_border_color(fwPill, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(fwPill, 1, 0);
    lockNoScroll(fwPill);

    settingsStatusLbl_ = lv_label_create(fwPill);
    lv_obj_set_style_text_font(settingsStatusLbl_, &lv_font_montserrat_8, 0);
    lv_obj_set_style_text_color(settingsStatusLbl_, C_OK, 0);
    lv_label_set_text(settingsStatusLbl_, "OBD2Android v2.4.0 • Google Pixel 11 Pro Edition");
    lv_obj_align(settingsStatusLbl_, LV_ALIGN_CENTER, 0, 0);
    lockNoScroll(settingsStatusLbl_);

    // Right Card (366x290 @ 410, 52)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 366, 290);
    lv_obj_set_pos(rightCard, 410, 52);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 14, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    // 1. Transmission Mode (AT vs MT)
    lv_obj_t* transHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(transHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(transHeader, C_DIM, 0);
    lv_label_set_text(transHeader, "TRANSMISSION TYPE");
    lv_obj_set_pos(transHeader, 12, 8);
    lockNoScroll(transHeader);

    btnTransAuto_   = makeBtn(rightCard, 12, 24, 165, 26, "AUTO (6AT)", 118);
    btnTransManual_ = makeBtn(rightCard, 187, 24, 165, 26, "MANUAL (6MT)", 119);

    // 2. Units
    lv_obj_t* unitHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(unitHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(unitHeader, C_DIM, 0);
    lv_label_set_text(unitHeader, "MEASUREMENT UNITS");
    lv_obj_set_pos(unitHeader, 12, 54);
    lockNoScroll(unitHeader);

    btnUnitUs_  = makeBtn(rightCard, 12, 70, 165, 26, "US (MPH/°F)", 110);
    btnUnitMet_ = makeBtn(rightCard, 187, 70, 165, 26, "METRIC (KM/H)", 111);

    // 3. Datalogger
    lv_obj_t* logHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(logHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(logHeader, C_DIM, 0);
    lv_label_set_text(logHeader, "INCIDENT DATALOGGER");
    lv_obj_set_pos(logHeader, 12, 100);
    lockNoScroll(logHeader);

    btnLogAuto_ = makeBtn(rightCard, 12, 116, 165, 26, "AUTO INCIDENT", 112);
    btnLogDis_  = makeBtn(rightCard, 187, 116, 165, 26, "DISABLED", 113);

    // 4. Privacy Masking
    lv_obj_t* maskHeader = lv_label_create(rightCard);
    lv_obj_set_style_text_font(maskHeader, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(maskHeader, C_DIM, 0);
    lv_label_set_text(maskHeader, "SPEED PRIVACY MASKING");
    lv_obj_set_pos(maskHeader, 12, 146);
    lockNoScroll(maskHeader);

    btnMaskOn_  = makeBtn(rightCard, 12, 162, 165, 26, "MASK: ON", 114);
    btnMaskOff_ = makeBtn(rightCard, 187, 162, 165, 26, "RAW: OFF", 115);

    // 5. BLE & TPMS Navigation
    lv_obj_t* bleNavBtn = makeBtn(rightCard, 12, 198, 165, 28, "BLE CONFIG >", 116);
    lv_obj_set_style_border_color(bleNavBtn, C_ACCENT, 0);

    lv_obj_t* tpmsBtn = makeBtn(rightCard, 187, 198, 165, 28, "TPMS LEARN >", 117);
    lv_obj_set_style_border_color(tpmsBtn, C_OK, 0);

    updateSettingsScreen();
    return scr;
}

// ---------------------------------------------------------------------------
// Sub-Screens 8-12, Modals, & Initialization
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildDiagSubFuel() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_DIAG_SUB_FUEL] = addSubScreenHeader(scr, "FUEL TRIMS & HPFP DIRECT INJECTION", SCREEN_DIAG_SUB_FUEL);

    // Left Card: Closed-Loop Fuel Trims (STFT & LTFT) (366x286 @ 24, 50)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 366, 286);
    lv_obj_set_pos(leftCard, 24, 50);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 16, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    lv_obj_t* lTitle = lv_label_create(leftCard);
    lv_obj_set_style_text_font(lTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lTitle, C_CHROME, 0);
    lv_label_set_text(lTitle, "CLOSED-LOOP FUEL TRIMS");
    lv_obj_align(lTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(lTitle);

    stftSeg_ = buildDottedArc(leftCard, 126, 30, 48, "SHORT TRIM", 14);
    ltftSeg_ = buildDottedArc(leftCard, 126, 204, 48, "LONG TRIM", 14);

    lv_obj_t* trimNote = lv_label_create(leftCard);
    lv_obj_set_style_text_font(trimNote, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(trimNote, C_DIM, 0);
    lv_label_set_text(trimNote, "Nominal closed-loop feedback window: +/- 10%");
    lv_obj_align(trimNote, LV_ALIGN_BOTTOM_MID, 0, -12);
    lockNoScroll(trimNote);

    // Right Top Card: Wideband Air/Fuel Ratio (AFR) (366x136 @ 410, 50)
    lv_obj_t* afrCard = lv_obj_create(scr);
    lv_obj_remove_style_all(afrCard);
    lv_obj_set_size(afrCard, 366, 136);
    lv_obj_set_pos(afrCard, 410, 50);
    lv_obj_set_style_bg_color(afrCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(afrCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(afrCard, 16, 0);
    lv_obj_set_style_border_color(afrCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(afrCard, 1, 0);
    lockNoScroll(afrCard);

    lv_obj_t* aTitle = lv_label_create(afrCard);
    lv_obj_set_style_text_font(aTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(aTitle, C_CHROME, 0);
    lv_label_set_text(aTitle, "WIDEBAND AIR / FUEL RATIO (AFR)");
    lv_obj_align(aTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(aTitle);

    diagAfrVal_ = lv_label_create(afrCard);
    lv_obj_set_style_text_font(diagAfrVal_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(diagAfrVal_, C_SPEED, 0);
    lv_label_set_text(diagAfrVal_, "-- : 1");
    lv_obj_align(diagAfrVal_, LV_ALIGN_LEFT_MID, 16, 4);
    lockNoScroll(diagAfrVal_);

    lv_obj_t* afrSub = lv_label_create(afrCard);
    lv_obj_set_style_text_font(afrSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(afrSub, C_DIM, 0);
    lv_label_set_text(afrSub, "Target: 14.70 : 1 Stoichiometric (Lambda 1.00)");
    lv_obj_align(afrSub, LV_ALIGN_BOTTOM_LEFT, 16, -10);
    lockNoScroll(afrSub);

    // Right Bottom Card: DI High-Pressure Fuel Rail (HPFP) (366x136 @ 410, 200)
    lv_obj_t* hpfpCard = lv_obj_create(scr);
    lv_obj_remove_style_all(hpfpCard);
    lv_obj_set_size(hpfpCard, 366, 136);
    lv_obj_set_pos(hpfpCard, 410, 200);
    lv_obj_set_style_bg_color(hpfpCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(hpfpCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(hpfpCard, 16, 0);
    lv_obj_set_style_border_color(hpfpCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(hpfpCard, 1, 0);
    lockNoScroll(hpfpCard);

    lv_obj_t* hTitle = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(hTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hTitle, C_CHROME, 0);
    lv_label_set_text(hTitle, "DI HIGH-PRESSURE FUEL RAIL (HPFP)");
    lv_obj_align(hTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(hTitle);

    diagHpfpVal_ = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(diagHpfpVal_, &lv_font_montserrat_28, 0);
    lv_obj_set_style_text_color(diagHpfpVal_, C_SPEED, 0);
    lv_label_set_text(diagHpfpVal_, "-- PSI");
    lv_obj_align(diagHpfpVal_, LV_ALIGN_LEFT_MID, 16, 4);
    lockNoScroll(diagHpfpVal_);

    lv_obj_t* hpfpSub = lv_label_create(hpfpCard);
    lv_obj_set_style_text_font(hpfpSub, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(hpfpSub, C_DIM, 0);
    lv_label_set_text(hpfpSub, "Skyactiv-G Direct Injection (500 - 4,350 PSI)");
    lv_obj_align(hpfpSub, LV_ALIGN_BOTTOM_LEFT, 16, -10);
    lockNoScroll(hpfpSub);

    diagEvapVal_ = nullptr;

    return scr;
}

lv_obj_t* AndroidMx5UI::buildDiagSubCyl() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_DIAG_SUB_CYL] = addSubScreenHeader(scr, "CYLINDERS & MISFIRE", SCREEN_DIAG_SUB_CYL);

    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 752, 286);
    lv_obj_set_pos(card, 24, 50);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    for (int i = 0; i < 4; i++) {
        lv_obj_t* box = lv_obj_create(card);
        lv_obj_remove_style_all(box);
        lv_obj_set_size(box, 160, 160);
        lv_obj_set_pos(box, 20 + i * 180, 20);
        lv_obj_set_style_bg_color(box, lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(box, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(box, 12, 0);
        lv_obj_set_style_border_color(box, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(box, 1, 0);
        lockNoScroll(box);

        lv_obj_t* cLbl = lv_label_create(box);
        lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(cLbl, C_CHROME, 0);
        lv_label_set_text_fmt(cLbl, "CYLINDER %d", i + 1);
        lv_obj_align(cLbl, LV_ALIGN_TOP_MID, 0, 12);
        lockNoScroll(cLbl);

        misfireCountLbl_[i] = lv_label_create(box);
        lv_obj_set_style_text_font(misfireCountLbl_[i], &lv_font_montserrat_28, 0);
        lv_obj_set_style_text_color(misfireCountLbl_[i], C_OK, 0);
        lv_label_set_text(misfireCountLbl_[i], "0");
        lv_obj_align(misfireCountLbl_[i], LV_ALIGN_CENTER, 0, 0);
        lockNoScroll(misfireCountLbl_[i]);

        lv_obj_t* u = lv_label_create(box);
        lv_obj_set_style_text_font(u, &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(u, C_DIM, 0);
        lv_label_set_text(u, "MISFIRES");
        lv_obj_align(u, LV_ALIGN_BOTTOM_MID, 0, -12);
        lockNoScroll(u);
    }

    return scr;
}

lv_obj_t* AndroidMx5UI::buildDiagSubChassis() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_DIAG_SUB_CHASSIS] = addSubScreenHeader(scr, "CHASSIS DYNAMICS & G-FORCE", SCREEN_DIAG_SUB_CHASSIS);

    // Left Card: Live Dynamics & G-Force (366x286 @ 24, 50)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 366, 286);
    lv_obj_set_pos(leftCard, 24, 50);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 16, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    lv_obj_t* lTitle = lv_label_create(leftCard);
    lv_obj_set_style_text_font(lTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lTitle, C_CHROME, 0);
    lv_label_set_text(lTitle, "ACCELERATION & POWER OUTPUT");
    lv_obj_align(lTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(lTitle);

    diagSasVal_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(diagSasVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagSasVal_, C_SPEED, 0);
    lv_label_set_text(diagSasVal_, "Est. Engine Output: -- HP / -- lb-ft");
    lv_obj_align(diagSasVal_, LV_ALIGN_TOP_LEFT, 14, 45);
    lockNoScroll(diagSasVal_);

    diagTransTempVal_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(diagTransTempVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagTransTempVal_, C_SPEED, 0);
    lv_label_set_text(diagTransTempVal_, "Engine Load / Demand: -- %");
    lv_obj_align(diagTransTempVal_, LV_ALIGN_TOP_LEFT, 14, 85);
    lockNoScroll(diagTransTempVal_);

    diagTccSlipVal_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(diagTccSlipVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(diagTccSlipVal_, C_SPEED, 0);
    lv_label_set_text(diagTccSlipVal_, "Dynamic Decel / Braking: 0%");
    lv_obj_align(diagTccSlipVal_, LV_ALIGN_TOP_LEFT, 14, 125);
    lockNoScroll(diagTccSlipVal_);

    lv_obj_t* noteG = lv_label_create(leftCard);
    lv_obj_set_style_text_font(noteG, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(noteG, C_DIM, 0);
    lv_label_set_text(noteG, "Dynamic load & braking computed in real time\nat maximum 40 Hz polling bandwidth.");
    lv_obj_align(noteG, LV_ALIGN_BOTTOM_LEFT, 14, -14);
    lockNoScroll(noteG);

    // Right Card: 6MT Transmission & Drivetrain (366x286 @ 410, 50)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 366, 286);
    lv_obj_set_pos(rightCard, 410, 50);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 16, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    bool isAuto = UserPrefs::getTransAuto();
    lv_obj_t* rTitle = lv_label_create(rightCard);
    lv_obj_set_style_text_font(rTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rTitle, C_CHROME, 0);
    lv_label_set_text(rTitle, isAuto ? "SKYACTIV-DRIVE 6AT GEAR RATIOS" : "SKYACTIV 6MT GEAR RATIOS");
    lv_obj_align(rTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(rTitle);

    const char* gearRatios6MT[6] = {
        "1st Gear:  5.087 : 1 (Launch)",
        "2nd Gear:  2.991 : 1",
        "3rd Gear:  2.035 : 1",
        "4th Gear:  1.594 : 1",
        "5th Gear:  1.286 : 1",
        "6th Gear:  1.000 : 1 (Direct)"
    };
    const char* gearRatios6AT[6] = {
        "1st Gear:  3.538 : 1 (Launch)",
        "2nd Gear:  2.060 : 1",
        "3rd Gear:  1.404 : 1",
        "4th Gear:  1.000 : 1 (Direct)",
        "5th Gear:  0.713 : 1 (Overdrive)",
        "6th Gear:  0.582 : 1 (Overdrive)"
    };
    const char* const* gearRatios = isAuto ? gearRatios6AT : gearRatios6MT;
    for (int i = 0; i < 6; i++) {
        lv_obj_t* gLbl = lv_label_create(rightCard);
        lv_obj_set_style_text_font(gLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(gLbl, C_TEXT, 0);
        lv_label_set_text(gLbl, gearRatios[i]);
        lv_obj_align(gLbl, LV_ALIGN_TOP_LEFT, 14, 42 + i * 24);
        lockNoScroll(gLbl);
    }

    lv_obj_t* fdLbl = lv_label_create(rightCard);
    lv_obj_set_style_text_font(fdLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(fdLbl, C_OK, 0);
    lv_label_set_text(fdLbl, isAuto ? "Final Drive: 3.454 : 1 (Open Diff)" : "Final Drive: 2.866 : 1 (Super LSD)");
    lv_obj_align(fdLbl, LV_ALIGN_BOTTOM_LEFT, 14, -14);
    lockNoScroll(fdLbl);

    return scr;
}

lv_obj_t* AndroidMx5UI::buildDiagSubSmog() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_DIAG_SUB_SMOG] = addSubScreenHeader(scr, "I/M SMOG READINESS", SCREEN_DIAG_SUB_SMOG);

    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 752, 286);
    lv_obj_set_pos(card, 24, 50);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    smogSummaryLbl_ = lv_label_create(card);
    lv_obj_set_style_text_font(smogSummaryLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(smogSummaryLbl_, C_WARN, 0);
    lv_label_set_text(smogSummaryLbl_, "EMISSION MONITORS INCOMPLETE (NOT READY)");
    lv_obj_align(smogSummaryLbl_, LV_ALIGN_TOP_LEFT, 24, 16);
    lockNoScroll(smogSummaryLbl_);

    const char* smogNames[8] = {"MISFIRE", "FUEL SYS", "COMPONENTS", "CATALYST", "EVAP", "O2 SENSOR", "O2 HEATER", "EGR / VVT"};
    for (int i = 0; i < 8; i++) {
        int row = i / 4;
        int col = i % 4;
        lv_obj_t* p = lv_obj_create(card);
        lv_obj_remove_style_all(p);
        lv_obj_set_size(p, 160, 75);
        lv_obj_set_pos(p, 24 + col * 180, 56 + row * 90);
        lv_obj_set_style_bg_color(p, lv_color_hex(0x0E1013), 0);
        lv_obj_set_style_bg_opa(p, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(p, 10, 0);
        lv_obj_set_style_border_color(p, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(p, 1, 0);
        lockNoScroll(p);

        smogPodDot_[i] = lv_obj_create(p);
        lv_obj_remove_style_all(smogPodDot_[i]);
        lv_obj_set_size(smogPodDot_[i], 10, 10);
        lv_obj_set_pos(smogPodDot_[i], 12, 14);
        lv_obj_set_style_bg_color(smogPodDot_[i], C_WARN, 0);
        lv_obj_set_style_bg_opa(smogPodDot_[i], LV_OPA_COVER, 0);
        lv_obj_set_style_radius(smogPodDot_[i], LV_RADIUS_CIRCLE, 0);
        lockNoScroll(smogPodDot_[i]);

        lv_obj_t* l = lv_label_create(p);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text(l, smogNames[i]);
        lv_obj_align(l, LV_ALIGN_LEFT_MID, 30, -8);
        lockNoScroll(l);

        smogPodLbl_[i] = lv_label_create(p);
        lv_obj_set_style_text_font(smogPodLbl_[i], &lv_font_montserrat_10, 0);
        lv_obj_set_style_text_color(smogPodLbl_[i], C_WARN, 0);
        lv_label_set_text(smogPodLbl_[i], "NOT READY");
        lv_obj_align(smogPodLbl_[i], LV_ALIGN_LEFT_MID, 30, 12);
        lockNoScroll(smogPodLbl_[i]);
    }

    return scr;
}

lv_obj_t* AndroidMx5UI::buildDiagSubLogs() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_DIAG_SUB_LOGS] = addSubScreenHeader(scr, "DATA LOGS & BLACK BOX", SCREEN_DIAG_SUB_LOGS);

    // Left Card: Continuous Live Telemetry Logging (366x286 @ 24, 50)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 366, 286);
    lv_obj_set_pos(leftCard, 24, 50);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 16, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    lv_obj_t* lTitle = lv_label_create(leftCard);
    lv_obj_set_style_text_font(lTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lTitle, C_CHROME, 0);
    lv_label_set_text(lTitle, "CONTINUOUS TELEMETRY LOGGING");
    lv_obj_align(lTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(lTitle);

    sdCardStatusLbl_ = lv_label_create(leftCard);
    lv_obj_set_style_text_font(sdCardStatusLbl_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(sdCardStatusLbl_, C_OK, 0);
    lv_label_set_text(sdCardStatusLbl_, "Storage: Flash • Active Logging");
    lv_obj_align(sdCardStatusLbl_, LV_ALIGN_TOP_LEFT, 14, 38);
    lockNoScroll(sdCardStatusLbl_);

    const char* logDetails[4] = {
        "Sampling Rate: ~40 Hz (25ms loop)",
        "Active Channels: 28 SAE PIDs & DIDs",
        "Rolling Ring Buffer: 60s Pre-Incident",
        "Flash Format: CSV (Auto-Flushed)"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* dLbl = lv_label_create(leftCard);
        lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(dLbl, C_DIM, 0);
        lv_label_set_text(dLbl, logDetails[i]);
        lv_obj_align(dLbl, LV_ALIGN_TOP_LEFT, 14, 75 + i * 26);
        lockNoScroll(dLbl);
    }

    lv_obj_t* lFoot = lv_label_create(leftCard);
    lv_obj_set_style_text_font(lFoot, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(lFoot, C_CHROME, 0);
    lv_label_set_text(lFoot, "Continuous background session active");
    lv_obj_align(lFoot, LV_ALIGN_BOTTOM_LEFT, 14, -12);
    lockNoScroll(lFoot);

    // Right Card: Black Box Incident Recorder (366x286 @ 410, 50)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 366, 286);
    lv_obj_set_pos(rightCard, 410, 50);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 16, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    incidentTitleLbl_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(incidentTitleLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(incidentTitleLbl_, C_CHROME, 0);
    lv_label_set_text(incidentTitleLbl_, "BLACK BOX INCIDENT RECORDER");
    lv_obj_align(incidentTitleLbl_, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(incidentTitleLbl_);

    lv_obj_t* incStatus = lv_label_create(rightCard);
    lv_obj_set_style_text_font(incStatus, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(incStatus, C_OK, 0);
    lv_label_set_text(incStatus, "Triggers: Armed • 0 Fault Latch");
    lv_obj_align(incStatus, LV_ALIGN_TOP_LEFT, 14, 38);
    lockNoScroll(incStatus);

    const char* triggers[4] = {
        "1. Coolant Temp Spike (> 226 °F)",
        "2. Engine Oil Temp Spike (> 250 °F)",
        "3. Knock Retard Spike (> 2.0 deg)",
        "4. Hard Braking / ABS (> 0.85g)"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* tLbl = lv_label_create(rightCard);
        lv_obj_set_style_text_font(tLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tLbl, C_TEXT, 0);
        lv_label_set_text(tLbl, triggers[i]);
        lv_obj_align(tLbl, LV_ALIGN_TOP_LEFT, 14, 75 + i * 26);
        lockNoScroll(tLbl);
    }

    lv_obj_t* rFoot = lv_label_create(rightCard);
    lv_obj_set_style_text_font(rFoot, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(rFoot, C_DIM, 0);
    lv_label_set_text(rFoot, "Telemetry snap auto-saved on trigger");
    lv_obj_align(rFoot, LV_ALIGN_BOTTOM_LEFT, 14, -12);
    lockNoScroll(rFoot);

    return scr;
}

void AndroidMx5UI::buildDtcRepairModal(lv_obj_t* parent) {
    dtcModalCard_ = lv_obj_create(parent);
    lv_obj_remove_style_all(dtcModalCard_);
    lv_obj_set_size(dtcModalCard_, 752, 286);
    lv_obj_set_pos(dtcModalCard_, 24, 50);
    lv_obj_set_style_bg_color(dtcModalCard_, lv_color_hex(0x0C0D0F), 0);
    lv_obj_set_style_bg_opa(dtcModalCard_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(dtcModalCard_, 16, 0);
    lv_obj_set_style_border_color(dtcModalCard_, C_ACCENT, 0);
    lv_obj_set_style_border_width(dtcModalCard_, 2, 0);
    lv_obj_add_flag(dtcModalCard_, LV_OBJ_FLAG_HIDDEN);
    lockNoScroll(dtcModalCard_);

    dtcModalTitle_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalTitle_, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(dtcModalTitle_, C_ACCENT, 0);
    lv_label_set_text(dtcModalTitle_, "DTC P0421 - REPAIR GUIDE");
    lv_obj_align(dtcModalTitle_, LV_ALIGN_TOP_LEFT, 24, 16);
    lockNoScroll(dtcModalTitle_);

    dtcModalMeaning_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalMeaning_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(dtcModalMeaning_, C_TEXT, 0);
    lv_label_set_text(dtcModalMeaning_, "Warm Up Catalyst Efficiency Below Threshold (Bank 1)");
    lv_obj_align(dtcModalMeaning_, LV_ALIGN_TOP_LEFT, 24, 54);
    lockNoScroll(dtcModalMeaning_);

    dtcModalChecklist_ = lv_label_create(dtcModalCard_);
    lv_obj_set_style_text_font(dtcModalChecklist_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(dtcModalChecklist_, C_DIM, 0);
    lv_label_set_text(dtcModalChecklist_, "1. Inspect O2 sensor upstream / downstream voltages\n2. Check for exhaust manifold leaks prior to catalytic converter\n3. Verify fuel trims (STFT/LTFT) within +/- 10%");
    lv_obj_align(dtcModalChecklist_, LV_ALIGN_TOP_LEFT, 24, 94);
    lockNoScroll(dtcModalChecklist_);

    lv_obj_t* closeBtn = lv_obj_create(dtcModalCard_);
    lv_obj_remove_style_all(closeBtn);
    lv_obj_set_size(closeBtn, 140, 38);
    lv_obj_align(closeBtn, LV_ALIGN_BOTTOM_RIGHT, -24, -16);
    lv_obj_set_style_bg_color(closeBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(closeBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(closeBtn, 8, 0);
    lv_obj_add_event_cb(closeBtn, onDtcModalCloseClick, LV_EVENT_CLICKED, this);
    lockNoScroll(closeBtn);

    lv_obj_t* cLbl = lv_label_create(closeBtn);
    lv_obj_set_style_text_font(cLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(cLbl, C_TEXT, 0);
    lv_label_set_text(cLbl, "CLOSE");
    lv_obj_center(cLbl);
    lv_obj_remove_flag(cLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(cLbl);
    dtcModalCloseLbl_ = cLbl;
}

void AndroidMx5UI::buildSdFormatModal(lv_obj_t*) {}
void AndroidMx5UI::renderIncidentChart(uint8_t) {}

// ---------------------------------------------------------------------------
// Screen 14 - BLE Connection & OBD-II Scanner Configuration
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildBleConfigScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_BLE_CONFIG] = addSubScreenHeader(scr, "OBD-II ADAPTER & BLUETOOTH", SCREEN_BLE_CONFIG);

    // Left Card: Connected Device Status (366x286 @ 24, 50)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 366, 286);
    lv_obj_set_pos(leftCard, 24, 50);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 16, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    lv_obj_t* lTitle = lv_label_create(leftCard);
    lv_obj_set_style_text_font(lTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(lTitle, C_CHROME, 0);
    lv_label_set_text(lTitle, "ACTIVE CONNECTION STATUS");
    lv_obj_align(lTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(lTitle);

    lv_obj_t* devNameLbl = lv_label_create(leftCard);
    lv_obj_set_style_text_font(devNameLbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(devNameLbl, C_OK, 0);
    lv_label_set_text(devNameLbl, "vLinker MS (Connected)");
    lv_obj_align(devNameLbl, LV_ALIGN_TOP_LEFT, 14, 38);
    lockNoScroll(devNameLbl);

    const char* details[4] = {
        "Transport: Bluetooth Classic (RFCOMM / SPP)",
        "Protocol: ISO 15765-4 CAN (11/500k)",
        "Baud / Throughput: 115200 bps (~40 Hz)",
        "Signal Quality: Excellent (-58 dBm)"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* dLbl = lv_label_create(leftCard);
        lv_obj_set_style_text_font(dLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(dLbl, C_DIM, 0);
        lv_label_set_text(dLbl, details[i]);
        lv_obj_align(dLbl, LV_ALIGN_TOP_LEFT, 14, 75 + i * 26);
        lockNoScroll(dLbl);
    }

    // Right Card: Scan & Management Tools (366x286 @ 410, 50)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 366, 286);
    lv_obj_set_pos(rightCard, 410, 50);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 16, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    lv_obj_t* rTitle = lv_label_create(rightCard);
    lv_obj_set_style_text_font(rTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(rTitle, C_CHROME, 0);
    lv_label_set_text(rTitle, "ADAPTER MANAGEMENT");
    lv_obj_align(rTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(rTitle);

    lv_obj_t* scanBtn = lv_obj_create(rightCard);
    lv_obj_remove_style_all(scanBtn);
    lv_obj_set_size(scanBtn, 338, 42);
    lv_obj_set_pos(scanBtn, 14, 45);
    lv_obj_set_style_bg_color(scanBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(scanBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(scanBtn, 10, 0);
    lockNoScroll(scanBtn);

    lv_obj_t* sLbl = lv_label_create(scanBtn);
    lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(sLbl, C_TEXT, 0);
    lv_label_set_text(sLbl, "AUTO-RECONNECT TO PAIRED ADAPTER");
    lv_obj_center(sLbl);
    lv_obj_remove_flag(sLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(sLbl);

    lv_obj_t* noteLbl = lv_label_create(rightCard);
    lv_obj_set_style_text_font(noteLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(noteLbl, C_DIM, 0);
    lv_label_set_text(noteLbl, "Compatible with vLinker MS / MC / FD,\nOBDLink MX+, Veepeak, and all standard\nELM327 Bluetooth Classic OBD-II adapters.");
    lv_obj_align(noteLbl, LV_ALIGN_TOP_LEFT, 14, 105);
    lockNoScroll(noteLbl);

    return scr;
}

lv_obj_t* AndroidMx5UI::buildSetupWizard() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_WIZARD] = addSubScreenHeader(scr, "INITIAL SETUP WIZARD", SCREEN_WIZARD);

    // Main Card (752x286 @ 24, 50)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 752, 286);
    lv_obj_set_pos(card, 24, 50);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lockNoScroll(card);

    lv_obj_t* wTitle = lv_label_create(card);
    lv_obj_set_style_text_font(wTitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(wTitle, C_ACCENT, 0);
    lv_label_set_text(wTitle, "WELCOME TO MX-5 ND2 DIGITAL CLUSTER");
    lv_obj_align(wTitle, LV_ALIGN_TOP_LEFT, 24, 18);
    lockNoScroll(wTitle);

    const char* steps[4] = {
        "1. Verify OBD-II Scanner Connection (vLinker MS / OBDLink / ELM327)",
        "2. Configure Transmission (Automatic 6AT vs Manual 6MT) & Measurement Units",
        "3. Calibrate 4-Corner TPMS Wheel Sensor ID Binding (DIDs 2A05 - 2A08)",
        "4. High-Refresh Telemetry Engine & Continuous Incident Datalogger Ready"
    };
    for (int i = 0; i < 4; i++) {
        lv_obj_t* sLbl = lv_label_create(card);
        lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(sLbl, C_TEXT, 0);
        lv_label_set_text(sLbl, steps[i]);
        lv_obj_align(sLbl, LV_ALIGN_TOP_LEFT, 24, 56 + i * 36);
        lockNoScroll(sLbl);
    }

    auto makeWizBtn = [this](lv_obj_t* parent, int16_t x, int16_t y, int16_t w, int16_t h, const char* txt, uintptr_t id, lv_color_t bgCol) -> lv_obj_t* {
        lv_obj_t* b = lv_obj_create(parent);
        lv_obj_remove_style_all(b);
        lv_obj_set_size(b, w, h);
        lv_obj_set_pos(b, x, y);
        lv_obj_set_style_bg_color(b, bgCol, 0);
        lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(b, 10, 0);
        lv_obj_set_style_border_color(b, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(b, 1, 0);
        lv_obj_set_user_data(b, (void*)id);
        lv_obj_add_event_cb(b, onWizardActionClick, LV_EVENT_CLICKED, this);
        lockNoScroll(b);

        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lv_obj_remove_flag(l, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(l);
        return b;
    };

    makeWizBtn(card, 24, 220, 160, 44, "BLUETOOTH SETUP", 401, lv_color_hex(0x191A20));
    makeWizBtn(card, 200, 220, 160, 44, "SETTINGS / UNITS", 402, lv_color_hex(0x191A20));
    makeWizBtn(card, 376, 220, 160, 44, "TPMS LEARN", 403, lv_color_hex(0x191A20));
    makeWizBtn(card, 552, 220, 176, 44, "FINISH SETUP", 404, C_ACCENT);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 16 - TPMS Wheel Calibration & Sensor Binding
// ---------------------------------------------------------------------------
lv_obj_t* AndroidMx5UI::buildWheelMapScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    speedChipLabel_[SCREEN_WHEEL_MAP] = addSubScreenHeader(scr, "TPMS WHEEL CALIBRATION", SCREEN_WHEEL_MAP);

    // Left Panel: 4 Wheel Target Cards & Chassis (460x286 @ 24, 50)
    lv_obj_t* leftCard = lv_obj_create(scr);
    lv_obj_remove_style_all(leftCard);
    lv_obj_set_size(leftCard, 460, 286);
    lv_obj_set_pos(leftCard, 24, 50);
    lv_obj_set_style_bg_color(leftCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(leftCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(leftCard, 16, 0);
    lv_obj_set_style_border_color(leftCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(leftCard, 1, 0);
    lockNoScroll(leftCard);

    // Real Mazda MX-5 RF Top-Down Image
    lv_obj_t* carImg = lv_image_create(leftCard);
    lv_image_set_src(carImg, &mx5_rf_cal_dsc);
    lv_obj_align(carImg, LV_ALIGN_CENTER, 0, 0);
    lockNoScroll(carImg);

    const char* names[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
    const char* defDids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
    const int16_t xs[4] = {14, 274, 14, 274};
    const int16_t ys[4] = {16, 16, 150, 150};

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* cell = lv_obj_create(leftCard);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, 172, 120);
        lv_obj_set_pos(cell, xs[i], ys[i]);
        lv_obj_set_style_bg_color(cell, (i == 0) ? lv_color_hex(0x201214) : lv_color_hex(0x13161B), 0);
        lv_obj_set_style_bg_opa(cell, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(cell, 12, 0);
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
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 10, 8);
        lv_obj_remove_flag(tag, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(tag);
        wheelCellLbl_[i] = tag;

        lv_obj_t* didLbl = lv_label_create(cell);
        lv_obj_set_style_text_font(didLbl, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(didLbl, C_OK, 0);
        lv_label_set_text(didLbl, defDids[i]);
        lv_obj_align(didLbl, LV_ALIGN_LEFT_MID, 10, -4);
        lv_obj_remove_flag(didLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(didLbl);
        wheelCellDid_[i] = didLbl;

        lv_obj_t* pressLbl = lv_label_create(cell);
        lv_obj_set_style_text_font(pressLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(pressLbl, C_SPEED, 0);
        lv_label_set_text(pressLbl, "-- PSI");
        lv_obj_align(pressLbl, LV_ALIGN_BOTTOM_LEFT, 10, -8);
        lv_obj_remove_flag(pressLbl, LV_OBJ_FLAG_CLICKABLE);
        lockNoScroll(pressLbl);
        wheelCellPress_[i] = pressLbl;
    }

    // Right Panel: Step-by-Step Instructions & Actions (272x286 @ 504, 50)
    lv_obj_t* rightCard = lv_obj_create(scr);
    lv_obj_remove_style_all(rightCard);
    lv_obj_set_size(rightCard, 272, 286);
    lv_obj_set_pos(rightCard, 504, 50);
    lv_obj_set_style_bg_color(rightCard, C_PANEL, 0);
    lv_obj_set_style_bg_opa(rightCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(rightCard, 16, 0);
    lv_obj_set_style_border_color(rightCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(rightCard, 1, 0);
    lockNoScroll(rightCard);

    lv_obj_t* insTitle = lv_label_create(rightCard);
    lv_obj_set_style_text_font(insTitle, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(insTitle, C_CHROME, 0);
    lv_label_set_text(insTitle, "CALIBRATION GUIDE");
    lv_obj_align(insTitle, LV_ALIGN_TOP_LEFT, 14, 12);
    lockNoScroll(insTitle);

    wheelPrompt_ = lv_label_create(rightCard);
    lv_obj_set_style_text_font(wheelPrompt_, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(wheelPrompt_, C_DIM, 0);
    lv_label_set_text(wheelPrompt_,
        "1. Tap a wheel card to select target\n"
        "2. Deflate or inflate tire by ~3 PSI\n"
        "3. Live BCM feed will bind the DID\n"
        "4. Auto-detect assigns sensor map");
    lv_obj_align(wheelPrompt_, LV_ALIGN_TOP_LEFT, 14, 38);
    lockNoScroll(wheelPrompt_);

    // Action 301: Auto-Detect
    lv_obj_t* autoBtn = lv_obj_create(rightCard);
    lv_obj_remove_style_all(autoBtn);
    lv_obj_set_size(autoBtn, 244, 40);
    lv_obj_set_pos(autoBtn, 14, 155);
    lv_obj_set_style_bg_color(autoBtn, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(autoBtn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(autoBtn, 10, 0);
    lv_obj_set_user_data(autoBtn, (void*)(uintptr_t)301);
    lv_obj_add_event_cb(autoBtn, onWheelMapActionClick, LV_EVENT_CLICKED, this);
    lockNoScroll(autoBtn);

    lv_obj_t* aLbl = lv_label_create(autoBtn);
    lv_obj_set_style_text_font(aLbl, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(aLbl, C_TEXT, 0);
    lv_label_set_text(aLbl, "AUTO-BIND DETECTED SENSORS");
    lv_obj_center(aLbl);
    lv_obj_remove_flag(aLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(aLbl);

    // Action 302: Reset
    lv_obj_t* rstBtn = lv_obj_create(rightCard);
    lv_obj_remove_style_all(rstBtn);
    lv_obj_set_size(rstBtn, 244, 34);
    lv_obj_set_pos(rstBtn, 14, 205);
    lv_obj_set_style_bg_color(rstBtn, lv_color_hex(0x13161B), 0);
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
    lv_label_set_text(rstLbl, "RESET TO FACTORY DIDs");
    lv_obj_center(rstLbl);
    lv_obj_remove_flag(rstLbl, LV_OBJ_FLAG_CLICKABLE);
    lockNoScroll(rstLbl);

    return scr;
}

// ---------------------------------------------------------------------------
// Lifecycle & Update Dispatcher
// ---------------------------------------------------------------------------
void AndroidMx5UI::begin() {
    transAuto_ = UserPrefs::getTransAuto();
    data_local_.isAutomatic = transAuto_;

    screens_[SCREEN_SPEED]            = buildSpeedScreen();
    screens_[SCREEN_TPMS]             = buildTpmsScreen();
    screens_[SCREEN_TEMPS]            = buildEngineScreen();
    screens_[SCREEN_DIAG]             = buildDiagnosticsScreen();
    screens_[SCREEN_TRACK]            = buildTrackScreen();
    screens_[SCREEN_RPM]              = buildRpmScreen();
    screens_[SCREEN_TRIP]             = buildTripScreen();
    screens_[SCREEN_MENU]             = buildMenuScreen();
    screens_[SCREEN_DIAG_SUB_FUEL]    = buildDiagSubFuel();
    screens_[SCREEN_DIAG_SUB_CYL]     = buildDiagSubCyl();
    screens_[SCREEN_DIAG_SUB_CHASSIS] = buildDiagSubChassis();
    screens_[SCREEN_DIAG_SUB_SMOG]    = buildDiagSubSmog();
    screens_[SCREEN_DIAG_SUB_LOGS]    = buildDiagSubLogs();
    screens_[SCREEN_SETTINGS]         = buildSettingsScreen();
    screens_[SCREEN_BLE_CONFIG]       = buildBleConfigScreen();
    screens_[SCREEN_WIZARD]           = buildSetupWizard();
    screens_[SCREEN_WHEEL_MAP]        = buildWheelMapScreen();

    for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
        if (screens_[i]) {
            lv_obj_add_event_cb(screens_[i], onScreenEvent, LV_EVENT_ALL, this);
        }
    }

    if (!UserPrefs::isConfigured()) {
        setScreen(SCREEN_WIZARD);
    } else {
        setScreen(SCREEN_SPEED);
    }
}

void AndroidMx5UI::update() {
    obd_.snapshot(data_local_);

    updateSpeedChip();

    switch (currentScreen_) {
        case SCREEN_SPEED:            updateSpeedScreen();       break;
        case SCREEN_TPMS:             updateTpmsScreen();        break;
        case SCREEN_TEMPS:            updateEngineScreen();      break;
        case SCREEN_DIAG:             updateDiagnosticsScreen(); break;
        case SCREEN_TRACK:            updateTrackScreen();       break;
        case SCREEN_RPM:              updateRpmScreen();         break;
        case SCREEN_TRIP:             updateTripScreen();        break;
        case SCREEN_MENU:             updateMenuScreen();        break;

        case SCREEN_DIAG_SUB_FUEL:    updateDiagSubFuel();       break;
        case SCREEN_DIAG_SUB_CYL:     updateDiagSubCyl();        break;
        case SCREEN_DIAG_SUB_CHASSIS: updateDiagSubChassis();    break;
        case SCREEN_DIAG_SUB_SMOG:    updateDiagSubSmog();       break;
        case SCREEN_DIAG_SUB_LOGS:    updateDiagSubLogs();       break;
        case SCREEN_WHEEL_MAP:        updateWheelMapScreen();    break;
        default: break;
    }

    if (themeMode_ == THEME_AUTO) {
        if (data_local_.isNightMode != currentNightMode_) {
            applyThemeMode(data_local_.isNightMode);
        }
    }
}

void AndroidMx5UI::updateSpeedChip() {
    for (uint8_t i = 0; i < SCREEN_COUNT; i++) {
        if (speedChipLabel_[i]) {
            lv_label_set_text_fmt(speedChipLabel_[i], "%3d MPH", speedU(data_local_.speedKmh));
        }
    }
}

void AndroidMx5UI::updateSpeedScreen() {
    // 1. Always update hero speedometer & gear (100% constant and uninterrupted)
    if (speedBigLabel_) {
        lv_label_set_text_fmt(speedBigLabel_, "%u", speedU(data_local_.speedKmh));
    }
    if (gearLbl_) {
        char gBuf[2] = {data_local_.gear ? data_local_.gear : 'N', '\0'};
        lv_label_set_text(gearLbl_, gBuf);
    }

    // 2. Evaluate Dynamic Warning Triggers
    float psis[4];
    bool hasLowTire = false;
    bool hasCritTire = false;
    for (uint8_t i = 0; i < 4; i++) {
        psis[i] = pressU(data_local_.tirePressure[i]);
        if (psis[i] > 4.0f && psis[i] < 26.0f) hasLowTire = true;
        if (psis[i] > 4.0f && psis[i] < 22.0f) hasCritTire = true;
    }

    int16_t coolF = tempU(data_local_.coolantC);
    int16_t oilF  = tempU(data_local_.oilTempC);
    bool hasOverheat = (coolF >= 226 || oilF >= 252);
    bool hasLowBat   = (data_local_.batteryVolts > 5.0f && data_local_.batteryVolts < 11.6f);
    bool hasDtc      = (data_local_.dtcCount > 0);

    bool shouldWarn = (hasLowTire || hasOverheat || hasLowBat || hasDtc);

    if (shouldWarn && !warningMutedForDrive_) {
        // Activate Dynamic Warning Panel & Hide Normal Dials
        if (speedNormalRightContainer_) lv_obj_add_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
        if (speedWarningContainer_) lv_obj_remove_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);

        if (hasLowTire) {
            currentWarningType_ = 1;
            if (speedWarnTitle_) {
                lv_label_set_text(speedWarnTitle_, hasCritTire ? "CRITICAL TIRE PRESSURE DROP • TAP TO DISMISS" : "LOW TIRE PRESSURE DETECTED • TAP TO DISMISS");
            }
            if (speedWarnBanner_) {
                lv_obj_set_style_bg_color(speedWarnBanner_, hasCritTire ? C_DANGER : C_ACCENT, 0);
            }
            if (speedWarnTempsCard_) lv_obj_add_flag(speedWarnTempsCard_, LV_OBJ_FLAG_HIDDEN);
            if (speedWarnCarImg_) lv_obj_remove_flag(speedWarnCarImg_, LV_OBJ_FLAG_HIDDEN);

            for (uint8_t i = 0; i < 4; i++) {
                if (speedWarnTpmsPod_[i]) {
                    lv_obj_remove_flag(speedWarnTpmsPod_[i], LV_OBJ_FLAG_HIDDEN);
                    bool isLow = (psis[i] > 4.0f && psis[i] < 26.0f);
                    lv_obj_set_style_bg_color(speedWarnTpmsPod_[i], isLow ? lv_color_hex(0x281014) : lv_color_hex(0x0E1013), 0);
                    lv_obj_set_style_border_color(speedWarnTpmsPod_[i], isLow ? C_ACCENT : C_PANEL_BRD, 0);
                    lv_obj_set_style_border_width(speedWarnTpmsPod_[i], isLow ? 2 : 1, 0);
                }
                if (speedWarnTpmsVal_[i]) {
                    bool isLow = (psis[i] > 4.0f && psis[i] < 26.0f);
                    lv_obj_set_style_text_color(speedWarnTpmsVal_[i], isLow ? C_ACCENT : C_SPEED, 0);
                    if (isLow) {
                        lv_label_set_text_fmt(speedWarnTpmsVal_[i], "%.1f PSI LOW", psis[i]);
                    } else {
                        lv_label_set_text_fmt(speedWarnTpmsVal_[i], "%.1f PSI", psis[i]);
                    }
                }
            }
            if (speedWarnSubDetail_) {
                lv_label_set_text(speedWarnSubDetail_, "Recommended Cold Pressure: 29.0 PSI • Check tire immediately");
            }
        } else if (hasOverheat) {
            currentWarningType_ = 2;
            if (speedWarnTitle_) lv_label_set_text(speedWarnTitle_, "ENGINE TEMPERATURE OVERHEAT • TAP TO DISMISS");
            if (speedWarnBanner_) lv_obj_set_style_bg_color(speedWarnBanner_, C_DANGER, 0);
            if (speedWarnTempsCard_) lv_obj_remove_flag(speedWarnTempsCard_, LV_OBJ_FLAG_HIDDEN);
            if (speedWarnCarImg_) lv_obj_add_flag(speedWarnCarImg_, LV_OBJ_FLAG_HIDDEN);
            for (uint8_t i = 0; i < 4; i++) {
                if (speedWarnTpmsPod_[i]) lv_obj_add_flag(speedWarnTpmsPod_[i], LV_OBJ_FLAG_HIDDEN);
            }

            if (speedWarnCoolantVal_) {
                lv_label_set_text_fmt(speedWarnCoolantVal_, "ENGINE COOLANT: %d F %s", coolF, (coolF >= 226) ? "OVERHEAT" : "NORMAL");
                lv_obj_set_style_text_color(speedWarnCoolantVal_, (coolF >= 226) ? C_DANGER : C_OK, 0);
            }
            if (speedWarnOilVal_) {
                lv_label_set_text_fmt(speedWarnOilVal_, "ENGINE OIL TEMP: %d F %s", oilF, (oilF >= 252) ? "OVERHEAT" : "NORMAL");
                lv_obj_set_style_text_color(speedWarnOilVal_, (oilF >= 252) ? C_DANGER : C_OK, 0);
            }
            if (speedWarnDetail_) {
                lv_label_set_text(speedWarnDetail_, "Reduce engine RPM and pull over safely to prevent engine damage.");
            }
        } else if (hasLowBat) {
            currentWarningType_ = 3;
            if (speedWarnTitle_) lv_label_set_text(speedWarnTitle_, "LOW BATTERY VOLTAGE • TAP TO DISMISS");
            if (speedWarnBanner_) lv_obj_set_style_bg_color(speedWarnBanner_, C_WARN, 0);
            if (speedWarnTempsCard_) lv_obj_remove_flag(speedWarnTempsCard_, LV_OBJ_FLAG_HIDDEN);
            if (speedWarnCarImg_) lv_obj_add_flag(speedWarnCarImg_, LV_OBJ_FLAG_HIDDEN);
            for (uint8_t i = 0; i < 4; i++) {
                if (speedWarnTpmsPod_[i]) lv_obj_add_flag(speedWarnTpmsPod_[i], LV_OBJ_FLAG_HIDDEN);
            }

            if (speedWarnCoolantVal_) {
                lv_label_set_text_fmt(speedWarnCoolantVal_, "BATTERY VOLTS: %.1f V LOW", data_local_.batteryVolts);
                lv_obj_set_style_text_color(speedWarnCoolantVal_, C_WARN, 0);
            }
            if (speedWarnOilVal_) {
                lv_label_set_text_fmt(speedWarnOilVal_, "CHARGING SYSTEM: INSUFFICIENT OUTPUT");
                lv_obj_set_style_text_color(speedWarnOilVal_, C_CHROME, 0);
            }
            if (speedWarnDetail_) {
                lv_label_set_text(speedWarnDetail_, "Alternator output is below 11.8V. Check battery terminals and belt.");
            }
        } else if (hasDtc) {
            currentWarningType_ = 4;
            if (speedWarnTitle_) lv_label_set_text(speedWarnTitle_, "CHECK ENGINE DTC FAULT • TAP TO DISMISS");
            if (speedWarnBanner_) lv_obj_set_style_bg_color(speedWarnBanner_, C_WARN, 0);
            if (speedWarnTempsCard_) lv_obj_remove_flag(speedWarnTempsCard_, LV_OBJ_FLAG_HIDDEN);
            if (speedWarnCarImg_) lv_obj_add_flag(speedWarnCarImg_, LV_OBJ_FLAG_HIDDEN);
            for (uint8_t i = 0; i < 4; i++) {
                if (speedWarnTpmsPod_[i]) lv_obj_add_flag(speedWarnTpmsPod_[i], LV_OBJ_FLAG_HIDDEN);
            }

            if (speedWarnCoolantVal_) {
                lv_label_set_text_fmt(speedWarnCoolantVal_, "STORED ECU FAULTS: %u CODE(S)", data_local_.dtcCount);
                lv_obj_set_style_text_color(speedWarnCoolantVal_, C_WARN, 0);
            }
            if (speedWarnOilVal_) {
                lv_label_set_text(speedWarnOilVal_, "CHECK DIAGNOSTICS MENU FOR CODES");
                lv_obj_set_style_text_color(speedWarnOilVal_, C_CHROME, 0);
            }
            if (speedWarnDetail_) {
                lv_label_set_text(speedWarnDetail_, "Swipe up to Menu Hub and tap Diagnostics to view/clear codes.");
            }
        }
    } else {
        // No Warnings (or Dismissed) -> Show Normal Dials & Update
        currentWarningType_ = 0;
        if (speedNormalRightContainer_) lv_obj_remove_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
        if (speedWarningContainer_) lv_obj_add_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);

        updateDottedValue(rpmSeg_, data_local_.rpm / 8000.0f);
        if (data_local_.rpm > 6500) warnTopDots(rpmSeg_);
        if (rpmSeg_.val) {
            lv_label_set_text_fmt(rpmSeg_.val, "%u", data_local_.rpm);
        }

        if (data_local_.fuelLevelPct == 0) {
            updateDottedValue(fuelSeg_, 0.0f);
            if (fuelSeg_.val) lv_label_set_text(fuelSeg_.val, "--%");
        } else {
            updateDottedValue(fuelSeg_, data_local_.fuelLevelPct / 100.0f);
            if (fuelSeg_.val) {
                lv_label_set_text_fmt(fuelSeg_.val, "%u%%", data_local_.fuelLevelPct);
            }
        }

        if (data_local_.ambientC == 0) {
            updateDottedValue(ambientSeg_, 0.0f);
            if (ambientSeg_.val) lv_label_set_text(ambientSeg_.val, "--°");
        } else {
            updateDottedValue(ambientSeg_, (tempU(data_local_.ambientC) + 20) / 140.0f);
            if (ambientSeg_.val) {
                lv_label_set_text_fmt(ambientSeg_.val, "%d°", tempU(data_local_.ambientC));
            }
        }
    }
}

void AndroidMx5UI::updateTpmsScreen() {
    for (uint8_t i = 0; i < 4; i++) {
        float psi = pressU(data_local_.tirePressure[i]);
        bool isKnown = data_local_.tireKnown[i] && (psi > 4.0f);
        bool isLow = isKnown && (psi < 26.0f);
        bool isCrit = isKnown && (psi < 22.0f);

        if (tpmsLabel_[i]) {
            if (isKnown) {
                lv_label_set_text_fmt(tpmsLabel_[i], "%.1f", psi);
            } else {
                lv_label_set_text(tpmsLabel_[i], "--");
            }
            lv_obj_set_style_text_color(tpmsLabel_[i], isLow ? C_ACCENT : C_SPEED, 0);
        }

        if (tpmsTemp_[i]) {
            if (isKnown && data_local_.tireTemp[i] > 0.0f) {
                lv_label_set_text_fmt(tpmsTemp_[i], "%d °F", tempU(data_local_.tireTemp[i]));
            } else {
                lv_label_set_text(tpmsTemp_[i], "-- °F");
            }
        }

        if (tpmsDot_[i]) {
            lv_color_t dotCol = !isKnown ? C_DIM : (isCrit ? C_DANGER : (isLow ? C_WARN : C_OK));
            lv_obj_set_style_bg_color(tpmsDot_[i], dotCol, 0);
        }

        if (tpmsCard_[i]) {
            lv_obj_set_style_border_color(tpmsCard_[i], isLow ? C_ACCENT : C_PANEL_BRD, 0);
            lv_obj_set_style_border_width(tpmsCard_[i], isLow ? 2 : 1, 0);
            lv_obj_set_style_bg_color(tpmsCard_[i], isLow ? lv_color_hex(0x281014) : C_PANEL, 0);
        }
    }
}

void AndroidMx5UI::updateRpmScreen() {
    updateDottedValue(rpmBigSeg_, data_local_.rpm / 8000.0f);
    if (data_local_.rpm > 6500) warnTopDots(rpmBigSeg_);
    if (rpmBigSeg_.val) lv_label_set_text_fmt(rpmBigSeg_.val, "%u", data_local_.rpm);

    updateDottedValue(loadSeg_, data_local_.engineLoadPct / 100.0f);
    if (loadSeg_.val) lv_label_set_text_fmt(loadSeg_.val, "%u%%", data_local_.engineLoadPct);

    updateDottedValue(throttleSeg_, data_local_.throttlePct / 100.0f);
    if (throttleSeg_.val) lv_label_set_text_fmt(throttleSeg_.val, "%u%%", data_local_.throttlePct);

    updateDottedValue(fuelArcSeg_, data_local_.fuelLevelPct / 100.0f);
    if (fuelArcSeg_.val) {
        if (data_local_.fuelLevelPct == 0) lv_label_set_text(fuelArcSeg_.val, "--%");
        else lv_label_set_text_fmt(fuelArcSeg_.val, "%u%%", data_local_.fuelLevelPct);
    }

    if (data_local_.batteryVolts < 1.0f) {
        updateDottedValue(batArcSeg_, 0.0f);
        if (batArcSeg_.val) lv_label_set_text(batArcSeg_.val, "--V");
    } else {
        updateDottedValue(batArcSeg_, (data_local_.batteryVolts - 10.0f) / 5.0f);
        if (batArcSeg_.val) lv_label_set_text_fmt(batArcSeg_.val, "%.1fV", data_local_.batteryVolts);
    }
}

void AndroidMx5UI::updateEngineScreen() {
    if (data_local_.coolantC == 0) {
        updateDottedValue(coolantSeg_, 0.0f);
        if (coolantSeg_.val) lv_label_set_text(coolantSeg_.val, "-- °F");
    } else {
        updateDottedValue(coolantSeg_, (tempU(data_local_.coolantC) - 100.0f) / 160.0f);
        if (coolantSeg_.val) lv_label_set_text_fmt(coolantSeg_.val, "%d °F", tempU(data_local_.coolantC));
    }

    if (data_local_.oilTempC == 0) {
        updateDottedValue(oilSeg_, 0.0f);
        if (oilSeg_.val) lv_label_set_text(oilSeg_.val, "-- °F");
    } else {
        updateDottedValue(oilSeg_, (tempU(data_local_.oilTempC) - 100.0f) / 180.0f);
        if (oilSeg_.val) lv_label_set_text_fmt(oilSeg_.val, "%d °F", tempU(data_local_.oilTempC));
    }

    if (data_local_.intakeAirC == 0) {
        updateDottedValue(intakeSeg_, 0.0f);
        if (intakeSeg_.val) lv_label_set_text(intakeSeg_.val, "-- °F");
    } else {
        updateDottedValue(intakeSeg_, (tempU(data_local_.intakeAirC) + 20.0f) / 140.0f);
        if (intakeSeg_.val) lv_label_set_text_fmt(intakeSeg_.val, "%d °F", tempU(data_local_.intakeAirC));
    }

    if (data_local_.batteryVolts < 1.0f) {
        updateDottedValue(batterySeg_, 0.0f);
        if (batterySeg_.val) lv_label_set_text(batterySeg_.val, "-- V");
    } else {
        updateDottedValue(batterySeg_, (data_local_.batteryVolts - 10.0f) / 5.0f);
        if (batterySeg_.val) lv_label_set_text_fmt(batterySeg_.val, "%.1f V", data_local_.batteryVolts);
    }
}

void AndroidMx5UI::updateTrackScreen() {
    if (trackTimerLbl_) {
        lv_label_set_text_fmt(trackTimerLbl_, "%.2f s", data_local_.accel0to60TimeSec);
    }
    if (trackBestLbl_) {
        if (data_local_.best0to60TimeSec <= 0.01f) {
            lv_label_set_text(trackBestLbl_, "BEST: -- s");
        } else {
            lv_label_set_text_fmt(trackBestLbl_, "BEST: %.2f s", data_local_.best0to60TimeSec);
        }
    }
    updateDottedValue(hpSeg_, data_local_.estHorsepower / 200.0f);
    if (hpSeg_.val) lv_label_set_text_fmt(hpSeg_.val, "%u HP", data_local_.estHorsepower);

    updateDottedValue(torqueSeg_, data_local_.estTorqueFtLb / 180.0f);
    if (torqueSeg_.val) lv_label_set_text_fmt(torqueSeg_.val, "%u lb-ft", data_local_.estTorqueFtLb);

    if (trackThrottleBar_) lv_bar_set_value(trackThrottleBar_, data_local_.throttlePct, LV_ANIM_OFF);
    if (trackThrottleVal_) lv_label_set_text_fmt(trackThrottleVal_, "%u%%", data_local_.throttlePct);

    if (trackLoadBar_) lv_bar_set_value(trackLoadBar_, data_local_.engineLoadPct, LV_ANIM_OFF);
    if (trackLoadVal_) lv_label_set_text_fmt(trackLoadVal_, "%u%%", data_local_.engineLoadPct);
}

void AndroidMx5UI::updateTripScreen() {
    updateDottedValue(instantMpgSeg_, data_local_.instantMpg / 60.0f);
    if (instantMpgSeg_.val) lv_label_set_text_fmt(instantMpgSeg_.val, "%.1f", data_local_.instantMpg);

    if (tripAvgVal_) {
        if (data_local_.tripAvgMpg <= 0.01f) {
            lv_label_set_text(tripAvgVal_, "--");
        } else {
            lv_label_set_text_fmt(tripAvgVal_, "%.1f", data_local_.tripAvgMpg);
        }
    }
    if (tripRangeVal_) {
        if (data_local_.rangeMiles == 0) {
            lv_label_set_text(tripRangeVal_, "--");
        } else {
            lv_label_set_text_fmt(tripRangeVal_, "%u", data_local_.rangeMiles);
        }
    }
    if (tripDistVal_) {
        if (data_local_.tripDistanceMiles <= 0.01f) {
            lv_label_set_text(tripDistVal_, "--");
        } else {
            lv_label_set_text_fmt(tripDistVal_, "%.1f", data_local_.tripDistanceMiles);
        }
    }
    if (tripFuelVal_) {
        if (data_local_.fuelLevelPct == 0) {
            lv_label_set_text(tripFuelVal_, "--%");
        } else {
            lv_label_set_text_fmt(tripFuelVal_, "%u%%", data_local_.fuelLevelPct);
        }
    }
}

void AndroidMx5UI::updateDiagnosticsScreen() {
    if (diagBattery_) {
        if (data_local_.batteryVolts < 1.0f) {
            lv_label_set_text(diagBattery_, "Battery: -- V");
        } else {
            lv_label_set_text_fmt(diagBattery_, "Battery: %.1f V", data_local_.batteryVolts);
        }
    }
    if (data_local_.dtcCount > 0) {
        if (diagDtcLbl_) {
            lv_label_set_text_fmt(diagDtcLbl_, "DTC %s: %s", data_local_.dtcCodes[0], data_local_.dtcDesc[0]);
            lv_obj_set_style_text_color(diagDtcLbl_, C_ACCENT, 0);
        }
        if (diagHint_) {
            lv_label_set_text(diagHint_, "Tap code box to open Technical Repair Guide");
            lv_obj_set_style_text_color(diagHint_, C_WARN, 0);
        }
        if (diagDtcBox_) {
            lv_obj_set_style_border_color(diagDtcBox_, C_ACCENT, 0);
            lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x281014), 0);
        }
    } else {
        if (diagDtcLbl_) {
            lv_label_set_text(diagDtcLbl_, "OK: NO FAULT CODES STORED (ECU & ABS NORMAL)");
            lv_obj_set_style_text_color(diagDtcLbl_, C_OK, 0);
        }
        if (diagHint_) {
            lv_label_set_text(diagHint_, "All powertrain and chassis modules reporting zero faults");
            lv_obj_set_style_text_color(diagHint_, C_DIM, 0);
        }
        if (diagDtcBox_) {
            lv_obj_set_style_border_color(diagDtcBox_, C_PANEL_BRD, 0);
            lv_obj_set_style_bg_color(diagDtcBox_, lv_color_hex(0x0E1013), 0);
        }
    }
}

void AndroidMx5UI::updateMenuScreen() {
    for (uint8_t i = 0; i < CONTENT_SCREEN_COUNT; i++) {
        if (menuPods_[i]) {
            bool active = (i == lastContentScreen_);
            lv_obj_set_style_border_color(menuPods_[i], active ? C_ACCENT : C_PANEL_BRD, 0);
            lv_obj_set_style_border_width(menuPods_[i], active ? 2 : 1, 0);
        }
    }
}

void AndroidMx5UI::updateSettingsScreen() {
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

    highlight(btnTransAuto_, transAuto_);
    highlight(btnTransManual_, !transAuto_);

    highlight(btnUnitUs_, unitsUs_);
    highlight(btnUnitMet_, !unitsUs_);

    highlight(btnLogAuto_, autoLogEnabled_);
    highlight(btnLogDis_, !autoLogEnabled_);

    highlight(btnMaskOn_, speedMaskEnabled_);
    highlight(btnMaskOff_, !speedMaskEnabled_);
}

void AndroidMx5UI::applyThemeMode(bool isNight) {
    currentNightMode_ = isNight;
    themeText_   = isNight ? C_ACCENT : lv_color_hex(0xFFFFFF);
    themeSpeed_  = isNight ? C_ACCENT : lv_color_hex(0xFFFFFF);
    themeDim_    = isNight ? C_ACCENT_DM : lv_color_hex(0x8E949F);
    themeDotLit_ = isNight ? C_ACCENT : lv_color_hex(0xFFFFFF);

    if (speedBigLabel_) lv_obj_set_style_text_color(speedBigLabel_, themeSpeed_, 0);
}

void AndroidMx5UI::setThemeMode(ThemeMode mode) {
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

void AndroidMx5UI::setNightMode(bool isNight) {
    applyThemeMode(isNight);
}

void AndroidMx5UI::setRotation(uint8_t rot) {
    currentRotation_ = rot;
    updateSettingsScreen();
}

void AndroidMx5UI::setBrightness(uint8_t pct) {
    if (pct > 100) pct = 100;
    userBrightness_ = pct;
    updateSettingsScreen();
}

// Callbacks
void AndroidMx5UI::onMenuIconClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t screenIdx = (uintptr_t)lv_obj_get_user_data(target);
    ui->setScreen((uint8_t)screenIdx);
}

void AndroidMx5UI::onSubScreenNavClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t targetScreen = (uintptr_t)lv_obj_get_user_data(target);
    ui->setScreen((uint8_t)targetScreen);
}

void AndroidMx5UI::onDtcActionClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* target = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t act = (uintptr_t)lv_obj_get_user_data(target);
    if (act == 1) {
        ui->openDtcGuide(ui->currentDtcIndex_);
    }
}

void AndroidMx5UI::onDtcCycleClick(lv_event_t*) {}
void AndroidMx5UI::onDtcModalCloseClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (ui && ui->dtcModalCard_) {
        lv_obj_add_flag(ui->dtcModalCard_, LV_OBJ_FLAG_HIDDEN);
    }
}
void AndroidMx5UI::onSdFormatClick(lv_event_t*) {}
void AndroidMx5UI::onIncidentSelectClick(lv_event_t*) {}

void AndroidMx5UI::onSettingsActionClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    switch (action) {
        case 101: ui->setRotation(1); UserPrefs::saveRotation(1); break;
        case 102: ui->setRotation(3); UserPrefs::saveRotation(3); break;
        case 103: ui->setThemeMode(THEME_AUTO); UserPrefs::saveThemeMode(0); break;
        case 104: ui->setThemeMode(THEME_DAY); UserPrefs::saveThemeMode(1); break;
        case 105: ui->setThemeMode(THEME_NIGHT); UserPrefs::saveThemeMode(2); break;
        case 106: ui->setBrightness(25); UserPrefs::saveBrightness(25); break;
        case 107: ui->setBrightness(50); UserPrefs::saveBrightness(50); break;
        case 108: ui->setBrightness(75); UserPrefs::saveBrightness(75); break;
        case 109: ui->setBrightness(100); UserPrefs::saveBrightness(100); break;
        case 110: ui->unitsUs_ = true; UserPrefs::saveUnits(1); ui->updateSettingsScreen(); break;
        case 111: ui->unitsUs_ = false; UserPrefs::saveUnits(0); ui->updateSettingsScreen(); break;
        case 112: ui->autoLogEnabled_ = true; UserPrefs::saveAutoLog(1); ui->updateSettingsScreen(); break;
        case 113: ui->autoLogEnabled_ = false; UserPrefs::saveAutoLog(0); ui->updateSettingsScreen(); break;
        case 114: ui->speedMaskEnabled_ = true; UserPrefs::saveSpeedMask(1); ui->updateSettingsScreen(); break;
        case 115: ui->speedMaskEnabled_ = false; UserPrefs::saveSpeedMask(0); ui->updateSettingsScreen(); break;
        case 116: ui->setScreen(SCREEN_BLE_CONFIG); break;
        case 117: ui->setScreen(SCREEN_WHEEL_MAP); break;
        case 118: ui->transAuto_ = true; UserPrefs::saveTransAuto(true); ui->data_local_.isAutomatic = true; ui->updateSettingsScreen(); break;
        case 119: ui->transAuto_ = false; UserPrefs::saveTransAuto(false); ui->data_local_.isAutomatic = false; ui->updateSettingsScreen(); break;
    }
}

void AndroidMx5UI::onBleConfigActionClick(lv_event_t*) {}
void AndroidMx5UI::onWizardActionClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    switch (action) {
        case 401: ui->setScreen(SCREEN_BLE_CONFIG); break;
        case 402: ui->setScreen(SCREEN_SETTINGS); break;
        case 403: ui->setScreen(SCREEN_WHEEL_MAP); break;
        case 404:
            UserPrefs::saveConfigured(true);
            ui->setScreen(SCREEN_SPEED);
            break;
    }
}

void AndroidMx5UI::onWheelMapActionClick(lv_event_t* e) {
    auto* ui = static_cast<AndroidMx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;
    lv_obj_t* targetObj = (lv_obj_t*)lv_event_get_current_target(e);
    uintptr_t action = (uintptr_t)lv_obj_get_user_data(targetObj);

    if (action >= 200 && action <= 203) {
        ui->wheelActive_ = (uint8_t)(action - 200);
        const char* names[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCell_[i]) {
                lv_obj_set_style_bg_color(ui->wheelCell_[i], (i == ui->wheelActive_) ? lv_color_hex(0x201214) : lv_color_hex(0x13161B), 0);
                lv_obj_set_style_border_color(ui->wheelCell_[i], (i == ui->wheelActive_) ? C_ACCENT : C_PANEL_BRD, 0);
                lv_obj_set_style_border_width(ui->wheelCell_[i], (i == ui->wheelActive_) ? 2 : 1, 0);
            }
            if (ui->wheelCellLbl_[i]) {
                lv_obj_set_style_text_color(ui->wheelCellLbl_[i], (i == ui->wheelActive_) ? C_ACCENT : C_CHROME, 0);
            }
        }
        if (ui->wheelPrompt_) {
            char buf[128];
            snprintf(buf, sizeof(buf), "Selected: %s\nDeflate/inflate tire by ~3 PSI.\nResponding BCM DID will bind.", names[ui->wheelActive_]);
            lv_label_set_text(ui->wheelPrompt_, buf);
        }
    } else if (action == 301) {
        // Auto-bind detected DIDs
        const char* dids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCellDid_[i]) {
                lv_label_set_text(ui->wheelCellDid_[i], dids[i]);
                lv_obj_set_style_text_color(ui->wheelCellDid_[i], C_OK, 0);
            }
        }
        if (ui->wheelPrompt_) {
            lv_label_set_text(ui->wheelPrompt_, "Auto-Learn Complete!\nAll 4 wheel DIDs mapped and saved.\nTap < BACK to return to settings.");
        }
    } else if (action == 302) {
        // Reset
        const char* dids[4] = {"DID: 2A05", "DID: 2A06", "DID: 2A07", "DID: 2A08"};
        for (uint8_t i = 0; i < 4; i++) {
            if (ui->wheelCellDid_[i]) {
                lv_label_set_text(ui->wheelCellDid_[i], dids[i]);
            }
        }
        if (ui->wheelPrompt_) {
            lv_label_set_text(ui->wheelPrompt_, "Reset to default factory mappings.\nTap Auto-Bind to re-learn.");
        }
    }
}

void AndroidMx5UI::updateDiagSubFuel() {
    if (data_local_.coolantC == 0 && data_local_.rpm == 0) {
        updateDottedValue(stftSeg_, 0.5f);
        if (stftSeg_.val) lv_label_set_text(stftSeg_.val, "--%");
        updateDottedValue(ltftSeg_, 0.5f);
        if (ltftSeg_.val) lv_label_set_text(ltftSeg_.val, "--%");
    } else {
        float stftFrac = (data_local_.shortTermFuelTrimPct + 25.0f) / 50.0f;
        updateDottedValue(stftSeg_, stftFrac);
        if (stftSeg_.val) lv_label_set_text_fmt(stftSeg_.val, "%+.1f%%", data_local_.shortTermFuelTrimPct);

        float ltftFrac = (data_local_.longTermFuelTrimPct + 25.0f) / 50.0f;
        updateDottedValue(ltftSeg_, ltftFrac);
        if (ltftSeg_.val) lv_label_set_text_fmt(ltftSeg_.val, "%+.1f%%", data_local_.longTermFuelTrimPct);
    }

    if (diagAfrVal_) {
        if (data_local_.airFuelRatio <= 0.01f) {
            lv_label_set_text(diagAfrVal_, "-- : 1");
        } else {
            lv_label_set_text_fmt(diagAfrVal_, "%.2f : 1", data_local_.airFuelRatio);
        }
    }
    if (diagHpfpVal_) {
        if (data_local_.fuelRailPressurePsi == 0) {
            lv_label_set_text(diagHpfpVal_, "-- PSI");
        } else {
            lv_label_set_text_fmt(diagHpfpVal_, "%u PSI", data_local_.fuelRailPressurePsi);
        }
    }
    if (diagEvapVal_) {
        if (data_local_.evapVaporPa == 0) {
            lv_label_set_text(diagEvapVal_, "-- Pa");
        } else {
            lv_label_set_text_fmt(diagEvapVal_, "%+d Pa", data_local_.evapVaporPa);
        }
    }
}

void AndroidMx5UI::updateDiagSubCyl() {
    for (int i = 0; i < 4; i++) {
        if (misfireCountLbl_[i]) {
            uint16_t cnt = data_local_.cylMisfireCount[i];
            lv_label_set_text_fmt(misfireCountLbl_[i], "%u", cnt);
            lv_obj_set_style_text_color(misfireCountLbl_[i], (cnt == 0) ? C_OK : C_WARN, 0);
        }
    }
}

void AndroidMx5UI::updateDiagSubChassis() {
    if (diagSasVal_) {
        if (data_local_.estHorsepower == 0 && data_local_.estTorqueFtLb == 0) {
            lv_label_set_text(diagSasVal_, "Est. Engine Output: -- HP / -- lb-ft");
        } else {
            lv_label_set_text_fmt(diagSasVal_, "Est. Engine Output: %u HP / %u lb-ft", data_local_.estHorsepower, data_local_.estTorqueFtLb);
        }
    }
    if (diagTransTempVal_) {
        if (data_local_.engineLoadPct == 0) {
            lv_label_set_text(diagTransTempVal_, "Engine Load / Demand: -- %");
        } else {
            lv_label_set_text_fmt(diagTransTempVal_, "Engine Load / Demand: %u %%", data_local_.engineLoadPct);
        }
    }
    if (diagTccSlipVal_) {
        lv_label_set_text_fmt(diagTccSlipVal_, "Dynamic Decel / Braking: %u%%", data_local_.brakePressurePct);
    }
}

void AndroidMx5UI::updateDiagSubSmog() {
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
    bool allReady = true;
    for (int i = 0; i < 8; i++) {
        if (!ready[i]) allReady = false;
        if (smogPodDot_[i]) {
            lv_obj_set_style_bg_color(smogPodDot_[i], ready[i] ? C_OK : C_WARN, 0);
        }
        if (smogPodLbl_[i]) {
            lv_label_set_text(smogPodLbl_[i], ready[i] ? "READY" : "NOT READY");
            lv_obj_set_style_text_color(smogPodLbl_[i], ready[i] ? C_OK : C_WARN, 0);
        }
    }
    if (smogSummaryLbl_) {
        if (allReady) {
            lv_label_set_text(smogSummaryLbl_, "ALL EMISSION MONITORS READY (PASS)");
            lv_obj_set_style_text_color(smogSummaryLbl_, C_OK, 0);
        } else {
            lv_label_set_text(smogSummaryLbl_, "EMISSION MONITORS INCOMPLETE (NOT READY)");
            lv_obj_set_style_text_color(smogSummaryLbl_, C_WARN, 0);
        }
    }
}
void AndroidMx5UI::updateDiagSubLogs() {}
void AndroidMx5UI::updateBleConfigScreen() {}
void AndroidMx5UI::updateSetupWizard() {}

void AndroidMx5UI::updateWheelMapScreen() {
    for (uint8_t i = 0; i < 4; i++) {
        if (wheelCellPress_[i]) {
            float psi = pressU(data_local_.tirePressure[i]);
            if (data_local_.tireKnown[i] && psi > 4.0f) {
                lv_label_set_text_fmt(wheelCellPress_[i], "%.1f PSI", psi);
                lv_obj_set_style_text_color(wheelCellPress_[i], (psi < 26.0f) ? C_ACCENT : C_SPEED, 0);
            } else {
                lv_label_set_text(wheelCellPress_[i], "-- PSI");
                lv_obj_set_style_text_color(wheelCellPress_[i], C_DIM, 0);
            }
        }
    }
}

void AndroidMx5UI::onSpeedWarnBannerClick(lv_event_t* e) {
    AndroidMx5UI* ui = (AndroidMx5UI*)lv_event_get_user_data(e);
    if (!ui) return;
    ui->warningMutedForDrive_ = true;
    if (ui->speedWarningContainer_) {
        lv_obj_add_flag(ui->speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
    }
    if (ui->speedNormalRightContainer_) {
        lv_obj_remove_flag(ui->speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
    }
}

void AndroidMx5UI::openDtcGuide(uint8_t index) {
    if (!dtcModalCard_) return;

    if (data_local_.dtcCount == 0) {
        lv_obj_set_style_border_color(dtcModalCard_, C_OK, 0);
        if (dtcModalTitle_) {
            lv_obj_set_style_text_color(dtcModalTitle_, C_OK, 0);
            lv_label_set_text(dtcModalTitle_, "VEHICLE HEALTH - ALL SYSTEMS NORMAL");
        }
        if (dtcModalMeaning_) {
            lv_obj_set_style_text_color(dtcModalMeaning_, C_TEXT, 0);
            lv_label_set_text(dtcModalMeaning_, "Zero active or pending Diagnostic Trouble Codes reported across all modules.");
        }
        if (dtcModalChecklist_) {
            lv_obj_set_style_text_color(dtcModalChecklist_, C_DIM, 0);
            lv_label_set_text(dtcModalChecklist_,
                "• SkyActiv-G 2.0L Engine Control Module: Nominal / 0 Codes\n"
                "• Dynamic Stability Control & ABS Controller: Nominal / 0 Codes\n"
                "• High-Speed CAN Bus Communications: Active & Healthy\n"
                "• On-Board Emissions Readiness Monitors: Complete & Passed");
        }
    } else {
        if (index >= data_local_.dtcCount) index = 0;
        const char* code = data_local_.dtcCodes[index];
        const char* desc = data_local_.dtcDesc[index];

        lv_obj_set_style_border_color(dtcModalCard_, C_ACCENT, 0);
        if (dtcModalTitle_) {
            lv_obj_set_style_text_color(dtcModalTitle_, C_ACCENT, 0);
            lv_label_set_text_fmt(dtcModalTitle_, "DTC %s - TECHNICAL REPAIR GUIDE", code);
        }
        if (dtcModalMeaning_) {
            lv_obj_set_style_text_color(dtcModalMeaning_, C_TEXT, 0);
            lv_label_set_text(dtcModalMeaning_, (desc && desc[0]) ? desc : "Powertrain Diagnostic Trouble Code Detected");
        }
        if (dtcModalChecklist_) {
            lv_obj_set_style_text_color(dtcModalChecklist_, C_DIM, 0);
            if (strncmp(code, "P030", 4) == 0) {
                lv_label_set_text(dtcModalChecklist_,
                    "1. Inspect spark plug gap (0.040 in) and electrode wear on cylinder.\n"
                    "2. Swap ignition coil pack to adjacent cylinder to trace misfire.\n"
                    "3. Check direct fuel injector spray resistance and compression.");
            } else if (strncmp(code, "P0171", 5) == 0 || strncmp(code, "P0172", 5) == 0) {
                lv_label_set_text(dtcModalChecklist_,
                    "1. Inspect Mass Airflow (MAF) sensor and clean with MAF cleaner.\n"
                    "2. Check intake tract for post-MAF vacuum leaks / unmetered air.\n"
                    "3. Monitor High Pressure Fuel Pump (HPFP) rail pressure (> 4.0 MPa).");
            } else if (strncmp(code, "P042", 4) == 0) {
                lv_label_set_text(dtcModalChecklist_,
                    "1. Inspect upstream wideband A/F and downstream O2 sensor voltages.\n"
                    "2. Check exhaust manifold collector for cracks or gasket leaks.\n"
                    "3. Verify fuel trims (STFT/LTFT) are within +/- 8% under load.");
            } else if (strncmp(code, "P0128", 5) == 0) {
                lv_label_set_text(dtcModalChecklist_,
                    "1. Check engine coolant thermostat opening temperature.\n"
                    "2. Inspect Engine Coolant Temperature (ECT) sensor wiring harness.\n"
                    "3. Verify coolant level in expansion reservoir.");
            } else {
                lv_label_set_text(dtcModalChecklist_,
                    "1. Connect OBD-II scanner and record freeze-frame sensor data.\n"
                    "2. Inspect module wiring harness connectors for corrosion/seating.\n"
                    "3. Perform live sensor verification and clear code after repair.");
            }
        }
    }

    if (dtcModalCloseLbl_) {
        lv_label_set_text(dtcModalCloseLbl_, (data_local_.dtcCount == 0) ? "CLOSE" : "CLOSE GUIDE");
        lv_obj_t* btnParent = lv_obj_get_parent(dtcModalCloseLbl_);
        if (btnParent) {
            lv_obj_set_style_bg_color(btnParent, (data_local_.dtcCount == 0) ? C_OK : C_ACCENT, 0);
        }
    }

    lv_obj_remove_flag(dtcModalCard_, LV_OBJ_FLAG_HIDDEN);
}


