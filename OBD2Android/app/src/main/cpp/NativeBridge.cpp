#include <jni.h>
#include <android/log.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <atomic>
#include <mutex>

#include "lvgl.h"
#include "android_ui/AndroidMx5UI.h"
#include "AndroidObdSource.h"
#include "UserPrefs.h"

#define LOG_TAG "Mx5NativeBridge"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

extern void UserPrefs_SetJvm(JavaVM* vm, jobject bridgeObj);

static JavaVM* gJvm = nullptr;
static AndroidObdSource gObdSource;
static AndroidMx5UI gUi(gObdSource);

static std::atomic<bool> gInitialized{false};
static lv_display_t* gDisp = nullptr;
static lv_indev_t* gIndev = nullptr;

static constexpr uint16_t W_RES = AndroidMx5UI::WIDTH;
static constexpr uint16_t H_RES = AndroidMx5UI::HEIGHT;

static uint16_t gFrameBuffer[W_RES * H_RES];
static lv_point_t gTouchPoint{0, 0};
static lv_indev_state_t gTouchState = LV_INDEV_STATE_RELEASED;
static bool gPendingPress = false;
static std::mutex gRenderMutex;
static std::mutex gTouchMutex;

static uint32_t custom_tick_get(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)(ts.tv_sec * 1000ULL + ts.tv_nsec / 1000000ULL);
}

static void disp_flush_cb(lv_display_t* disp, const lv_area_t* area, uint8_t* px_map) {
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t x = area->x1;
    uint32_t y = area->y1;

    for (uint32_t row = 0; row < h; row++) {
        uint32_t dst = (y + row) * W_RES + x;
        uint32_t src = row * w;
        memcpy(&gFrameBuffer[dst], px_map + src * 2, w * 2);
    }
    lv_display_flush_ready(disp);
}

static void indev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    (void)indev;
    std::lock_guard<std::mutex> lock(gTouchMutex);
    data->point.x = gTouchPoint.x;
    data->point.y = gTouchPoint.y;
    if (gPendingPress) {
        data->state = LV_INDEV_STATE_PRESSED;
        gPendingPress = false;
    } else {
        data->state = gTouchState;
    }
}

static void rgb565_to_argb8888(const uint16_t* src, int* dst, int count) {
    for (int i = 0; i < count; i++) {
        uint16_t p = src[i];
        int r = ((p >> 11) & 0x1F); r = (r << 3) | (r >> 2);
        int g = ((p >> 5)  & 0x3F); g = (g << 2) | (g >> 4);
        int b = (p & 0x1F);         b = (b << 3) | (b >> 2);
        dst[i] = 0xFF000000 | (r << 16) | (g << 8) | b;
    }
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
    gJvm = vm;
    return JNI_VERSION_1_6;
}

extern "C" {

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeInit(
        JNIEnv* env, jclass /*clazz*/, jobject prefBridge) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    if (gInitialized.load()) return;

    if (gJvm && prefBridge) {
        UserPrefs_SetJvm(gJvm, prefBridge);
        UserPrefs::loadAll();
    }

    lv_init();
    lv_tick_set_cb(custom_tick_get);

    static lv_color_t drawBuf[W_RES * 40];
    gDisp = lv_display_create(W_RES, H_RES);
    lv_display_set_buffers(gDisp, drawBuf, nullptr, sizeof(drawBuf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(gDisp, disp_flush_cb);

    gIndev = lv_indev_create();
    lv_indev_set_type(gIndev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(gIndev, indev_read_cb);

    gUi.begin();

    // Default to speed screen
    gUi.setScreen(AndroidMx5UI::SCREEN_SPEED);

    gInitialized.store(true);
    LOGI("NativeBridge initialized with %dx%d widescreen engine", W_RES, H_RES);
}

JNIEXPORT jboolean JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeRender(
        JNIEnv* env, jclass /*clazz*/, jintArray outPixels, jint len) {
    if (!gInitialized.load()) return JNI_FALSE;
    if (!outPixels || len < (W_RES * H_RES)) return JNI_FALSE;

    std::lock_guard<std::mutex> lock(gRenderMutex);

    gUi.update();
    lv_timer_handler();

    jint* dst = env->GetIntArrayElements(outPixels, nullptr);
    if (!dst) return JNI_FALSE;

    rgb565_to_argb8888(gFrameBuffer, (int*)dst, W_RES * H_RES);
    env->ReleaseIntArrayElements(outPixels, dst, 0);

    return JNI_TRUE;
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeTouch(
        JNIEnv* /*env*/, jclass /*clazz*/, jint action, jint x, jint y) {
    std::lock_guard<std::mutex> lock(gTouchMutex);
    gTouchPoint.x = x;
    gTouchPoint.y = y;
    if (action == 0) {
        gTouchState = LV_INDEV_STATE_PRESSED;
        gPendingPress = true;
    } else {
        gTouchState = LV_INDEV_STATE_RELEASED;
    }
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeNext(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.nextScreen();
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativePrev(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.prevScreen();
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeToggleMenu(JNIEnv*, jclass) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.toggleMenu();
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeSetScreen(JNIEnv*, jclass, jint index) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.setScreen((uint8_t)index);
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeSetThemeMode(JNIEnv*, jclass, jint mode) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.setThemeMode((AndroidMx5UI::ThemeMode)mode);
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeSetNightMode(JNIEnv*, jclass, jboolean isNight) {
    std::lock_guard<std::mutex> lock(gRenderMutex);
    gUi.setNightMode(isNight != 0);
}

JNIEXPORT void JNICALL
Java_com_mx5dash_obd2android_bridge_NativeBridge_nativeUpdateFullTelemetry(
        JNIEnv* env, jclass /*clazz*/,
        jint rpm, jint speedKmh, jint coolantC, jint oilTempC, jint intakeC, jint ambientC,
        jfloat batVolts, jint loadPct, jint throttlePct, jint fuelPct,
        jchar gear, jint brakePct, jint hp, jint torque,
        jfloat accel0to60, jfloat best0to60, jint accelTimerState,
        jfloat instantMpg, jfloat tripAvgMpg, jfloat tripDistance, jint rangeMiles,
        jfloat afr, jfloat stft, jfloat ltft, jfloat knockRetard, jint railPressurePsi,
        jfloat sparkAdvance, jfloat vvtIntake, jfloat vvtExhaust,
        jint transFluidTempC, jint tccSlipRpm, jfloat steeringAngle,
        jfloat flPsi, jfloat frPsi, jfloat rlPsi, jfloat rrPsi,
        jfloat flTemp, jfloat frTemp, jfloat rlTemp, jfloat rrTemp,
        jint dtcCount, jboolean isNight, jboolean connected) {

    VehicleData d;
    memset(&d, 0, sizeof(d));

    d.rpm = (uint16_t)rpm;
    d.speedKmh = (uint8_t)speedKmh;
    d.coolantC = (uint8_t)coolantC;
    d.oilTempC = (uint8_t)oilTempC;
    d.intakeAirC = (uint8_t)intakeC;
    d.ambientC = (uint8_t)ambientC;
    d.batteryVolts = batVolts;
    d.engineLoadPct = (uint8_t)loadPct;
    d.throttlePct = (uint8_t)throttlePct;
    d.fuelLevelPct = (uint8_t)fuelPct;
    d.gear = (char)gear;

    d.brakePressurePct = (uint8_t)brakePct;
    d.estHorsepower = (uint16_t)hp;
    d.estTorqueFtLb = (uint16_t)torque;
    d.accel0to60TimeSec = accel0to60;
    d.best0to60TimeSec = best0to60;
    d.accelTimerState = (uint8_t)accelTimerState;

    d.instantMpg = instantMpg;
    d.tripAvgMpg = tripAvgMpg;
    d.tripDistanceMiles = tripDistance;
    d.rangeMiles = (uint16_t)rangeMiles;

    d.airFuelRatio = afr;
    d.shortTermFuelTrimPct = stft;
    d.longTermFuelTrimPct = ltft;
    d.knockRetardDeg = knockRetard;
    d.fuelRailPressurePsi = (uint16_t)railPressurePsi;
    d.sparkAdvanceDeg = sparkAdvance;
    d.vvtIntakeDeg = vvtIntake;
    d.vvtExhaustDeg = vvtExhaust;

    d.transFluidTempC = (uint8_t)transFluidTempC;
    d.tccSlipRpm = (uint16_t)tccSlipRpm;
    d.steeringAngleDeg = steeringAngle;

    d.tirePressure[0] = flPsi / 14.5038f;
    d.tirePressure[1] = frPsi / 14.5038f;
    d.tirePressure[2] = rlPsi / 14.5038f;
    d.tirePressure[3] = rrPsi / 14.5038f;

    d.tireTemp[0] = flTemp;
    d.tireTemp[1] = frTemp;
    d.tireTemp[2] = rlTemp;
    d.tireTemp[3] = rrTemp;

    d.tireKnown[0] = true;
    d.tireKnown[1] = true;
    d.tireKnown[2] = true;
    d.tireKnown[3] = true;

    d.dtcCount = (uint8_t)dtcCount;
    if (dtcCount > 0) {
        strncpy(d.dtcCodes[0], "P0421", sizeof(d.dtcCodes[0]) - 1);
        strncpy(d.dtcDesc[0], "Warm Up Catalyst Bank 1", sizeof(d.dtcDesc[0]) - 1);
    }

    d.imMisfireReady = true;
    d.imFuelReady = true;
    d.imCompReady = true;
    d.imCatReady = true;
    d.imEvapReady = true;
    d.imO2Ready = true;
    d.imO2HeaterReady = true;
    d.imEgrVvtReady = true;

    d.isNightMode = (isNight != 0);
    d.connected = (connected != 0);

    gObdSource.update(d);
}

} // extern "C"
