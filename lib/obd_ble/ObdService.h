#pragma once

#include "../mx5_config/ObdSource.h"

/**
 * ObdService - real vehicle telemetry service for the ESP32.
 *
 * Owns the BLE ELM327 transport and polls Mode 01 PIDs round-robin at
 * staggered cadences from a FreeRTOS task pinned to core 0. VehicleData is
 * read/written under a critical section so the UI task (core 1) can snapshot
 * it without blocking.
 *
 * This header is deliberately host-safe: all BLE/FreeRTOS internals live in
 * the pimpl (struct Impl) defined in ObdService.cpp, so both the device code
 * and the desktop preview can include it.
 *
 *   PID        Query   Value           Cadence
 *   rpm        010C    16-bit /4       100 ms
 *   speed      010D    1 byte          300 ms
 *   coolant    0105    byte - 40       300 ms
 *   load       0104    byte %          1200 ms
 *   throttle   0111    byte %          1200 ms
 *   fuel       012F    byte %          1200 ms
 *   intake air 010F    byte - 40       1200 ms
 *   battery    0142    byte * 0.1 V    1200 ms
 *   ambient    0146    byte - 40       1200 ms
 *   oil temp   221310  (best effort)   3000 ms
 *   TPMS       222Axx  manufacturer DIDs, MS-CAN, see Config.h
 */
class ObdService : public ObdSource {
public:
    // start() begins BLE scanning and spawns the OBD task. Call once in setup().
    void start();

    // Blocking copy of the latest telemetry (critical-section protected).
    void snapshot(VehicleData& out) override;

    bool connected() const override { return connected_; }

    // View-driven dynamic polling hint from UI layer
    void setActiveScreen(uint8_t screenIndex) override;

    // --- BLE config forwarding to BleElm ------------------------------------
    void setBlePrefix(const char* prefix) override;
    void setBleScanTimeout(uint16_t ms) override;
    void rescan() override;
    void disconnect() override;

    const char* getBlePrefix() const override;
    uint16_t    getBleScanTimeout() const override;
    bool        isScanning() const override;
    const char* getConnectedDeviceName() const override;
    int8_t      getRssi() const override;

    void startBleScan() override;
    void stopBleScan() override;
    uint8_t getDiscoveredDeviceCount() const override;
    bool getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const override;
    void pairDevice(const char* mac, const char* name) override;
    void forgetPairedDevice() override;
    void getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const override;

    // --- Setup wizard / TPMS calibration --------------------------------------
    void freeze(bool frozen) override;
    void startCalibration(uint8_t activeWheel) override;
    void stopCalibration() override;
    void calibrationMarkBound(uint8_t wheel) override;
    bool didCaptured(char* didBuf, size_t len) const override;
    bool calibrationActive() const override;

private:
    static void taskMain(void* arg);  // FreeRTOS entry, runs on core 0
    void loopTask();                  // one service tick per ~5 ms

    struct Impl;                      // BLE/FreeRTOS internals - see ObdService.cpp

    // Raw ELM327 query helpers (operate on the pimpl; free of ESP types on
    // the public surface so the header is safe for the desktop preview).
    static bool readUint8(Impl& i, const char* cmd, const char* pidHex, uint8_t& out);
    static bool readUint16(Impl& i, const char* cmd, const char* pidHex, uint16_t& out);
    static void pollTick(Impl& i, uint32_t now);   // rotates through PID cadences
    static void pollTpms(Impl& i, uint32_t now);   // Mode 22 (see Config.h)
    static void pollCalibration(Impl& i, uint32_t now); // raw DID capture

    Impl* impl_ = nullptr;

    volatile bool connected_ = false;
};
