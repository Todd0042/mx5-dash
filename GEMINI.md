# MX-5 Dash Project Rules & Engineering Guidelines

This document defines the core architecture, operational rules, and constraints for developing the **MX-5 ND2 Digital Instrument Dash** (both the ESP32-S3 Waveshare 3.5B physical display and the Android OBD-II companion application).

---

## Rule 1: Mandatory User Confirmation & Informed Decision Context
Whenever you are about to utilize, apply, or enforce any rule or constraint defined in this document for a given user request:
1. **Explicitly Ask the User**: Inquire if the user wants that specific rule to apply to the current request.
2. **Provide Full Context**: Give all relevant details needed to make an informed decision, including:
   - Which rule applies and why it was triggered.
   - The exact impact, architectural implications, or code changes involved.
   - The alternative approach if the rule is bypassed or modified for that task.

---

## Rule 2: Harmonized Dual-Platform User Experience & Form-Factor Adaptation
The ESP32-S3 dedicated instrument display and the Android companion application are fundamentally different hardware architectures (differing in physical screen dimensions, aspect ratios, and Bluetooth connection stacks). They must deliver a **harmonized, seamless user experience** that is as similar as possible while appropriately adapting to each hardware platform:
- **Display Geometry & Layout Adaptation**:
  - **ESP32-S3 3.5" Panel**: 480×320 (3:2 aspect ratio), compact dedicated dashboard display rendered via FreeRTOS / LVGL.
  - **Android Companion App**: Modern smartphone widescreen format (e.g. 19.5:9 / 20:9), rendered via Android NDK / LVGL + Kotlin.
  - Layouts, widget margins, and arc radii should naturally adapt to the respective aspect ratio without awkward clipping or squishing, while maintaining consistent typography, colors (Mazda OEM red/amber/dark palette), and gauge styling.
- **Connection Stack Independence**:
  - **ESP32-S3**: Communicates over Bluetooth Low Energy (BLE 5.0 / NimBLE) on Core 0.
  - **Android**: Communicates over Bluetooth Classic RFCOMM (SPP) with foreground service persistence.
- **Logical Feature & Navigation Alignment**:
  - Both platforms share the identical screen carousel sequence:
    - `0`: **`SCREEN_SPEED`** (Hero Speedometer, RPM, Fuel Level)
    - `1`: **`SCREEN_TPMS`** (4-Corner Tire Pressures & Temperatures)
    - `2`: **`SCREEN_TEMPS`** (Coolant Temp, Oil Temp, Intake Air Temp, Battery Volts)
    - `3`: **`SCREEN_DIAG`** (Diagnostic Hub, DTC Fault Codes, Fuel Trims)
    - `4`: **`SCREEN_TRACK` / "FAFO"** (0-60 MPH Timer, Horsepower, Torque, Braking Dynamics)
    - `5`: **`SCREEN_RPM`** (High-Refresh Engine Tachometer)
    - `6`: **`SCREEN_TRIP`** (Trip Distance, Average & Instantaneous MPG, Range)
    - `7`: **`SCREEN_MENU`** (Swipe-Up Quick Launcher)
    - `8..12`: **Diagnostic Sub-Dashboards** (Fuel Trims, Misfires, Chassis, I/M Smog, Incident Logs)
  - Both platforms share the same transmission state machine (`trans_auto`), PRND decoding, and telemetry math.

---

## Rule 3: CAN Bus Routing & Protocol Header Boundaries
The Mazda SkyActiv-G (ND2) network segregates telemetry across multiple ECU headers over high-speed CAN (HS-CAN) and mid-speed CAN (MS-CAN via Gateway):
1. **PCM (Header `7E0` / `ATSH 7E0`)**:
   - Standard SAE J1979 Mode 01 PIDs: Speed (`010D`), RPM (`010C`), Fuel Level (`012F`), Coolant Temp (`0105`), Intake Air Temp (`010F`), Battery Volts (`0142`), Throttle (`0111`), Load (`0104`), STFT (`0106`), LTFT (`0107`), Rail Pressure (`0123`), AFR (`0124`), Spark Advance (`010E`).
   - Ambient Air Temperature: Must be polled via Mode 01 PID `0146` (`4146` $\rightarrow A - 40\ ^\circ\text{C}$).
   - SkyActiv Engine Oil Temperature: Mode 22 DID `221310` (`621310` $\rightarrow A - 40\ ^\circ\text{C}$).
2. **BCM & Instrument Cluster (Header `720` / `ATSH 720`)**:
   - 4-Corner TPMS DIDs: `222A05` (FL), `222A06` (FR), `222A07` (RL), `222A08` (RR).
   - Transmission PRND Selector DID: `222A27` (`1`=P, `2`=R, `3`=N, `4`=D, `5`=M).
3. **Header Isolation Guarantee**:
   - Whenever switching to Header `720` (`ATSH 720`) for TPMS or PRND queries, the polling engine **MUST immediately restore** Header `7E0` (`ATSH 7E0`) before continuing engine telemetry queries.

---

## Rule 4: Clean Initial Baselines & Signal Normalization (No Realistic Placeholders)
To ensure testing transparency and avoid masking missing OBD responses:
- **Default / Initial Values**: All telemetry metrics must initialize to `0`, `0.0`, `--`, or `-` on startup. Never use realistic default numbers (e.g. 200° or 70 MPH).
- **Throttle Position Normalization**: Mazda ND2 idle throttle reports ~13% raw pedal count. Normalize so $\le 13\%$ is treated as $0\%$, scaled via:
  $$\text{effectiveThrottlePct} = \min\left(100, \frac{(\text{rawPct} - 13) \times 100}{87}\right)$$
- **Telemetry Simulation**: The simulation driver (`TelemetrySimulator.kt` / `sim_main.cpp`) must only run when explicitly in mock/preview mode, never overwriting live OBD data.

---

## Rule 5: Transmission Architecture & Shifter State Machine
The dashboard supports both 6AT Automatic (SkyActiv-Drive) and 6MT Manual gearboxes:
- The user's transmission preference is toggled in Settings (`[AUTO (6AT)]` vs `[MANUAL (6MT)]`) and persisted in NVS / SharedPreferences (`trans_auto`).
- **Automatic (6AT) Mode**:
  - In Reverse (`tcmPrnd == 'R'`), display **`'R'`** (not gear `1`).
  - In Park (`tcmPrnd == 'P'`) or stopped at idle ($< 2\text{ km/h}$), display **`'P'`**.
  - In Neutral (`tcmPrnd == 'N'`), display **`'N'`**.
  - In Drive / Forward motion, calculate gear ratios `1` through `6` from $\text{RPM} / \text{Speed}$.
- **Manual (6MT) Mode**:
  - Stationary idle ($< 3\text{ km/h}$) displays **`'N'`**.
  - Moving forward calculates gear ratios `1` through `6`.

---

## Rule 6: View-Driven Polling & Socket Timeout Resilience
The ELM327 / vLinker Bluetooth dongle is single-command and half-duplex (prompt-terminated with `>`):
- **View-Driven Polling**: High-speed bandwidth must be focused exclusively on what is visible on the current active screen (e.g., fast RPM and Load on Tachometer; 4-corner pressures on TPMS).
- **Safety Sweep**: Non-active critical signals (TPMS, Coolant, Oil Temp, PRND, Ambient Temp) are queried on a low-overhead background rotation (~25s interval).
- **Socket Timeouts**: Serial/Bluetooth reads must enforce strict ~180–200ms socket read timeouts to prevent thread lockups or frame stutter on lost packets.

---

## Rule 7: Multi-Target Build Verification & Hardware Deployment
Any modification to telemetry decoding, UI widgets, settings, or layout must be verified across all build targets:
1. **PlatformIO (ESP32-S3 + Native Preview)**:
   ```bash
   ~/.platformio/penv/bin/pio run -e native_preview -e esp32s3_touch_lcd_3_5b
   ```
2. **Android Debug APK**:
   ```bash
   cd OBD2Android && ./gradlew assembleDebug
   ```
3. **Device Deployment**: When an Android device is attached via ADB, install and verify the build.
