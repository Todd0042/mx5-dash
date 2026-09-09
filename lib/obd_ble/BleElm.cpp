#include "BleElm.h"

#include <cstring>
#include <strings.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>

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
        "bimmer", "viecar", "obdlink", "ios", "mc", "ms", "v-link", "cv305", "cx"
    };
    for (const char* kw : keywords) {
        if (containsIgnoreCase(name, kw)) return true;
    }
    return false;
}

static bool isVLinkerMacPrefix(const char* mac) {
    if (!mac) return false;
    static const char* macPrefixes[] = {
        "48:23:35", "64:8c:bb", "dc:0d:30", "00:10:cc", "e4:04:39", "1d:a5:00", "00:1d:a5", "00:04:3e"
    };
    for (const char* prefix : macPrefixes) {
        if (strncasecmp(mac, prefix, strlen(prefix)) == 0) return true;
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
    explicit BleElmScanCallbacks(BleElm* owner)
        : owner_(owner) {}

    void onResult(NimBLEAdvertisedDevice* advertisedDevice) override {
        if (!advertisedDevice || !owner_) return;

        std::string name = advertisedDevice->getName();
        std::string mac = advertisedDevice->getAddress().toString();
        if (mac == "00:00:00:00:00:00") return;
        int rssi = advertisedDevice->getRSSI();

        if (owner_->isBlacklisted(mac.c_str())) return;

        // Print every advertisement seen by radio so we can identify vLinker name/MAC
        Serial.printf("[bleElm] RX adv: '%s' %s rssi=%d\n",
                      name.empty() ? "(unnamed)" : name.c_str(), mac.c_str(), rssi);

        // Surface OBD adapters (matching name keyword, IEEE OUI MAC prefix, or OBD service UUIDs)
        bool isVLinker   = isVLinkerName(name.c_str()) || isVLinkerMacPrefix(mac.c_str());
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

        if (isVLinker || isObdUuid || isPairedMac) {
            Serial.printf("[bleElm] RX OBD Candidate: '%s' %s rssi=%d\n",
                          name.empty() ? "(unnamed)" : name.c_str(), mac.c_str(), rssi);
        }

        // Surface all devices with RSSI > -80 dBm as candidates when unpaired
        bool isNearbyCandidate = (rssi > -80 && strlen(owner_->pairedMac_) == 0 && advertisedDevice->isConnectable());
        if (!isVLinker && !isObdUuid && !isPairedMac && !isNearbyCandidate) return;

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
            strncpy(d.name, name.empty() ? "OBD / BLE Device" : name.c_str(), sizeof(d.name) - 1);
            strncpy(d.mac, mac.c_str(), sizeof(d.mac) - 1);
            d.rssi = (int8_t)rssi;
            d.isObdCandidate = (isVLinker || isObdUuid || isNearbyCandidate);
            d.isPaired = isPairedMac;
        }

        // Auto-connect if this is the paired adapter, or if we are unpaired and find a candidate device.
        bool shouldConnect = isPairedMac || (strlen(owner_->pairedMac_) == 0 && (isVLinker || isObdUuid || isNearbyCandidate));
        if (!owner_->hasTarget_ && shouldConnect) {
            Serial.printf("[bleElm] -> MATCHED OBD candidate: '%s' (%s) [rssi=%d, addrType=%d, connectable=%d]\n",
                          name.empty() ? "(unnamed)" : name.c_str(), mac.c_str(), rssi,
                          advertisedDevice->getAddress().getType(),
                          advertisedDevice->isConnectable() ? 1 : 0);
            owner_->targetAdvDevice_ = *advertisedDevice;
            owner_->targetAddress_ = advertisedDevice->getAddress();
            owner_->targetConfirmedOBD_ = (isVLinker || isObdUuid || isPairedMac);
            owner_->hasTarget_ = true;
            NimBLEDevice::getScan()->stop();
        }
    }

private:
    BleElm* owner_;
};

static volatile bool g_securityAuthComplete = false;

// TEMP DIAGNOSTIC bootstrap: the OBDLink CX (48:23:35:57:99:16) often stops
// advertising to non-bonded hosts, so the scan never surfaces it. Fall back to
// connecting DIRECTLY by MAC address. Once paired, the MAC is saved and this
// path idles (it only fires while unpaired).
static const char g_forcedAdapterMac[] = "48:23:35:57:99:16";
static uint32_t g_lastForcedMacAttemptMs = 0;

class BleElmSecurityCallbacks : public NimBLESecurityCallbacks {
public:
    uint32_t onPassKeyRequest() override {
        Serial.println("[bleElm] Security PassKey requested -> returning 123456");
        return 123456;
    }
    void onPassKeyNotify(uint32_t pass_key) override {
        Serial.printf("[bleElm] Security PassKey notify: %06lu\n", (unsigned long)pass_key);
    }
    bool onSecurityRequest() override {
        Serial.println("[bleElm] Security Request from peer -> accepting");
        return true;
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        if (desc) {
            Serial.printf("[bleElm] Security Auth Complete: bonded=%d enc=%d auth=%d\n",
                          desc->sec_state.bonded, desc->sec_state.encrypted, desc->sec_state.authenticated);
        }
        g_securityAuthComplete = true;
    }
    bool onConfirmPIN(uint32_t pin) override {
        Serial.printf("[bleElm] Security Confirm PIN: %06lu -> auto-accepting\n", (unsigned long)pin);
        return true;
    }
};

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

bool BleElm::begin(const char* targetNamePrefix) {
    namePrefix_ = targetNamePrefix ? targetNamePrefix : "OBDLink";

    // Load paired device from NVS preferences
    UserPrefs::getPairedMac(pairedMac_, sizeof(pairedMac_));
    UserPrefs::getPairedName(pairedName_, sizeof(pairedName_));

    if (!ringBuf_) {
        ringBuf_ = xRingbufferCreate(1024, RINGBUF_TYPE_BYTEBUF);
    }

    // Persistent scan callback, created exactly once and reused across scans.
    if (!scanCbs_) {
        scanCbs_ = new BleElmScanCallbacks(this);
    }

    Serial.printf("[bleElm] begin step0 core=%d\n", xPortGetCoreID());
    NimBLEDevice::init("MX5-Dash");
    Serial.printf("[bleElm] begin step1 init-ok\n");
    // Do NOT deleteAllBonds() on every boot! The OBDLink CX only accepts new
    // bonding during the first 5 minutes after ITS power-on. Keeping the stored
    // LTK in NVS means later reboots reuse the existing bond instantly instead
    // of demanding a fresh CX power cycle.
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);   // Max TX power for car cabin range
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setSecurityAuth(true, false, true); // bonding=true, mitm=false, sc=true for OBDLink CX / BLE standard
    NimBLEDevice::setSecurityCallbacks(new BleElmSecurityCallbacks());
    NimBLEDevice::setMTU(247); // OBDLink CX MTU max size is 247 bytes

    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(new BleElmClientCallbacks(&connected_, &initialized_), true);
        pClient_->setConnectTimeout(8);
    }
    Serial.printf("[bleElm] begin step2 config-ok (pClient=%p)\n", pClient_);

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
    pScan->setAdvertisedDeviceCallbacks(scanCbs_, true); // wantDuplicates = true
    pScan->setActiveScan(true);  // Active scan: transmits SCAN_REQ to request SCAN_RSP payload (local device name and UUIDs)
    pScan->setInterval(160);     // 100 ms interval (in 0.625ms units)
    pScan->setWindow(160);       // 100 ms window (100% duty cycle)

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
    targetAddress_ = NimBLEAddress(mac);
    hasTarget_ = true;
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
                      connected_, initialized_, scanning_, hasTarget_,
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
        if (!hasTarget_) {
            // The known adapter may not be visible to the scan (directed
            // advertising / already-bonded behavior). Poke it by MAC address
            // periodically so it re-opens a connection. Uses the paired MAC
            // when available, else the hardcoded known-adapter fallback.
            const char* macToPoke = pairedMac_[0] ? pairedMac_ : g_forcedAdapterMac;
            if (macToPoke[0] && !scanning_ &&
                (now - lastScanEndMs_ >= retryDelayMs_) &&
                (now - g_lastForcedMacAttemptMs >= 5000)) {
                g_lastForcedMacAttemptMs = now;
                Serial.printf("[bleElm] scan has not surfaced known OBD adapter (%s) -> forcing direct-address connect\n", macToPoke);
                targetAddress_ = NimBLEAddress(macToPoke);
                targetConfirmedOBD_ = true;
                hasTarget_ = true;
                return;
            }
            if (!scanning_ && (now - lastScanEndMs_ >= retryDelayMs_)) {
                Serial.printf("[bleElm] starting BLE scan (paired MAC: '%s', prefix: '%s')...\n", pairedMac_, namePrefix_);
                if (!startScanInternal(5)) {
                    // Stack not synced yet etc.: back off briefly and retry.
                    lastScanEndMs_ = now;
                }
            }
            return;
        }

        // Never try to connect while a discovery window is still running.
        if (scanning_) {
            stopScan();
            return; // Wait for onScanCompleteStatic callback to run on next tick
        }

        // We have a target device: attempt connection
        if (now - lastConnectAttemptMs_ >= 1000) {
            lastConnectAttemptMs_ = now;
            if (connectToDevice()) {
                if (discoverGatt() && initAdapter()) {
                    connected_ = true;
                    initialized_ = true;
                    retryDelayMs_ = 2000;
                    if (strlen(pairedMac_) == 0 && hasTarget_) {
                        std::string targetMac = targetAddress_.toString();
                        std::string targetName = targetAdvDevice_.getName();
                        pairDevice(targetMac.c_str(), targetName.empty() ? "OBDLink CX" : targetName.c_str());
                    }
                    Serial.println("[bleElm] adapter fully ready for live telemetry");
                    return;
                }
                Serial.println("[bleElm] GATT/ELM init failed, retrying...");
                if (strlen(pairedMac_) == 0 && !targetConfirmedOBD_) {
                    addBlacklist(targetAddress_.toString().c_str());
                }
                teardown();
            } else {
                Serial.println("[bleElm] connection failed, clearing target and retrying scan");
                if (strlen(pairedMac_) == 0 && !targetConfirmedOBD_) {
                    addBlacklist(targetAddress_.toString().c_str());
                }
                static uint8_t failCount = 0;
                failCount++;
                if (failCount >= 2 && strlen(pairedMac_) > 0) {
                    Serial.printf("[bleElm] Paired MAC '%s' failed %d times -> forgetting paired MAC to auto-scan fresh\n", pairedMac_, failCount);
                    forgetPairedDevice();
                    failCount = 0;
                } else {
                    teardown();
                }
                retryDelayMs_ = 2000;
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
    if (!hasTarget_) return false;

    // Give BLE host stack time to settle after stopping scan
    NimBLEScan* pScan = NimBLEDevice::getScan();
    if (pScan && pScan->isScanning()) {
        pScan->stop();
        delay(300);
    } else {
        delay(150);
    }

    // Attempt connection with target address (defaulting to BLE_ADDR_PUBLIC if Vgate)
    NimBLEAddress addrList[2] = {
        targetAddress_,
        NimBLEAddress(targetAddress_.toString(), (targetAddress_.getType() == BLE_ADDR_PUBLIC) ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC)
    };

    bool ok = false;
    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(new BleElmClientCallbacks(&connected_, &initialized_), true);
        pClient_->setConnectTimeout(8);
#if CONFIG_BT_NIMBLE_EXT_ADV
        pClient_->setConnectPhy(BLE_GAP_LE_PHY_1M_MASK);
#endif
    }

    if (pClient_->isConnected()) {
        pClient_->disconnect();
        delay(200);
    }

    // First attempt: Connect using the full advertised device structure.
    // Skip when no advertised device was captured (forced-direct-address path).
    NimBLEAddress emptyAddr("00:00:00:00:00:00");
    bool hasAdvDevice = (targetAdvDevice_.getAddress() != emptyAddr);
    if (hasAdvDevice) {
        Serial.printf("[bleElm] connecting using advertised device %s (type=%d, connectable=%d)...\n",
                      targetAdvDevice_.getAddress().toString().c_str(),
                      targetAdvDevice_.getAddress().getType(),
                      targetAdvDevice_.isConnectable() ? 1 : 0);
        esp_task_wdt_reset();
        ok = pClient_->connect(&targetAdvDevice_, true);
        esp_task_wdt_reset();
    } else {
        Serial.println("[bleElm] connecting using forced target address (no advertised device captured)...");
        ok = false;
    }

    if (!ok) {
        Serial.printf("[bleElm] connect(&targetAdvDevice_) failed (last error=%d), trying fallback address types...\n",
                      pClient_->getLastError());
        for (int i = 0; i < 2; i++) {
            NimBLEAddress currentAddr = addrList[i];
            Serial.printf("[bleElm] connecting to fallback target %s (type=%d)...\n",
                          currentAddr.toString().c_str(), currentAddr.getType());
            esp_task_wdt_reset();
            ok = pClient_->connect(currentAddr, true);
            esp_task_wdt_reset();
            if (ok) {
                targetAddress_ = currentAddr;
                break;
            }
            Serial.printf("[bleElm] fallback connect failed for type %d (last error=%d)\n",
                          currentAddr.getType(), pClient_->getLastError());
            delay(300);
        }
    }

    if (!ok) {
        Serial.println("[bleElm] connect() failed on all address types");
        return false;
    }

    Serial.println("[bleElm] physical connect OK");
    // Request fast connection parameters (7.5ms min, 15ms max) for low latency
    pClient_->setConnectionParams(6, 12, 0, 100);
    delay(50);
    Serial.println("[bleElm] securing connection...");
    g_securityAuthComplete = false;
    pClient_->secureConnection();

    // Wait up to 2500ms for BLE bonding/encryption handshake to finish before GATT discovery
    uint32_t secStart = millis();
    while (millis() - secStart < 2500) {
        if (g_securityAuthComplete || pClient_->getConnInfo().isEncrypted()) {
            Serial.printf("[bleElm] BLE link secured & encrypted in %lu ms\n", (unsigned long)(millis() - secStart));
            break;
        }
        delay(50);
    }
    delay(200);
    return true;
}

bool BleElm::discoverGatt() {
    if (!pClient_ || !pClient_->isConnected()) return false;

    pWriteChar_ = nullptr;
    pNotifyChar_ = nullptr;

    auto* services = pClient_->getServices(true);
    if (!services || services->empty()) {
        Serial.println("[bleElm] no services found or connection dropped");
        return false;
    }
    Serial.printf("[bleElm] discovered %d services:\n", (int)services->size());
    for (auto* s : *services) {
        Serial.printf("[bleElm]   GATT svc: %s\n", s->getUUID().toString().c_str());
        auto* chars = s->getCharacteristics(true);
        if (chars) {
            for (auto* c : *chars) {
                Serial.printf("[bleElm]     char: %s [write=%d, writeNoResp=%d, notify=%d, indicate=%d]\n",
                              c->getUUID().toString().c_str(),
                              c->canWrite() ? 1 : 0, c->canWriteNoResponse() ? 1 : 0,
                              c->canNotify() ? 1 : 0, c->canIndicate() ? 1 : 0);
            }
        }
    }

    // Priority 1: Check standard FFF0 / E781 / FFE0 OBD serial service UUIDs
    const char* knownServices[] = {
        "0000fff0-0000-1000-8000-00805f9b34fb",   // Standard FFF0 128-bit (OBDLink CX / Vgate)
        "FFF0",                                 // Standard FFF0 16-bit
        "e7810a71-73ae-499d-8c15-faa9aef0c3f2",   // vLinker proprietary 128-bit
        "0000e781-0000-1000-8000-00805f9b34fb",   // vLinker 16-bit E781 expanded
        "E781",                                 // vLinker 16-bit E781
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
    // Force CCCD write WITH response. The OBDLink CX is "always encrypted" and
    // silently ignores CCCD writes done write-without-response; subscribe(true)
    // reports success either way since it can't detect a fire-and-forget
    // failure. Use the (response=true) overload, then read the CCCD back to
    // confirm the peripheral actually enabled notifications.
    if (!pNotifyChar_->subscribe(useNotify, notifyCallback, true)) {
        Serial.println("[bleElm] ERROR: subscribe failed");
        return false;
    }
    NimBLERemoteDescriptor* cccd = pNotifyChar_->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    if (cccd) {
        uint16_t v = cccd->readValue<uint16_t>();
        Serial.printf("[bleElm] CCCD readback = 0x%04x (expected 0x0001 for notify)\n", v);
    } else {
        Serial.println("[bleElm] WARN: no CCCD descriptor on notify char!");
    }

    return true;
}

void BleElm::notifyCallback(NimBLERemoteCharacteristic* pChar, uint8_t* pData, size_t length, bool isNotify) {
    if (!ringBuf_ || !pData || length == 0) return;
    Serial.printf("[bleElm] RX notify len=%d: '%.*s'\n", (int)length, (int)length, (const char*)pData);
    xRingbufferSend(ringBuf_, pData, length, 0);
}

bool BleElm::initAdapter() {
    char resp[MAX_RESPONSE];

    // Give BLE notifications time to settle, wake up chip with CR, then clear buffer
    delay(250);
    sendCommand("\r");
    delay(200);
    flush();

    // ELM327 initialization commands. ATZ must run FIRST!
    const char* cmds[] = {
        "AT\r",           // 0: Ping adapter -> wake up STN/ELM chip
        "ATZ\r",          // 1: Reset -> returns adapter banner
        "ATE0\r",         // 2: Echo off
        "ATL0\r",         // 3: Linefeeds off
        "ATS0\r",         // 4: Spaces off
        "ATH0\r",         // 5: Headers off
        "ATAT2\r",        // 6: Adaptive timing mode 2
        "ATST32\r",       // 7: Adapter timeout 200ms
        "ATSP6\r",        // 8: Protocol 6 = ISO 15765-4 CAN 11-bit/500k
        "ATSH 7E0\r",     // 9: Default to PCM CAN Header 7E0
    };

    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        bool ok = false;
        for (int retry = 0; retry < 2; retry++) {
            if (sendCommand(cmds[i]) && readResponse(resp, sizeof(resp), 1500)) {
                Serial.printf("[bleElm] RX resp for '%s': '%s'\n", cmds[i], resp);
                ok = true;
                break;
            }
            Serial.printf("[bleElm] retry %d for '%s'...\n", retry + 1, cmds[i]);
            sendCommand("\r");
            delay(150);
            flush();
        }

        if (!ok) {
            Serial.printf("[bleElm] failed/timeout initializing '%s'\n", cmds[i]);
            return false;
        }

        if (i == 1) {
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

    hasTarget_ = false;

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

    // OBDLink CX FFF2 declares BOTH "Write" and "Write without Response".
    // The STN serial UART bridge consumes packets written WITHOUT response;
    // write-with-response on this profile is often acked by the link but
    // never forwarded to the UART, which shows up as "no notification back".
    // So prefer Write-Without-Response whenever the characteristic allows it.
    bool ok = false;
    if (pWriteChar_->canWriteNoResponse()) {
        ok = pWriteChar_->writeValue((const uint8_t*)buf, len, false);
    }
    if (!ok && pWriteChar_->canWrite()) {
        ok = pWriteChar_->writeValue((const uint8_t*)buf, len, true);
    }
    if (!ok) {
        Serial.printf("[bleElm] write to %s FAILED (%d bytes)\n",
                      pWriteChar_->getUUID().toString().c_str(), (int)len);
        Serial.printf("[bleElm] diag: canWrite=%d canWriteNoResp=%d\n",
                      pWriteChar_->canWrite() ? 1 : 0,
                      pWriteChar_->canWriteNoResponse() ? 1 : 0);
    }
    return ok;
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

bool BleElm::isBlacklisted(const char* mac) const {
    if (!mac) return false;
    for (uint8_t i = 0; i < blacklistedCount_; i++) {
        if (strcasecmp(blacklistedMacs_[i], mac) == 0) return true;
    }
    return false;
}

void BleElm::addBlacklist(const char* mac) {
    if (!mac || isBlacklisted(mac)) return;
    if (blacklistedCount_ < MAX_BLACKLIST) {
        strncpy(blacklistedMacs_[blacklistedCount_++], mac, sizeof(blacklistedMacs_[0]) - 1);
        Serial.printf("[bleElm] Blacklisted non-OBD candidate: %s\n", mac);
    }
}
