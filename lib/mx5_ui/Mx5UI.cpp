#include "Mx5UI.h"

#include <Config.h>
#include <math.h>

// ---------------------------------------------------------------------------
// Palette (MX-5 ND2 instrument cluster)
// ---------------------------------------------------------------------------
static const lv_color_t C_BG        = lv_color_hex(0x111111);   // anti-glare charcoal
static const lv_color_t C_PANEL     = lv_color_hex(0x1C1C1C);   // glass card base
static const lv_color_t C_ACCENT    = lv_color_hex(0xFF6600);   // Mazda instrument amber
static const lv_color_t C_ACCENT_DM = lv_color_hex(0x9E4700);   // dark amber (chip)
static const lv_color_t C_TEXT      = lv_color_hex(0xFFFFFF);   // pure white
static const lv_color_t C_DIM       = lv_color_hex(0x9AA0A6);   // neutral grey
static const lv_color_t C_SPEED     = lv_color_hex(0xFFFFFF);   // hero reads white
static const lv_color_t C_RPM_HI    = lv_color_hex(0xE8315B);   // redline
static const lv_color_t C_WARN      = lv_color_hex(0xFF6600);   // threshold amber

// ---------------------------------------------------------------------------
// Shared style helpers
// ---------------------------------------------------------------------------
static void setTextFont(lv_obj_t* obj) {
    lv_obj_set_style_text_font(obj, &lv_font_montserrat_14, 0);
}
static void labelLine(lv_obj_t* obj, const char* txt, lv_color_t color) {
    lv_label_set_text(obj, txt);
    lv_obj_set_style_text_color(obj, color, 0);
}

// ---------------------------------------------------------------------------
// Background graphics layer
// ---------------------------------------------------------------------------
// A dedicated object placed BELOW all widgets. It is the permanent visual
// foundation (themed background, borders, gauge dials) that text/metrics sit
// on top of. Swap/extend this to swap the whole theme.
static lv_obj_t* addBackgroundLayer(lv_obj_t* parent) {
    lv_obj_t* bg = lv_obj_create(parent);
    lv_obj_remove_style_all(bg);
    lv_obj_set_size(bg, lv_pct(100), lv_pct(100));
    lv_obj_set_style_bg_color(bg, C_BG, 0);
    lv_obj_set_style_bg_opa(bg, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(bg, 0, 0);
    lv_obj_set_pos(bg, 0, 0);

    // thin accent rule across the top (visual border foundation)
    lv_obj_t* topLine = lv_obj_create(bg);
    lv_obj_remove_style_all(topLine);
    lv_obj_set_size(topLine, lv_pct(100), 2);
    lv_obj_set_pos(topLine, 0, 0);
    lv_obj_set_style_bg_color(topLine, C_ACCENT, 0);
    lv_obj_set_style_bg_opa(topLine, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(topLine, 0, 0);

    lv_obj_move_to_index(bg, 0);
    return bg;
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void Mx5UI::begin() {
    screens_[0] = buildSpeedScreen();
    screens_[1] = buildRpmScreen();
    screens_[2] = buildEngineScreen();
    screens_[3] = buildTpmsScreen();
    screens_[4] = buildDiagnosticsScreen();

    // each screen listens for horizontal swipes to switch pages
    for (uint8_t i = 0; i < 5; i++) {
        lv_obj_add_event_cb(screens_[i], onGesture, LV_EVENT_GESTURE, this);
    }

    lv_screen_load(screens_[0]);
    currentScreen_ = 0;
}

void Mx5UI::update() {
    obd_.snapshot(data_local_);

    // shared sticky chip is always repainted
    updateSpeedChip();

    switch (currentScreen_) {
        case 0: updateSpeedScreen();  break;
        case 1: updateRpmScreen();    break;
        case 2: updateEngineScreen(); break;
        case 3: updateTpmsScreen();   break;
        case 4: updateDiagnosticsScreen(); break;
        default: break;
    }
}

// ---------------------------------------------------------------------------
// Swipe navigation
// ---------------------------------------------------------------------------

void Mx5UI::onGesture(lv_event_t* e) {
    auto* ui = static_cast<Mx5UI*>(lv_event_get_user_data(e));
    if (!ui) return;

    lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());

    if (dir == LV_DIR_LEFT) {
        if (ui->currentScreen_ < 4) ui->currentScreen_++;
        else ui->currentScreen_ = 0;
    } else if (dir == LV_DIR_RIGHT) {
        if (ui->currentScreen_ > 0) ui->currentScreen_--;
        else ui->currentScreen_ = 4;
    } else {
        return;
    }

    lv_screen_load(ui->screens_[ui->currentScreen_]);
    ui->update();   // immediate repaint of the newly shown screen
}

// ---------------------------------------------------------------------------
// Shared sticky speed chip (always top-left, all screens)
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::addSpeedChip(lv_obj_t* parent) {
    lv_obj_t* chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, 66, 30);
    lv_obj_set_pos(chip, 6, 6);
    lv_obj_set_style_bg_color(chip, C_ACCENT_DM, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(chip, 6, 0);
    lv_obj_set_style_border_color(chip, C_ACCENT, 0);
    lv_obj_set_style_border_width(chip, 1, 0);

    lv_obj_t* lbl = lv_label_create(chip);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(lbl, C_SPEED, 0);
    lv_label_set_text_fmt(lbl, "--");
    lv_obj_center(lbl);

    return lbl;   // keep handle to the label, repaint in updateSpeedChip()
}

// ---------------------------------------------------------------------------
// Segmented (dotted) arc meter
// ---------------------------------------------------------------------------
// Mazda's factory fuel / temperature readouts are a row of digital cells. We
// emulate that with rounded dots placed on the same 135deg..405deg sweep that
// the physical gauge uses (lower-left -> top -> lower-right), so the lit dot
// count forms a curved dotted progression.
Mx5UI::SegArc Mx5UI::buildDottedArc(lv_obj_t* parent, uint16_t size, int16_t posX,
                                    int16_t posY, const char* cap, uint8_t segCount) {
    SegArc seg;
    seg.count = segCount;

    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, size, size);
    lv_obj_set_pos(wrap, posX, posY);
    lv_obj_set_style_bg_color(wrap, C_PANEL, 0);
    lv_obj_set_style_bg_opa(wrap, 120, 0);          // translucent glass card
    lv_obj_set_style_radius(wrap, 12, 0);
    lv_obj_set_style_border_color(wrap, C_ACCENT, 0);
    lv_obj_set_style_border_width(wrap, 1, 0);

    const float cx = size * 0.5f;
    const float cy = size * 0.5f;
    const float r = size * 0.5f - 9.0f;
    for (uint8_t i = 0; i < segCount; i++) {
        float angDeg = 135.0f + (float)i * 270.0f / (float)(segCount - 1);
        float rad = angDeg * PI / 180.0f;
        lv_obj_t* dot = lv_obj_create(wrap);
        lv_obj_remove_style_all(dot);
        lv_obj_set_size(dot, 8, 8);
        lv_obj_set_pos(dot, (int16_t)(cx + r * cosf(rad) - 4),
                       (int16_t)(cy + r * sinf(rad) - 4));
        lv_obj_set_style_bg_color(dot, C_DIM, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_30, 0);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        seg.dots[i] = dot;
    }

    lv_obj_t* val = lv_label_create(wrap);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, C_SPEED, 0);
    lv_label_set_text_fmt(val, "--");
    lv_obj_align(val, LV_ALIGN_CENTER, 0, 0);
    seg.val = val;

    lv_obj_t* capLbl = lv_label_create(wrap);
    lv_obj_set_style_text_font(capLbl, &lv_font_montserrat_10, 0);
    lv_obj_set_style_text_color(capLbl, C_DIM, 0);
    lv_label_set_text(capLbl, cap);
    lv_obj_align(capLbl, LV_ALIGN_BOTTOM_MID, 0, -3);

    return seg;
}

void Mx5UI::updateDottedValue(SegArc& seg, float frac) {
    if (seg.count == 0) return;
    if (frac < 0.0f) frac = 0.0f;
    if (frac > 1.0f) frac = 1.0f;
    uint8_t filled = (uint8_t)roundf(frac * (float)(seg.count - 1));
    for (uint8_t i = 0; i < seg.count; i++) {
        bool lit = i < filled;
        lv_obj_set_style_bg_color(seg.dots[i], lit ? C_ACCENT : C_DIM, 0);
        lv_obj_set_style_bg_opa(seg.dots[i], lit ? LV_OPA_COVER : LV_OPA_30, 0);
    }
}

void Mx5UI::warnTopDots(SegArc& seg) {
    for (uint8_t i = seg.count - (seg.count >= 3 ? 2 : 0); i < seg.count; i++) {
        lv_obj_set_style_bg_color(seg.dots[i], C_RPM_HI, 0);
        lv_obj_set_style_bg_opa(seg.dots[i], LV_OPA_COVER, 0);
    }
}

// ---------------------------------------------------------------------------
// Automatic transmission gear frame
// ---------------------------------------------------------------------------
// Sharp square cell with a thin white border (ND2 cluster style), isolating
// the active gear character (P/R/N/D/M).
lv_obj_t* Mx5UI::addGearFrame(lv_obj_t* parent) {
    lv_obj_t* frame = lv_obj_create(parent);
    lv_obj_remove_style_all(frame);
    lv_obj_set_size(frame, 44, 44);
    lv_obj_align(frame, LV_ALIGN_CENTER, 0, 78);
    lv_obj_set_style_bg_color(frame, C_BG, 0);
    lv_obj_set_style_bg_opa(frame, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(frame, C_SPEED, 0);
    lv_obj_set_style_border_width(frame, 2, 0);
    lv_obj_set_style_radius(frame, 4, 0);

    lv_obj_t* gearLbl = lv_label_create(frame);
    lv_obj_set_style_text_font(gearLbl, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(gearLbl, C_SPEED, 0);
    lv_label_set_text(gearLbl, "-");
    lv_obj_center(gearLbl);

    return gearLbl;
}

void Mx5UI::updateSpeedChip() {
    lv_label_set_text_fmt(speedChipLabel_, "%3d", data_local_.speedKmh);
}

void Mx5UI::addEdgeCard(lv_obj_t* parent, lv_align_t align, int8_t x, int8_t y,
                        uint16_t w, uint16_t h, lv_color_t accent) {
    lv_obj_t* card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, w, h);
    lv_obj_align(card, align, x, y);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_border_color(card, accent, 0);
    lv_obj_set_style_border_width(card, 1, 0);
}

// ---------------------------------------------------------------------------
// Screen 0 - Speed
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildSpeedScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    addBackgroundLayer(scr);

    // sticky chip (top-left), pure white on dark amber
    speedChipLabel_ = addSpeedChip(scr);

    // translucent glass card beneath the hero readout
    lv_obj_t* card = lv_obj_create(scr);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 172, 96);
    lv_obj_align(card, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_color(card, C_PANEL, 0);
    lv_obj_set_style_bg_opa(card, 120, 0);      // translucent glass overlay
    lv_obj_set_style_radius(card, 14, 0);
    lv_obj_set_style_border_color(card, C_ACCENT, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_move_to_index(card, 0);              // behind the hero text

    // prominent centered digital speedometer - lv_font_montserrat_48 @ (0,0)
    speedBigLabel_ = lv_label_create(scr);
    lv_obj_set_style_text_font(speedBigLabel_, &lv_font_montserrat_48, 0);
    lv_obj_set_style_text_color(speedBigLabel_, C_SPEED, 0);
    lv_label_set_text_fmt(speedBigLabel_, "--");
    lv_obj_align(speedBigLabel_, LV_ALIGN_CENTER, 0, 0);

    speedUnitLabel_ = lv_label_create(scr);
    setTextFont(speedUnitLabel_);
    labelLine(speedUnitLabel_, "km/h", C_DIM);
    lv_obj_align(speedUnitLabel_, LV_ALIGN_CENTER, 0, 34);

    // gear frame (square, thin white border) just below the hero readout
    gearLbl_ = addGearFrame(scr);

    // segmented RPM telemetry arc - dotted, bottom centre, narrow sweep
    rpmSeg_ = buildDottedArc(scr, 124, 98, 350, "RPM", 18);

    // fuel + coolant meters flank the RPM arc (ND2 digital readout cells)
    fuelSeg_ = buildDottedArc(scr, 84, 8, 390, "FUEL", 12);
    coolantSeg_ = buildDottedArc(scr, 84, 228, 390, "COOL", 12);

    // swipe hint
    lv_obj_t* hint = lv_label_create(scr);
    setTextFont(hint);
    labelLine(hint, "< swipe >", C_DIM);
    lv_obj_align(hint, LV_ALIGN_TOP_RIGHT, -8, 8);

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 1 - RPM / gauges
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildRpmScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    addBackgroundLayer(scr);
    speedChipLabel_ = addSpeedChip(scr);   // sticky top-left

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, "ENGINE");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 10);

    // big segmented RPM meter, centred
    rpmBigSeg_ = buildDottedArc(scr, 210, 55, 115, "", 24);
    lv_obj_set_style_text_font(rpmBigSeg_.val, &lv_font_montserrat_24, 0);
    lv_label_set_text_fmt(rpmBigSeg_.val, "%d rpm", 0);
    rpmArcVal_ = rpmBigSeg_.val;

    // load + throttle gauges below
    loadArcVal_ = addMiniGauge(scr, "LOAD", 0);
    throttleArcVal_ = addMiniGauge(scr, "THR", 1);

    return scr;
}

lv_obj_t* Mx5UI::addMiniGauge(lv_obj_t* parent, const char* cap, uint8_t idx) {
    lv_obj_t* wrap = lv_obj_create(parent);
    lv_obj_remove_style_all(wrap);
    lv_obj_set_size(wrap, 140, 110);
    lv_obj_align(wrap, LV_ALIGN_BOTTOM_LEFT, (idx == 0) ? 12 : 168, -6);
    lv_obj_set_style_bg_color(wrap, C_PANEL, 0);
    lv_obj_set_style_bg_opa(wrap, 120, 0);      // translucent glass overlay
    lv_obj_set_style_radius(wrap, 8, 0);
    lv_obj_set_style_border_color(wrap, C_ACCENT, 0);   // outer tick border
    lv_obj_set_style_border_width(wrap, 1, 0);

    lv_obj_t* capLbl = lv_label_create(wrap);
    setTextFont(capLbl);
    labelLine(capLbl, cap, C_DIM);
    lv_obj_align(capLbl, LV_ALIGN_TOP_MID, 0, 4);

    lv_obj_t* val = lv_label_create(wrap);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, C_SPEED, 0);
    lv_label_set_text_fmt(val, "---");
    lv_obj_align(val, LV_ALIGN_BOTTOM_MID, 0, -4);

    return val;
}

// ---------------------------------------------------------------------------
// Screen 2 - Engine / environment
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildEngineScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    addBackgroundLayer(scr);
    speedChipLabel_ = addSpeedChip(scr);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, "TEMPERATURES");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 10);

    int16_t y = 70;
    engCoolantVal_ = addMetricRow(scr, "COOLANT", "C", y);
    y += 70;
    engIntakeVal_ = addMetricRow(scr, "INTAKE AIR", "C", y);
    y += 70;
    engBatteryVal_ = addMetricRow(scr, "BATTERY", "V", y);
    y += 70;
    engOilVal_ = addMetricRow(scr, "OIL TEMP", "C", y);

    return scr;
}

lv_obj_t* Mx5UI::addMetricRow(lv_obj_t* parent, const char* cap, const char* unit, int16_t y) {
    lv_obj_t* row = lv_obj_create(parent);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, 300, 48);
    lv_obj_set_pos(row, 10, y);
    lv_obj_set_style_bg_color(row, C_PANEL, 0);
    lv_obj_set_style_bg_opa(row, 120, 0);          // translucent glass overlay
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_border_color(row, C_ACCENT, 0);
    lv_obj_set_style_border_width(row, 1, 0);

    lv_obj_t* capLbl = lv_label_create(row);
    setTextFont(capLbl);
    lv_obj_set_style_text_color(capLbl, C_DIM, 0);
    lv_label_set_text(capLbl, cap);
    lv_obj_align(capLbl, LV_ALIGN_LEFT_MID, 12, 0);

    lv_obj_t* val = lv_label_create(row);
    lv_obj_set_style_text_font(val, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(val, C_SPEED, 0);
    lv_label_set_text_fmt(val, "-- %s", unit);
    lv_obj_align(val, LV_ALIGN_RIGHT_MID, -12, 0);

    return val;
}

// ---------------------------------------------------------------------------
// Screen 3 - TPMS
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildTpmsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    addBackgroundLayer(scr);
    speedChipLabel_ = addSpeedChip(scr);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, "TIRE PRESSURE");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 10);

    // 4 wheels in a 2x2 grid
    static const char* pos[4] = {"FL", "FR", "RL", "RR"};
    for (uint8_t i = 0; i < 4; i++) {
        int col = i & 1;
        int row = i >> 1;

        lv_obj_t* cell = lv_obj_create(scr);
        lv_obj_remove_style_all(cell);
        lv_obj_set_size(cell, 150, 170);
        lv_obj_set_pos(cell, 10 + col * 155, 70 + row * 175);
        lv_obj_set_style_bg_color(cell, C_PANEL, 0);
        lv_obj_set_style_bg_opa(cell, 120, 0);      // translucent glass overlay
        lv_obj_set_style_radius(cell, 10, 0);
        lv_obj_set_style_border_color(cell, C_ACCENT, 0);
        lv_obj_set_style_border_width(cell, 1, 0);

        lv_obj_t* corner = lv_label_create(cell);
        lv_obj_set_style_text_font(corner, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(corner, C_DIM, 0);
        lv_label_set_text(corner, pos[i]);
        lv_obj_align(corner, LV_ALIGN_TOP_LEFT, 10, 6);

        lv_obj_t* red = lv_label_create(cell);
        lv_obj_set_style_text_font(red, &lv_font_montserrat_24, 0);
        lv_obj_set_style_text_color(red, C_SPEED, 0);
        lv_label_set_text_fmt(red, "--.?");
        lv_obj_align(red, LV_ALIGN_CENTER, 0, -12);

        lv_obj_t* temp = lv_label_create(cell);
        setTextFont(temp);
        lv_obj_set_style_text_color(temp, C_DIM, 0);
        lv_label_set_text_fmt(temp, "-- C");
        lv_obj_align(temp, LV_ALIGN_BOTTOM_MID, 0, -10);

        tpmsLabel_[i] = red;
        tpmsTemp_[i] = temp;
    }

    return scr;
}

// ---------------------------------------------------------------------------
// Screen 4 - Diagnostics
// ---------------------------------------------------------------------------

lv_obj_t* Mx5UI::buildDiagnosticsScreen() {
    lv_obj_t* scr = lv_obj_create(NULL);
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, C_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    addBackgroundLayer(scr);
    speedChipLabel_ = addSpeedChip(scr);

    lv_obj_t* title = lv_label_create(scr);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(title, C_ACCENT, 0);
    lv_label_set_text(title, "DIAGNOSTICS");
    lv_obj_align(title, LV_ALIGN_TOP_RIGHT, -12, 10);

    diagStatus_ = lv_label_create(scr);
    lv_obj_set_style_text_font(diagStatus_, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(diagStatus_, C_WARN, 0);
    lv_label_set_text(diagStatus_, "Connecting...");
    lv_obj_align(diagStatus_, LV_ALIGN_TOP_LEFT, 12, 60);

    diagBattery_ = lv_label_create(scr);
    setTextFont(diagBattery_);
    lv_obj_set_style_text_color(diagBattery_, C_DIM, 0);
    lv_label_set_text_fmt(diagBattery_, "Battery: --.-V");
    lv_obj_align(diagBattery_, LV_ALIGN_TOP_LEFT, 12, 100);

    diagError_ = lv_label_create(scr);
    setTextFont(diagError_);
    lv_obj_set_style_text_color(diagError_, C_DIM, 0);
    lv_label_set_text_fmt(diagError_, "Errors: 0");
    lv_obj_align(diagError_, LV_ALIGN_TOP_LEFT, 12, 130);

    diagHint_ = lv_label_create(scr);
    setTextFont(diagHint_);
    lv_obj_set_style_text_color(diagHint_, C_DIM, 0);
    lv_label_set_text(diagHint_, "Swipe to return");
    lv_obj_align(diagHint_, LV_ALIGN_BOTTOM_MID, 0, -30);

    return scr;
}

// ---------------------------------------------------------------------------
// Live update pass (runs each frame from update())
// ---------------------------------------------------------------------------

void Mx5UI::updateSpeedScreen() {
    lv_label_set_text_fmt(speedBigLabel_, "%3d", data_local_.speedKmh);

    // segmented RPM arc + numeric
    updateDottedValue(rpmSeg_, data_local_.rpm / 8000.0f);
    if (data_local_.rpm > 6500) warnTopDots(rpmSeg_);
    lv_label_set_text_fmt(rpmSeg_.val, "%d rpm", data_local_.rpm);

    // fuel + coolant digital meters
    updateDottedValue(fuelSeg_, data_local_.fuelLevelPct / 100.0f);
    lv_label_set_text_fmt(fuelSeg_.val, "%d%%", data_local_.fuelLevelPct);
    updateDottedValue(coolantSeg_, data_local_.coolantC / 120.0f);
    lv_label_set_text_fmt(coolantSeg_.val, "%d C", data_local_.coolantC);

    // active gear character (P/R/N/D/M style cell)
    char gearBuf[2];
    gearBuf[0] = data_local_.gear;
    gearBuf[1] = '\0';
    lv_label_set_text(gearLbl_, gearBuf);
}

void Mx5UI::updateRpmScreen() {
    updateDottedValue(rpmBigSeg_, data_local_.rpm / 8000.0f);
    if (data_local_.rpm > 6500) warnTopDots(rpmBigSeg_);
    lv_label_set_text_fmt(rpmArcVal_, "%d rpm", data_local_.rpm);
    lv_label_set_text_fmt(loadArcVal_, "%d %%", data_local_.engineLoadPct);
    lv_label_set_text_fmt(throttleArcVal_, "%d %%", data_local_.throttlePct);
}

void Mx5UI::updateEngineScreen() {
    lv_label_set_text_fmt(engCoolantVal_, "%d C", data_local_.coolantC);
    lv_label_set_text_fmt(engIntakeVal_, "%d C", data_local_.intakeAirC);
    lv_label_set_text_fmt(engBatteryVal_, "%.1f V", data_local_.batteryVolts);
    lv_label_set_text_fmt(engOilVal_, "%d C", data_local_.oilTempC);
}

void Mx5UI::updateTpmsScreen() {
    if (!MX5_TPMS_ENABLED) {
        for (uint8_t i = 0; i < 4; i++) {
            lv_label_set_text(tpmsLabel_[i], "--");
            lv_label_set_text(tpmsTemp_[i], "-- C");
        }
        return;
    }
    for (uint8_t i = 0; i < 4; i++) {
        if (data_local_.tireKnown[i]) {
            lv_label_set_text_fmt(tpmsLabel_[i], "%.1f", data_local_.tirePressure[i]);
            lv_label_set_text_fmt(tpmsTemp_[i], "%.0f C", data_local_.tireTemp[i]);
        } else {
            lv_label_set_text(tpmsLabel_[i], "--");
            lv_label_set_text(tpmsTemp_[i], "-- C");
        }
    }
}

void Mx5UI::updateDiagnosticsScreen() {
    if (data_local_.connected) {
        lv_obj_set_style_text_color(diagStatus_, lv_color_hex(0x22C55E), 0);
        lv_label_set_text(diagStatus_, "Connected");
    } else {
        lv_obj_set_style_text_color(diagStatus_, C_WARN, 0);
        lv_label_set_text(diagStatus_, "Connecting...");
    }
    lv_label_set_text_fmt(diagBattery_, "Battery: %.1fV", data_local_.batteryVolts);
    lv_label_set_text_fmt(diagError_, data_local_.canError ? "CAN errors" : "No errors");
}