#include "WifiElm.h"

#include <WiFi.h>

// ---------------------------------------------------------------------------
// lifecycle
// ---------------------------------------------------------------------------

bool WifiElm::begin(const char* ssid, const char* pass, IPAddress host, uint16_t port) {
    ssid_ = ssid;
    pass_ = pass;
    host_ = host;
    port_ = port;

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);
    WiFi.setAutoReconnect(true);
    return true;
}

void WifiElm::loop() {
    uint32_t now = millis();

    // --- WiFi station link -------------------------------------------------
    if (WiFi.status() == WL_CONNECTED) {
        if (!wifiUp_) {
            wifiUp_ = true;
            retryDelayMs_ = 2000;   // reset backoff once link is up
            tcpUp_ = false;         // (re)connect TCP next
        }
    } else {
        if (wifiUp_) {
            wifiUp_ = false;
            teardown();
        }
        // back off, then retry associating with the adapter AP
        if (now - lastIoMs_ >= retryDelayMs_) {
            lastIoMs_ = now;
            retryDelayMs_ = constrain(retryDelayMs_ * 2, 2000, 30000);
            WiFi.begin(ssid_, pass_);
        }
        return;
    }

    // --- TCP command socket + init handshake -------------------------------
    if (!tcpUp_) {
        if (now - lastIoMs_ >= 1000) {
            lastIoMs_ = now;
            if (connectSocket(5000)) {
                if (initAdapter()) {
                    tcpUp_ = true;
                    initialized_ = true;
                    Serial.println("[wifiElm] adapter initialized");
                    return;
                }
                // TCP attached but ELM handshake failed - drop and retry
                client_.stop();
                tcpUp_ = false;
                initialized_ = false;
                Serial.println("[wifiElm] init handshake failed, retrying");
            }
        }
        return;
    }

    if (!client_.connected()) {
        Serial.println("[wifiElm] socket lost, reconnecting");
        teardown();
    }
}

void WifiElm::flush() {
    while (client_.available()) client_.read();
}

void WifiElm::teardown() {
    if (client_) client_.stop();
    tcpUp_ = false;
    initialized_ = false;
    adapterVersion_[0] = '\0';
}

bool WifiElm::connectSocket(uint32_t timeoutMs) {
    Serial.printf("[wifiElm] connecting to %s:%u...\n",
                  host_.toString().c_str(), port_);
    if (!client_.connect(host_, port_, timeoutMs)) {
        Serial.println("[wifiElm] TCP connect failed");
        return false;
    }
    // The v1.5-control adapter prints a banner and a prompt on connect.
    return true;
}

// Verify the adapter is ready by waiting for the '>' prompt on a fresh
// socket before we issue any ELM commands. This clears any connect-time
// banner AND, if we just reset, the burst of "OK"/banner that follows ATZ.
// Returns true once a prompt has arrived.
bool WifiElm::waitForPrompt(uint32_t timeoutMs) {
    char sink[MAX_RESPONSE];
    return readResponse(sink, sizeof(sink), timeoutMs);
}

bool WifiElm::initAdapter() {
    char resp[MAX_RESPONSE];

    // 1) Actively wait for the adapter's '>' prompt. Only proceed once the
    //    parser observes the prompt character in the stream, confirming the
    //    ELM327 is talking before we send ATZ. Anything else (banner bytes,
    //    partial data) is drained as part of this wait.
    if (!waitForPrompt(3000)) {
        Serial.println("[wifiElm] no prompt seen before ATZ - adapter not ready");
        return false;
    }

    // 2) Bare minimum sane ELM327 config. Each cmd gets its own response.
    const char* cmds[] = {
        "ATE0\r",         // echo off
        "ATL0\r",         // linefeeds off
        "ATS0\r",         // spaces off
        "ATH0\r",         // headers off (simpler parsing; see TPMS note in service)
        "ATSP0\r",        // auto protocol detect
        "ATZ\r",          // reset; answer is the "ELM327 v1.5" banner we use
    };

    for (unsigned i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        client_.print(cmds[i]);
        if (!readResponse(resp, sizeof(resp), 1500)) {
            Serial.printf("[wifiElm] init timed out on '%s'\n", cmds[i]);
            return false;
        }
        if (i == 4) {
            // ATZ response is the adapter banner (usually "ELM327 v1.5" style)
            strncpy(adapterVersion_, resp, sizeof(adapterVersion_) - 1);
        }
    }

    // 3) A reset just happened - re-verify a fresh prompt (requirement: always
    //    confirm the '>' prompt before trusting the stream is sane).
    if (!waitForPrompt(2000)) {
        Serial.println("[wifiElm] no prompt after final flush - reconnecting");
        return false;
    }

    flush();
    return true;
}

// ---------------------------------------------------------------------------
// ELM327 I/O
// ---------------------------------------------------------------------------

bool WifiElm::sendCommand(const char* cmd) {
    if (!tcpUp_ || !client_.connected()) return false;
    client_.print(cmd);
    client_.print('\r');
    return true;
}

bool WifiElm::readResponse(char* out, size_t maxLen, uint32_t timeoutMs) {
    size_t n = 0;
    char prev = 0;
    uint32_t start = millis();

    while (millis() - start < timeoutMs) {
        while (client_.available()) {
            char c = client_.read();

            // '>' at the start of a line marks the end of a response.
            if (c == '>' && (prev == '\r' || prev == '\n' || n == 0)) {
                // trim trailing whitespace
                while (n > 0 && (out[n - 1] == ' ' || out[n - 1] == '\r' || out[n - 1] == '\n'))
                    n--;
                out[n] = '\0';
                return true;
            }
            // strip CR/LF from the payload
            if (c != '\r' && c != '\n') {
                if (n + 1 < maxLen) out[n++] = c;
            }
            prev = c;
        }
        delay(1);
    }

    if (n > 0) {
        out[n] = '\0';
        return false;   // partial data, timed out before the prompt
    }
    out[0] = '\0';
    return false;
}

bool WifiElm::sendQuery(const char* cmd, char* out, size_t maxLen, uint32_t timeoutMs) {
    if (!sendCommand(cmd)) return false;
    return readResponse(out, maxLen, timeoutMs);
}