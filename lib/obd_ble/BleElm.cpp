#include "BleElm.h"

#include <cstring>
#include <strings.h>

RingbufHandle_t BleElm::ringBuf_ = nullptr;

// ---------------------------------------------------------------------------
// Callbacks
// ---------------------------------------------------------------------------

class BleElmClientCallbacks : public NimBLEClientCallbacks {
public:
    explicit BleElmClientCallbacks(bool* pConnected, bool* pInitialized)
        : connected_(pConnected), initialized_(pInitialized) {}

    void onConnect(NimBLEClient* pClient) override {
        Serial.println("[bleElm] BLE physical link connected");
    }

    void onDisconnect(NimBLEClient* pClient) override {
        Serial.println("[bleElm] BLE link disconnected");
        if (connected_) *connected_ = false;
        if (initialized_) *initialized_ = false;
    }

private:
    bool* connected_;
    bool* initialized_;
};

#include "../mx5_config/UserPrefs.h"

class BleElmScanCallbacks : public NimBLEAdvertisedDeviceCallbacks {
public:
    BleElmScanCallbacks(BleElm* owner, NimBLEAdvertisedDevice** outDevice)
        : owner_(owner), outDevice_(outDevice) {}

    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        if (!advertisedDevice || !owner_) return;

        std::string name = advertisedDevice->getName();
        std::string mac = advertisedDevice->getAddress().toString();
        int rssi = advertisedDevice->getRSSI();

        if (!name.empty() || !mac.empty()) {
            bool isObd = (strcasestr(name.c_str(), "vLinker") != nullptr ||
                          strcasestr(name.c_str(), "OBD") != nullptr ||
                          strcasestr(name.c_str(), "ELM") != nullptr ||
                          strcasestr(name.c_str(), "Link") != nullptr ||
                          strcasestr(name.c_str(), "Veepeak") != nullptr ||
                          strcasestr(name.c_str(), "iCar") != nullptr ||
                          strcasestr(name.c_str(), "Scan") != nullptr ||
                          advertisedDevice->isAdvertisingService(NimBLEUUID("FFF0")) ||
                          advertisedDevice->isAdvertisingService(NimBLEUUID("FFE0")));

            // Add or update in discovered list
            bool exists = false;
            for (uint8_t i = 0; i < owner_->discoveredCount_; i++) {
                if (strcasecmp(owner_->discoveredDevices_[i].mac, mac.c_str()) == 0) {
                    owner_->discoveredDevices_[i].rssi = (int8_t)rssi;
                    if (!name.empty()) strncpy(owner_->discoveredDevices_[i].name, name.c_str(), sizeof(owner_->discoveredDevices_[i].name) - 1);
                    exists = true;
                    break;
                }
            }
            if (!exists && owner_->discoveredCount_ < BleElm::MAX_DISCOVERED) {
                BleDeviceInfo& d = owner_->discoveredDevices_[owner_->discoveredCount_++];
                strncpy(d.name, name.empty() ? "OBD Scanner" : name.c_str(), sizeof(d.name) - 1);
                strncpy(d.mac, mac.c_str(), sizeof(d.mac) - 1);
                d.rssi = (int8_t)rssi;
                d.isObdCandidate = isObd;
                d.isPaired = (strlen(owner_->pairedMac_) > 0 && strcasecmp(owner_->pairedMac_, mac.c_str()) == 0);
            }

            // Auto-match if we have a paired MAC or paired name prefix
            if (!*outDevice_) {
                if ((strlen(owner_->pairedMac_) > 0 && strcasecmp(owner_->pairedMac_, mac.c_str()) == 0) ||
                    (strlen(owner_->pairedMac_) == 0 && (strcasestr(name.c_str(), owner_->namePrefix_) != nullptr || isObd))) {
                    Serial.printf("[bleElm] -> MATCHED target OBD device: '%s' (%s)\n", name.c_str(), mac.c_str());
                    *outDevice_ = new NimBLEAdvertisedDevice(*advertisedDevice);
                    NimBLEDevice::getScan()->stop();
                }
            }
        }
    }

private:
    BleElm* owner_;
    NimBLEAdvertisedDevice** outDevice_;
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool BleElm::begin(const char* targetNamePrefix) {
    namePrefix_ = targetNamePrefix ? targetNamePrefix : "vLinker";

    // Load paired device from NVS preferences
    UserPrefs::getPairedMac(pairedMac_, sizeof(pairedMac_));
    UserPrefs::getPairedName(pairedName_, sizeof(pairedName_));

    if (!ringBuf_) {
        ringBuf_ = xRingbufferCreate(1024, RINGBUF_TYPE_BYTEBUF);
    }

    NimBLEDevice::init("MX5-Dash");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // Max TX power for car cabin range
    NimBLEDevice::setSecurityAuth(true, true, true);

    Serial.printf("[bleElm] initialized BLE subsystem (paired MAC: '%s', prefix: '%s')\n",
                  pairedMac_, namePrefix_);
    return true;
}

void BleElm::startScan(uint32_t durationSec) {
    discoveredCount_ = 0;
    scanning_ = true;
    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(new BleElmScanCallbacks(this, &targetDevice_), true);
    pScan->setActiveScan(true);
    pScan->setInterval(97);
    pScan->setWindow(67);
    pScan->start(durationSec, false);
    scanning_ = false;
}

void BleElm::stopScan() {
    NimBLEDevice::getScan()->stop();
    scanning_ = false;
}

bool BleElm::getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const {
    if (index >= discoveredCount_) return false;
    out = discoveredDevices_[index];
    return true;
}

void BleElm::pairDevice(const char* mac, const char* name) {
    if (!mac) return;
    strncpy(pairedMac_, mac, sizeof(pairedMac_) - 1);
    pairedMac_[sizeof(pairedMac_) - 1] = '\0';
    if (name) {
        strncpy(pairedName_, name, sizeof(pairedName_) - 1);
        pairedName_[sizeof(pairedName_) - 1] = '\0';
    }
    UserPrefs::savePairedMac(pairedMac_);
    UserPrefs::savePairedName(pairedName_);

    teardown();
    NimBLEScanResults results = NimBLEDevice::getScan()->getResults();
    for (int i = 0; i < results.getCount(); i++) {
        NimBLEAdvertisedDevice dev = results.getDevice(i);
        if (strcasecmp(dev.getAddress().toString().c_str(), mac) == 0) {
            targetDevice_ = new NimBLEAdvertisedDevice(dev);
            break;
        }
    }
}

void BleElm::forgetPairedDevice() {
    pairedMac_[0] = '\0';
    pairedName_[0] = '\0';
    UserPrefs::clearPairedDevice();
    teardown();
    startScan(5);
}

void BleElm::getPairedDevice(char* macBuf, size_t macLen, char* nameBuf, size_t nameLen) const {
    if (macBuf && macLen > 0) {
        strncpy(macBuf, pairedMac_, macLen - 1);
        macBuf[macLen - 1] = '\0';
    }
    if (nameBuf && nameLen > 0) {
        strncpy(nameBuf, pairedName_, nameLen - 1);
        nameBuf[nameLen - 1] = '\0';
    }
}

void BleElm::loop() {
    uint32_t now = millis();

    // 1) Handle disconnected state -> trigger scan or reconnect
    if (!connected_) {
        if (pClient_ && pClient_->isConnected()) {
            pClient_->disconnect();
        }
        initialized_ = false;

        // Start scanning if we don't have a target device yet
        if (!targetDevice_) {
            if (!scanning_ && (now - lastScanMs_ >= retryDelayMs_)) {
                lastScanMs_ = now;
                scanning_ = true;
                Serial.printf("[bleElm] starting BLE scan (paired MAC: '%s', prefix: '%s')...\n", pairedMac_, namePrefix_);

                NimBLEScan* pScan = NimBLEDevice::getScan();
                pScan->setAdvertisedDeviceCallbacks(new BleElmScanCallbacks(this, &targetDevice_), true);
                pScan->setActiveScan(true);
                pScan->setInterval(97);
                pScan->setWindow(67);
                pScan->start(5, false);   // scan for 5 seconds
                scanning_ = false;
            }
            return;
        }

        // We have a target device: attempt connection
        if (now - lastScanMs_ >= 1000) {
            lastScanMs_ = now;
            if (connectToDevice()) {
                if (discoverGatt() && initAdapter()) {
                    connected_ = true;
                    initialized_ = true;
                    retryDelayMs_ = 2000;
                    Serial.println("[bleElm] adapter fully ready for live telemetry");
                    return;
                }
                Serial.println("[bleElm] GATT/ELM init failed, retrying...");
                teardown();
            } else {
                Serial.println("[bleElm] connection failed, clearing target and retrying scan");
                teardown();
                retryDelayMs_ = constrain(retryDelayMs_ * 2, 2000, 15000);
            }
        }
        return;
    }

    // 2) Monitor active connection
    if (pClient_ && !pClient_->isConnected()) {
        Serial.println("[bleElm] connection lost in loop, tearing down");
        teardown();
    }
}

bool BleElm::connectToDevice() {
    if (!targetDevice_) return false;

    Serial.printf("[bleElm] connecting to %s...\n", targetDevice_->getAddress().toString().c_str());

    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(new BleElmClientCallbacks(&connected_, &initialized_), true);
        pClient_->setConnectTimeout(6);
    }

    if (!pClient_->connect(targetDevice_)) {
        Serial.println("[bleElm] connect() failed");
        return false;
    }

    Serial.println("[bleElm] physical connect OK");
    delay(50);
    return true;
}

bool BleElm::discoverGatt() {
    if (!pClient_ || !pClient_->isConnected()) return false;

    pWriteChar_ = nullptr;
    pNotifyChar_ = nullptr;

    auto* services = pClient_->getServices(true);
    if (!services) {
        Serial.println("[bleElm] no services found");
        return false;
    }
    Serial.printf("[bleElm] discovered %d services\n", (int)services->size());

    // Priority 1: Check known vLinker and OBD serial service UUIDs
    const char* knownServices[] = {
        "e7810a71-73ae-499d-8c15-faa9aef0c3f2",   // vLinker proprietary
        "0000fff0-0000-1000-8000-00805f9b34fb",   // Standard FFF0
        "0000ffe0-0000-1000-8000-00805f9b34fb",   // Standard FFE0
        "000018f0-0000-1000-8000-00805f9b34fb",   // 18F0
    };

    for (const char* uuidStr : knownServices) {
        NimBLERemoteService* s = pClient_->getService(uuidStr);
        if (s) {
            Serial.printf("[bleElm] matched known service: %s\n", uuidStr);
            auto* chars = s->getCharacteristics(true);
            if (chars) {
                for (auto* c : *chars) {
                    if (!pWriteChar_ && (c->canWrite() || c->canWriteNoResponse())) pWriteChar_ = c;
                    if (!pNotifyChar_ && (c->canNotify() || c->canIndicate())) pNotifyChar_ = c;
                }
            }
            if (pWriteChar_ && pNotifyChar_) break;
        }
    }

    // Priority 2: Generic fallback - find any service with Write + Notify
    if (!pWriteChar_ || !pNotifyChar_) {
        for (auto* s : *services) {
            auto* chars = s->getCharacteristics(true);
            if (chars) {
                for (auto* c : *chars) {
                    if (!pWriteChar_ && (c->canWrite() || c->canWriteNoResponse())) pWriteChar_ = c;
                    if (!pNotifyChar_ && (c->canNotify() || c->canIndicate())) pNotifyChar_ = c;
                }
            }
            if (pWriteChar_ && pNotifyChar_) {
                Serial.printf("[bleElm] found generic write/notify chars in service: %s\n",
                              s->getUUID().toString().c_str());
                break;
            }
        }
    }

    if (!pWriteChar_ || !pNotifyChar_) {
        Serial.println("[bleElm] ERROR: could not find valid Write and Notify characteristics");
        return false;
    }

    Serial.printf("[bleElm] write char: %s, notify char: %s\n",
                  pWriteChar_->getUUID().toString().c_str(),
                  pNotifyChar_->getUUID().toString().c_str());

    if (!pNotifyChar_->subscribe(true, notifyCallback)) {
        Serial.println("[bleElm] ERROR: subscribe failed");
        return false;
    }

    return true;
}

void BleElm::notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (!ringBuf_ || !pData || length == 0) return;
    xRingbufferSend(ringBuf_, pData, length, 0);
}

bool BleElm::initAdapter() {
    char resp[MAX_RESPONSE];

    // Drain initial banner / prompt bytes
    waitForPrompt(1000);
    flush();

    // ELM327 initialization commands. ATZ must run FIRST!
    const char* cmds[] = {
        "ATZ\r",          // 0: Reset -> returns adapter banner
        "ATE0\r",         // 1: Echo off
        "ATL0\r",         // 2: Linefeeds off
        "ATS0\r",         // 3: Spaces off
        "ATH0\r",         // 4: Headers off
        "ATSP0\r",        // 5: Automatic protocol selection
    };

    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        if (!sendCommand(cmds[i])) {
            Serial.printf("[bleElm] failed writing '%s'\n", cmds[i]);
            return false;
        }
        if (!readResponse(resp, sizeof(resp), 2000)) {
            Serial.printf("[bleElm] timeout waiting response for '%s'\n", cmds[i]);
            return false;
        }

        if (i == 0) {
            // Store the adapter version banner from ATZ
            strncpy(adapterVersion_, resp, sizeof(adapterVersion_) - 1);
            adapterVersion_[sizeof(adapterVersion_) - 1] = '\0';
            Serial.printf("[bleElm] adapter banner: '%s'\n", adapterVersion_);
        }
    }

    flush();
    return true;
}

void BleElm::teardown() {
    if (pNotifyChar_) {
        pNotifyChar_->unsubscribe();
        pNotifyChar_ = nullptr;
    }
    pWriteChar_ = nullptr;

    if (pClient_) {
        if (pClient_->isConnected()) {
            pClient_->disconnect();
        }
        NimBLEDevice::deleteClient(pClient_);
        pClient_ = nullptr;
    }

    if (targetDevice_) {
        delete targetDevice_;
        targetDevice_ = nullptr;
    }

    connected_ = false;
    initialized_ = false;
    adapterVersion_[0] = '\0';
    flush();
}

void BleElm::flush() {
    if (!ringBuf_) return;
    size_t size = 0;
    void* item = nullptr;
    while ((item = xRingbufferReceiveUpTo(ringBuf_, &size, 0, 1024)) != nullptr) {
        vRingbufferReturnItem(ringBuf_, item);
    }
}

bool BleElm::sendCommand(const char* cmd) {
    if (!pWriteChar_ || !pClient_ || !pClient_->isConnected()) return false;

    size_t len = strlen(cmd);
    char buf[128];
    if (len >= sizeof(buf) - 2) return false;

    memcpy(buf, cmd, len);
    if (len == 0 || buf[len - 1] != '\r') {
        buf[len++] = '\r';
    }
    buf[len] = '\0';

    // Write without response if supported for lower latency, else with response
    bool noResponse = pWriteChar_->canWriteNoResponse();
    return pWriteChar_->writeValue((const uint8_t*)buf, len, !noResponse);
}

bool BleElm::readResponse(char* out, size_t maxLen, uint32_t timeoutMs) {
    if (!out || maxLen == 0) return false;

    size_t n = 0;
    char prev = 0;
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        if (ringBuf_) {
            size_t itemSize = 0;
            char* item = (char*)xRingbufferReceiveUpTo(ringBuf_, &itemSize, pdMS_TO_TICKS(5),
                                                        maxLen > (n + 1) ? (maxLen - n - 1) : 1);
            if (item) {
                for (size_t i = 0; i < itemSize; i++) {
                    char c = item[i];

                    // '>' prompt indicates end of ELM327 response
                    if (c == '>' && (prev == '\r' || prev == '\n' || n == 0)) {
                        while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n')) {
                            n--;
                        }
                        out[n] = '\0';
                        vRingbufferReturnItem(ringBuf_, (void*)item);
                        return true;
                    }

                    // Strip CR/LF from payload
                    if (c != '\r' && c != '\n') {
                        if (n + 1 < maxLen) {
                            out[n++] = c;
                        }
                    }
                    prev = c;
                }
                vRingbufferReturnItem(ringBuf_, (void*)item);
            }
        }
        delay(1);
    }

    if (n > 0) {
        out[n] = '\0';
        return false;   // timed out before prompt, but returned partial
    }
    out[0] = '\0';
    return false;
}

bool BleElm::waitForPrompt(uint32_t timeoutMs) {
    char sink[MAX_RESPONSE];
    return readResponse(sink, sizeof(sink), timeoutMs);
}

bool BleElm::sendQuery(const char* cmd, char* out, size_t maxLen, uint32_t timeoutMs) {
    if (!sendCommand(cmd)) return false;
    return readResponse(out, maxLen, timeoutMs);
}
