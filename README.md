# 🏎️ Mazda MX-5 Miata (ND2) Digital Telemetry Dashboard

[![ESP32-S3](https://img.shields.io/badge/Hardware-ESP32--S3-orange.svg)](https://www.waveshare.com/esp32-s3-touch-lcd-3.5b.htm)
[![Android](https://img.shields.io/badge/Platform-Android%20App-green.svg)](https://developer.android.com)
[![LVGL](https://img.shields.io/badge/UI%20Engine-LVGL%209.5-blue.svg)](https://lvgl.io)
[![License](https://img.shields.io/badge/License-MIT-purple.svg)](LICENSE)

An open-source, automotive digital gauge cluster and telemetry system built specifically for the **Mazda MX-5 Miata (ND2 / SkyActiv-G 2.0L)**. 

This repository provides **two complete, full-featured implementations**:
1. 📟 **Dedicated Physical Display Firmware**: Built for the **Waveshare ESP32-S3-Touch-LCD-3.5B** ($480 \times 320$ capacitive touch, Bluetooth Low Energy 5.0, PSRAM, MicroSD logging).
2. 📱 **Standalone Android Application (`OBD2Android`)**: Native Android Studio app (Kotlin + C++ LVGL engine) for smartphones, tablets, or Android head units.

---

## 📸 Dashboard Screens & Capabilities

Designed from the ground up for sports car cockpits with **3-foot driving glanceability**, sunlight anti-glare high contrast, and smooth 40+ Hz speed response.

### 🏁 Driving & Track Suite (Screens 0–5)
* **Screen 0: Hero Speedometer**: 120px hero speed digits, active gear indicator in signature Soul Red frame, live Tachometer arc, Fuel %, Ambient temperature, and slide-in critical alarm banner (TPMS drop / coolant overheat).
* **Screen 1: TPMS & Tire Temperatures**: 3-column layout with ND2 RF silhouette, individual FL / FR / RL / RR tire pressures in PSI and tire temperatures in °F with instant green/amber/red status beacons.
* **Screen 2: Engine Tachometer Cluster**: Large 7,500 RPM tachometer with redline dots, plus live Engine Load %, Throttle Valve %, Fuel %, and Battery Volts.
* **Screen 3: Temperatures & Fluid Health**: Coolant (ECT), Engine Oil Temp (proprietary SkyActiv DID), Intake Air Temp (IAT), and Alternator/Battery Voltage.
* **Screen 4: Track Dynamics & 0–60**: Automated 0–60 MPH timer with trigger detection & best-run memory, live estimated Horsepower & Torque (lb-ft) dyno output, and dedicated dual Throttle vs Brake pedal response bars.
* **Screen 5: Trip & Fuel Economy**: Hero Instant MPG arc gauge + 4-card matrix (Trip Average MPG, Cruising Range, Trip Distance, Fuel Tank Level %).

### 🔧 Diagnostic & Service Suite (Screens 6–16)
* **Screen 6: Live DTC Scanner**: Read active & pending Diagnostic Trouble Codes with clear-text descriptions, freeze frame parameters, and one-tap ECU DTC erase.
* **Screens 8–12: Advanced Telemetry Dashboards**:
  * **Fuel & Air Trims**: STFT, LTFT, High-Pressure Fuel Rail PSI (direct injection 500–2,900 PSI), and Wideband AFR / Lambda.
  * **Cylinder Health & Timing**: Ignition advance angle (°BTDC) and cylinder diagnostics.
  * **Chassis & Steering**: Steering Angle Sensor (SAS) degrees.
  * **Smog / I/M Readiness**: CAT, EVAP, O2, EGR monitor completion status.
  * **SD Card Blackbox Logger**: High-frequency CSV logging to MicroSD with incident tagging and storage management.
* **Screen 13: Display Settings**: Theme modes (Auto / Day / Night), screen brightness control, and unit toggles (US / Metric).
* **Screen 14: Wireless Scanner Pairing**: Auto-scans all nearby BLE OBD-II adapters (`vLinker`, `OBDLink`, `VEEPEAK`, `iCar Pro`), tap-to-pair, and NVS persistent auto-reconnect.

---

## 🛠️ Hardware Requirements (Physical Screen Build)

To build the standalone dash display for your car:

| Component | Description | Est. Cost |
| :--- | :--- | :--- |
| **Display Unit** | [Waveshare ESP32-S3-Touch-LCD-3.5B](https://www.waveshare.com/esp32-s3-touch-lcd-3.5b.htm) ($480 \times 320$, QSPI IPS, Capacitive Touch, Octal PSRAM) | ~$25–$30 |
| **OBD-II Scanner** | **Vgate vLinker MS** (BLE 5.0, MS-CAN support) or **OBDLink CX** | ~$35–$50 |
| **Power / Cable** | Right-angle USB-C cable to 12V accessory adapter or switched 12V OBD tap | ~$8 |
| **Storage (Opt.)**| MicroSD Card (FAT32, 8GB–64GB) for blackbox telemetry CSV logging | ~$7 |

---

## 🚀 Getting Started

### 1. Standalone ESP32-S3 Firmware

The ESP32-S3 firmware is built with [PlatformIO](https://platformio.org/).

#### Prerequisites
- Install [Visual Studio Code](https://code.visualstudio.com/) + [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode) (or the PlatformIO CLI).

#### Build & Flash
```bash
# 1. Clone this repository
git clone https://github.com/<your-username>/mx5-dash.git
cd mx5-dash

# 2. Flash to connected Waveshare ESP32-S3 board
pio run -e esp32s3_touch_lcd_3_5b -t upload

# 3. (Optional) Run the native desktop preview simulator on your PC
pio run -e native_preview
.pio/build/native_preview/program
```

---

### 2. Standalone Android Application (`OBD2Android`)

The Android application is located in [`OBD2Android/`](file:///home/todd/Documents/GitHub/mx5-dash/OBD2Android).

#### Prerequisites
- Android Studio Ladybug or newer / JDK 17+ / Android NDK 27+.
- Android Device running Android 8.0+ (API 26+) with Bluetooth enabled.

#### Build & Install
```bash
cd OBD2Android

# Build Debug APK
./gradlew assembleDebug

# Install directly to your connected Android phone via ADB
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

---

## 🔬 Technical Details: SkyActiv-G 2.0L Telemetry

For comprehensive CAN IDs, PID formulas, and timing diagrams, see **[`ND2_TELEMETRY_NOTES.md`](ND2_TELEMETRY_NOTES.md)**.

### Multi-Module CAN Interleaving Schedule
Standard ELM327 adapters operate in half-duplex, which normally limits polling to 3–4 Hz if querying 10+ sensors sequentially. 

This project solves latency using an **interleaved transaction scheduler**:
- **Speed (`010D`)** is queried on **every alternate transaction**, maintaining a silky smooth **~40 Hz speed refresh rate**.
- Secondary metrics (RPM, Throttle, Coolant, Load, Oil Temp) cycle on the in-between ticks with the Engine PCM (`Header 7E0`).
- Every 25–30 seconds, the engine briefly sends `ATSH 720` to query the **Body Control Module (BCM / Instrument Cluster)** for all 4 tire pressures/temperatures (`222A05`..`08`) and fuel tank level % (`222A26`), then seamlessly restores `ATSH 7E0`.

---

## 📜 License

This project is licensed under the MIT License — see the [LICENSE](LICENSE) file for details.

Developed with ❤️ for the Mazda MX-5 Miata community!
