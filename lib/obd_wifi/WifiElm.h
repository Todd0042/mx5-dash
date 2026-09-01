#pragma once

#include <Arduino.h>
#include <IPAddress.h>
#include <WiFi.h>

/**
 * WifiElm
 *
 * Transport layer for an ELM327-compatible Wi-Fi OBD2 adapter.
 *
 * The adapter hosts its own Wi-Fi access point. We join it as a station and
 * open a raw TCP socket to the adapter's command server (default
 * 192.168.0.10:35000). Once connected, the adapter speaks the classic ELM327
 * ASCII protocol: commands end with '\r', and every response is terminated by
 * the '>' prompt (preceded by CR/LF).
 *
 * A response is considered complete when we see '>' preceded by CR or LF (or
 * as the first byte). CR/LF/newlines are stripped from the returned payload so
 * the caller gets a clean hex/ASCII string (with ATE0 ATS0 ATH0 enabled, a
 * standard PID query returns e.g. "410D48").
 *
 * Use from a single dedicated task; blocking reads with short timeouts keep
 * the UI task responsive.
 */

class WifiElm {
public:
    static constexpr size_t MAX_RESPONSE = 256;

    bool begin(const char* ssid, const char* pass, IPAddress host, uint16_t port);

    // Call frequently from the OBD task: keeps WiFi + TCP up, runs the init
    // handshake on connect, and handles reconnects with backoff.
    void loop();

    bool isInitialized() const { return initialized_; }
    bool wifiUp() const { return wifiUp_; }
    bool tcpUp() const { return tcpUp_; }
    const char* adapterVersion() const { return adapterVersion_; }

    // Send an ELM command (adds '\r'). Returns true if written.
    bool sendCommand(const char* cmd);

    // Read one response, i.e. everything up to the next '>' prompt.
    // out is NUL-terminated with CR/LF stripped. Returns true when a prompt
    // ends the response, false on timeout (partial data may still be in out).
    bool readResponse(char* out, size_t maxLen, uint32_t timeoutMs);

    // sendCommand + readResponse in one call. Returns true if a prompt arrived.
    bool sendQuery(const char* cmd, char* out, size_t maxLen, uint32_t timeoutMs);

    void flush();

private:
    bool connectSocket(uint32_t timeoutMs);
    bool initAdapter();
    bool waitForPrompt(uint32_t timeoutMs);
    void teardown();

    const char* ssid_ = nullptr;
    const char* pass_ = nullptr;
    IPAddress host_;
    uint16_t port_ = 35000;

    WiFiClient client_;

    bool wifiUp_ = false;
    bool tcpUp_ = false;
    bool initialized_ = false;

    uint32_t lastIoMs_ = 0;
    uint32_t retryDelayMs_ = 2000;

    char adapterVersion_[32] = "";   // from the ATZ banner
};