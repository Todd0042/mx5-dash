#define SDL_MAIN_HANDLED

// ---------------------------------------------------------------------------
// mx5-dash desktop preview (native SDL + LVGL)
//
// Reuses the exact same UI code as the device (lib/mx5_ui) - same palette,
// Montserrat fonts, segmented dotted arcs and screen layouts - but replaces
// the hardware (Waveshare35B + ESP32) with an SDL window and the Wi-Fi OBD
// stack (ObdService/FreeRTOS) with an animated fake-telemetry mock.
//
//   drag to swipe between screens   (match the capacitive touch gesture)
//   arrow keys  ← →  also swipe     (keyboard fallback)
//   Esc / close to quit
//
// Build: pio run -e native_preview
// ---------------------------------------------------------------------------

#include <SDL.h>

#include <lvgl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "Mx5UI.h"
#include "../lib/mx5_config/ObdSource.h"

#define SIM_W 480
#define SIM_H 320

// ---------------------------------------------------------------------------
// Frame buffer (device uses RGB565 via LV_COLOR_16_SWAP, byte-swapped)
// ---------------------------------------------------------------------------
static SDL_Texture* frameTex = nullptr;
static uint16_t g_currentFramebuffer[SIM_W * SIM_H];

static void flushCb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    (void)area;
    const uint16_t* src = (const uint16_t*)px_map;

    for (uint32_t k = 0; k < (uint32_t)(SIM_W * SIM_H); k++) {
        uint16_t px = src[k];
        g_currentFramebuffer[k] = (uint16_t)((px << 8) | (px >> 8));
    }
    if (frameTex) {
        SDL_UpdateTexture(frameTex, nullptr, g_currentFramebuffer, SIM_W * sizeof(uint16_t));
    }
    lv_display_flush_ready(disp);
}

static void saveRgb565Bmp(const char* filename, const uint16_t* rgb565, int width, int height) {
    FILE* f = fopen(filename, "wb");
    if (!f) return;

    uint32_t rowSize = (width * 3 + 3) & ~3;
    uint32_t imageSize = rowSize * height;
    uint32_t fileSize = 54 + imageSize;

    uint8_t header[54] = {
        'B', 'M',
        (uint8_t)(fileSize), (uint8_t)(fileSize >> 8), (uint8_t)(fileSize >> 16), (uint8_t)(fileSize >> 24),
        0, 0, 0, 0,
        54, 0, 0, 0,
        40, 0, 0, 0,
        (uint8_t)(width), (uint8_t)(width >> 8), (uint8_t)(width >> 16), (uint8_t)(width >> 24),
        (uint8_t)(height), (uint8_t)(height >> 8), (uint8_t)(height >> 16), (uint8_t)(height >> 24),
        1, 0,
        24, 0,
        0, 0, 0, 0,
        (uint8_t)(imageSize), (uint8_t)(imageSize >> 8), (uint8_t)(imageSize >> 16), (uint8_t)(imageSize >> 24),
        0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
    };
    fwrite(header, 1, 54, f);

    uint8_t* row = (uint8_t*)calloc(1, rowSize);
    for (int y = height - 1; y >= 0; y--) {
        for (int x = 0; x < width; x++) {
            uint16_t px = rgb565[y * width + x];
            // RGB565 to RGB888 (Little Endian in BMP: B, G, R)
            uint8_t r = (px >> 11) & 0x1F;
            uint8_t g = (px >> 5) & 0x3F;
            uint8_t b = px & 0x1F;
            row[x * 3 + 0] = (b * 527 + 23) >> 6; // B
            row[x * 3 + 1] = (g * 259 + 33) >> 6; // G
            row[x * 3 + 2] = (r * 527 + 23) >> 6; // R
        }
        fwrite(row, 1, rowSize, f);
    }
    free(row);
    fclose(f);
}

// ---------------------------------------------------------------------------
// Input glue - real mouse + synthesized keyboard swipes
// ---------------------------------------------------------------------------
static bool mouseDown = false;
static int mouseX = 0, mouseY = 0;
static bool quit = false;

static struct {
    bool active = false;
    float dx = 0, dy = 0;
    int phase = 0;              // 1 press center, 2 drag, 3 release
    uint32_t t0 = 0;
} swipe;

static void startSwipe(float dx, float dy) {
    swipe = {true, dx, dy, 1, SDL_GetTicks()};
}

// LVGL display flush + pointer input glue
static void indevRead(lv_indev_t* indev, lv_indev_data_t* data) {
    int px = mouseX, py = mouseY;
    bool down = mouseDown;

    if (swipe.active) {
        uint32_t dt = SDL_GetTicks() - swipe.t0;
        switch (swipe.phase) {
            case 1:  // press in the middle of the screen
                px = SIM_W / 2; py = SIM_H / 2; down = true;
                if (dt >= 40) { swipe.phase = 2; swipe.t0 = SDL_GetTicks(); }
                break;
            case 2: {  // drag outwards
                float f = fminf((float)dt / 160.0f, 1.0f);
                px = SIM_W / 2 + (int)(swipe.dx * 150.0f * f);
                py = SIM_H / 2 + (int)(swipe.dy * 150.0f * f);
                down = true;
                if (dt >= 160) { swipe.phase = 3; swipe.t0 = SDL_GetTicks(); }
                break;
            }
            case 3:  // release
                px = SIM_W / 2 + (int)(swipe.dx * 150.0f);
                py = SIM_H / 2 + (int)(swipe.dy * 150.0f);
                down = false;
                swipe.active = false;
                break;
        }
    }

    data->point.x = px;
    data->point.y = py;
    data->state = down ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

#include <time.h>

static uint32_t nowMs(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
}

static uint32_t tickCb(void) {
    return nowMs();
}

static float ease(float x) { return x * x * (3.0f - 2.0f * x); }

static void driveCycle(VehicleData& out, float t) {
    const float cycle = fmodf(t, 60.0f);   // 60 s loop

    // speed profile (km/h): accelerates up to 215 km/h (134 MPH) for 3-digit testing
    float spd;
    if (cycle < 12.0f)      spd = 215.0f * ease(cycle / 12.0f);
    else if (cycle < 22.0f) spd = 215.0f - 85.0f * ease((cycle - 12.0f) / 10.0f); // 130 km/h = 81 MPH
    else if (cycle < 30.0f) spd = 130.0f + 65.0f * ease((cycle - 22.0f) / 8.0f);  // 195 km/h = 121 MPH
    else if (cycle < 42.0f) spd = 195.0f - 140.0f * ease((cycle - 30.0f) / 12.0f); // 55 km/h = 34 MPH
    else                    spd = 55.0f * (1.0f - ease((cycle - 42.0f) / 18.0f));  // back to 0

    // ND2 6MT: rpm per km/h per gear (1st..6th)
    static const float ratio[6] = {157.0f, 92.0f, 63.0f, 49.0f, 40.0f, 31.0f};
    int gi;
    if      (spd < 12)  gi = 0;
    else if (spd < 32)  gi = 1;
    else if (spd < 50)  gi = 2;
    else if (spd < 75)  gi = 3;
    else if (spd < 105) gi = 4;
    else                gi = 5;

    out.speedKmh = (uint8_t)(spd + 0.5f);
    out.rpm = (uint16_t)(spd * ratio[gi] + (spd < 1.0f ? 780.0f : 0.0f) + 0.5f);

    // mirror ObdService::estimateGear() thresholds so the white gear box
    // behaves exactly like the firmware
    if (out.rpm == 0 && out.speedKmh == 0)      out.gear = '-';
    else if (out.speedKmh == 0)                 out.gear = 'N';
    else {
        float r = (float)out.rpm / (float)out.speedKmh;
        if      (r < 35.0f) out.gear = '6';
        else if (r < 45.0f) out.gear = '5';
        else if (r < 56.0f) out.gear = '4';
        else if (r < 77.0f) out.gear = '3';
        else if (r < 125.0f) out.gear = '2';
        else                out.gear = '1';
    }

    out.coolantC      = (uint8_t)(83.0f + 4.0f * sinf(t / 3.1f));
    out.intakeAirC    = (uint8_t)(29.0f + 7.0f * sinf(t / 5.3f));
    out.ambientC      = (uint8_t)(24.0f + 3.0f * sinf(t / 7.1f));
    out.throttlePct   = (uint8_t)(14.0f + 80.0f * (0.5f + 0.5f * sinf(t / 2.3f)));
    out.engineLoadPct = (uint8_t)(22.0f + 70.0f * (0.5f + 0.5f * sinf(t / 1.9f)));
    out.batteryVolts  = 14.1f + 0.35f * sinf(t / 6.7f);
    out.oilTempC      = (uint8_t)(91.0f + 5.0f * sinf(t / 9.1f));
    out.fuelLevelPct  = (uint8_t)fmaxf(64.0f - t / 40.0f, 15.0f);

    // Track & Dynamics simulation
    bool braking = (cycle >= 12.0f && cycle < 16.0f) || (cycle >= 30.0f && cycle < 36.0f);
    out.brakePressurePct = braking ? (uint8_t)(65.0f * sinf((cycle - 12.0f) * 0.78f)) : 0;
    out.estHorsepower = (uint16_t)((out.rpm / 7500.0f) * 181.0f * (out.throttlePct / 100.0f));
    out.estTorqueFtLb = (uint16_t)(151.0f * (out.engineLoadPct / 100.0f));
    if (out.estHorsepower > 181) out.estHorsepower = 181;
    if (out.estTorqueFtLb > 151) out.estTorqueFtLb = 151;

    // 0-60 MPH timer simulation
    if (cycle < 5.42f) {
        out.accel0to60TimeSec = cycle;
        out.accelTimerState = 1; // Running
    } else {
        out.accel0to60TimeSec = 5.42f;
        out.accelTimerState = 2; // Finished
    }
    out.best0to60TimeSec = 5.28f;

    // Trip & Fuel Economy simulation
    out.instantMpg = (out.speedKmh > 5) ? (float)(32.0f + 14.0f * cosf(t / 3.0f)) : 0.0f;
    if (out.instantMpg > 60.0f) out.instantMpg = 60.0f;
    out.tripAvgMpg = 32.8f;
    out.tripDistanceMiles = 14.2f + t / 40.0f;
    out.rangeMiles = (uint16_t)(out.fuelLevelPct * 4.2f);

    for (int i = 0; i < 4; i++) {
        out.tireKnown[i] = true;
        out.tirePressure[i] = 2.05f + 0.05f * sinf(t / 10.0f + (float)i);
        out.tireTemp[i] = 23.0f + 2.0f * sinf(t / 12.0f + (float)i);
        out.wheelSpeedKmh[i] = (float)out.speedKmh + ((i % 2 == 0) ? 0.2f : -0.2f);
    }

    // Diagnostics Telemetry Mock
    out.shortTermFuelTrimPct = (float)(3.2f + 2.5f * sinf(t / 2.0f));
    out.longTermFuelTrimPct = +5.8f;
    out.airFuelRatio = (out.throttlePct > 80) ? 12.5f : (float)(14.7f + 0.2f * sinf(t / 1.5f));
    out.fuelRailPressurePsi = (uint16_t)(1200 + out.engineLoadPct * 12);
    out.evapVaporPa = (int16_t)(12 + 4.0f * sinf(t / 5.0f));
    out.evapPurgePct = 18;

    out.cylMisfireCount[0] = 0;
    out.cylMisfireCount[1] = 0;
    out.cylMisfireCount[2] = 0;
    out.cylMisfireCount[3] = 0;
    out.sparkAdvanceDeg = (float)(14.0f + 12.0f * (out.rpm / 7500.0f));
    out.knockRetardDeg = 0.0f;
    out.vvtIntakeDeg = (float)(12.0f + 16.0f * (out.rpm / 7500.0f));
    out.vvtExhaustDeg = (float)(4.0f + 8.0f * (out.rpm / 7500.0f));

    out.steeringAngleDeg = (float)(2.5f * sinf(t / 4.0f));
    out.transFluidTempC = 78;
    out.tccSlipRpm = (out.speedKmh > 20) ? 0 : 45;

    out.imMisfireReady = true;
    out.imFuelReady = true;
    out.imCompReady = true;
    out.imCatReady = true;
    out.imEvapReady = true;
    out.imO2Ready = true;
    out.imO2HeaterReady = true;
    out.imEgrVvtReady = true;

    out.dtcCount = 0;

    out.connected = true;
    out.canError = false;
    out.lastUpdateMs = nowMs();
}

class SimObd : public ObdSource {
public:
    bool overrideTireLow = false;
    void snapshot(VehicleData& out) override {
        driveCycle(out, (float)nowMs() / 1000.0f);
        if (overrideTireLow) {
            out.tirePressure[0] = 1.50f; // ~21.8 PSI
        }
    }
    bool connected() const override { return true; }

    // Wizard/calibration mock: simulate a sensor DID capture ~2s after
    // calibration starts, so the wheel-map flow is testable on the desktop.
    bool didCaptured(char* didBuf, size_t len) const override {
        if (!calibActive_ || didBuf == nullptr || len == 0) return false;
        if (SDL_GetTicks() - calibStartMs_ < 2000) return false;
        static const char* dids[4] = {"2A05", "2A06", "2A07", "2A08"};
        snprintf(didBuf, len, "%s", dids[wheelIdx_ % 4]);
        calibActive_ = false;
        return true;
    }
    bool calibrationActive() const override { return calibActive_; }
    void startCalibration(uint8_t activeWheel) override {
        calibActive_ = true;
        calibStartMs_ = SDL_GetTicks();
        wheelIdx_ = activeWheel;
    }
    void stopCalibration() override { calibActive_ = false; }
    void freeze(bool frozen) override { frozen_ = frozen; }
    bool frozen() const { return frozen_; }

    bool isScanning() const override { return scanning_; }
    void startBleScan() override {
        scanning_ = true;
        scanStartMs_ = SDL_GetTicks();
    }
    void stopBleScan() override { scanning_ = false; }
    uint8_t getDiscoveredDeviceCount() const override { return 4; }
    bool getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const override {
        static const BleDeviceInfo mockDevs[4] = {
            {"vLinker MS 08449", "64:8C:BB:1A:08:0A", -58, true, true},
            {"OBDLink CX BLE",   "F0:B5:D1:22:90:4C", -72, true, false},
            {"VEEPEAK OBD-II",   "B8:1F:5E:44:11:02", -81, true, false},
            {"iCar Pro BLE4.0",  "DC:06:98:50:31:AA", -88, true, false}
        };
        if (index >= 4) return false;
        out = mockDevs[index];
        if (strlen(pairedMac_) > 0) {
            out.isPaired = (strcasecmp(out.mac, pairedMac_) == 0);
        }
        return true;
    }
    void pairDevice(const char* mac, const char* name) override {
        if (mac) {
            strncpy(pairedMac_, mac, sizeof(pairedMac_) - 1);
            pairedMac_[sizeof(pairedMac_) - 1] = '\0';
        }
        if (name) {
            strncpy(pairedName_, name, sizeof(pairedName_) - 1);
            pairedName_[sizeof(pairedName_) - 1] = '\0';
        }
        scanning_ = false;
    }
    void forgetPairedDevice() override {
        pairedMac_[0] = '\0';
        pairedName_[0] = '\0';
        startBleScan();
    }
    void getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const override {
        if (macBuf && macLen > 0) {
            strncpy(macBuf, (pairedMac_[0] != '\0') ? pairedMac_ : "64:8C:BB:1A:08:0A", macLen - 1);
            macBuf[macLen - 1] = '\0';
        }
        if (nameBuf && nameLen > 0) {
            strncpy(nameBuf, (pairedName_[0] != '\0') ? pairedName_ : "vLinker MS 08449", nameLen - 1);
            nameBuf[nameLen - 1] = '\0';
        }
    }
    int8_t getRssi() const override { return -58; }

private:
    mutable bool calibActive_ = false;
    mutable uint32_t calibStartMs_ = 0;
    mutable uint8_t wheelIdx_ = 0;
    mutable bool frozen_ = false;
    mutable bool scanning_ = false;
    mutable uint32_t scanStartMs_ = 0;
    mutable char pairedMac_[20] = "64:8C:BB:1A:08:0A";
    mutable char pairedName_[32] = "vLinker MS 08449";
};

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main(int argc, char** argv) {
    // Check if called with --dump-screens <output_dir> (Headless mode)
    if (argc >= 3 && strcmp(argv[1], "--dump-screens") == 0) {
        lv_init();
        lv_tick_set_cb(tickCb);

        static lv_color_t buf[2][SIM_W * SIM_H];
        lv_display_t* disp = lv_display_create(SIM_W, SIM_H);
        lv_display_set_buffers(disp, buf[0], buf[1], sizeof(buf[0]),
                               LV_DISPLAY_RENDER_MODE_FULL);
        lv_display_set_flush_cb(disp, flushCb);

        SimObd obd;
        Mx5UI ui(obd);
        ui.begin();

        const char* outDir = argv[2];
        for (uint8_t s = 0; s < Mx5UI::SCREEN_COUNT; s++) {
            ui.setScreen(s);
            ui.update();
            lv_timer_handler();
            lv_refr_now(disp);
            char path[512];
            snprintf(path, sizeof(path), "%s/screen_%d.bmp", outDir, s);
            saveRgb565Bmp(path, g_currentFramebuffer, SIM_W, SIM_H);
            printf("[SCREENSHOT] Saved %s\n", path);
            fflush(stdout);
        }

        // Also dump DTC Diagnostic Health Modal
        ui.setScreen(Mx5UI::SCREEN_DIAG);
        ui.openDtcGuide(0);
        ui.update();
        lv_timer_handler();
        lv_refr_now(disp);
        char modalPath[512];
        snprintf(modalPath, sizeof(modalPath), "%s/screen_6_modal.bmp", outDir);
        saveRgb565Bmp(modalPath, g_currentFramebuffer, SIM_W, SIM_H);
        printf("[SCREENSHOT] Saved %s\n", modalPath);
        fflush(stdout);
        ui.hideDtcRepairGuide();

        // Also dump Dynamic Warning on Speedometer Screen
        obd.overrideTireLow = true;
        ui.setScreen(Mx5UI::SCREEN_SPEED);
        ui.update();
        lv_timer_handler();
        lv_refr_now(disp);
        char warnPath[512];
        snprintf(warnPath, sizeof(warnPath), "%s/screen_0_warning.bmp", outDir);
        saveRgb565Bmp(warnPath, g_currentFramebuffer, SIM_W, SIM_H);
        printf("[SCREENSHOT] Saved %s\n", warnPath);
        fflush(stdout);
        obd.overrideTireLow = false;

        // Also dump SD Card Format Modal
        ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_LOGS);
        ui.showSdFormatModal();
        ui.update();
        lv_timer_handler();
        lv_refr_now(disp);
        char sdPath[512];
        snprintf(sdPath, sizeof(sdPath), "%s/screen_12_modal.bmp", outDir);
        saveRgb565Bmp(sdPath, g_currentFramebuffer, SIM_W, SIM_H);
        printf("[SCREENSHOT] Saved %s\n", sdPath);
        fflush(stdout);
        ui.hideSdFormatModal();

        // Also dump Night Mode Speedometer & RPM Gauges
        ui.setNightMode(true);
        ui.setScreen(Mx5UI::SCREEN_SPEED);
        ui.update();
        lv_timer_handler();
        lv_refr_now(disp);
        char nightSpeedPath[512];
        snprintf(nightSpeedPath, sizeof(nightSpeedPath), "%s/screen_0_night.bmp", outDir);
        saveRgb565Bmp(nightSpeedPath, g_currentFramebuffer, SIM_W, SIM_H);
        printf("[SCREENSHOT] Saved %s\n", nightSpeedPath);
        fflush(stdout);

        ui.setScreen(Mx5UI::SCREEN_RPM);
        ui.update();
        lv_timer_handler();
        lv_refr_now(disp);
        char nightRpmPath[512];
        snprintf(nightRpmPath, sizeof(nightRpmPath), "%s/screen_2_night.bmp", outDir);
        saveRgb565Bmp(nightRpmPath, g_currentFramebuffer, SIM_W, SIM_H);
        printf("[SCREENSHOT] Saved %s\n", nightRpmPath);
        fflush(stdout);
        ui.setNightMode(false);

        fflush(stdout);
        return 0;
    }

    if (SDL_Init(SDL_INIT_VIDEO) < 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return 1;
    }

    SDL_Window* win = SDL_CreateWindow(
        "MX-5 Dash (480x320 landscape) - LVGL preview (drag or arrows to swipe)",
        SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        SIM_W * 2, SIM_H * 2, SDL_WINDOW_SHOWN | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) { fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError()); return 1; }
    SDL_ShowWindow(win);
    SDL_RaiseWindow(win);

    SDL_Renderer* rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_ACCELERATED);
    if (!rend) rend = SDL_CreateRenderer(win, -1, SDL_RENDERER_SOFTWARE);
    if (!rend) { fprintf(stderr, "SDL_CreateRenderer failed: %s\n", SDL_GetError()); return 1; }

    SDL_SetRenderDrawColor(rend, 0x11, 0x11, 0x11, 255);
    frameTex = SDL_CreateTexture(rend, SDL_PIXELFORMAT_RGB565,
                                 SDL_TEXTUREACCESS_STREAMING, SIM_W, SIM_H);

    lv_init();
    lv_tick_set_cb(tickCb);

    static lv_color_t buf[2][SIM_W * SIM_H];
    lv_display_t* disp = lv_display_create(SIM_W, SIM_H);
    lv_display_set_buffers(disp, buf[0], buf[1], sizeof(buf[0]),
                           LV_DISPLAY_RENDER_MODE_FULL);
    lv_display_set_flush_cb(disp, flushCb);

    lv_indev_t* indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, indevRead);

    SimObd obd;              // uses the sim `driveCycle()` mock
    Mx5UI ui(obd);
    ui.begin();

    const uint32_t TARGET_FRAME_TIME_MS = 16; // ~60 FPS

    while (!quit) {
        uint32_t frameStart = SDL_GetTicks();

        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_QUIT) {
                quit = true;
            } else if (e.type == SDL_KEYDOWN) {
                if (e.key.keysym.sym == SDLK_ESCAPE)      quit = true;
                else if (e.key.keysym.sym == SDLK_LEFT)   ui.prevScreen();
                else if (e.key.keysym.sym == SDLK_RIGHT)  ui.nextScreen();
                else if (e.key.keysym.sym == SDLK_UP || e.key.keysym.sym == SDLK_DOWN) ui.toggleMenu();
                else if (e.key.keysym.sym >= SDLK_1 && e.key.keysym.sym <= SDLK_8) {
                    ui.setScreen((uint8_t)(e.key.keysym.sym - SDLK_1));
                } else if (e.key.keysym.sym == SDLK_9 || e.key.keysym.sym == SDLK_f) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_FUEL);
                } else if (e.key.keysym.sym == SDLK_0 || e.key.keysym.sym == SDLK_m) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_CYL);
                } else if (e.key.keysym.sym == SDLK_c) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_CHASSIS);
                } else if (e.key.keysym.sym == SDLK_s) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_SMOG);
                } else if (e.key.keysym.sym == SDLK_l) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG_SUB_LOGS);
                } else if (e.key.keysym.sym == SDLK_p) {
                    ui.setScreen(Mx5UI::SCREEN_SETTINGS);
                } else if (e.key.keysym.sym == SDLK_b) {
                    ui.setScreen(Mx5UI::SCREEN_BLE_CONFIG);
                } else if (e.key.keysym.sym == SDLK_w) {
                    ui.runSetupWizard();
                } else if (e.key.keysym.sym == SDLK_t) {
                    ui.runTpmsRecalibration();
                } else if (e.key.keysym.sym == SDLK_g) {
                    ui.setScreen(Mx5UI::SCREEN_DIAG);
                    ui.showDtcRepairGuide("P0171");
                } else if (e.key.keysym.sym == SDLK_n) {
                    ui.setNightMode(!ui.isNightMode());
                    printf("[NIGHT MODE] Toggled: %s\n", ui.isNightMode() ? "ON (Amber Night Vision)" : "OFF (Daytime White)");
                    fflush(stdout);
                }
            } else if (e.type == SDL_MOUSEBUTTONDOWN) {
                mouseDown = true;
                mouseX = e.button.x / 2; mouseY = e.button.y / 2;
            } else if (e.type == SDL_MOUSEBUTTONUP) {
                mouseDown = false;
                mouseX = e.button.x / 2; mouseY = e.button.y / 2;
            } else if (e.type == SDL_MOUSEMOTION) {
                if (mouseDown || swipe.active) { mouseX = e.motion.x / 2; mouseY = e.motion.y / 2; }
            }
        }

        ui.update();                      // pull animated telemetry + repaint
        lv_timer_handler();

        SDL_RenderClear(rend);
        SDL_RenderCopy(rend, frameTex, nullptr, nullptr);
        SDL_RenderPresent(rend);

        uint32_t elapsed = SDL_GetTicks() - frameStart;
        if (elapsed < TARGET_FRAME_TIME_MS) {
            SDL_Delay(TARGET_FRAME_TIME_MS - elapsed);
        }
    }

    SDL_DestroyTexture(frameTex);
    SDL_DestroyRenderer(rend);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}