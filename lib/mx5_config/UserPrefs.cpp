#include "UserPrefs.h"
#include "Config.h"

#include <cstring>
#include <cstdio>

#ifdef ARDUINO
#include <Preferences.h>
static Preferences prefs;
static const char* NS = "sys_prefs";
#endif

// Cached values (loaded once at boot, written on every save)
static uint8_t  sRotation   = MX5_LCD_ROTATION;
static bool     sUnits      = MX5_UNITS_US;
static uint8_t  sThemeMode  = 0;
static uint8_t  sBrightness = 95;
static bool     sAutoLog    = true;
static bool     sSpeedMask  = true;
static bool     sTransAuto  = true; // Default to Automatic (6AT)
static char     sBlePrefix[32] = MX5_BLE_DEVICE_PREFIX;
static uint16_t sBleScanTimeout = 5000;
static char     sPairedMac[20] = "64:8C:BB:1A:08:0A"; // default paired scanner MAC
static char     sPairedName[32] = "vLinker MS 08449";

// Setup / calibration state
static bool sConfigured   = false;             // first boot => wizard
static bool sTpmsEnabled  = (MX5_TPMS_ENABLED != 0);
static char sWheelDid[4][8] = {"", "", "", ""}; // FL,FR,RL,RR -> DID hex string

void UserPrefs::loadAll() {
#ifdef ARDUINO
    prefs.begin(NS, true);  // read-only
    sRotation   = prefs.getUChar("rotation",   MX5_LCD_ROTATION);
    sUnits      = prefs.getBool("units_us",    MX5_UNITS_US);
    sThemeMode  = prefs.getUChar("theme_mode", 0);
    sBrightness = prefs.getUChar("brightness", 95);
    sAutoLog    = prefs.getBool("auto_log",    true);
    sSpeedMask  = prefs.getBool("speed_mask",  true);
    sTransAuto  = prefs.getBool("trans_auto",  true);

    // getString with 3-arg form; apply default if key missing/empty
    size_t n = prefs.getString("ble_prefix", sBlePrefix, sizeof(sBlePrefix));
    if (n == 0 || sBlePrefix[0] == '\0') {
        strncpy(sBlePrefix, MX5_BLE_DEVICE_PREFIX, sizeof(sBlePrefix) - 1);
    }
    sBleScanTimeout = prefs.getUShort("ble_scan_tmo", 5000);

    prefs.getString("paired_mac", sPairedMac, sizeof(sPairedMac));
    prefs.getString("paired_name", sPairedName, sizeof(sPairedName));

    sConfigured  = prefs.getBool("is_configured", false);
    sTpmsEnabled = prefs.getBool("tpms_enabled", (MX5_TPMS_ENABLED != 0));
    for (int w = 0; w < 4; w++) {
        char key[8];
        snprintf(key, sizeof(key), "did_%d", w);
        prefs.getString(key, sWheelDid[w], sizeof(sWheelDid[w]));
    }
    prefs.end();
    Serial.printf("[prefs] loaded: rot=%d units=%d theme=%d bri=%d ble='%s' mac='%s' configured=%d\n",
                  sRotation, sUnits, sThemeMode, sBrightness, sBlePrefix, sPairedMac, sConfigured);
#endif
}

// ---------------------------------------------------------------------------
// Save helpers - write to NVS + update cached value
// ---------------------------------------------------------------------------

void UserPrefs::saveRotation(uint8_t rotation) {
    sRotation = rotation;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putUChar("rotation", rotation);
    prefs.end();
#endif
}

void UserPrefs::saveUnits(bool us) {
    sUnits = us;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("units_us", us);
    prefs.end();
#endif
}

void UserPrefs::saveThemeMode(uint8_t mode) {
    sThemeMode = mode;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putUChar("theme_mode", mode);
    prefs.end();
#endif
}

void UserPrefs::saveBrightness(uint8_t pct) {
    sBrightness = pct;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putUChar("brightness", pct);
    prefs.end();
#endif
}

void UserPrefs::saveAutoLog(bool enabled) {
    sAutoLog = enabled;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("auto_log", enabled);
    prefs.end();
#endif
}

void UserPrefs::saveSpeedMask(bool enabled) {
    sSpeedMask = enabled;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("speed_mask", enabled);
    prefs.end();
#endif
}

void UserPrefs::saveTransAuto(bool isAuto) {
    sTransAuto = isAuto;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("trans_auto", isAuto);
    prefs.end();
#endif
}

void UserPrefs::saveBlePrefix(const char* prefix) {
    strncpy(sBlePrefix, prefix, sizeof(sBlePrefix) - 1);
    sBlePrefix[sizeof(sBlePrefix) - 1] = '\0';
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putString("ble_prefix", prefix);
    prefs.end();
#endif
}

void UserPrefs::saveBleScanTimeout(uint16_t ms) {
    sBleScanTimeout = ms;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putUShort("ble_scan_tmo", ms);
    prefs.end();
#endif
}

void UserPrefs::savePairedMac(const char* mac) {
    strncpy(sPairedMac, mac, sizeof(sPairedMac) - 1);
    sPairedMac[sizeof(sPairedMac) - 1] = '\0';
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putString("paired_mac", mac);
    prefs.end();
#endif
}

void UserPrefs::savePairedName(const char* name) {
    strncpy(sPairedName, name, sizeof(sPairedName) - 1);
    sPairedName[sizeof(sPairedName) - 1] = '\0';
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putString("paired_name", name);
    prefs.end();
#endif
}

void UserPrefs::clearPairedDevice() {
    sPairedMac[0] = '\0';
    sPairedName[0] = '\0';
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.remove("paired_mac");
    prefs.remove("paired_name");
    prefs.end();
#endif
}

// ---------------------------------------------------------------------------
// Getters (read from cached values, no NVS access per call)
// ---------------------------------------------------------------------------

uint8_t  UserPrefs::getRotation()       { return sRotation; }
bool     UserPrefs::getUnits()          { return sUnits; }
uint8_t  UserPrefs::getThemeMode()      { return sThemeMode; }
uint8_t  UserPrefs::getBrightness()     { return sBrightness; }
bool     UserPrefs::getAutoLog()        { return sAutoLog; }
bool     UserPrefs::getSpeedMask()      { return sSpeedMask; }
bool     UserPrefs::getTransAuto()      { return sTransAuto; }
uint16_t UserPrefs::getBleScanTimeout() { return sBleScanTimeout; }

void UserPrefs::getBlePrefix(char* buf, size_t len) {
    strncpy(buf, sBlePrefix, len - 1);
    buf[len - 1] = '\0';
}

void UserPrefs::getPairedMac(char* buf, size_t len) {
    if (len == 0 || !buf) return;
    strncpy(buf, sPairedMac, len - 1);
    buf[len - 1] = '\0';
}

void UserPrefs::getPairedName(char* buf, size_t len) {
    if (len == 0 || !buf) return;
    strncpy(buf, sPairedName, len - 1);
    buf[len - 1] = '\0';
}

bool UserPrefs::hasPairedDevice() {
    return (sPairedMac[0] != '\0');
}

// ---------------------------------------------------------------------------
// Setup wizard / calibration state
// ---------------------------------------------------------------------------

void UserPrefs::saveConfigured(bool configured) {
    sConfigured = configured;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("is_configured", configured);
    prefs.end();
#endif
}

bool UserPrefs::isConfigured() {
    return sConfigured;
}

void UserPrefs::saveTpmsEnabled(bool enabled) {
    sTpmsEnabled = enabled;
#ifdef ARDUINO
    prefs.begin(NS, false);
    prefs.putBool("tpms_enabled", enabled);
    prefs.end();
#endif
}

bool UserPrefs::getTpmsEnabled() {
    return sTpmsEnabled;
}

void UserPrefs::saveWheelDid(uint8_t wheel, const char* didHex) {
    if (wheel >= 4) return;
    strncpy(sWheelDid[wheel], didHex, sizeof(sWheelDid[wheel]) - 1);
    sWheelDid[wheel][sizeof(sWheelDid[wheel]) - 1] = '\0';
#ifdef ARDUINO
    prefs.begin(NS, false);
    char key[8];
    snprintf(key, sizeof(key), "did_%d", wheel);
    prefs.putString(key, didHex);
    prefs.end();
#endif
}

void UserPrefs::getWheelDid(uint8_t wheel, char* buf, size_t len) {
    if (wheel >= 4 || len == 0) { if (len) buf[0] = '\0'; return; }
    strncpy(buf, sWheelDid[wheel], len - 1);
    buf[len - 1] = '\0';
}
