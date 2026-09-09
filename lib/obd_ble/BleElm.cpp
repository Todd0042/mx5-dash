#include "BleElm.h"

#include <cstring>
#include <strings.h>
#include <stdarg.h>
#include <esp_task_wdt.h>
#include <esp_mac.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>

// ---------------------------------------------------------------------------
// Persistent connection log (BT-Tinkering Phase 0).
//
// The connection phase can't be watched live from the laptop (it happens in
// the car). Every [conn] line is mirrored into a RAM ring buffer and flushed
// to a file on the SPIFFS partition every ~2s, so after a drive the log can be
// recovered: on the NEXT boot begin() replays the stored file over USB serial
// (tagged [log-replay]) and then clears it for the new session.
// ---------------------------------------------------------------------------
namespace {
constexpr size_t kLogRingCap = 4096;      // file = most recent ~4KB
char g_logRing[kLogRingCap];
size_t g_logRingLen = 0;
uint32_t g_logFlushMs = 0;
uint32_t g_logSnapMs = 0;
uint32_t g_logFileIdx = 0;
uint32_t g_logConnectedSinceMs = 0;
bool g_logFsReady = false;
bool g_logFsMounted = false;
bool g_logSealed = false;   // true once connection is 5s stable -> writes stop

// SPIFFS is demonstrably NOT crash-safe on this board: the abrupt ACC power-cut
// in the car left the partition empty on the next boot (even the A/B rotated
// files were gone). NVS, by contrast, survived every power cut today. So every
// N seconds we also snapshot the ring into NVS (4 slices x 1024B covers the
// whole 4096B ring); on boot, if SPIFFS came up empty we replay the snapshot.
constexpr size_t kLogSnapKeys = 4;
constexpr size_t kLogSnapSlice = 1024;

static void logSnapKey(char* out, int i) {
    snprintf(out, 8, "clg%d", i);
}

// Persist the ring to NVS. Cheap (only the dirty slice bytes) - call on a
// slow cadence + on state transitions, NOT every 2s flush (NVS wear).
void logSnapSave() {
    if (g_logRingLen == 0) return;
    Preferences p;
    if (!p.begin("ble_clog", false)) return;
    for (int i = 0; i < kLogSnapKeys; i++) {
        char k[8];
        logSnapKey(k, i);
        size_t off = (size_t)i * kLogSnapSlice;
        if (off < g_logRingLen) {
            size_t n = kLogSnapSlice;
            if (n > g_logRingLen - off) n = g_logRingLen - off;
            p.putBytes(k, g_logRing + off, n);
        } else {
            p.remove(k);
        }
    }
    p.end();
}

// Dump the NVS snapshot (if any) to serial, then purge it.
void logSnapReplay() {
    bool any = false;
    {
        Preferences p;
        if (!p.begin("ble_clog", true)) return;
        for (int i = 0; i < kLogSnapKeys; i++) {
            char k[8];
            logSnapKey(k, i);
            size_t len = p.getBytesLength(k);
            if (len > 0 && len <= kLogSnapSlice) {
                uint8_t tmp[kLogSnapSlice];
                size_t got = p.getBytes(k, tmp, sizeof(tmp));
                if (got > 0) {
                    if (!any) Serial.println("======== [log-replay] previous session (NVS snapshot) ========");
                    Serial.write(tmp, got);
                    any = true;
                }
            }
        }
        p.end();
    }
    if (any) {
        Preferences p;
        if (p.begin("ble_clog", false)) {
            for (int i = 0; i < kLogSnapKeys; i++) {
                char k[8];
                logSnapKey(k, i);
                p.remove(k);
            }
            p.end();
        }
        Serial.println("======== [log-replay] end ========");
    }
}

// Keep the tail of the session: overwrite the oldest bytes once the ring
// fills (a split line at the wrap point is acceptable for diagnostics).
void logRingAppend(const char* line, size_t n) {
    if (n == 0) return;
    if (g_logRingLen + n + 1 > kLogRingCap) {
        size_t drop = (g_logRingLen + n + 1) - kLogRingCap;
        if (drop < g_logRingLen) {
            memmove(g_logRing, g_logRing + drop, g_logRingLen - drop);
            g_logRingLen -= drop;
        } else {
            g_logRingLen = 0;
        }
    }
    memcpy(g_logRing + g_logRingLen, line, n);
    g_logRingLen += n;
    g_logRing[g_logRingLen++] = '\n';
}

// Write the whole ring out, alternating between /conn.a and /conn.b so an
// abrupt power cut mid-write can only trash ONE copy; boot-replay reads both.
// f.flush() forces the SPIFFS write buffers to flash before closing.
void logFsFlush() {
    if (!g_logFsReady || g_logRingLen == 0) return;
    const char* path = (g_logFileIdx++ & 1) ? "/conn.b" : "/conn.a";
    File f = SPIFFS.open(path, FILE_WRITE);
    if (f) {
        f.write((const uint8_t*)g_logRing, g_logRingLen);
        f.flush();
        f.close();
    }
    g_logFlushMs = millis();
}

// Mount the FS once. Replay + clear any previous session's log, then leave
// the FS ready for this session's flushes. Returns true when FS logging works.
bool logFsBegin() {
    if (g_logFsMounted) return g_logFsReady;
    g_logFsMounted = true;
    bool ok = SPIFFS.begin(true);   // auto-format on very first boot
    if (!ok) {
        Serial.println("[conn] WARN: SPIFFS mount failed -> RAM-only logging");
        return false;
    }
    g_logFsReady = true;

    bool spiffsReplayed = false;
    const char* replayPaths[] = { "/conn.b", "/conn.a" };  // newest written last
    for (const char* p : replayPaths) {
        File f = SPIFFS.open(p, FILE_READ);
        if (f && f.size() > 0) {
            char ch;
            spiffsReplayed = true;
            Serial.printf("======== [log-replay] previous session (%s) ========\n", p);
            while (f.available()) {
                ch = (char)f.read();
                Serial.write((uint8_t)ch);
            }
            Serial.println("======== [log-replay] end ========");
            f.close();
        } else if (f) {
            f.close();
        }
        SPIFFS.remove(p);
    }
    if (!spiffsReplayed) {
        logSnapReplay();   // SPIFFS empty/absent -> recover the NVS snapshot
    }
    return true;
}
}  // namespace

// The PIO upload wipes the NVS partition, so the paired-device bond (saved to
// NVS by UserPrefs) is lost on every firmware flash. Mirror it to SPIFFS too -
// that partition survives the upload (proven by the conn.log replay) - and on
// boot restore it back into NVS whenever NVS comes up empty. Structure:
//   line 1: mac
//   line 2: name
static void spifsSavePaired(const char* mac, const char* name) {
    if (!mac || !mac[0]) return;
    if (!SPIFFS.begin(true)) return;
    File f = SPIFFS.open("/paired.txt", FILE_WRITE);
    if (f) {
        f.print(mac);
        f.print('\n');
        if (name) f.print(name);
        f.flush();
        f.close();
    }
}

static bool spifsLoadPaired(char* macOut, size_t macLen, char* nameOut, size_t nameLen) {
    macOut[0] = '\0';
    if (nameOut && nameLen > 0) nameOut[0] = '\0';
    if (!SPIFFS.begin(true)) return false;
    File f = SPIFFS.open("/paired.txt", FILE_READ);
    if (!f || f.size() == 0) {
        if (f) f.close();
        return false;
    }
    String mac = f.readStringUntil('\n');
    String name = f.readStringUntil('\n');
    f.close();
    mac.trim();
    name.trim();
    if (mac.length() == 0) return false;
    strncpy(macOut, mac.c_str(), macLen - 1);
    macOut[macLen - 1] = '\0';
    if (nameOut && nameLen > 0) {
        strncpy(nameOut, name.c_str(), nameLen - 1);
        nameOut[nameLen - 1] = '\0';
    }
    return true;
}

static void spifsClearPaired() {
    if (SPIFFS.begin(true)) SPIFFS.remove("/paired.txt");
}

// Connection-phase logging. Every line is tagged [conn] with ms-from-boot so
// the whole bring-up sequence can be replayed and analyzed from the shell
// capture. Goes to BOTH the live serial stream AND the persistent file.
static void connLog(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
static void connLog(const char* fmt, ...) {
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    char out[280];
    int n = snprintf(out, sizeof(out), "[conn] t=%lu %s\n", (unsigned long)millis(), buf);
    if (n > 0) {
        Serial.print(out);
        logRingAppend(out, (size_t)n);
    }
}

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
        connLog("disconnected");
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
    logFsBegin();   // replay previous session's log if any, then start fresh
    connLog("begin name=%s", targetNamePrefix ? targetNamePrefix : "OBDLink");
    namePrefix_ = targetNamePrefix ? targetNamePrefix : "OBDLink";

    // Load paired device from NVS preferences. If NVS came up empty (e.g. it
    // was just wiped by a firmware flash), fall back to the SPIFFS mirror so
    // the CX bond survives reflashing without a re-pair.
    UserPrefs::getPairedMac(pairedMac_, sizeof(pairedMac_));
    UserPrefs::getPairedName(pairedName_, sizeof(pairedName_));
    if (pairedMac_[0] == '\0') {
        char altMac[24], altName[40];
        if (spifsLoadPaired(altMac, sizeof(altMac), altName, sizeof(altName))) {
            strncpy(pairedMac_, altMac, sizeof(pairedMac_) - 1);
            pairedMac_[sizeof(pairedMac_) - 1] = '\0';
            strncpy(pairedName_, altName, sizeof(pairedName_) - 1);
            pairedName_[sizeof(pairedName_) - 1] = '\0';
            UserPrefs::savePairedMac(pairedMac_);
            UserPrefs::savePairedName(pairedName_);
            Serial.printf("[bleElm] restored paired '%s' from SPIFFS mirror\n", pairedMac_);
        }
    }
    connLog("begin paired='%s' name='%s'", pairedMac_[0] ? pairedMac_ : "-",
            pairedName_[0] ? pairedName_ : "-");

    if (!ringBuf_) {
        ringBuf_ = xRingbufferCreate(1024, RINGBUF_TYPE_BYTEBUF);
    }

    // Persistent scan callback, created exactly once and reused across scans.
    if (!scanCbs_) {
        scanCbs_ = new BleElmScanCallbacks(this);
    }

    NimBLEDevice::init("MX5-Dash");
    connLog("nimble-init ok");
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
        pClient_->setConnectTimeout(4);
    }
    connLog("begin-done retry=%lu", (unsigned long)retryDelayMs_);

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
    if (g_activeScanOwner) {
        BleElm* owner = g_activeScanOwner;
        owner->scanning_ = false;
        owner->lastScanEndMs_ = millis();
        connLog("scan-end dur=%lu found=%d",
                (unsigned long)(owner->lastScanEndMs_ - owner->lastScanStartMs_),
                (int)results.getCount());
        g_activeScanOwner = nullptr;
    }
}

bool BleElm::getDiscoveredDevice(uint8_t index, BleDeviceInfo& out) const {
    if (index >= discoveredCount_) return false;
    out = discoveredDevices_[index];
    return true;
}

void BleElm::savePairedDevice(const char* mac, const char* name) {
    // Persist the MAC+name WITHOUT tearing down the live connection. Used on
    // the auto-connect success path: the first-ever connect is already linked
    // to this exact MAC, so pairDevice()'s teardown-then-retarget would kill
    // the brand-new bond and force a 2nd full connect cycle.
    if (!mac) return;
    strncpy(pairedMac_, mac, sizeof(pairedMac_) - 1);
    pairedMac_[sizeof(pairedMac_) - 1] = '\0';
    if (name) {
        strncpy(pairedName_, name, sizeof(pairedName_) - 1);
        pairedName_[sizeof(pairedName_) - 1] = '\0';
    }
    UserPrefs::savePairedMac(pairedMac_);
    UserPrefs::savePairedName(pairedName_);
    spifsSavePaired(pairedMac_, pairedName_);
    connLog("saved paired device mac=%s name='%s'", pairedMac_, pairedName_);
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
    spifsSavePaired(pairedMac_, pairedName_);

    teardown();
    targetAddress_ = NimBLEAddress(mac);
    hasTarget_ = true;
}

void BleElm::forgetPairedDevice() {
    pairedMac_[0] = '\0';
    pairedName_[0] = '\0';
    UserPrefs::clearPairedDevice();
    spifsClearPaired();
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

    // Phase 0: flag any >500ms gap between loop ticks while trying to connect.
    // connectToDevice()'s internal delays/timeouts are serialized here, so the
    // gap quantifies how long each attempt actually blocks the task.
    static uint32_t s_prevLoop = 0;
    if (s_prevLoop && (now - s_prevLoop > 500)) {
        connLog("loop-gap=%lu", (unsigned long)(now - s_prevLoop));
    }
    s_prevLoop = now;

    // Phase 0: persist the ring while in the CONNECT phase only. Once the link has
    // been stable for 5s the session is "sealed": the connect story is written,
    // and all flash/NVS writes stop so an abrupt ACC power-cut can never strike
    // mid-write (that mid-write cut was corrupting SPIFFS and losing sessions).
    if (g_logFsReady && !g_logSealed && (now - g_logFlushMs >= 2000)) {
        logFsFlush();
    }
    if (!g_logSealed && connected_ && (now - g_logConnectedSinceMs >= 5000)) {
        connLog("session sealed (link stable 5s)");
        g_logSealed = true;
        logFsFlush();
        logSnapSave();
        g_logFlushMs = now;
        g_logSnapMs = now;
    }
    if (!g_logSealed && (now - g_logSnapMs >= 30000)) {
        logSnapSave();
        g_logSnapMs = now;
    }

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

        if (!hasTarget_) {
            const bool hasPaired = pairedMac_[0] != '\0';

            if (hasPaired) {
                // ---- PAIRED: DIRECT-FIRST, no discovery scan on the hot path.
                // A bonded CX fast-blinking is directed-advertising to its bond
                // and does NOT undirected-advertise, so a scan almost never
                // surfaces it. Poke it straight by MAC on a short cadence
                // instead of burning a 5s scan + quiet period before connect.
                if (connectStartMs_ == 0) connectStartMs_ = now;
                if (!scanning_ && (now - lastDirectPokeMs_ >= 1500)) {
                    lastDirectPokeMs_ = now;
                    connLog("direct-poke mac=%s fails=%u", pairedMac_, (unsigned)directFailures_);
                    targetAddress_ = NimBLEAddress(pairedMac_);
                    targetConfirmedOBD_ = true;
                    hasTarget_ = true;
                    return;
                }
                // Safety net: if the direct path keeps failing, take a short
                // scan pass (covers a CX that lost ITS bond and is back to
                // undirected advertising). Resume direct pokes afterwards.
                if (!scanning_ && directFailures_ >= 3 &&
                    (now - lastScanEndMs_ >= retryDelayMs_)) {
                    connLog("scan-fallback after %u direct failures (3s pass)", (unsigned)directFailures_);
                    if (!startScanInternal(3)) lastScanEndMs_ = now;
                    return;
                }
                return;
            }

            // ---- UNPAIRED: discovery scan is the primary path. Poke the
            // known CX MAC as a fallback once a quiet period + gate expire
            // (first-ever pairing / NVS cleared).
            const char* macToPoke = g_forcedAdapterMac;
            if (macToPoke[0] && !scanning_ &&
                (now - lastScanEndMs_ >= retryDelayMs_) &&
                (now - g_lastForcedMacAttemptMs >= 5000)) {
                g_lastForcedMacAttemptMs = now;
                connLog("direct-poke (unpaired fallback) mac=%s", macToPoke);
                targetAddress_ = NimBLEAddress(macToPoke);
                targetConfirmedOBD_ = true;
                hasTarget_ = true;
                return;
            }
            if (!scanning_ && (now - lastScanEndMs_ >= retryDelayMs_)) {
                connLog("scan-start (unpaired) dur=5s target='%s'", namePrefix_);
                if (!startScanInternal(5)) {
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
                    directFailures_ = 0;
                    connectStartMs_ = 0;
                    if (strlen(pairedMac_) == 0 && hasTarget_) {
                        std::string targetMac = targetAddress_.toString();
                        std::string targetName = targetAdvDevice_.getName();
                        savePairedDevice(targetMac.c_str(), targetName.empty() ? "OBDLink CX" : targetName.c_str());
                    }
                    connLog("READY data-path up");
                    g_logConnectedSinceMs = now;
                    g_logSealed = false;   // re-arm seal() for this link
                    logFsFlush();
                    logSnapSave();
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
                directFailures_++;
                // NO auto-forget. The scan-fallback below (>=3 direct fails
                // -> 3s discovery pass) already recovers a replaced/new CX by
                // name and re-pairs on success. Auto-forgetting instead burned
                // a perfectly good bond on every bench boot where the CX is
                // simply OFF (nothing distinguishable from "gone"), forcing a
                // full slow re-pair on the next car trip. Forever-poke a stale
                // MAC + periodic scan passes is harmless; losing the bond is
                // not.
                teardown();
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
    uint32_t ccStart = millis();

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
    connLog("connect-attempt mac=%s (hasAdv=%d)", targetAddress_.toString().c_str(),
            (targetAdvDevice_.getAddress() != NimBLEAddress("00:00:00:00:00:00")) ? 1 : 0);

    bool ok = false;
    if (!pClient_) {
        pClient_ = NimBLEDevice::createClient();
        pClient_->setClientCallbacks(new BleElmClientCallbacks(&connected_, &initialized_), true);
        pClient_->setConnectTimeout(4);
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
        connLog("connect adv-device -> %d elapsed=%lu", ok ? 1 : 0,
                (unsigned long)(millis() - ccStart));
    } else {
        connLog("forced address path (no advertised device captured)");
        ok = false;
    }

    if (!ok) {
        // A confirmed OBD device with a PUBLIC address (the OBDLink CX) only
        // ever answers on that one type - the RANDOM retry is a guaranteed 8s
        // timeout. Skip it to halve outage cost.
        int addrCount = (targetConfirmedOBD_ && addrList[0].getType() == BLE_ADDR_PUBLIC) ? 1 : 2;
        for (int i = 0; i < addrCount; i++) {
            NimBLEAddress currentAddr = addrList[i];
            Serial.printf("[bleElm] connecting to fallback target %s (type=%d)...\n",
                          currentAddr.toString().c_str(), currentAddr.getType());
            uint32_t addrStart = millis();
            esp_task_wdt_reset();
            ok = pClient_->connect(currentAddr, true);
            esp_task_wdt_reset();
            connLog("connect addr[%d]=%s type=%d -> %d elapsed=%lu err=%d%s",
                    i, currentAddr.toString().c_str(), currentAddr.getType(), ok ? 1 : 0,
                    (unsigned long)(millis() - addrStart), pClient_->getLastError(),
                    ok ? "" : "");
            if (ok) {
                targetAddress_ = currentAddr;
                break;
            }
            delay(300);
        }
    }

    if (!ok) {
        connLog("connect FAILED all address types elapsed=%lu",
                (unsigned long)(millis() - ccStart));
        Serial.println("[bleElm] connect() failed on all address types");
        return false;
    }

    connLog("phys-connect ok elapsed=%lu", (unsigned long)(millis() - ccStart));
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
            connLog("link secured+encrypted elapsed=%lu", (unsigned long)(millis() - secStart));
            break;
        }
        delay(50);
    }
    delay(200);
    connLog("connectToDevice done elapsed=%lu", (unsigned long)(millis() - ccStart));
    return true;
}

bool BleElm::discoverGatt() {
    if (!pClient_ || !pClient_->isConnected()) return false;
    uint32_t gattStart = millis();

    pWriteChar_ = nullptr;
    pNotifyChar_ = nullptr;

    auto* services = pClient_->getServices(true);
    if (!services || services->empty()) {
        connLog("gatt no-services elapsed=%lu", (unsigned long)(millis() - gattStart));
        Serial.println("[bleElm] no services found or connection dropped");
        return false;
    }
    connLog("gatt services=%d elapsed=%lu", (int)services->size(),
            (unsigned long)(millis() - gattStart));

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

    connLog("gatt chars w=%s n=%s elapsed=%lu",
            pWriteChar_ ? pWriteChar_->getUUID().toString().c_str() : "?",
            pNotifyChar_ ? pNotifyChar_->getUUID().toString().c_str() : "?",
            (unsigned long)(millis() - gattStart));

    bool useNotify = pNotifyChar_->canNotify();
    // Force CCCD write WITH response. The OBDLink CX is "always encrypted" and
    // silently ignores CCCD writes done write-without-response; subscribe(true)
    // reports success either way since it can't detect a fire-and-forget
    // failure. Use the (response=true) overload, then read the CCCD back to
    // confirm the peripheral actually enabled notifications.
    uint32_t subStart = millis();
    if (!pNotifyChar_->subscribe(useNotify, notifyCallback, true)) {
        connLog("subscribe FAILED elapsed=%lu", (unsigned long)(millis() - subStart));
        Serial.println("[bleElm] ERROR: subscribe failed");
        return false;
    }
    NimBLERemoteDescriptor* cccd = pNotifyChar_->getDescriptor(NimBLEUUID((uint16_t)0x2902));
    uint16_t cccdVal = 0;
    if (cccd) {
        cccdVal = cccd->readValue<uint16_t>();
        connLog("subscribed cccd=0x%04x elapsed=%lu (wrote with response)",
                cccdVal, (unsigned long)(millis() - subStart));
    } else {
        connLog("NO CCCD descriptor on notify char elapsed=%lu",
                (unsigned long)(millis() - subStart));
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
    uint32_t elmStart = millis();

    // Give BLE notifications time to settle, wake up chip with CR, then clear buffer
    delay(250);
    sendCommand("\r");
    delay(200);
    flush();

    // ELM327 initialization commands. ATZ must run FIRST!
    const char* cmds[] = {
        "ATZ\r",          // 0: Reset -> wakes the STN chip AND returns adapter banner
        "ATE0\r",         // 1: Echo off
        "ATL0\r",         // 2: Linefeeds off
        "ATS0\r",         // 3: Spaces off
        "ATH0\r",         // 4: Headers off
        "ATAT2\r",        // 5: Adaptive timing mode 2
        "ATST32\r",       // 6: Adapter timeout 200ms
        "ATSP6\r",        // 7: Protocol 6 = ISO 15765-4 CAN 11-bit/500k
        "ATSH 7E0\r",     // 8: Default to PCM CAN Header 7E0
    };

    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        bool ok = false;
        int retries = 0;
        for (int retry = 0; retry < 2; retry++) {
            if (sendCommand(cmds[i]) && readResponse(resp, sizeof(resp), 1500)) {
                ok = true;
                break;
            }
            retries++;
            Serial.printf("[bleElm] retry %d for '%s'...\n", retry + 1, cmds[i]);
            sendCommand("\r");
            delay(150);
            flush();
        }
        connLog("elm '%s' -> %s retries=%d elapsed=%lu", cmds[i], ok ? "ok" : "FAIL",
                retries, (unsigned long)(millis() - elmStart));

        if (!ok) {
            connLog("elm INIT ABORTED at '%s' elapsed=%lu",
                    cmds[i], (unsigned long)(millis() - elmStart));
            Serial.printf("[bleElm] failed/timeout initializing '%s'\n", cmds[i]);
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
    connLog("elm init complete elapsed=%lu", (unsigned long)(millis() - elmStart));
    return true;
}

void BleElm::teardown() {
    connLog("teardown");
    g_logSealed = false;   // new connect attempt must log again
    logFsFlush();
    logSnapSave();
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
