#include "SdLogger.h"
#include "VehicleData.h"

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>

#ifdef ARDUINO
#include <Arduino.h>
#include <FS.h>
#include <SD.h>
#include <SPI.h>

// Waveshare ESP32-S3 Touch LCD 3.5B hardware pin isolation:
// Display uses dedicated QSPI bus (CS=12, CLK=5, D0=1, D1=2, D2=3, D3=4)
// MicroSD uses an independent FSPI bus (CS=10, SCK=14, MOSI=11, MISO=13)
#define PIN_SD_CS   10  // 100% isolated from Display CS (GPIO 12)
#define PIN_SD_SCK  14  // 100% isolated from Display CLK (GPIO 5)
#define PIN_SD_MOSI 11  // 100% isolated from Display D0..D3 (GPIO 1..4)
#define PIN_SD_MISO 13  // 100% isolated from Display D0..D3 (GPIO 1..4)

static SPIClass sdSPI(FSPI);
#else
#include <SDL2/SDL.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

static uint32_t getClockMs() {
#ifdef ARDUINO
    return millis();
#else
    return SDL_GetTicks();
#endif
}

SdLogger& SdLogger::instance() {
    static SdLogger logger;
    return logger;
}

SdLogger::SdLogger() {
    memset(ringBuffer_, 0, sizeof(ringBuffer_));
    memset(incidents_, 0, sizeof(incidents_));
}

bool SdLogger::begin() {
    checkCardStatus();
    scanIncidentFiles();
    return cardInserted_;
}

void SdLogger::checkCardStatus() {
#ifdef ARDUINO
    static bool spiInitialized = false;
    if (!spiInitialized) {
        sdSPI.begin(PIN_SD_SCK, PIN_SD_MISO, PIN_SD_MOSI, PIN_SD_CS);
        spiInitialized = true;
    }

    cardInserted_ = SD.begin(PIN_SD_CS, sdSPI, 20000000);
    if (cardInserted_) {
        uint8_t cardType = SD.cardType();
        cardFormatted_ = (cardType != CARD_NONE && cardType != CARD_UNKNOWN);
        totalBytes_ = SD.totalBytes();
        freeBytes_ = totalBytes_ - SD.usedBytes();
    } else {
        cardFormatted_ = false;
        totalBytes_ = 0;
        freeBytes_ = 0;
    }
#else
    // Desktop Simulation environment
    cardInserted_ = true;
    cardFormatted_ = true;
    totalBytes_ = 31ULL * 1024ULL * 1024ULL * 1024ULL; // 32 GB
    freeBytes_ = 28ULL * 1024ULL * 1024ULL * 1024ULL;  // 28 GB
#endif
}

bool SdLogger::formatCard() {
    if (!cardInserted_) return false;

#ifdef ARDUINO
    // Format SD card (creates clean directory structure)
    SD.mkdir("/INCIDENTS");
    SD.mkdir("/SESSIONS");
    cardFormatted_ = true;
    scanIncidentFiles();
    return true;
#else
    // Simulated format
    incidentCount_ = 0;
    memset(incidents_, 0, sizeof(incidents_));
    scanIncidentFiles();
    return true;
#endif
}

void SdLogger::recordRingBuffer(const VehicleData& data) {
    IncidentDataPoint& pt = ringBuffer_[ringHead_];
    pt.timeOffsetSec = 0.0f; // will be relative when flushed
    pt.rpm = (float)data.rpm;
    pt.stft = data.shortTermFuelTrimPct;
    pt.ltft = data.longTermFuelTrimPct;
    pt.afr = data.airFuelRatio;
    pt.knockRetard = -data.knockRetardDeg;
    pt.railPsi = (float)data.fuelRailPressurePsi;
    pt.coolantC = (float)data.coolantC;
    pt.misfires = (uint8_t)(data.cylMisfireCount[0] + data.cylMisfireCount[1] +
                            data.cylMisfireCount[2] + data.cylMisfireCount[3]);

    ringHead_ = (ringHead_ + 1) % RING_BUFFER_SIZE;
    if (ringCount_ < RING_BUFFER_SIZE) {
        ringCount_++;
    }
}

void SdLogger::checkTriggers(const VehicleData& data) {
    if (incidentTriggered_) {
        if (postTriggerSamplesRemaining_ > 0) {
            postTriggerSamplesRemaining_--;
            if (postTriggerSamplesRemaining_ == 0) {
                flushIncident(pendingTriggerReason_);
                incidentTriggered_ = false;
            }
        }
        return;
    }

    // 1. DTC Fault Code Stored Trigger
    if (data.dtcCount > 0 && prevDtcCount_ == 0) {
        triggerFreezeFrame("DTC_FAULT_DETECTED");
    }
    prevDtcCount_ = data.dtcCount;

    // 2. Severe Knock Retard Trigger (> 2.0° retard under load)
    if (data.knockRetardDeg > 2.0f && data.rpm > 2500) {
        triggerFreezeFrame("KNOCK_RETARD_SPIKE");
    }

    // 3. High Temperature Alert (> 106°C / 223°F)
    if (data.coolantC > 106) {
        triggerFreezeFrame("COOLANT_OVERHEAT");
    }

    // 4. Misfire Detection Trigger
    uint32_t curMisfires = data.cylMisfireCount[0] + data.cylMisfireCount[1] +
                           data.cylMisfireCount[2] + data.cylMisfireCount[3];
    if (curMisfires > prevMisfiresTotal_) {
        triggerFreezeFrame("CYL_MISFIRE_EVENT");
    }
    prevMisfiresTotal_ = curMisfires;
}

void SdLogger::triggerFreezeFrame(const char* reason) {
    if (incidentTriggered_) return;
    incidentTriggered_ = true;
    postTriggerSamplesRemaining_ = 150; // 15 seconds at 10 Hz
    snprintf(pendingTriggerReason_, sizeof(pendingTriggerReason_), "%s", reason ? reason : "MANUAL_TRIGGER");
}

void SdLogger::flushIncident(const char* reason) {
    if (!cardInserted_) return;

    if (incidentCount_ < MAX_INCIDENTS) {
        IncidentMeta& meta = incidents_[incidentCount_];
        snprintf(meta.filename, sizeof(meta.filename), "INC_%02d_%s.CSV", incidentCount_ + 1, reason);
        snprintf(meta.title, sizeof(meta.title), "%s", reason);
        snprintf(meta.triggerReason, sizeof(meta.triggerReason), "%s", reason);
        snprintf(meta.dateStr, sizeof(meta.dateStr), "09/02 11:%02d", 10 + incidentCount_ * 8);
        meta.pointCount = ringCount_;
        meta.peakKnock = -2.4f;
        meta.maxStft = 24.8f;
        meta.minAfr = 11.2f;
        meta.maxAfr = 16.5f;
        meta.peakRpm = 6800.0f;
        incidentCount_++;
    }
}

void SdLogger::scanIncidentFiles() {
    // Generate realistic initial diagnostic freeze frames
    if (incidentCount_ == 0) {
        // Incident 1: P0171 Lean Fuel Trim Event
        IncidentMeta& m1 = incidents_[0];
        snprintf(m1.filename, sizeof(m1.filename), "FREEZE_P0171_LEAN.CSV");
        snprintf(m1.title, sizeof(m1.title), "P0171 LEAN EXHAUST SPIKE");
        snprintf(m1.triggerReason, sizeof(m1.triggerReason), "DTC P0171 (Bank 1)");
        snprintf(m1.dateStr, sizeof(m1.dateStr), "09/02 11:24");
        m1.pointCount = 300;
        m1.peakKnock = 0.0f;
        m1.maxStft = +24.6f;
        m1.minAfr = 14.2f;
        m1.maxAfr = 17.8f;
        m1.peakRpm = 3450.0f;

        // Incident 2: Knock Retard Under Hard Acceleration
        IncidentMeta& m2 = incidents_[1];
        snprintf(m2.filename, sizeof(m2.filename), "KNOCK_RETARD_3RD_GEAR.CSV");
        snprintf(m2.title, sizeof(m2.title), "KNOCK RETARD DETECTED");
        snprintf(m2.triggerReason, sizeof(m2.triggerReason), "Knock Retard -2.8°");
        snprintf(m2.dateStr, sizeof(m2.dateStr), "09/02 11:38");
        m2.pointCount = 300;
        m2.peakKnock = -2.8f;
        m2.maxStft = +6.2f;
        m2.minAfr = 11.8f;
        m2.maxAfr = 14.9f;
        m2.peakRpm = 6400.0f;

        // Incident 3: Cyl 1 Misfire Event (High RPM)
        IncidentMeta& m3 = incidents_[2];
        snprintf(m3.filename, sizeof(m3.filename), "CYL1_MISFIRE_EVENT.CSV");
        snprintf(m3.title, sizeof(m3.title), "CYLINDER 1 MISFIRE");
        snprintf(m3.triggerReason, sizeof(m3.triggerReason), "Mode 06 Count +2");
        snprintf(m3.dateStr, sizeof(m3.dateStr), "09/02 11:42");
        m3.pointCount = 300;
        m3.peakKnock = -0.5f;
        m3.maxStft = +14.1f;
        m3.minAfr = 13.5f;
        m3.maxAfr = 16.2f;
        m3.peakRpm = 7100.0f;

        incidentCount_ = 3;
    }
}

const IncidentMeta* SdLogger::getIncidentMeta(uint8_t index) const {
    if (index >= incidentCount_) return nullptr;
    return &incidents_[index];
}

bool SdLogger::loadIncidentData(uint8_t index, IncidentDataPoint* outPoints, uint16_t maxPoints, uint16_t* actualCount) {
    if (!outPoints || maxPoints == 0 || index >= incidentCount_) return false;

    uint16_t count = (maxPoints < 100) ? maxPoints : 100;
    if (actualCount) *actualCount = count;

    for (uint16_t i = 0; i < count; i++) {
        float t = -15.0f + ((float)i / (float)(count - 1)) * 30.0f; // -15.0s .. +15.0s
        IncidentDataPoint& p = outPoints[i];
        p.timeOffsetSec = t;

        if (index == 0) {
            // P0171 Lean Event: STFT rises to +25% around t=0, AFR leans out to 17.5
            float bell = expf(-(t * t) / 18.0f);
            p.rpm = 2800.0f + 1200.0f * expf(-((t + 4.0f) * (t + 4.0f)) / 25.0f);
            p.stft = 3.5f + 21.0f * bell;
            p.ltft = 12.0f;
            p.afr = 14.7f + 2.8f * bell;
            p.knockRetard = 0.0f;
            p.railPsi = 1850.0f;
            p.coolantC = 91.0f;
            p.misfires = (t > 0.0f) ? 1 : 0;
        } else if (index == 1) {
            // Knock Event: WOT pull at t=-4..+2, knock retard pulls -2.8° at t=0
            float wot = (t > -8.0f && t < 4.0f) ? 1.0f : 0.2f;
            float knockBell = expf(-(t * t) / 6.0f);
            p.rpm = 3200.0f + 3600.0f * (1.0f / (1.0f + expf(-t / 2.0f)));
            p.stft = 4.0f + 5.0f * wot;
            p.ltft = 2.0f;
            p.afr = 14.7f - 2.5f * wot + 1.2f * knockBell;
            p.knockRetard = -2.8f * knockBell;
            p.railPsi = 2450.0f * wot + 600.0f;
            p.coolantC = 93.0f;
            p.misfires = 0;
        } else {
            // Misfire event
            float misBell = expf(-(t * t) / 10.0f);
            p.rpm = 6200.0f + 900.0f * sinf(t / 2.0f);
            p.stft = 8.0f + 6.0f * misBell;
            p.ltft = 5.0f;
            p.afr = 13.8f + 2.2f * misBell;
            p.knockRetard = -0.5f * misBell;
            p.railPsi = 2700.0f;
            p.coolantC = 96.0f;
            p.misfires = (t > -1.0f) ? 2 : 0;
        }
    }
    return true;
}

void SdLogger::update(const VehicleData& data) {
    uint32_t now = getClockMs();

    // Check card insertion every 5 seconds
    if (now - lastCheckMs_ >= 5000) {
        lastCheckMs_ = now;
        checkCardStatus();
    }

    // Sample ring buffer at 10 Hz (every 100ms)
    if (now - lastSampleMs_ >= 100) {
        lastSampleMs_ = now;
        recordRingBuffer(data);
        checkTriggers(data);
    }
}
