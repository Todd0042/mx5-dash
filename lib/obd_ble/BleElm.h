#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <freertos/FreeRTOS.h>
#include <freertos/ringbuf.h>
#include "../mx5_config/ObdSource.h"

/**
 * BleElm
 *
 * Transport layer for an ELM327-compatible Bluetooth Low Energy (BLE) OBD2 adapter
 * such as the Vgate vLinker MS / MC+ / FD+ or standard BLE OBD dongles.
 *
 * Handles BLE scanning, auto-connecting to advertised device names, service/characteristic
 * discovery, notifications, and ELM327 ASCII request/response handling.
 */

class BleElm {
public:
    static constexpr size_t MAX_RESPONSE = 256;

    bool begin(const char* targetNamePrefix = "vLinker");

    // Call frequently from the OBD task: keeps BLE connection healthy, runs the init
    // handshake on connect, and handles reconnects with backoff.
    void loop();

    bool isInitialized() const { return initialized_; }
    bool isConnected() const { return connected_; }
    bool isScanning() const { return scanning_; }
    const char* adapterVersion() const { return adapterVersion_; }
    const char* namePrefix() const { return namePrefix_; }
    int8_t rssi() const { return (pClient_ && pClient_->isConnected()) ? pClient_->getRssi() : 0; }
    const char* connectedDeviceName() const { return targetDevice_ ? targetDevice_->getName().c_str() : ""; }

    // Runtime config (changes take effect on next reconnect)
    void setNamePrefix(const char* prefix) { namePrefix_ = prefix; }
    void setRetryDelay(uint32_t ms) { retryDelayMs_ = ms; }
    uint32_t retryDelay() const { return retryDelayMs_; }

    // Force a fresh scan cycle (tears down current target if any)
    void rescan() { teardown(); }

    // Disconnect and tear down the current connection
    void forceDisconnect() { teardown(); }

    // --- Multi-Device Discovery & Explicit Pairing -------------------------
    void startScan(uint32_t durationSec = 5);
    void stopScan();
    uint8_t getDiscoveredCount() const { return discoveredCount_; }
    bool getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const;
    void pairDevice(const char* mac, const char* name);
    void forgetPairedDevice();
    void getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const;

    // Send an ELM command (appends '\r'). Returns true if written.
    bool sendCommand(const char* cmd);

    // Read one response, i.e. everything up to the next '>' prompt.
    // out is NUL-terminated with CR/LF stripped. Returns true when a prompt
    // ends the response, false on timeout (partial data may still be in out).
    bool readResponse(char* out, size_t maxLen, uint32_t timeoutMs);

    // sendCommand + readResponse in one call. Returns true if a prompt arrived.
    bool sendQuery(const char* cmd, char* out, size_t maxLen, uint32_t timeoutMs);

    void flush();
    void teardown();

private:
    friend class BleElmScanCallbacks;

    bool connectToDevice();
    bool discoverGatt();
    bool initAdapter();
    bool waitForPrompt(uint32_t timeoutMs);

    static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

    const char* namePrefix_ = "vLinker";
    NimBLEAdvertisedDevice* targetDevice_ = nullptr;
    NimBLEClient* pClient_ = nullptr;
    NimBLERemoteCharacteristic* pWriteChar_ = nullptr;
    NimBLERemoteCharacteristic* pNotifyChar_ = nullptr;

    bool scanning_ = false;
    bool connected_ = false;
    bool initialized_ = false;

    uint32_t lastScanMs_ = 0;
    uint32_t retryDelayMs_ = 2000;

    char adapterVersion_[32] = "";   // from the ATZ banner

    static constexpr uint8_t MAX_DISCOVERED = 8;
    BleDeviceInfo discoveredDevices_[MAX_DISCOVERED] = {};
    uint8_t discoveredCount_ = 0;
    char pairedMac_[20] = "";
    char pairedName_[32] = "";

    static RingbufHandle_t ringBuf_;
};
