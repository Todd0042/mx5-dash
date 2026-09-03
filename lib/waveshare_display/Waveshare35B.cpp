#include "Waveshare35B.h"
#include "Arduino_AXS15231B.h"

#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <Wire.h>

#include <Config.h>

// ---------- Pin definitions (Waveshare schematic) ----------
#define PIN_QSPI_CS  12
#define PIN_QSPI_CLK 5
#define PIN_QSPI_D0  1
#define PIN_QSPI_D1  2
#define PIN_QSPI_D2  3
#define PIN_QSPI_D3  4

#define PIN_I2C_SDA  8
#define PIN_I2C_SCL  7

#define TCA_ADDR   0x20
#define TCA_PWR_PIN 1

// Panel native resolution (pre-rotation). WIDTH/HEIGHT above are the logical
// 480x320 landscape space LVGL draws in.
#define PANEL_W 320
#define PANEL_H 480

// AXS5106L capacitive touch controller
#define TOUCH_I2C_ADDR 0x3B
// Command frame used to read up to 2 touch points (finger-print format)
static const uint8_t TOUCH_CMD[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};

// ---------- Globals ----------
static Arduino_ESP32QSPI* bus = nullptr;
static Arduino_GFX* gfx = nullptr;
static TCA9554 TCA(TCA_ADDR);
static uint8_t currentLcdRotation = MX5_LCD_ROTATION;

void Waveshare35B::setRotation(uint8_t rotation) {
    currentLcdRotation = rotation;
    if (gfx) gfx->setRotation(rotation);
}

uint8_t Waveshare35B::getRotation() {
    return currentLcdRotation;
}

static lv_color_t* disp_draw_buf1 = nullptr;
static lv_color_t* disp_draw_buf2 = nullptr;

static uint16_t touch_x = 0;
static uint16_t touch_y = 0;
static bool touch_active = false;

// ============================================================
// Power: gate display + touch power through the TCA9554 expander
// ============================================================
void Waveshare35B::initPower() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);

    if (!TCA.begin()) {
        Serial.println("[display] TCA9554 not found");
    }

    // Configure pin1 as output and drive it high -> powers the panel/touch
    TCA.pinMode1(TCA_PWR_PIN, OUTPUT);
    TCA.write1(TCA_PWR_PIN, 1);
    delay(10);
    TCA.write1(TCA_PWR_PIN, 0);
    delay(10);
    TCA.write1(TCA_PWR_PIN, 1);
    delay(200);
}

// ============================================================
// Touch: raw polling of the AXS5106L over I2C
// ============================================================
void Waveshare35B::initTouch() {
    // Nothing needs starting; the touch IC is always listening once powered.
    // Reset sequence is handled inside begin() via a quick power pulse.
    // (We keep this hook separate so soft-resets that skip initPower can
    //  simply re-call this without touching power.)
}

void Waveshare35B::my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
    uint8_t resp[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

    Wire.beginTransmission(TOUCH_I2C_ADDR);
    Wire.write(TOUCH_CMD, 11);
    Wire.endTransmission();
    Wire.requestFrom((uint8_t)TOUCH_I2C_ADDR, (uint8_t)14);
    uint8_t n = Wire.readBytes(resp, 14);

    (void)n;

    // Validity checks (port of Waveshare's bsp_touch_read behaviour)
    if (resp[1] == 0 || resp[2] == 0 || resp[3] < 2 || resp[5] < 2) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    if (resp[0] == 0xff || resp[1] > 2) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint8_t touchNum = resp[1];
    if (touchNum == 0) {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    uint16_t x = ((resp[2] & 0x0F) << 8) | resp[3];
    uint16_t y = ((resp[4] & 0x0F) << 8) | resp[5];

    // The AXS5106L reports raw panel coordinates (native 320x480). Rotate them
    // into the logical landscape space (480x320) to match currentLcdRotation.
    uint16_t lx = x, ly = y;
    switch (currentLcdRotation) {
        case 1:  // 90 deg CW (USB left)
            lx = y;
            ly = (PANEL_W - 1) - x;
            break;
        case 2:  // 180 deg
            lx = (PANEL_W - 1) - x;
            ly = (PANEL_H - 1) - y;
            break;
        case 3:  // 270 deg CW (USB right) - default
            lx = (PANEL_H - 1) - y;
            ly = x;
            break;
        default:  // 0 = portrait, no transform
            break;
    }

    touch_x = lx;
    touch_y = ly;
    touch_active = true;

    data->state = LV_INDEV_STATE_PRESSED;
    data->point.x = lx;
    data->point.y = ly;
}

// ============================================================
// LVGL flush callback -> push pixels to the physical panel
// ============================================================
void Waveshare35B::my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);

    gfx->draw16bitRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);

    lv_display_flush_ready(disp);
}

// ============================================================
// Wire up the LVGL display object
// ============================================================
lv_display_t* Waveshare35B::createDisplay() {
    lv_display_t* disp = lv_display_create(WIDTH, HEIGHT);
    lv_display_set_flush_cb(disp, my_disp_flush);

    // Use two full-frame buffers in PSRAM (DIRECT/full render mode)
    size_t bufSize = (size_t)WIDTH * HEIGHT;
    disp_draw_buf1 = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    disp_draw_buf2 = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    if (!disp_draw_buf1 || !disp_draw_buf2) {
        Serial.println("[display] PSRAM buffer alloc failed, falling back to partial");
        bufSize = (size_t)WIDTH * 40;
        disp_draw_buf1 = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        disp_draw_buf2 = (lv_color_t*)heap_caps_malloc(bufSize * sizeof(lv_color_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
        lv_display_set_buffers(disp, disp_draw_buf1, disp_draw_buf2, bufSize, LV_DISPLAY_RENDER_MODE_PARTIAL);
    } else {
        lv_display_set_buffers(disp, disp_draw_buf1, disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_FULL);
    }

    return disp;
}

// ============================================================
// Wire up the LVGL input device
// ============================================================
lv_indev_t* Waveshare35B::createInput() {
    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, my_touchpad_read);
    return indev;
}

// ============================================================
// Public entry point
// ============================================================
bool Waveshare35B::begin() {
    Serial.println("[display] init");

    initPower();

    bus = new Arduino_ESP32QSPI(PIN_QSPI_CS, PIN_QSPI_CLK, PIN_QSPI_D0,
                                PIN_QSPI_D1, PIN_QSPI_D2, PIN_QSPI_D3);
    // constructor takes the NATIVE panel size (320x480); the odd rotation code
    // swaps it to the logical 480x320 landscape space
    gfx = new Arduino_AXS15231B(bus, -1 /*RST*/, MX5_LCD_ROTATION, false, PANEL_W, PANEL_H);

    if (!gfx->begin()) {
        Serial.println("[display] gfx->begin() failed");
        return false;
    }
    gfx->fillScreen(RGB565_BLACK);

    // Enable touch BEFORE lv_init so the input device is ready at creation.
    initTouch();

    lv_init();
    lv_tick_set_cb(static_cast<uint32_t (*)(void)>([]() -> uint32_t { return millis(); }));

    createDisplay();
    createInput();

    Serial.println("[display] ready");
    return true;
}

void Waveshare35B::loop() {
    lv_timer_handler();
    delay(5);
}
