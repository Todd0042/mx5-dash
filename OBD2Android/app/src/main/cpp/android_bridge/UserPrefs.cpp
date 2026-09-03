#include "UserPrefs.h"
#include "Config.h"
#include <jni.h>
#include <cstring>
#include <cstdio>
#include <mutex>
#include <android/log.h>

#define LOG_TAG "UserPrefsNative"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

static JavaVM* gJvm = nullptr;
static jobject gPrefBridgeObj = nullptr;
static jclass gPrefBridgeCls = nullptr;
static std::mutex gPrefMutex;

#ifndef MX5_LCD_ROTATION
#define MX5_LCD_ROTATION 1
#endif
#ifndef MX5_UNITS_US
#define MX5_UNITS_US 1
#endif
#ifndef MX5_BLE_DEVICE_PREFIX
#define MX5_BLE_DEVICE_PREFIX "vLinker"
#endif
#ifndef MX5_TPMS_ENABLED
#define MX5_TPMS_ENABLED 1
#endif

static uint8_t  sRotation   = MX5_LCD_ROTATION;
static bool     sUnits      = (MX5_UNITS_US != 0);
static uint8_t  sThemeMode  = 0;
static uint8_t  sBrightness = 95;
static bool     sAutoLog    = true;
static bool     sSpeedMask  = true;
static char     sBlePrefix[32] = MX5_BLE_DEVICE_PREFIX;
static uint16_t sBleScanTimeout = 5000;
static bool     sConfigured  = true;
static bool     sTpmsEnabled = (MX5_TPMS_ENABLED != 0);
static char     sWheelDid[4][8] = {"2A05", "2A08", "2A0B", "2A0E"};

void UserPrefs_SetJvm(JavaVM* vm, jobject bridgeObj) {
    std::lock_guard<std::mutex> lock(gPrefMutex);
    gJvm = vm;
    if (gPrefBridgeObj && vm) {
        JNIEnv* env = nullptr;
        if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
            env->DeleteGlobalRef(gPrefBridgeObj);
            if (gPrefBridgeCls) env->DeleteGlobalRef(gPrefBridgeCls);
        }
        gPrefBridgeObj = nullptr;
        gPrefBridgeCls = nullptr;
    }
    if (bridgeObj && vm) {
        JNIEnv* env = nullptr;
        if (vm->GetEnv((void**)&env, JNI_VERSION_1_6) == JNI_OK) {
            gPrefBridgeObj = env->NewGlobalRef(bridgeObj);
            jclass cls = env->GetObjectClass(bridgeObj);
            gPrefBridgeCls = (jclass)env->NewGlobalRef(cls);
        }
    }
    LOGI("UserPrefs JavaVM and GlobalRef initialized");
}

static JNIEnv* get_env(bool& needDetach) {
    needDetach = false;
    if (!gJvm) return nullptr;
    JNIEnv* env = nullptr;
    jint res = gJvm->GetEnv((void**)&env, JNI_VERSION_1_6);
    if (res == JNI_OK) return env;
    if (res == JNI_EDETACHED) {
        if (gJvm->AttachCurrentThread(&env, nullptr) == JNI_OK) {
            needDetach = true;
            return env;
        }
    }
    return nullptr;
}

static void release_env(bool needDetach) {
    if (needDetach && gJvm) {
        gJvm->DetachCurrentThread();
    }
}

static void jni_save_int(const char* key, int value) {
    std::lock_guard<std::mutex> lock(gPrefMutex);
    if (!gJvm || !gPrefBridgeObj || !gPrefBridgeCls) return;
    bool needDetach = false;
    JNIEnv* env = get_env(needDetach);
    if (!env) return;

    jmethodID mid = env->GetMethodID(gPrefBridgeCls, "saveInt", "(Ljava/lang/String;I)V");
    if (mid) {
        jstring jkey = env->NewStringUTF(key);
        env->CallVoidMethod(gPrefBridgeObj, mid, jkey, (jint)value);
        env->DeleteLocalRef(jkey);
    }
    release_env(needDetach);
}

static void jni_save_bool(const char* key, bool value) {
    std::lock_guard<std::mutex> lock(gPrefMutex);
    if (!gJvm || !gPrefBridgeObj || !gPrefBridgeCls) return;
    bool needDetach = false;
    JNIEnv* env = get_env(needDetach);
    if (!env) return;

    jmethodID mid = env->GetMethodID(gPrefBridgeCls, "saveBoolean", "(Ljava/lang/String;Z)V");
    if (mid) {
        jstring jkey = env->NewStringUTF(key);
        env->CallVoidMethod(gPrefBridgeObj, mid, jkey, (jboolean)value);
        env->DeleteLocalRef(jkey);
    }
    release_env(needDetach);
}

static void jni_save_string(const char* key, const char* value) {
    std::lock_guard<std::mutex> lock(gPrefMutex);
    if (!gJvm || !gPrefBridgeObj || !gPrefBridgeCls) return;
    bool needDetach = false;
    JNIEnv* env = get_env(needDetach);
    if (!env) return;

    jmethodID mid = env->GetMethodID(gPrefBridgeCls, "saveString", "(Ljava/lang/String;Ljava/lang/String;)V");
    if (mid) {
        jstring jkey = env->NewStringUTF(key);
        jstring jval = env->NewStringUTF(value ? value : "");
        env->CallVoidMethod(gPrefBridgeObj, mid, jkey, jval);
        env->DeleteLocalRef(jkey);
        env->DeleteLocalRef(jval);
    }
    release_env(needDetach);
}

void UserPrefs::loadAll() {
    std::lock_guard<std::mutex> lock(gPrefMutex);
    if (!gJvm || !gPrefBridgeObj || !gPrefBridgeCls) return;
    bool needDetach = false;
    JNIEnv* env = get_env(needDetach);
    if (!env) return;

    jmethodID midGetInt = env->GetMethodID(gPrefBridgeCls, "getInt", "(Ljava/lang/String;I)I");
    jmethodID midGetBool = env->GetMethodID(gPrefBridgeCls, "getBoolean", "(Ljava/lang/String;Z)Z");
    jmethodID midGetStr = env->GetMethodID(gPrefBridgeCls, "getString", "(Ljava/lang/String;Ljava/lang/String;)Ljava/lang/String;");

    if (midGetInt) {
        jstring kRot = env->NewStringUTF("rotation");
        sRotation = (uint8_t)env->CallIntMethod(gPrefBridgeObj, midGetInt, kRot, (jint)sRotation);
        env->DeleteLocalRef(kRot);

        jstring kThm = env->NewStringUTF("theme_mode");
        sThemeMode = (uint8_t)env->CallIntMethod(gPrefBridgeObj, midGetInt, kThm, (jint)sThemeMode);
        env->DeleteLocalRef(kThm);

        jstring kBri = env->NewStringUTF("brightness");
        sBrightness = (uint8_t)env->CallIntMethod(gPrefBridgeObj, midGetInt, kBri, (jint)sBrightness);
        env->DeleteLocalRef(kBri);
    }

    if (midGetBool) {
        jstring kUnits = env->NewStringUTF("units_us");
        sUnits = env->CallBooleanMethod(gPrefBridgeObj, midGetBool, kUnits, (jboolean)sUnits);
        env->DeleteLocalRef(kUnits);

        jstring kLog = env->NewStringUTF("auto_log");
        sAutoLog = env->CallBooleanMethod(gPrefBridgeObj, midGetBool, kLog, (jboolean)sAutoLog);
        env->DeleteLocalRef(kLog);

        jstring kMask = env->NewStringUTF("speed_mask");
        sSpeedMask = env->CallBooleanMethod(gPrefBridgeObj, midGetBool, kMask, (jboolean)sSpeedMask);
        env->DeleteLocalRef(kMask);
    }

    release_env(needDetach);
    LOGI("UserPrefs synchronized from Android SharedPreferences");
}

void UserPrefs::saveRotation(uint8_t rotation) {
    sRotation = rotation;
    jni_save_int("rotation", rotation);
}

void UserPrefs::saveUnits(bool us) {
    sUnits = us;
    jni_save_bool("units_us", us);
}

void UserPrefs::saveThemeMode(uint8_t mode) {
    sThemeMode = mode;
    jni_save_int("theme_mode", mode);
}

void UserPrefs::saveBrightness(uint8_t pct) {
    sBrightness = pct;
    jni_save_int("brightness", pct);
}

void UserPrefs::saveAutoLog(bool enabled) {
    sAutoLog = enabled;
    jni_save_bool("auto_log", enabled);
}

void UserPrefs::saveSpeedMask(bool enabled) {
    sSpeedMask = enabled;
    jni_save_bool("speed_mask", enabled);
}

void UserPrefs::saveBlePrefix(const char* prefix) {
    if (!prefix) return;
    strncpy(sBlePrefix, prefix, sizeof(sBlePrefix) - 1);
    sBlePrefix[sizeof(sBlePrefix) - 1] = '\0';
    jni_save_string("ble_prefix", prefix);
}

void UserPrefs::saveBleScanTimeout(uint16_t ms) {
    sBleScanTimeout = ms;
    jni_save_int("ble_scan_tmo", ms);
}

uint8_t  UserPrefs::getRotation()       { return sRotation; }
bool     UserPrefs::getUnits()          { return sUnits; }
uint8_t  UserPrefs::getThemeMode()      { return sThemeMode; }
uint8_t  UserPrefs::getBrightness()     { return sBrightness; }
bool     UserPrefs::getAutoLog()        { return sAutoLog; }
bool     UserPrefs::getSpeedMask()      { return sSpeedMask; }
uint16_t UserPrefs::getBleScanTimeout() { return sBleScanTimeout; }

void UserPrefs::getBlePrefix(char* buf, size_t len) {
    if (!buf || len == 0) return;
    strncpy(buf, sBlePrefix, len - 1);
    buf[len - 1] = '\0';
}

void UserPrefs::saveConfigured(bool configured) {
    sConfigured = configured;
    jni_save_bool("is_configured", configured);
}

bool UserPrefs::isConfigured() {
    return sConfigured;
}

void UserPrefs::saveTpmsEnabled(bool enabled) {
    sTpmsEnabled = enabled;
    jni_save_bool("tpms_enabled", enabled);
}

bool UserPrefs::getTpmsEnabled() {
    return sTpmsEnabled;
}

void UserPrefs::saveWheelDid(uint8_t wheel, const char* didHex) {
    if (wheel >= 4 || !didHex) return;
    strncpy(sWheelDid[wheel], didHex, sizeof(sWheelDid[wheel]) - 1);
    sWheelDid[wheel][sizeof(sWheelDid[wheel]) - 1] = '\0';
    char key[16];
    snprintf(key, sizeof(key), "wheel_did_%d", wheel);
    jni_save_string(key, didHex);
}

void UserPrefs::getWheelDid(uint8_t wheel, char* buf, size_t len) {
    if (wheel >= 4 || !buf || len == 0) return;
    strncpy(buf, sWheelDid[wheel], len - 1);
    buf[len - 1] = '\0';
}
