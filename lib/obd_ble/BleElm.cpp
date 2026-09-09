#include "BleElm.h"

#include <cstring>
#include <strings.h>

// ---------------------------------------------------------------------------
// OBD Bluetooth adapter name matcher.
// Real Vgate / vLinker / OBDLink / Veepeak adapters advertise under several spellings:
// "vLinker MC", "vLinker MS 08449", "V-LINKER", "Vgate", "OBDLink CX", "VEEPEAK", etc.
// ---------------------------------------------------------------------------
static bool containsIgnoreCase(const char* haystack, const char* needle) {
    if (!haystack || !needle || !*haystack || !*needle) return false;
    size_t hlen = strlen(haystack);
    size_t nlen = strlen(needle);
    if (nlen > hlen) return false;
    for (size_t i = 0; i <= hlen - nlen; i++) {
        if (strncasecmp(haystack + i, needle, nlen) == 0) return true;
    }
    return false;
}

static bool isVLinkerName(const char* name) {
    if (!name || !*name) return false;
    static const char* keywords[] = {
        "linker", "vgate", "obd", "elm", "icar", "veepeak",
        "bimmer", "viecar", "obdlink"
    };
    for (const char* kw : keywords) {
        if (containsIgnoreCase(name, kw)) return true;
    }
    return false;
}

RingbufHandle_t BleElm::ringBuf_ = nullptr;

// The one BleElm instance currently running an async scan; onScanCompleteStatic
// routes the NimBLE completion callback back to it.
static BleElm* g_activeScanOwner = nullptr;

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

        // TEMP DIAGNOSTIC: log every advertisement the radio actually sees so
        // we can tell "vLinker not advertising" apart from "filter dropped it".
        Serial.printf("[bleElm] RX adv: '%s' %s rssi=%d svc_cnt=%u\n",
                      name.empty() ? "(no name)" : name.c_str(), mac.c_str(),
                      rssi, (unsigned)advertisedDevice->getServiceUUIDCount());
        for (uint8_t k = 0; k < advertisedDevice->getServiceUUIDCount(); k++) {
            Serial.printf("[bleElm]   svc[%u] = %s\n", k,
                          advertisedDevice->getServiceUUID(k).toString().c_str());
        }
        {
            std::string manu = advertisedDevice->getManufacturerData();
            char manuHex[32];
            manuHex[0] = '\0';
            for (size_t k = 0; k < manu.size() && k < 12; k++) {
                size_t pos = strlen(manuHex);
                snprintf(manuHex + pos, sizeof(manuHex) - pos, "%02X", (uint8_t)manu[k]);
            }
            int need = snprintf(nullptr, 0, "%s|%s|%ddBm|manu=%s ", name.empty() ? "(unnamed)" : name.c_str(),
                                mac.c_str(), rssi, manuHex);
            if ((int)owner_->diagLen_ + need < (int)sizeof(owner_->diagBuf_) - 1) {
                owner_->diagLen_ += (size_t)snprintf(owner_->diagBuf_ + owner_->diagLen_,
                                                     sizeof(owner_->diagBuf_) - owner_->diagLen_,
                                                     "%s|%s|%ddBm|manu=%s ", name.empty() ? "(unnamed)" : name.c_str(),
                                                     mac.c_str(), rssi, manuHex);
            }
        }
        owner_->diagCount_++;

        // Surface OBD adapters (matching name keyword or OBD service UUIDs)
        bool isVLinker   = isVLinkerName(name.c_str());
        bool isObdUuid   = (advertisedDevice->isAdvertisingService(NimBLEUUID("FFF0")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("FFE0")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("18F0")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("E781")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("e7810a71-73ae-499d-8c15-faa9aef0c3f2")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("0000fff0-0000-1000-8000-00805f9b34fb")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("0000ffe0-0000-1000-8000-00805f9b34fb")) ||
                            advertisedDevice->isAdvertisingService(NimBLEUUID("000018f0-0000-1000-8000-00805f9b34fb")));
        bool isPairedMac = (strlen(owner_->pairedMac_) > 0 &&
                            strcasecmp(owner_->pairedMac_, mac.c_str()) == 0);
        if (!isVLinker && !isObdUuid && !isPairedMac) return;

        // Add or update in discovered list
        bool exists = false;
        for (uint8_t i = 0; i < owner_->discoveredCount_; i++) {
            if (strcasecmp(owner_->discoveredDevices_[i].mac, mac.c_str()) == 0) {
                owner_->discoveredDevices_[i].rssi = (int8_t)rssi;
                if (!name.empty()) {
                    strncpy(owner_->discoveredDevices_[i].name, name.c_str(), sizeof(owner_->discoveredDevices_[i].name) - 1);
                }
                exists = true;
                break;
            }
        }
        if (!exists && owner_->discoveredCount_ < BleElm::MAX_DISCOVERED) {
            BleDeviceInfo& d = owner_->discoveredDevices_[owner_->discoveredCount_++];
            strncpy(d.name, name.empty() ? "vLinker / OBD2" : name.c_str(), sizeof(d.name) - 1);
            strncpy(d.mac, mac.c_str(), sizeof(d.mac) - 1);
            d.rssi = (int8_t)rssi;
            d.isObdCandidate = true;
            d.isPaired = isPairedMac;
        }

        // Auto-connect ONLY to the explicitly paired adapter. Never auto-pair
        // a new device from a scan; pairing is explicit on the BLE config screen.
        if (!*outDevice_ && isPairedMac) {
            Serial.printf("[bleElm] -> MATCHED target OBD device: '%s' (%s)\n", name.c_str(), mac.c_str());
            *outDevice_ = new NimBLEAdvertisedDevice(*advertisedDevice);
            NimBLEDevice::getScan()->stop();
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

    // Persistent scan callback, created exactly once and reused across scans.
    if (!scanCbs_) {
        scanCbs_ = new BleElmScanCallbacks(this, &targetDevice_);
    }

    Serial.printf("[bleElm] begin step0 core=%d\n", xPortGetCoreID());
    NimBLEDevice::init("MX5-Dash");
    Serial.printf("[bleElm] begin step1 init-ok\n");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // Max TX power for car cabin range
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true); // bonding=true, mitm=false ("Just Works" for headless OBD dongles), sc=true
    NimBLEDevice::setMTU(512);
    Serial.printf("[bleElm] begin step2 config-ok\n");

    Serial.printf("[bleElm] initialized BLE subsystem (this=%p, paired MAC: '%s', prefix: '%s')\n",
                  this, pairedMac_, namePrefix_);
    initReady_ = true;
    return true;
}

void BleElm::startScan(uint32_t durationSec) {
    if (scanning_) return;              // never stack/restart an active scan
    discoveredCount_ = 0;               // fresh list for a user-initiated scan
    diagBuf_[0] = '\0';                 // TEMP DIAGNOSTIC reset
    diagLen_ = 0;
    diagCount_ = 0;
    startScanInternal(durationSec);
}

void BleElm::stopScan() {
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan->isScanning()) {
        pScan->stop();
    }
    scanning_ = false;
    lastScanEndMs_ = millis();
}

bool BleElm::startScanInternal(uint32_t durationSec) {
    if (scanning_) return false;
    if (!scanCbs_) return false;

    NimBLEScan* pScan = NimBLEDevice::getScan();
    pScan->setAdvertisedDeviceCallbacks(scanCbs_, true); // wantDuplicates = true (allows SCAN_RSP delivery without HCI controller filtering)
    pScan->setActiveScan(true);
    pScan->setInterval(100); // 100 ms interval
    pScan->setWindow(99);    // 99 ms window (continuous 100% duty cycle)

    scanning_ = true;
    lastScanStartMs_ = millis();
    g_activeScanOwner = this;

    // Async start. The completion callback only fires when the discovery window
    // really ends (duration elapsed or stop()), so loop() can pace the next scan
    // from actual completion instead of assumption. Restarting an active scan
    // returns BLE_HS_EALREADY (still true) but does NOT restart anything - the
    // old pacing used just this and let discovery silently die.
    if (!pScan->start(durationSec, &BleElm::onScanCompleteStatic, false)) {
        scanning_ = false;
        lastScanEndMs_ = millis();
        g_activeScanOwner = nullptr;
        return false;
    }
    return true;
}

void BleElm::onScanCompleteStatic(NimBLEScanResults results) {
    (void)results;
    if (g_activeScanOwner) {
        g_activeScanOwner->scanning_ = false;
        g_activeScanOwner->lastScanEndMs_ = millis();
        g_activeScanOwner = nullptr;
    }
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
    if (!initReady_) return;               // begin() not finished yet
    uint32_t now = millis();

    // TEMP DIAGNOSTIC: explain why auto-scan is not starting
    static uint32_t s_lastProbe = 0;
    if (now - s_lastProbe >= 10000) {
        s_lastProbe = now;
        Serial.printf("[bleElm] probe: conn=%d init=%d scan=%d target=%d now=%lu end=%lu retry=%lu dt=%lu\n",
                      connected_, initialized_, scanning_, (targetDevice_ != nullptr),
                      (unsigned long)now, (unsigned long)lastScanEndMs_, (unsigned long)retryDelayMs_,
                      (unsigned long)(now - lastScanEndMs_));
    }

    // 1) Handle disconnected state -> trigger scan or reconnect
    if (!connected_) {
        if (pClient_ && pClient_->isConnected()) {
            pClient_->disconnect();
        }
        initialized_ = false;

        // Safety net: if a scan was started but its completion callback never
        // ran (e.g. host stack hiccup), force it closed so pacing can continue.
        if (scanning_ && (now - lastScanStartMs_ > 6000)) {
            stopScan();
        }

        // Start a new scan only when none is active AND the quiet period since
        // the previous scan actually ENDED has elapsed. Scanning for 5s, then
        // idling retryDelayMs_, gives every advertiser plenty of windows.
        if (!targetDevice_) {
            if (!scanning_ && (now - lastScanEndMs_ >= retryDelayMs_)) {
                Serial.printf("[bleElm] starting BLE scan (paired MAC: '%s', prefix: '%s')...\n", pairedMac_, namePrefix_);
                if (!startScanInternal(5)) {
                    // Stack not synced yet etc.: back off briefly and retry.
                    lastScanEndMs_ = now - retryDelayMs_ + 500;
                }
            }
            return;
        }

        // Never try to connect while a discovery window is still running.
        if (scanning_) {
            stopScan();
        }

        // We have a target device: attempt connection
        if (now - lastConnectAttemptMs_ >= 1000) {
            lastConnectAttemptMs_ = now;
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
    // Request fast connection parameters (7.5ms min, 15ms max) for low latency
    pClient_->setConnectionParams(6, 12, 0, 100);
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
    Serial.printf("[bleElm] discovered %d services:\n", (int)services->size());
    for (auto* s : *services) {
        Serial.printf("[bleElm]   GATT svc: %s\n", s->getUUID().toString().c_str());
    }

    // Priority 1: Check known vLinker and OBD serial service UUIDs
    const char* knownServices[] = {
        "e7810a71-73ae-499d-8c15-faa9aef0c3f2",   // vLinker proprietary 128-bit
        "0000e781-0000-1000-8000-00805f9b34fb",   // vLinker 16-bit E781 expanded
        "E781",                                 // vLinker 16-bit E781
        "0000fff0-0000-1000-8000-00805f9b34fb",   // Standard FFF0 128-bit
        "FFF0",                                 // Standard FFF0 16-bit
        "0000ffe0-0000-1000-8000-00805f9b34fb",   // Standard FFE0 128-bit
        "FFE0",                                 // Standard FFE0 16-bit
        "000018f0-0000-1000-8000-00805f9b34fb",   // 18F0 128-bit
        "18F0",                                 // 18F0 16-bit
    };

    for (const char* uuidStr : knownServices) {
        NimBLERemoteService* s = pClient_->getService(uuidStr);
        if (s) {
            Serial.printf("[bleElm] matched known OBD service: %s\n", uuidStr);
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

    if (!pWriteChar_ || !pNotifyChar_) {
        Serial.println("[bleElm] ERROR: Connected device does not expose any recognized OBD GATT service UUIDs");
        return false;
    }

    Serial.printf("[bleElm] write char: %s, notify char: %s\n",
                  pWriteChar_->getUUID().toString().c_str(),
                  pNotifyChar_->getUUID().toString().c_str());

    bool useNotify = pNotifyChar_->canNotify();
    if (!pNotifyChar_->subscribe(useNotify, notifyCallback)) {
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
        "ATAT2\r",        // 5: Adaptive timing mode 2 (aggressive query cadence)
        "ATST32\r",       // 6: Adapter timeout 200ms (prevents long stalls)
        "ATSP6\r",        // 7: Protocol 6 = ISO 15765-4 CAN 11-bit/500k (Mazda SkyActiv standard)
        "ATSH 7E0\r",     // 8: Default to PCM CAN Header 7E0
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
    stopScan();   // never leave the controller mid-discovery while tearing down

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

    // Timeout recovery: abort in-flight command in ELM327 and recover '>' prompt
    sendCommand("\r");
    uint32_t resyncStart = millis();
    while (millis() - resyncStart < 300) {
        if (ringBuf_) {
            size_t itemSize = 0;
            char* item = (char*)xRingbufferReceiveUpTo(ringBuf_, &itemSize, pdMS_TO_TICKS(5), 64);
            if (item) {
                bool found = false;
                for (size_t i = 0; i < itemSize; i++) {
                    if (item[i] == '>') {
                        found = true;
                        break;
                    }
                }
                vRingbufferReturnItem(ringBuf_, (void*)item);
                if (found) break;
            }
        }
        delay(1);
    }
    flush(); // Purge any residual bytes so the next command starts completely clean
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
