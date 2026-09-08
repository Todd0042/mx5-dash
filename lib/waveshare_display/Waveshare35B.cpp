#include "Waveshare35B.h"
#include <Arduino_GFX_Library.h>
#include <TCA9554.h>
#include <Wire.h>

#define XPOWERS_CHIP_AXP2101
#include <XPowersLib.h>

#include <Config.h>

// ---------- Pin definitions (Waveshare 3.5B QSPI) ----------
#define PIN_QSPI_CS  12
#define PIN_QSPI_CLK 5
#define PIN_QSPI_D0  1
#define PIN_QSPI_D1  2
#define PIN_QSPI_D2  3
#define PIN_QSPI_D3  4

// ---------- Pin definitions (Waveshare 3.5 Standard SPI) ----------
#define PIN_ST7796_DC    3
#define PIN_ST7796_CS   -1
#define PIN_ST7796_RST  -1
#define PIN_ST7796_SCK   5
#define PIN_ST7796_MOSI  1
#define PIN_ST7796_MISO  2

#define PIN_I2C_SDA  8
#define PIN_I2C_SCL  7

#define TCA_ADDR   0x20
#define TCA_PWR_PIN 1

// Panel native resolution (pre-rotation). WIDTH/HEIGHT above are the logical
// 480x320 landscape space LVGL draws in.
#define PANEL_W 320
#define PANEL_H 480

// Touch controllers
#define TOUCH_AXS5106L_ADDR 0x3B
#define TOUCH_FT6336_ADDR   0x38

// Command frame used to read up to 2 touch points on AXS5106L
static const uint8_t TOUCH_AXS_CMD[11] = {0xb5, 0xab, 0xa5, 0x5a, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00};

// ---------- Globals ----------
bool Waveshare35B::isModelB_ = true;
static uint8_t touchI2cAddr_ = TOUCH_FT6336_ADDR;
static Arduino_DataBus* bus = nullptr;
static Arduino_GFX* gfx = nullptr;
static TCA9554 TCA(TCA_ADDR);
static XPowersPMU pmu;
static uint8_t currentLcdRotation = MX5_LCD_ROTATION;

void Waveshare35B::setRotation(uint8_t rotation) {
    currentLcdRotation = rotation;
    if (!isModelB_) {
        if (gfx) gfx->setRotation(rotation);
    }
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
// Power: probe I2C and power display + touch
// ============================================================
void Waveshare35B::initPower() {
    Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
    delay(50);

    // Diagnostic I2C bus scan: reports all onboard chips (PMIC, RTC, IMU, Touch, Expander)
    Serial.println("[display] scanning I2C bus (SDA:8, SCL:7)...");
    uint8_t found = 0;
    for (uint8_t addr = 1; addr < 127; addr++) {
        Wire.beginTransmission(addr);
        if (Wire.endTransmission() == 0) {
            Serial.printf("[display]   -> found I2C chip at 0x%02X\n", addr);
            found++;
        }
    }

    if (found == 0) {
        // Alternative pinout fallback (some boards wire SCL to GPIO 9)
        Serial.println("[display] no devices on SCL:7; probing fallback SDA:8, SCL:9...");
        Wire.begin(8, 9);
        delay(50);
        for (uint8_t addr = 1; addr < 127; addr++) {
            Wire.beginTransmission(addr);
            if (Wire.endTransmission() == 0) {
                Serial.printf("[display]   -> found I2C chip at 0x%02X (on SCL:9)\n", addr);
                found++;
            }
        }
    }

    // Initialize AXP2101 PMIC if present (powers display panel logic and peripherals)
    Wire.beginTransmission(0x34);
    if (Wire.endTransmission() == 0) {
        Serial.println("[display] detected AXP2101 PMIC at 0x34; initializing power rails...");
        if (pmu.begin(Wire, AXP2101_SLAVE_ADDRESS, PIN_I2C_SDA, PIN_I2C_SCL)) {
            Serial.printf("[display]   -> AXP2101 chip ID: 0x%02X\n", pmu.getChipID());
            pmu.setVbusVoltageLimit(XPOWERS_AXP2101_VBUS_VOL_LIM_4V36);
            pmu.setVbusCurrentLimit(XPOWERS_AXP2101_VBUS_CUR_LIM_1500MA);
            pmu.setSysPowerDownVoltage(2600);

            // Configure DCDC rails
            pmu.setDC1Voltage(3300);
            pmu.setDC2Voltage(1000);
            pmu.setDC3Voltage(3300);
            pmu.setDC4Voltage(1000);
            pmu.setDC5Voltage(3300);

            // Configure LDO rails:
            // BLDO2 = 2.8V (LCD VDD logic)
            // DLDO1/2 = 3.3V (LCD VDDIO / peripherals)
            // ALDO1-4 = 3.3V
            pmu.setALDO1Voltage(3300);
            pmu.setALDO2Voltage(3300);
            pmu.setALDO3Voltage(3300);
            pmu.setALDO4Voltage(3300);
            pmu.setBLDO1Voltage(1500);
            pmu.setBLDO2Voltage(2800);
            pmu.setCPUSLDOVoltage(1000);
            pmu.setDLDO1Voltage(3300);
            pmu.setDLDO2Voltage(3300);

            // Enable DCDC and LDO rails
            pmu.enableDC2();
            pmu.enableDC3();
            pmu.enableDC4();
            pmu.enableDC5();

            pmu.enableALDO1();
            pmu.enableALDO2();
            pmu.enableALDO3();
            pmu.enableALDO4();
            pmu.enableBLDO1();
            pmu.enableBLDO2();
            pmu.enableCPUSLDO();
            pmu.enableDLDO1();
            pmu.enableDLDO2();
            Serial.println("[display]   -> AXP2101 power rails fully energized (ALDO, BLDO, DLDO)");
        } else {
            Serial.println("[display]   -> AXP2101 begin failed");
        }
    }

    // Auto-detect whether capacitive touch IC is AXS5106L (0x3B) or FT6336 (0x38)
    // and determine board model:
    // Waveshare 3.5B uses AXS5106L (0x3B) and AXS15231B QSPI display.
    // Waveshare 3.5 / 3.5-C uses FT6336 (0x38) and ST7796 SPI display.
    Wire.beginTransmission(TOUCH_AXS5106L_ADDR);
    if (Wire.endTransmission() == 0) {
        isModelB_ = true;
        touchI2cAddr_ = TOUCH_AXS5106L_ADDR;
        Serial.println("[display] detected Waveshare 3.5B (AXS15231B QSPI + AXS5106L Touch at 0x3B)");
    } else {
        isModelB_ = false;
        touchI2cAddr_ = TOUCH_FT6336_ADDR;
        Serial.println("[display] detected Waveshare 3.5 / 3.5-C (ST7796 SPI + FT6336 Touch at 0x38)");
    }

    // Hardware Reset LCD panel via TCA9554 Pin 1 if TCA9554 is present at 0x20
    Wire.beginTransmission(TCA_ADDR);
    if (Wire.endTransmission() == 0) {
        Serial.println("[display] TCA9554 found at 0x20, pulsing pin 1 (LCD Reset)...");
        if (TCA.begin()) {
            TCA.pinMode1(TCA_PWR_PIN, OUTPUT);
            TCA.write1(TCA_PWR_PIN, 1);
            delay(10);
            TCA.write1(TCA_PWR_PIN, 0);
            delay(10);
            TCA.write1(TCA_PWR_PIN, 1);
            delay(200);
            Serial.println("[display] LCD hardware reset via TCA9554 complete.");
        } else {
            Serial.println("[display] TCA9554 begin failed");
        }
    } else if (!isModelB_ && PIN_ST7796_RST >= 0) {
        pinMode(PIN_ST7796_RST, OUTPUT);
        digitalWrite(PIN_ST7796_RST, LOW);
        delay(20);
        digitalWrite(PIN_ST7796_RST, HIGH);
        delay(150);
    }
}

// ============================================================
// Touch: raw polling of the capacitive touch IC over I2C
// ============================================================
void Waveshare35B::initTouch() {
    // Nothing needs starting; the touch IC is always listening once powered.
}

void Waveshare35B::my_touchpad_read(lv_indev_t* indev, lv_indev_data_t* data) {
    if (touchI2cAddr_ == TOUCH_AXS5106L_ADDR) {
        uint8_t resp[14] = {0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

        Wire.beginTransmission(TOUCH_AXS5106L_ADDR);
        Wire.write(TOUCH_AXS_CMD, 11);
        Wire.endTransmission();
        Wire.requestFrom((uint8_t)TOUCH_AXS5106L_ADDR, (uint8_t)14);
        Wire.readBytes(resp, 14);

        // Validity checks (port of Waveshare's bsp_touch_read behaviour)
        if (resp[1] == 0 || resp[2] == 0 || resp[3] < 2 || resp[5] < 2 || resp[0] == 0xff || resp[1] > 2) {
            data->state = LV_INDEV_STATE_RELEASED;
            return;
        }

        uint16_t x = ((resp[2] & 0x0F) << 8) | resp[3];
        uint16_t y = ((resp[4] & 0x0F) << 8) | resp[5];

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
            default:  // 0 = portrait
                break;
        }

        touch_x = lx;
        touch_y = ly;
        touch_active = true;

        data->state = LV_INDEV_STATE_PRESSED;
        data->point.x = lx;
        data->point.y = ly;
    } else {
        // FT6336 on 0x38
        Wire.beginTransmission(TOUCH_FT6336_ADDR);
        Wire.write(0x02);
        if (Wire.endTransmission() == 0) {
            Wire.requestFrom((uint8_t)TOUCH_FT6336_ADDR, (uint8_t)5);
            if (Wire.available() >= 5) {
                uint8_t touches = Wire.read() & 0x0F;
                if (touches > 0 && touches <= 2) {
                    uint8_t x_hi = Wire.read();
                    uint8_t x_lo = Wire.read();
                    uint8_t y_hi = Wire.read();
                    uint8_t y_lo = Wire.read();
                    uint16_t x = ((x_hi & 0x0F) << 8) | x_lo;
                    uint16_t y = ((y_hi & 0x0F) << 8) | y_lo;

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
                        default:  // 0 = portrait
                            break;
                    }

                    touch_x = lx;
                    touch_y = ly;
                    touch_active = true;

                    data->state = LV_INDEV_STATE_PRESSED;
                    data->point.x = lx;
                    data->point.y = ly;
                    return;
                }
            }
        }
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

// ============================================================
// LVGL flush callback -> push pixels to the physical panel
// ============================================================
void Waveshare35B::my_disp_flush(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    static uint32_t flushCount = 0;
    if (flushCount++ < 5) {
        Serial.printf("[display] flush #%lu: x1=%d y1=%d x2=%d y2=%d\n",
                      (unsigned long)flushCount, area->x1, area->y1, area->x2, area->y2);
    }

    if (isModelB_) {
        // AXS15231B hardware panel is native 320x480 portrait and cannot do hardware rotation.
        // Waveshare's dedicated draw16bitBeRGBBitmapR1 rotates the 480x320 buffer on-the-fly via DMA.
        gfx->draw16bitBeRGBBitmapR1(0, 0, (uint16_t*)px_map, WIDTH, HEIGHT);
    } else {
        // ST7796 is fed the LVGL pixel buffer. With LV_COLOR_16_SWAP=1 that buffer holds
        // byte-swapped (Big Endian wire order) RGB565, so we MUST use the BE-aware draw call.
        // draw16bitRGBBitmap (LE) would re-swap every pixel and corrupt every hue.
        uint32_t w = lv_area_get_width(area);
        uint32_t h = lv_area_get_height(area);
        gfx->draw16bitBeRGBBitmap(area->x1, area->y1, (uint16_t*)px_map, w, h);
    }

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
        lv_display_set_buffers(disp, disp_draw_buf1, disp_draw_buf2, bufSize * sizeof(lv_color_t), LV_DISPLAY_RENDER_MODE_PARTIAL);
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
    lv_indev_set_display(indev, lv_display_get_default());
    return indev;
}

// ============================================================
// Public entry point
// ============================================================
bool Waveshare35B::begin() {
    Serial.println("[display] init");

    initPower();

    if (isModelB_) {
        bus = new Arduino_ESP32QSPI(PIN_QSPI_CS, PIN_QSPI_CLK, PIN_QSPI_D0,
                                    PIN_QSPI_D1, PIN_QSPI_D2, PIN_QSPI_D3);
        // AXS15231B QSPI hardware panel MUST be initialized in native portrait (rotation 0, 320x480).
        // Hardware rotation (MV=1) breaks QSPI rasterization and causes address bounds failure.
        // Landscape orientation (480x320) is handled in software by LVGL v9.
        gfx = new Arduino_AXS15231B(bus, -1 /*RST*/, 0 /*rotation*/, false, PANEL_W, PANEL_H);
    } else {
        bus = new Arduino_ESP32SPI(PIN_ST7796_DC, PIN_ST7796_CS, PIN_ST7796_SCK,
                                   PIN_ST7796_MOSI, PIN_ST7796_MISO);
        gfx = new Arduino_ST7796(bus, PIN_ST7796_RST, MX5_LCD_ROTATION, true, PANEL_W, PANEL_H);
    }

    if (!gfx->begin()) {
        Serial.println("[display] gfx->begin() failed");
        return false;
    }

    gfx->displayOn();

    // Power up backlight
    pinMode(MX5_PIN_BACKLIGHT, OUTPUT);
    digitalWrite(MX5_PIN_BACKLIGHT, HIGH);

    #if MX5_DISPLAY_SELFTEST
    Serial.println("[display] Testing screen: RED...");
    gfx->fillScreen(RGB565_RED);
    delay(1000);
    Serial.println("[display] Testing screen: GREEN...");
    gfx->fillScreen(RGB565_GREEN);
    delay(1000);
    Serial.println("[display] Testing screen: BLUE...");
    gfx->fillScreen(RGB565_BLUE);
    delay(1000);
    gfx->fillScreen(RGB565_BLACK);
#endif

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
