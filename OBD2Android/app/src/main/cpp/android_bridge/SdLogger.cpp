#include "SdLogger.h"
#include "VehicleData.h"
#include <cstring>
#include <cmath>

static SdLogger* sInstance = nullptr;

SdLogger& SdLogger::instance() {
    if (!sInstance) sInstance = new SdLogger();
    return *sInstance;
}

SdLogger::SdLogger() {
    cardInserted_ = true;
    cardFormatted_ = true;
    totalBytes_ = 32ull * 1024 * 1024 * 1024 / 1024;
    freeBytes_ = 28ull * 1024 * 1024 * 1024 / 1024;
    incidentCount_ = 0;
}

bool SdLogger::begin() {
    cardInserted_ = true;
    cardFormatted_ = true;
    totalBytes_ = 32ull * 1024 * 1024 * 1024 / 1024;
    freeBytes_ = 28ull * 1024 * 1024 * 1024 / 1024;
    incidentCount_ = 3;

    // Populate mock incidents so Sub-Screen 12 chart engine is fully testable on Android
    strncpy(incidents_[0].filename, "INC_001_KNOCK_EVENT.CSV", sizeof(incidents_[0].filename) - 1);
    strncpy(incidents_[0].title, "KNOCK RETARD: -4.2° • CYL 3", sizeof(incidents_[0].title) - 1);
    strncpy(incidents_[0].triggerReason, "KNOCK_DETECTED", sizeof(incidents_[0].triggerReason) - 1);
    strncpy(incidents_[0].dateStr, "2026-09-02 10:15", sizeof(incidents_[0].dateStr) - 1);
    incidents_[0].pointCount = 60;
    incidents_[0].peakKnock = -4.2f;
    incidents_[0].maxStft = 12.5f;
    incidents_[0].minAfr = 11.2f;
    incidents_[0].maxAfr = 14.7f;
    incidents_[0].peakRpm = 6800.0f;

    strncpy(incidents_[1].filename, "INC_002_LEAN_SURGE.CSV", sizeof(incidents_[1].filename) - 1);
    strncpy(incidents_[1].title, "LEAN SPIKE: 16.8 AFR • WOT", sizeof(incidents_[1].title) - 1);
    strncpy(incidents_[1].triggerReason, "LEAN_SURGE", sizeof(incidents_[1].triggerReason) - 1);
    strncpy(incidents_[1].dateStr, "2026-09-02 09:42", sizeof(incidents_[1].dateStr) - 1);
    incidents_[1].pointCount = 60;
    incidents_[1].peakKnock = -1.5f;
    incidents_[1].maxStft = 18.0f;
    incidents_[1].minAfr = 13.0f;
    incidents_[1].maxAfr = 16.8f;
    incidents_[1].peakRpm = 5400.0f;

    strncpy(incidents_[2].filename, "INC_003_OVERHEAT.CSV", sizeof(incidents_[2].filename) - 1);
    strncpy(incidents_[2].title, "COOLANT TEMP: 232°F / 111°C", sizeof(incidents_[2].title) - 1);
    strncpy(incidents_[2].triggerReason, "HIGH_COOLANT_TEMP", sizeof(incidents_[2].triggerReason) - 1);
    strncpy(incidents_[2].dateStr, "2026-09-02 08:30", sizeof(incidents_[2].dateStr) - 1);
    incidents_[2].pointCount = 60;
    incidents_[2].peakKnock = -0.5f;
    incidents_[2].maxStft = 5.0f;
    incidents_[2].minAfr = 12.8f;
    incidents_[2].maxAfr = 14.9f;
    incidents_[2].peakRpm = 4200.0f;

    return true;
}

void SdLogger::update(const VehicleData& /*data*/) {
}

bool SdLogger::formatCard() {
    incidentCount_ = 0;
    return true;
}

const IncidentMeta* SdLogger::getIncidentMeta(uint8_t index) const {
    if (index >= incidentCount_ || incidentCount_ == 0) return nullptr;
    return &incidents_[index];
}

bool SdLogger::loadIncidentData(uint8_t index, IncidentDataPoint* outPoints,
                                uint16_t maxPoints, uint16_t* actualCount) {
    if (!outPoints || maxPoints == 0 || index >= incidentCount_) {
        if (actualCount) *actualCount = 0;
        return false;
    }
    uint16_t count = (maxPoints < 60) ? maxPoints : 60;
    for (uint16_t i = 0; i < count; i++) {
        float t = -15.0f + (float)i * 0.5f; // -15s to +15s
        outPoints[i].timeOffsetSec = t;
        float progress = (float)i / (float)count;

        if (index == 0) {
            // Knock incident
            outPoints[i].rpm = 3000.0f + 3800.0f * progress;
            outPoints[i].stft = 4.0f + 8.5f * sinf(progress * 3.14f);
            outPoints[i].ltft = 2.0f;
            outPoints[i].afr = 14.7f - 2.5f * progress;
            outPoints[i].knockRetard = (i >= 25 && i <= 35) ? -4.2f * sinf((float)(i - 25) / 10.0f * 3.14f) : 0.0f;
            outPoints[i].railPsi = 1800.0f + 400.0f * progress;
            outPoints[i].coolantC = 92.0f;
            outPoints[i].misfires = (i >= 30) ? 1 : 0;
        } else if (index == 1) {
            // Lean spike incident
            outPoints[i].rpm = 2500.0f + 2900.0f * progress;
            outPoints[i].stft = 18.0f * progress;
            outPoints[i].ltft = 5.0f;
            outPoints[i].afr = (i >= 20 && i <= 40) ? 16.8f : 14.7f;
            outPoints[i].knockRetard = -1.5f;
            outPoints[i].railPsi = 1500.0f;
            outPoints[i].coolantC = 89.0f;
            outPoints[i].misfires = 0;
        } else {
            // Overheat incident
            outPoints[i].rpm = 2000.0f + 2200.0f * progress;
            outPoints[i].stft = 3.0f;
            outPoints[i].ltft = 1.0f;
            outPoints[i].afr = 14.7f;
            outPoints[i].knockRetard = 0.0f;
            outPoints[i].railPsi = 1600.0f;
            outPoints[i].coolantC = 100.0f + 11.0f * progress;
            outPoints[i].misfires = 0;
        }
    }
    if (actualCount) *actualCount = count;
    return true;
}

void SdLogger::triggerFreezeFrame(const char* /*reason*/) {
}
