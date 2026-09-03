#pragma once

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

struct VehicleData;

/**
 * Incident Log Entry for Chart Rendering
 */
struct IncidentDataPoint {
    float timeOffsetSec;   // -15.0s .. +15.0s (0 = trigger event)
    float rpm;             // Engine RPM
    float stft;            // Short-Term Fuel Trim %
    float ltft;            // Long-Term Fuel Trim %
    float afr;             // Wideband AFR
    float knockRetard;     // Knock Retard Deg (0..-10°)
    float railPsi;         // DI Fuel Rail PSI
    float coolantC;        // Coolant Temp °C
    uint8_t misfires;      // Total Misfire Events
};

struct IncidentMeta {
    char filename[64];
    char title[48];
    char triggerReason[32];
    char dateStr[24];
    uint16_t pointCount;
    float peakKnock;
    float maxStft;
    float minAfr;
    float maxAfr;
    float peakRpm;
};

/**
 * SdLogger
 *
 * Manages MicroSD card detection, FAT32 formatting, 30-second rolling ring buffer,
 * incident freeze frame captures, and privacy-shielded CSV logging.
 *
 * Privacy Policy:
 * - NO GPS coordinates or geographic data are ever requested, processed, or saved.
 * - Time is stored strictly as relative drive offsets (T-15s .. T0 .. T+15s).
 * - Vehicle speed is redacted/masked to prevent self-incriminating velocity logs.
 */
class SdLogger {
public:
    static SdLogger& instance();

    bool begin();
    void update(const VehicleData& data);

    bool isCardInserted() const { return cardInserted_; }
    bool isCardFormatted() const { return cardFormatted_; }
    uint64_t totalBytes() const { return totalBytes_; }
    uint64_t freeBytes() const { return freeBytes_; }

    // Format SD card to clean FAT32 filesystem
    bool formatCard();

    // Incident list & loading for on-device charting
    uint8_t getIncidentCount() const { return incidentCount_; }
    const IncidentMeta* getIncidentMeta(uint8_t index) const;
    bool loadIncidentData(uint8_t index, IncidentDataPoint* outPoints, uint16_t maxPoints, uint16_t* actualCount);

    // Trigger an incident manually or from threshold breach
    void triggerFreezeFrame(const char* reason);

private:
    SdLogger();

    void checkCardStatus();
    void recordRingBuffer(const VehicleData& data);
    void checkTriggers(const VehicleData& data);
    void flushIncident(const char* reason);
    void scanIncidentFiles();

    bool cardInserted_ = false;
    bool cardFormatted_ = false;
    uint64_t totalBytes_ = 0;
    uint64_t freeBytes_ = 0;
    uint32_t lastCheckMs_ = 0;
    uint32_t lastSampleMs_ = 0;

    // 30-second circular ring buffer in PSRAM/heap (10 Hz = 300 samples)
    static constexpr uint16_t RING_BUFFER_SIZE = 300;
    IncidentDataPoint ringBuffer_[RING_BUFFER_SIZE];
    uint16_t ringHead_ = 0;
    uint16_t ringCount_ = 0;

    // Incident tracking state
    bool incidentTriggered_ = false;
    uint16_t postTriggerSamplesRemaining_ = 0;
    char pendingTriggerReason_[32] = {};

    // Detected on-disk incidents (up to 16 recent incidents)
    static constexpr uint8_t MAX_INCIDENTS = 16;
    IncidentMeta incidents_[MAX_INCIDENTS];
    uint8_t incidentCount_ = 0;

    // Trigger threshold trackers
    uint8_t prevDtcCount_ = 0;
    uint32_t prevMisfiresTotal_ = 0;
};
