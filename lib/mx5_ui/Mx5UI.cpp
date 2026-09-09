#include "Mx5UI.h"
#include "../mx5_config/DtcDatabase.h"
#include "../mx5_config/UserPrefs.h"

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
static const lv_color_t C_PANEL_BRD = lv_color_hex(0x353B47);   // Metallic Dark Slate/Silver Border
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
        case Mx5UI::SCREEN_SPEED:  return "SPEEDOMETER";
        case Mx5UI::SCREEN_TPMS:   return "TIRE MONITOR (TPMS)";
        case Mx5UI::SCREEN_WIZARD: return "SETUP WIZARD";
        default: return "MX-5 DASHBOARD";
    }
}

void Mx5UI::setScreenWithTransition(uint8_t targetIndex, bool forward) {
    (void)forward;
    setScreen(targetIndex);
}

void Mx5UI::setScreen(uint8_t index) {
    if (index >= SCREEN_COUNT || !screens_[index]) return;
    currentScreen_ = index;
    obd_.setActiveScreen(index);
    lv_obj_scroll_to(screens_[currentScreen_], 0, 0, LV_ANIM_OFF);
    lv_screen_load_anim(screens_[currentScreen_], LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
    update();
}

void Mx5UI::nextScreen() {
    uint8_t next = (currentScreen_ == SCREEN_SPEED) ? SCREEN_TPMS : SCREEN_SPEED;
    setScreen(next);
}

void Mx5UI::prevScreen() {
    uint8_t prev = (currentScreen_ == SCREEN_SPEED) ? SCREEN_TPMS : SCREEN_SPEED;
    setScreen(prev);
}

void Mx5UI::toggleMenu() {
    nextScreen();
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

static uint32_t lastTapTime = 0;

static void onScreenEvent(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;

    uint32_t now = lv_tick_get();
    if (now - lastTapTime < 250) return;
    lastTapTime = now;

    uint8_t cur = ui->getCurrentScreen();
    if (cur == Mx5UI::SCREEN_SPEED || cur == Mx5UI::SCREEN_TPMS) {
        ui->nextScreen();
    }
}

void Mx5UI::begin() {
    transAuto_ = UserPrefs::getTransAuto();
    data_local_.isAutomatic = transAuto_;

    screens_[SCREEN_SPEED]  = buildSpeedScreen();
    screens_[SCREEN_TPMS]   = buildTpmsScreen();
    screens_[SCREEN_WIZARD] = buildWizardScreen();

    // Attach simple tap event handler to content screens
    for (uint8_t i = 0; i < CONTENT_SCREEN_COUNT; i++) {
        if (!screens_[i]) continue;
        lockNoScroll(screens_[i]);
        lv_obj_add_event_cb(screens_[i], onScreenEvent, LV_EVENT_CLICKED, this);
    }

    // Global connection status pill on top layer (visible across all screens)
    connStatusPill_ = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(connStatusPill_);
    lv_obj_set_size(connStatusPill_, 230, 26);
    lv_obj_set_pos(connStatusPill_, 125, 6);
    lv_obj_set_style_bg_color(connStatusPill_, lv_color_hex(0x15161A), 0);
    lv_obj_set_style_bg_opa(connStatusPill_, LV_OPA_90, 0);
    lv_obj_set_style_radius(connStatusPill_, 13, 0);
    lv_obj_set_style_border_color(connStatusPill_, C_ACCENT, 0);
    lv_obj_set_style_border_width(connStatusPill_, 1, 0);
    lockNoScroll(connStatusPill_);

    connStatusLbl_ = lv_label_create(connStatusPill_);
    lv_obj_set_style_text_font(connStatusLbl_, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(connStatusLbl_, C_WARN, 0);
    lv_label_set_text(connStatusLbl_, "SEARCHING FOR OBD-II...");
    lv_obj_center(connStatusLbl_);
    lockNoScroll(connStatusLbl_);

    // Speedometer is the default home screen
    setScreen(SCREEN_SPEED);
}

void Mx5UI::update() {
    obd_.snapshot(data_local_);

    // Update global connection status pill
    if (connStatusPill_ && connStatusLbl_) {
        uint32_t now = lv_tick_get();
        if (data_local_.connected) {
            if (!wasConnected_) {
                wasConnected_ = true;
                connConnectedSinceMs_ = now;
                lv_obj_remove_flag(connStatusPill_, LV_OBJ_FLAG_HIDDEN);
                lv_obj_set_style_border_color(connStatusPill_, C_OK, 0);
                lv_obj_set_style_text_color(connStatusLbl_, C_OK, 0);
                lv_label_set_text(connStatusLbl_, "LIVE OBD-II • CONNECTED");
            } else {
                // Auto-fade / hide after 3 seconds of continuous connection
                if (now - connConnectedSinceMs_ > 3000) {
                    lv_obj_add_flag(connStatusPill_, LV_OBJ_FLAG_HIDDEN);
                }
            }
        } else {
            wasConnected_ = false;
            lv_obj_remove_flag(connStatusPill_, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_border_color(connStatusPill_, C_ACCENT, 0);
            lv_obj_set_style_text_color(connStatusLbl_, C_WARN, 0);
            lv_label_set_text(connStatusLbl_, "DISCONNECTED • RECONNECTING...");
        }
    }

    // Shared sticky speed chip
    updateSpeedChip();

    switch (currentScreen_) {
        case SCREEN_SPEED:  updateSpeedScreen();  break;
        case SCREEN_TPMS:   updateTpmsScreen();   break;
        case SCREEN_WIZARD: updateWizardScreen(); break;
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
    lv_obj_set_style_border_width(wrap, 2, 0);
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

// Fuel gauge: dots trace a "U" around the card - up the left side, across the
// top, down the right side - mirroring the ND cluster's trough pattern.
//   Left side  : 2.5%, 5%, 7.5%, 10% (10% at top-left corner)
//   Top row    : 20% ... 90% (one dot per 10%, 90% at top-right corner)
//   Right side : 92.5%, 95%, 97.5% (down to bottom-right)
Mx5UI::SegArc Mx5UI::buildFuelGauge(lv_obj_t* parent, uint16_t width, uint16_t height,
                                    int16_t posX, int16_t posY) {
    SegArc seg;
    static const uint16_t kPct10[15] = {25, 50, 75, 100, 200, 300, 400, 500,
                                        600, 700, 800, 900, 925, 950, 975};
    seg.count = (uint8_t)(sizeof(kPct10) / sizeof(kPct10[0]));

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, width, height);
    lv_obj_set_pos(wrap, posX, posY);
    lv_obj_set_style_bg_color(wrap, C_PANEL, 0);
    lv_obj_set_style_bg_opa(wrap, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(wrap, 16, 0);
    lv_obj_set_style_border_color(wrap, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(wrap, 2, 0);
    lockNoScroll(wrap);
    seg.wrap = wrap;

    const int16_t padX = 14;
    const int16_t topY = 12;
    const int16_t botY = height - 14;
    const int16_t lx = padX;
    const int16_t rx = width - padX;
    const uint8_t ds = 6;

    for (uint8_t i = 0; i < seg.count; i++) {
        int16_t px = 0, py = 0;
        if (i < 4) {                              // left side up: 2.5..10
            px = lx;
            py = (int16_t)(botY + (topY - botY) * i / 3);
        } else if (i <= 11) {                     // top row across: 20..90
            px = (int16_t)(lx + (rx - lx) * (i - 4) / 7);
            py = topY;
        } else {                                  // right side down: 92.5..97.5
            px = rx;
            py = (int16_t)(topY + (botY - topY) * (i - 11) / 3);
        }

        lv_obj_t* dot = lv_obj_create(wrap);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, ds, ds);
        lv_obj_set_pos(dot, px - (ds / 2), py - (ds / 2));
        lv_obj_set_style_bg_color(dot, C_DOT_UNLIT, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lockNoScroll(dot);
        seg.dots[i] = dot;
        seg.pct10[i] = kPct10[i];
    }

    // "C x.x gal" current fuel (white)
    lv_obj_t* val = lv_label_create(wrap);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(val, C_SPEED, 0);
    lv_label_set_text_fmt(val, "C -- gal");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, -10);
    lockNoScroll(val);
    seg.val = val;

    // "M y.y gal" missing fuel (Soul Red accent, matches gear indicator)
    lv_obj_t* sub = lv_label_create(wrap);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_18, 0);
    lv_obj_set_style_text_color(sub, C_ACCENT, 0);
    lv_label_set_text_fmt(sub, "M -- gal");
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 10);
    lockNoScroll(sub);
    seg.sub = sub;

    return seg;
}

// Lights every fuel dot whose percent threshold is at or below the current level
void Mx5UI::updateFuelGauge(SegArc& seg, uint8_t pct) {
    if (seg.count == 0) return;
    uint16_t target = (uint16_t)pct * 10U;
    for (uint8_t i = 0; i < seg.count; i++) {
        bool lit = (seg.pct10[i] <= target);
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
// Screen 0 - Speedometer (Hero 120pt Speed + Gear + Dotted Arc Gauges)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildSpeedScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addBackgroundLayer(scr);
    speedChipLabel_[SCREEN_SPEED] = nullptr;

    // Hero speed card (280x266 @ 14, 42)
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 280, 266);
    lv_obj_set_pos(card, 14, 42);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 16, 0);
    lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(card, 2, 0);
    lockNoScroll(card);

    // Hero Speed Readout (Extra Large 120px bold monospace digits)
    speedBigLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedBigLabel_, &lv_font_mono_120, 0);
    lv_obj_set_style_text_color(speedBigLabel_, C_SPEED, 0);
    lv_label_set_text(speedBigLabel_, "0");
    lv_obj_align(speedBigLabel_, LV_ALIGN_CENTER, 0, -22);
    lockNoScroll(speedBigLabel_);

    // MPH text
    speedUnitLabel_ = lv_label_create(card);
    lv_obj_set_style_text_font(speedUnitLabel_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(speedUnitLabel_, C_CHROME, 0);
    lv_label_set_text(speedUnitLabel_, MX5_UNITS_US ? "MPH" : "km/h");
    lv_obj_align(speedUnitLabel_, LV_ALIGN_CENTER, 0, 80);
    lockNoScroll(speedUnitLabel_);

    // AT Gear indicator frame
    gearLbl_ = addGearFrame(card, 226, 204);

    // Right-side normal container (300, 42, w: 166, h: 266)
    speedNormalRightContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedNormalRightContainer_);
    lv_obj_set_size(speedNormalRightContainer_, 166, 266);
    lv_obj_set_pos(speedNormalRightContainer_, 300, 42);
    lockNoScroll(speedNormalRightContainer_);

    // Right-side circular arc gauges inside container
    rpmSeg_     = buildDottedArc(speedNormalRightContainer_, 166, 0, 0, "RPM", 18);
    fuelSeg_    = buildFuelGauge(speedNormalRightContainer_, 166, 72, 0, 178);

    // Right-side dynamic warning container
    speedWarningContainer_ = lv_obj_create(scr);
    lv_obj_remove_style_all(speedWarningContainer_);
    lv_obj_set_size(speedWarningContainer_, 166, 266);
    lv_obj_set_pos(speedWarningContainer_, 300, 42);
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
    lv_obj_set_size(speedWarnCard_, 166, 222);
    lv_obj_set_pos(speedWarnCard_, 0, 42);
    lv_obj_set_style_bg_color(speedWarnCard_, lv_color_hex(0x191014), 0);
    lv_obj_set_style_bg_opa(speedWarnCard_, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(speedWarnCard_, 12, 0);
    lv_obj_set_style_border_color(speedWarnCard_, C_ACCENT, 0);
    lv_obj_set_style_border_width(speedWarnCard_, 2, 0);
    lv_obj_add_event_cb(speedWarnCard_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnCard_);

    speedWarnMainVal_ = lv_label_create(speedWarnCard_);
    lv_obj_set_style_text_font(speedWarnMainVal_, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(speedWarnMainVal_, C_ACCENT, 0);
    lv_label_set_text(speedWarnMainVal_, "CRITICAL ALERT");
    lv_obj_align(speedWarnMainVal_, LV_ALIGN_TOP_MID, 0, 16);
    lv_obj_add_event_cb(speedWarnMainVal_, onSpeedWarnBannerClick, LV_EVENT_CLICKED, this);
    lockNoScroll(speedWarnMainVal_);

    speedWarnSubVal_ = lv_label_create(speedWarnCard_);
    lv_obj_set_style_text_font(speedWarnSubVal_, &lv_font_montserrat_12, 0);
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

    const char* titles[4] = {"FRONT LEFT", "FRONT RIGHT", "REAR LEFT", "REAR RIGHT"};
    // Left col: FL(12, 42), RL(12, 176). Right col: FR(308, 42), RR(308, 176)
    const int16_t xs[4] = {12, 308, 12, 308};
    const int16_t ys[4] = {42, 42, 176, 176};
    const int16_t cardW = 160;
    const int16_t cardH = 126;

    for (uint8_t i = 0; i < 4; i++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, cardW, cardH);
        lv_obj_set_pos(card, xs[i], ys[i]);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lockNoScroll(card);
        tpmsCard_[i] = card;

        lv_obj_t* tag = lv_label_create(card);
        lv_obj_set_style_text_font(tag, &lv_font_montserrat_12, 0);
        lv_obj_set_style_text_color(tag, C_CHROME, 0);
        lv_label_set_text(tag, titles[i]);
        lv_obj_align(tag, LV_ALIGN_TOP_LEFT, 12, 8);
        lockNoScroll(tag);

        // Status indicator dot (14x14 with halo)
        lv_obj_t* dot = lv_obj_create(card);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 14, 14);
        lv_obj_set_pos(dot, cardW - 24, 8);
        lv_obj_set_style_bg_color(dot, C_OK, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_color(dot, lv_color_hex(0x0C0D0F), 0);
        lv_obj_set_style_border_width(dot, 1, 0);
        lockNoScroll(dot);
        tpmsDot_[i] = dot;

        tpmsLabel_[i] = lv_label_create(card);
        lv_obj_set_style_text_font(tpmsLabel_[i], &lv_font_montserrat_32, 0);
        lv_obj_set_style_text_color(tpmsLabel_[i], C_SPEED, 0);
        lv_label_set_text(tpmsLabel_[i], "--");
        lv_obj_align(tpmsLabel_[i], LV_ALIGN_LEFT_MID, 12, -2);
        lockNoScroll(tpmsLabel_[i]);

        lv_obj_t* uLbl = lv_label_create(card);
        lv_obj_set_style_text_font(uLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(uLbl, C_DIM, 0);
        lv_label_set_text(uLbl, MX5_UNITS_US ? "PSI" : "bar");
        lv_obj_align(uLbl, LV_ALIGN_LEFT_MID, 80, 2);
        lockNoScroll(uLbl);

        tpmsTemp_[i] = lv_label_create(card);
        lv_obj_set_style_text_font(tpmsTemp_[i], &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(tpmsTemp_[i], C_DIM, 0);
        lv_label_set_text(tpmsTemp_[i], "-- °F");
        lv_obj_align(tpmsTemp_[i], LV_ALIGN_BOTTOM_LEFT, 12, -8);
        lockNoScroll(tpmsTemp_[i]);
    }

    // Center Vehicle Silhouette Card (120x260 @ 180, 42)
    lv_obj_t* carCard = lv_obj_create(scr);
    lv_obj_remove_style_all(carCard);
    lv_obj_set_size(carCard, 120, 260);
    lv_obj_set_pos(carCard, 180, 42);
    lv_obj_set_style_bg_color(carCard, lv_color_hex(0x0E1013), 0);
    lv_obj_set_style_bg_opa(carCard, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(carCard, 14, 0);
    lv_obj_set_style_border_color(carCard, C_PANEL_BRD, 0);
    lv_obj_set_style_border_width(carCard, 2, 0);
    lockNoScroll(carCard);

    // Real Mazda MX-5 RF Top-Down Image
    lv_obj_t* carImg = lv_image_create(carCard);
    lv_image_set_src(carImg, &mx5_rf_cal_dsc);
    lv_obj_align(carImg, LV_ALIGN_CENTER, 0, 0);
    lockNoScroll(carImg);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 2 - Initial Setup Wizard (4-Step Guided Flow)
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::buildWizardScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lockNoScroll(scr);

    addSubScreenHeader(scr, "INITIAL SETUP WIZARD", SCREEN_WIZARD);

    // Step Progress Bar (Top Bar @ y=42)
    const char* stepTitles[4] = {"1. CONNECT", "2. CONFIG", "3. TPMS", "4. READY"};
    for (int i = 0; i < 4; i++) {
        lv_obj_t* badge = lv_obj_create(scr);
        lv_obj_remove_style_all(badge);
        lv_obj_set_size(badge, 108, 34);
        lv_obj_set_pos(badge, 14 + i * 114, 42);
        lv_obj_set_style_bg_color(badge, (i == 0) ? C_ACCENT_DM : lv_color_hex(0x13151A), 0);
        lv_obj_set_style_bg_opa(badge, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(badge, 8, 0);
        lv_obj_set_style_border_color(badge, (i == 0) ? C_ACCENT : C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(badge, 2, 0);
        lockNoScroll(badge);
        wizStepBadge_[i] = badge;

        lv_obj_t* bLbl = lv_label_create(badge);
        lv_obj_set_style_text_font(bLbl, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(bLbl, (i == 0) ? C_ACCENT : C_DIM, 0);
        lv_label_set_text(bLbl, stepTitles[i]);
        lv_obj_center(bLbl);
        lockNoScroll(bLbl);
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
        lv_obj_set_style_border_width(b, 2, 0);
        lv_obj_set_user_data(b, (void*)id);
        lv_obj_add_event_cb(b, onSettingsActionClick, LV_EVENT_CLICKED, this);
        lockNoScroll(b);

        lv_obj_t* l = lv_label_create(b);
        lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(l, C_TEXT, 0);
        lv_label_set_text(l, txt);
        lv_obj_center(l);
        lockNoScroll(l);
        return b;
    };

    // Card Container (452x226 @ 14, 82)
    for (int step = 0; step < 4; step++) {
        lv_obj_t* card = lv_obj_create(scr);
        lv_obj_remove_style_all(card);
        lv_obj_set_size(card, 452, 226);
        lv_obj_set_pos(card, 14, 82);
        lv_obj_set_style_bg_color(card, C_PANEL, 0);
        lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
        lv_obj_set_style_radius(card, 14, 0);
        lv_obj_set_style_border_color(card, C_PANEL_BRD, 0);
        lv_obj_set_style_border_width(card, 2, 0);
        lockNoScroll(card);
        wizStepCard_[step] = card;

        if (step != 0) lv_obj_add_flag(card, LV_OBJ_FLAG_HIDDEN);

        if (step == 0) {
            // STEP 0: OBD Connection
            lv_obj_t* t = lv_label_create(card);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(t, C_ACCENT, 0);
            lv_label_set_text(t, "STEP 1: OBD-II BLE SCANNER CONNECTION");
            lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
            lockNoScroll(t);

            wizConnStatusLbl_ = lv_label_create(card);
            lv_obj_set_style_text_font(wizConnStatusLbl_, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(wizConnStatusLbl_, C_WARN, 0);
            lv_label_set_text(wizConnStatusLbl_, "SEARCHING FOR OBDLINK CX ADAPTER...");
            lv_obj_align(wizConnStatusLbl_, LV_ALIGN_TOP_LEFT, 16, 44);
            lockNoScroll(wizConnStatusLbl_);

            wizConnSubLbl_ = lv_label_create(card);
            lv_obj_set_style_text_font(wizConnSubLbl_, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(wizConnSubLbl_, C_DIM, 0);
            lv_label_set_text(wizConnSubLbl_, "Power on OBDLink CX in vehicle port.\nAuto-connecting upon discovery.");
            lv_obj_align(wizConnSubLbl_, LV_ALIGN_TOP_LEFT, 16, 74);
            lockNoScroll(wizConnSubLbl_);

            makeWizBtn(card, 286, 164, 150, 46, "NEXT STEP >", 123, C_ACCENT);

        } else if (step == 1) {
            // STEP 1: Transmission & Units
            lv_obj_t* t = lv_label_create(card);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(t, C_ACCENT, 0);
            lv_label_set_text(t, "STEP 2: TRANSMISSION & UNITS");
            lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
            lockNoScroll(t);

            lv_obj_t* l1 = lv_label_create(card);
            lv_obj_set_style_text_font(l1, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(l1, C_TEXT, 0);
            lv_label_set_text(l1, "Transmission:");
            lv_obj_align(l1, LV_ALIGN_TOP_LEFT, 16, 44);
            lockNoScroll(l1);

            makeWizBtn(card, 140, 38, 140, 42, "6AT AUTO", 120, C_ACCENT_DM);
            makeWizBtn(card, 290, 38, 140, 42, "6MT MANUAL", 121, lv_color_hex(0x191A20));

            lv_obj_t* l2 = lv_label_create(card);
            lv_obj_set_style_text_font(l2, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(l2, C_TEXT, 0);
            lv_label_set_text(l2, "Units:");
            lv_obj_align(l2, LV_ALIGN_TOP_LEFT, 16, 98);
            lockNoScroll(l2);

            makeWizBtn(card, 140, 92, 140, 42, "US (MPH/PSI)", 104, C_ACCENT_DM);
            makeWizBtn(card, 290, 92, 140, 42, "METRIC (KM/H)", 105, lv_color_hex(0x191A20));

            makeWizBtn(card, 16, 164, 130, 46, "< PREV", 124, lv_color_hex(0x191A20));
            makeWizBtn(card, 306, 164, 130, 46, "NEXT STEP >", 123, C_ACCENT);

        } else if (step == 2) {
            // STEP 2: TPMS Setup
            lv_obj_t* t = lv_label_create(card);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_16, 0);
            lv_obj_set_style_text_color(t, C_ACCENT, 0);
            lv_label_set_text(t, "STEP 3: TPMS TIRE SENSOR CONFIG");
            lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
            lockNoScroll(t);

            lv_obj_t* desc = lv_label_create(card);
            lv_obj_set_style_text_font(desc, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(desc, C_TEXT, 0);
            lv_label_set_text(desc, "Mazda MX-5 ND2 Factory TPMS DIDs:\nFL=2A05, FR=2A06, RL=2A07, RR=2A08.\nSensors update over BCM CAN Bus.");
            lv_obj_align(desc, LV_ALIGN_TOP_LEFT, 16, 42);
            lockNoScroll(desc);

            makeWizBtn(card, 16, 164, 130, 46, "< PREV", 124, lv_color_hex(0x191A20));
            makeWizBtn(card, 226, 164, 210, 46, "USE FACTORY DIDs >", 123, C_ACCENT);

        } else if (step == 3) {
            // STEP 3: Complete / Launch
            lv_obj_t* t = lv_label_create(card);
            lv_obj_set_style_text_font(t, &lv_font_montserrat_18, 0);
            lv_obj_set_style_text_color(t, C_OK, 0);
            lv_label_set_text(t, "MX-5 ND2 DIGITAL CLUSTER READY!");
            lv_obj_align(t, LV_ALIGN_TOP_LEFT, 16, 12);
            lockNoScroll(t);

            const char* readySummary = 
                "• OBD-II Telemetry: BLE Active (OBDLink CX)\n"
                "• Continuous Incident Datalogger: Active\n"
                "• 60 FPS Gauge Engine: Active";
            lv_obj_t* sLbl = lv_label_create(card);
            lv_obj_set_style_text_font(sLbl, &lv_font_montserrat_14, 0);
            lv_obj_set_style_text_color(sLbl, C_TEXT, 0);
            lv_label_set_text(sLbl, readySummary);
            lv_obj_align(sLbl, LV_ALIGN_TOP_LEFT, 16, 46);
            lockNoScroll(sLbl);

            makeWizBtn(card, 16, 164, 130, 46, "< PREV", 124, lv_color_hex(0x191A20));
            makeWizBtn(card, 160, 164, 276, 46, "START DRIVE CLUSTER >", 119, C_ACCENT);
        }
    }

    return scr;
}

void Mx5UI::updateWizardScreen() {
    uint32_t now = lv_tick_get();

    // 1. Update progress bar badges
    for (int i = 0; i < 4; i++) {
        bool active = (i == wizardStep_);
        if (wizStepBadge_[i]) {
            lv_obj_set_style_bg_color(wizStepBadge_[i], active ? C_ACCENT_DM : lv_color_hex(0x13151A), 0);
            lv_obj_set_style_border_color(wizStepBadge_[i], active ? C_ACCENT : C_PANEL_BRD, 0);
        }
        if (wizStepCard_[i]) {
            if (active) {
                lv_obj_remove_flag(wizStepCard_[i], LV_OBJ_FLAG_HIDDEN);
            } else {
                lv_obj_add_flag(wizStepCard_[i], LV_OBJ_FLAG_HIDDEN);
            }
        }
    }

    // 2. Step 0: Auto-advance upon BLE scanner connection
    if (wizardStep_ == 0) {
        if (data_local_.connected) {
            if (wizConnStatusLbl_) {
                lv_label_set_text(wizConnStatusLbl_, "OBDLINK CX CONNECTED & CAN HANDSHAKE OK!");
                lv_obj_set_style_text_color(wizConnStatusLbl_, C_OK, 0);
            }
            if (wizConnSubLbl_) {
                lv_label_set_text(wizConnSubLbl_, "ISO 15765-4 CAN 500k • Connected to vehicle ECU. Advancing...");
                lv_obj_set_style_text_color(wizConnSubLbl_, C_ACCENT, 0);
            }

            if (!wizAutoAdvancing_) {
                wizAutoAdvancing_ = true;
                wizAutoAdvanceMs_ = now;
            } else if (now - wizAutoAdvanceMs_ >= 1200) {
                wizardStep_ = 1; // Auto-advance to Step 1 (Transmission & Units)
                wizAutoAdvancing_ = false;
            }
        } else {
            wizAutoAdvancing_ = false;
            if (wizConnStatusLbl_) {
                lv_label_set_text(wizConnStatusLbl_, "SEARCHING FOR OBDLINK CX ADAPTER...");
                lv_obj_set_style_text_color(wizConnStatusLbl_, C_WARN, 0);
            }
            if (wizConnSubLbl_) {
                lv_label_set_text(wizConnSubLbl_, "Power on OBDLink CX in vehicle port.\nAuto-connecting upon discovery.");
                lv_obj_set_style_text_color(wizConnSubLbl_, C_DIM, 0);
            }
        }
    }
}

void Mx5UI::onSettingsActionClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;

    lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
    uint32_t actionId = (uint32_t)(uintptr_t)lv_obj_get_user_data(btn);

    switch (actionId) {
        case 100: // Speedometer Screen
            ui->setScreen(SCREEN_SPEED);
            break;
        case 101: // TPMS Screen
            ui->setScreen(SCREEN_TPMS);
            break;
        case 104: // US Units
            ui->unitsUs_ = true;
            UserPrefs::saveUnits(true);
            break;
        case 105: // Metric Units
            ui->unitsUs_ = false;
            UserPrefs::saveUnits(false);
            break;
        case 118: // Initial Setup Wizard Screen
            ui->obd_.freeze(true);
            ui->wizardStep_ = 0;
            ui->wizAutoAdvancing_ = false;
            ui->setScreen(SCREEN_WIZARD);
            break;
        case 119: // Wizard Finish -> Home Screen
            UserPrefs::saveConfigured(true);
            ui->obd_.freeze(false);
            ui->setScreen(SCREEN_SPEED);
            break;
        case 120: // Transmission Auto (6AT)
            ui->transAuto_ = true;
            UserPrefs::saveTransAuto(true);
            ui->data_local_.isAutomatic = true;
            break;
        case 121: // Transmission Manual (6MT)
            ui->transAuto_ = false;
            UserPrefs::saveTransAuto(false);
            ui->data_local_.isAutomatic = false;
            break;
        case 123: // Wizard Next Step
            if (ui->wizardStep_ < 3) ui->wizardStep_++;
            ui->wizAutoAdvancing_ = false;
            break;
        case 124: // Wizard Prev Step
            if (ui->wizardStep_ > 0) ui->wizardStep_--;
            ui->wizAutoAdvancing_ = false;
            break;
    }
}

// ---------------------------------------------------------------------------
// Header & Chip Decoration
// ---------------------------------------------------------------------------
lv_obj_t* Mx5UI::addSubScreenHeader(lv_obj_t* parent, const char* title, uint8_t subScreenIndex) {
    (void)subScreenIndex;
    addBackgroundLayer(parent);
    addSpeedChip(parent);
    return addPageTitle(parent, title);
}

// ---------------------------------------------------------------------------
// Speedometer Screen Updates
// ---------------------------------------------------------------------------
void Mx5UI::updateSpeedScreen() {
    uint16_t spd = speedU(data_local_.speedKmh);
    if (speedBigLabel_) {
        lv_label_set_text_fmt(speedBigLabel_, "%u", spd);
    }
    if (speedUnitLabel_) {
        lv_label_set_text(speedUnitLabel_, unitsUs_ ? "MPH" : "KM/H");
    }
    if (gearLbl_) {
        char gBuf[4];
        if (data_local_.gear != '-' && data_local_.gear != '\0') {
            snprintf(gBuf, sizeof(gBuf), "%c", data_local_.gear);
        } else {
            snprintf(gBuf, sizeof(gBuf), "D");
        }
        lv_label_set_text(gearLbl_, gBuf);
    }

    // Arc meters
    updateDottedValue(rpmSeg_, (float)data_local_.rpm / 7500.0f);
    updateFuelGauge(fuelSeg_, data_local_.fuelLevelPct);

    if (rpmSeg_.val) {
        lv_label_set_text_fmt(rpmSeg_.val, "%u", data_local_.rpm);
    }
    if (fuelSeg_.val) {
        float curGal = (float)data_local_.fuelLevelPct / 100.0f * 11.0f;
        lv_label_set_text_fmt(fuelSeg_.val, "C %.1f gal", curGal);
        if (fuelSeg_.sub) {
            float missGal = 11.0f - curGal;
            if (missGal < 0.0f) missGal = 0.0f;
            lv_label_set_text_fmt(fuelSeg_.sub, "M %.1f gal", missGal);
        }
    }

    // Warning state checks
    bool coolantWarn = (data_local_.coolantC >= 105);
    bool fuelWarn = (data_local_.fuelLevelPct > 0 && data_local_.fuelLevelPct <= 10);

    if (coolantWarn || fuelWarn) {
        if (!warningMutedForDrive_) {
            if (speedNormalRightContainer_) lv_obj_add_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
            if (speedWarningContainer_) {
                lv_obj_remove_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
                if (coolantWarn && speedWarnTitle_ && speedWarnMainVal_) {
                    lv_label_set_text(speedWarnTitle_, "HIGH ENGINE TEMP ALERT");
                    lv_label_set_text_fmt(speedWarnMainVal_, "%u °F", tempU(data_local_.coolantC));
                    if (speedWarnSubVal_) lv_label_set_text(speedWarnSubVal_, "PULL OVER • ENGINE OVERHEATING");
                } else if (fuelWarn && speedWarnTitle_ && speedWarnMainVal_) {
                    lv_label_set_text(speedWarnTitle_, "LOW FUEL WARNING");
                    lv_label_set_text_fmt(speedWarnMainVal_, "%u %%", data_local_.fuelLevelPct);
                    if (speedWarnSubVal_) lv_label_set_text(speedWarnSubVal_, "REFUEL SOON");
                }
            }
        }
    } else {
        warningMutedForDrive_ = false;   // condition cleared -> re-arm for next episode
        if (speedWarningContainer_) lv_obj_add_flag(speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
        if (speedNormalRightContainer_) lv_obj_remove_flag(speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
    }
}

void Mx5UI::onSpeedWarnBannerClick(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (ui) {
        ui->warningMutedForDrive_ = true;
        if (ui->speedWarningContainer_) {
            lv_obj_add_flag(ui->speedWarningContainer_, LV_OBJ_FLAG_HIDDEN);
        }
        if (ui->speedNormalRightContainer_) {
            lv_obj_remove_flag(ui->speedNormalRightContainer_, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

// ---------------------------------------------------------------------------
// TPMS Screen Updates
// ---------------------------------------------------------------------------
void Mx5UI::updateTpmsScreen() {
    for (int i = 0; i < 4; i++) {
        if (tpmsLabel_[i]) {
            if (data_local_.tirePressure[i] > 0.1f) {
                if (unitsUs_) {
                    uint16_t psi = (uint16_t)lroundf(data_local_.tirePressure[i] * 14.50377f);
                    lv_label_set_text_fmt(tpmsLabel_[i], "%u", psi);
                } else {
                    lv_label_set_text_fmt(tpmsLabel_[i], "%.1f", data_local_.tirePressure[i]);
                }
            } else {
                lv_label_set_text(tpmsLabel_[i], "--");
            }
        }

        if (tpmsTemp_[i]) {
            if (data_local_.tireTemp[i] > -40.0f) {
                if (unitsUs_) {
                    uint16_t f = (uint16_t)lroundf(data_local_.tireTemp[i] * 9.0f / 5.0f + 32.0f);
                    lv_label_set_text_fmt(tpmsTemp_[i], "%u °F", f);
                } else {
                    lv_label_set_text_fmt(tpmsTemp_[i], "%d °C", (int)data_local_.tireTemp[i]);
                }
            } else {
                lv_label_set_text(tpmsTemp_[i], "-- °F");
            }
        }

        // Status Card border & dot styling
        float pBar = data_local_.tirePressure[i];
        if (tpmsCard_[i] && tpmsDot_[i]) {
            if (pBar > 0.1f && (pBar < 1.9f || pBar > 2.8f)) {
                lv_obj_set_style_border_color(tpmsCard_[i], C_WARN, 0);
                lv_obj_set_style_bg_color(tpmsDot_[i], C_WARN, 0);
            } else if (pBar > 0.1f) {
                lv_obj_set_style_border_color(tpmsCard_[i], C_PANEL_BRD, 0);
                lv_obj_set_style_bg_color(tpmsDot_[i], C_OK, 0);
            } else {
                lv_obj_set_style_border_color(tpmsCard_[i], C_PANEL_BRD, 0);
                lv_obj_set_style_bg_color(tpmsDot_[i], C_DOT_UNLIT, 0);
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Theme & Backlight Helpers
// ---------------------------------------------------------------------------
void Mx5UI::setThemeMode(ThemeMode mode) {
    themeMode_ = mode;
    applyThemeMode(mode == THEME_NIGHT);
}

void Mx5UI::setNightMode(bool isNight) {
    if (themeMode_ == THEME_AUTO) {
        applyThemeMode(isNight);
    }
}

void Mx5UI::applyThemeMode(bool isNight) {
    currentNightMode_ = isNight;
    targetBacklightDuty_ = isNight ? 65 : 242;
    setBacklightDuty(targetBacklightDuty_);
}

void Mx5UI::setBacklightDuty(uint8_t duty) {
    currentBacklightDuty_ = duty;
#if defined(ARDUINO) && !defined(PLATFORM_NATIVE)
    ledcWrite(MX5_LEDC_BACKLIGHT_CH, duty);
#endif
}

void Mx5UI::setBrightness(uint8_t pct) {
    userBrightness_ = pct;
    uint8_t duty = (uint8_t)lroundf((pct / 100.0f) * 255.0f);
    setBacklightDuty(duty);
}

