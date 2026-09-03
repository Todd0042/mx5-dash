#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * UserPrefs - NVS (non-volatile storage) persistence for all user settings.
 *
 * Uses ESP32 Preferences library to store settings that survive reboots.
 * On native/sim builds, all functions are no-ops (settings default each run).
 *
 * Namespace: "sys_prefs"
 */

class UserPrefs {
public:
    static void loadAll();

    static void saveRotation(uint8_t rotation);
    static void saveUnits(bool us);
    static void saveThemeMode(uint8_t mode);
    static void saveBrightness(uint8_t pct);
    static void saveAutoLog(bool enabled);
    static void saveSpeedMask(bool enabled);
    static void saveBlePrefix(const char* prefix);
    static void saveBleScanTimeout(uint16_t ms);
    static void savePairedMac(const char* mac);
    static void savePairedName(const char* name);
    static void clearPairedDevice();

    static uint8_t  getRotation();
    static bool     getUnits();
    static uint8_t  getThemeMode();
    static uint8_t  getBrightness();
    static bool     getAutoLog();
    static bool     getSpeedMask();
    static void     getBlePrefix(char* buf, size_t len);
    static uint16_t getBleScanTimeout();
    static void     getPairedMac(char* buf, size_t len);
    static void     getPairedName(char* buf, size_t len);
    static bool     hasPairedDevice();

    // --- Setup wizard / calibration state -----------------------------------
    // is_configured: false (default/missing) on first boot => run 4-step wizard.
    static void saveConfigured(bool configured);
    static bool isConfigured();

    // Active TPMS checks on/off (mirrors MX5_TPMS_ENABLED at runtime).
    static void saveTpmsEnabled(bool enabled);
    static bool getTpmsEnabled();

    // Persistent wheel-mapping matrix: 0=FL 1=FR 2=RL 3=RR -> the raw Mode 22
    // DID hex string ("2A05" ...) that was bound to that physical tire during
    // calibration.
    static void saveWheelDid(uint8_t wheel, const char* didHex);
    static void getWheelDid(uint8_t wheel, char* buf, size_t len);
};
