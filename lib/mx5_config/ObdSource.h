#pragma once

#include <stddef.h>
#include "VehicleData.h"

struct BleDeviceInfo {
    char name[32];
    char mac[20];
    int8_t rssi;
    bool isObdCandidate;
    bool isPaired;
};

/**
 * ObdSource - abstract telemetry source consumed by the UI layer.
 *
 * Decouples Mx5UI from the ESP32 implementation (BLE ELM327 + FreeRTOS task)
 * so the exact same UI code compiles for:
 *   - the device      -> ObdService (lib/obd_ble)
 *   - desktop preview -> the animated mock in sim/main.cpp
 */
class ObdSource {
public:
    virtual ~ObdSource() = default;

    // Blocking copy of the latest telemetry (critical-section protected on the
    // device; the preview mock just fills its animated values).
    virtual void snapshot(VehicleData& out) = 0;

    // True when the OBD link is up (BLE + ELM handshake complete).
    virtual bool connected() const = 0;

    // View-driven dynamic polling hint from UI layer
    virtual void setActiveScreen(uint8_t screenIndex) { (void)screenIndex; }

    // --- BLE config & Device Discovery / Pairing ----------------------------

    virtual void setBlePrefix(const char* prefix) { (void)prefix; }
    virtual void setBleScanTimeout(uint16_t ms)   { (void)ms; }
    virtual void rescan()                         {}
    virtual void disconnect()                     {}

    virtual const char* getBlePrefix() const { return "vLinker"; }
    virtual uint16_t    getBleScanTimeout() const { return 5000; }
    virtual bool        isScanning()  const { return false; }
    virtual const char* getConnectedDeviceName() const { return ""; }
    virtual int8_t      getRssi() const { return 0; }

    // Multi-device discovery & explicit pairing interface
    virtual void startBleScan() {}
    virtual void stopBleScan() {}
    virtual uint8_t getDiscoveredDeviceCount() const { return 0; }
    virtual bool getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const { (void)index; (void)out; return false; }
    virtual void pairDevice(const char* mac, const char* name) { (void)mac; (void)name; }
    virtual void forgetPairedDevice() {}
    virtual void getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const {
        if (macBuf && macLen > 0) macBuf[0] = '\0';
        if (nameBuf && nameLen > 0) nameBuf[0] = '\0';
    }

    // --- Setup wizard / TPMS calibration --------------------------------------
    virtual void freeze(bool frozen) { (void)frozen; }
    virtual void startCalibration(uint8_t activeWheel)        { (void)activeWheel; }
    virtual void stopCalibration()                            {}
    virtual void calibrationMarkBound(uint8_t wheel)          { (void)wheel; }
    virtual bool didCaptured(char* didBuf, size_t len) const  { (void)didBuf; (void)len; return false; }
    virtual bool calibrationActive() const                    { return false; }
};