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

class BleElmScanCallbacks;   // defined in BleElm.cpp

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
    bool initReady() const { return initReady_; }
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

    // TEMP DIAGNOSTIC: every advertisement the radio receives during a scan is
    // mirrored into diagBuf_ so core-1 (UI) code can dump it after the window,
    // since BleElm's own core-0 serial prints are unreliable over USB CDC.
    const char* diagBuffer() const { return diagBuf_; }
    size_t diagCount() const { return diagCount_; }

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

    // Start an async scan, but ONLY if no scan is currently active. Returns
    // false when a scan is already running or the stack rejects the start.
    bool startScanInternal(uint32_t durationSec);

    // Invoked by the NimBLE host when an async discovery window completes
    // (either normally or via stop()). Static because NimBLEScan's completion
    // callback is a plain function pointer; routes through g_activeScanOwner.
    static void onScanCompleteStatic(NimBLEScanResults results);

    static void notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify);

    const char* namePrefix_ = "vLinker";
    BleElmScanCallbacks* scanCbs_ = nullptr;
    NimBLEAdvertisedDevice* targetDevice_ = nullptr;
    NimBLEClient* pClient_ = nullptr;
    NimBLERemoteCharacteristic* pWriteChar_ = nullptr;
    NimBLERemoteCharacteristic* pNotifyChar_ = nullptr;

    volatile bool scanning_ = false;
    bool connected_ = false;
    bool initialized_ = false;
    volatile bool initReady_ = false;      // set true when begin() completes (cross-core)

    uint32_t lastScanStartMs_ = 0;
    uint32_t lastScanEndMs_ = 0;        // waits retryDelayMs_ before next scan
    uint32_t lastConnectAttemptMs_ = 0;
    uint32_t retryDelayMs_ = 2000;

    char adapterVersion_[32] = "";   // from the ATZ banner

    static constexpr uint8_t MAX_DISCOVERED = 8;
    BleDeviceInfo discoveredDevices_[MAX_DISCOVERED] = {};
    uint8_t discoveredCount_ = 0;
    char pairedMac_[20] = "";
    char pairedName_[32] = "";

    char diagBuf_[2048] = "";
    size_t diagLen_ = 0;
    size_t diagCount_ = 0;

    static RingbufHandle_t ringBuf_;
};
