# Mazda MX-5 Miata (ND2 2019–2023) OBD-II & CAN Telemetry Reference

This document is the authoritative engineering and implementation guide for the **Mazda MX-5 Miata ND2 (SkyActiv-G 2.0L)** telemetry system across both the **Android Application (`OBD2Android`)** and the **ESP32-S3 Physical Display Firmware (`lib/` & `src/`)**.

---

## 1. Complete Sensor & DID Availability Matrix

| Telemetry Channel | Available on ND2? | OBD Mode / DID | Module Header | Raw Formula / Conversion | Units | Engineering Notes |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: |
| **Vehicle Speed** | ✅ **YES** | `010D` (PCM) | `7E0` (PCM) | `A` | km/h (or mph $\times 0.621371$) | Highest priority PID. Polled at ~35–45 Hz. |
| **Engine RPM** | ✅ **YES** | `010C` (PCM) | `7E0` (PCM) | `((A * 256) + B) / 4.0` | RPM | High priority PID (~20–30 Hz). Redline 7,500 RPM. |
| **Throttle Position (TPS)** | ✅ **YES** | `0111` (PCM) | `7E0` (PCM) | $T_{\text{raw}} = \frac{A \times 100}{255}$, $T_{\text{eff}} = \text{clamp}\left(\frac{(T_{\text{raw}} - 13) \times 100}{87}, 0, 100\right)$ | % | Calibrated: 13% raw idle is mapped to 0% baseline throttle. |
| **Calculated Engine Load** | ✅ **YES** | `0104` (PCM) | `7E0` (PCM) | `(A * 100) / 255.0` | % | Absolute engine load. |
| **Coolant Temp (ECT)** | ✅ **YES** | `0105` (PCM) | `7E0` (PCM) | `A - 40` | °C (or °F $= C \times 1.8 + 32$) | Nominal: 185–215°F. Overheat threshold: $\ge 226^\circ\text{F}$. |
| **Intake Air Temp (IAT)** | ✅ **YES** | `010F` (PCM) | `7E0` (PCM) | `A - 40` | °C | Pre-manifold intake charge air temperature. |
| **Engine Oil Temp** | ✅ **YES** | `221310` (PCM DID) | `7E0` (PCM) | `A - 40` | °C | SkyActiv proprietary DID. Nominal: 190–235°F. Overheat $\ge 252^\circ\text{F}$. |
| **Battery / Module Volts** | ✅ **YES** | `0142` / `ATRV` | `7E0` / Adapter | `((A * 256) + B) / 1000.0` | Volts | Alternator target: 13.8–14.6V. Low bat threshold: $< 11.6\text{V}$. |
| **Short Term Fuel Trim (STFT)** | ✅ **YES** | `0106` (PCM) | `7E0` (PCM) | `((A - 128) * 100) / 128.0` | % | Instantaneous closed-loop fuel correction. |
| **Long Term Fuel Trim (LTFT)** | ✅ **YES** | `0108` (PCM) | `7E0` (PCM) | `((A - 128) * 100) / 128.0` | % | Learned fuel trim map correction. |
| **High Pressure Fuel Rail (FRP)**| ✅ **YES** | `0123` (PCM) | `7E0` (PCM) | `((A * 256) + B) * 10` | kPa (or PSI $\times 0.145038$) | SkyActiv direct-injection fuel rail (500–2,900 PSI). |
| **Wideband Lambda / AFR** | ✅ **YES** | `0124` (PCM) | `7E0` (PCM) | `(((A * 256) + B) / 32768.0) * 14.7`| AFR | Direct linear wideband O2 ratio. Stoich = 14.7. |
| **Ignition Timing Advance** | ✅ **YES** | `010E` (PCM) | `7E0` (PCM) | `(A / 2.0) - 64.0` | Degrees BTDC | Cylinder 1 ignition spark advance. |
| **Tire Pressures (FL, FR, RL, RR)**| ✅ **YES** | `222A05`..`08` | `720` (BCM/Cluster) | `((A * 1373) / 1000) * 0.145038` | PSI | Mazda OEM TPMS sensors on Body Control Module. |
| **Tire Temperatures (4 Wheels)** | ✅ **YES** | `222A05`..`08` (Byte B)| `720` (BCM/Cluster) | `B - 40` | °C | Internal tire sensor temperature. |
| **Fuel Tank Level %** | ⚠️ **Special** | `222A26` / `22012F` | `720` (Instrument Cluster)| `(A * 100) / 255.0` | % | Fuel level float is wired to Cluster `720`, NOT PCM `7E0`. |
| **Ambient Air Temp** | ⚠️ **Special** | `220146` | `720` (Cluster) / `733` | `A - 40` | °C | Bumper thermistor is wired to Cluster / HVAC. |
| **Brake Pedal Pressure** | ⚠️ **Special** | Decel Calc or `760` | Dynamic or DSC Module | Decel: $\frac{\Delta v}{\Delta t} \times 100$ | % | Calculated from vehicle deceleration rate when throttle $= 0\%$. |
| **Steering Angle (SAS)** | ⚠️ **Special** | CAN ID `0x086` / `760` | Direct CAN / EPS Module | CAN byte unpack | Degrees | Broadcast on high-speed CAN bus ID `0x086`. |

---

## 2. Multi-Module Interleaved Polling Architecture

### The Half-Duplex ELM327 Constraint
Standard OBD-II queries require sequential request-response handshakes. Round-robin polling of 10+ PIDs slows speed updates down to an unacceptable 3–4 Hz. 

### The Solution: Tiered Interleaving Schedule
Speed (`010D`) is polled on **every single transaction**, alternating with one secondary metric.

```mermaid
graph TD
    subgraph Engine PCM (Header 7E0) - Fast 32-Tick Cycle (~40 Hz Speed)
        S1[010D: Speed] --> R1[010C: RPM]
        R1 --> S2[010D: Speed]
        S2 --> T1[0111: Throttle]
        T1 --> S3[010D: Speed]
        S3 --> C1[0105: Coolant Temp]
        C1 --> S4[010D: Speed]
        S4 --> L1[0104: Engine Load]
        L1 --> S5[010D: Speed]
        S5 --> O1[221310: Oil Temp]
        O1 --> S6[010D: Speed]
        S6 --> D1[0106/0123/0124/010E: Diag]
    end

    subgraph Body Module (Header 720) - 25s Staggered Query
        B1[ATSH 720] --> B2[222A05: FL TPMS]
        B2 --> B3[222A06: FR TPMS]
        B3 --> B4[222A07: RL TPMS]
        B4 --> B5[222A08: RR TPMS]
        B5 --> B6[222A26: Fuel Level %]
        B6 --> B7[ATSH 7E0 Restore]
    end
```

### Staggered BCM Query Routine (Every 25–30 Seconds):
1. Send `ATSH 720` (Switch header to Instrument Cluster / BCM).
2. Query `222A05` (Front Left TPMS), `222A06` (Front Right TPMS), `222A07` (Rear Left TPMS), `222A08` (Rear Right TPMS).
3. Query `222A26` (Fuel Tank Level).
4. Send `ATSH 7E0` (Restore Engine PCM header immediately).
5. Resume fast Tier 1/Tier 2 speed interleaved cycle.

---

## 3. Dynamic Warning Architecture (Speedometer Screen 0)

### Behavioral Rules
1. **Hero Speedometer Protection**:
   * The left side of Screen 0 (`290x290` card housing the Big Speed Number, MPH label, and 6MT Gear indicator) is **never hidden, masked, or replaced**. It updates continuously at ~40 Hz.
2. **Normal State (All Systems Nominal)**:
   * Displays the standard layout on the right side:
     * **Tachometer Segmented Arc** (RPM)
     * **Fuel Tank Level Dial** (%)
     * **Ambient / Intake Air Temp Dial** (°F)
3. **Active Alert State**:
   * The auxiliary right-side dials are automatically hidden and replaced with the **Dynamic Alert Card**:
     * **Top Banner**: Bold background (`C_DANGER` red or `C_WARN` amber) with clean ASCII text: `[!] CRITICAL TIRE PRESSURE DROP • TAP TO DISMISS`.
     * **Tire Pressure Alert**: Triggered if any tire $< 26.0\text{ PSI}$. Displays top-down MX-5 RF visual, highlights compromised tire in red border with `21.8 PSI LOW`, and displays cold pressure reference (`29.0 PSI`).
     * **Engine Overheat Alert**: Coolant $\ge 226^\circ\text{F}$ or Oil $\ge 252^\circ\text{F}$. Displays live temperature readouts and pull-over guidance.
     * **Low Battery Alert**: Voltage $< 11.6\text{V}$.
     * **ECU DTC Fault**: Active diagnostic trouble codes.
4. **Tap-to-Dismiss for Remainder of Drive**:
   * Tapping anywhere on the banner or title triggers `onSpeedWarnBannerClick()`.
   * Sets `warningMutedForDrive_ = true`.
   * Instantly restores normal Tachometer/Fuel/Ambient gauges for the remainder of the session.

---

## 4. TPMS Status & Calibration Screens (Screens 1 & 16)

1. **Embedded MX-5 RF Visual Asset**:
   * Uses high-resolution top-down Machine Grey Metallic Mazda MX-5 RF asset embedded as C-array image descriptors (`mx5_rf_tpms_dsc` and `mx5_rf_cal_dsc`). Zero disk I/O latency.
2. **Status Indicator Dots & Card Borders**:
   * **Green Dot (`#00C853`)**: Pressure $\ge 26.0\text{ PSI}$. Normal border and background.
   * **Amber Warning Dot (`#FFA000`)**: Pressure $22.0 - 25.9\text{ PSI}$.
   * **Red Danger Dot (`#FF5252`)**: Pressure $< 22.0\text{ PSI}$. Card border highlights in red (`#D32F2F`) with subtle red alert background tint (`#281014`).
3. **Wheel Map Calibration (`SCREEN_WHEEL_MAP`)**:
   * Displays live pressure for all 4 mapped DIDs with dynamic color shifting.

---

## 5. DTC Diagnostics & Technical Repair Guide (Screen 6)

1. **Zero Fault State (`dtcCount == 0`)**:
   * Code box displays: `OK: NO FAULT CODES STORED (ECU & ABS NORMAL)`.
   * Subtitle confirms: `All powertrain and chassis modules reporting zero faults`.
   * Tapping the box or *REPAIR GUIDE* button opens **`VEHICLE HEALTH - ALL SYSTEMS NORMAL`** inspection modal with green accents (`C_OK`), confirming PCM, ABS, CAN bus, and OBD readiness monitors are complete.
   * **No false alarm guides**: Hardcoded fallback codes (such as dummy `P0421`) must never be displayed on healthy vehicles.
2. **Active Fault State (`dtcCount > 0`)**:
   * Code box turns red and highlights the active DTC.
   * Tapping opens **`DTC <CODE> - TECHNICAL REPAIR GUIDE`** dynamically populated with real SkyActiv inspection steps:
     * `P030x`: Spark plug gap (0.040 in), coil pack swap, direct injector spray test.
     * `P017x`: MAF sensor cleaning, post-MAF vacuum leak inspection, HPFP rail pressure (> 4.0 MPa).
     * `P042x`: Wideband A/F lambda check, exhaust manifold weld inspection, fuel trim verification.
     * `P0128`: Thermostat opening temperature and ECT harness resistance.

---

## 6. Font Rendering & Typography Rules

* **Zero Unicode Emoji Characters**: Embedded LVGL fonts (and ESP32 FreeType/Bitmap engines) do not include Emoji glyphs (`⚠️`, `🚨`, `🚗`). Emojis render as empty white/red rectangle boxes (`[]`).
* **ASCII Text Tags**: Always use clean, standard typography:
  * Use `[!]` or `ALERT:` instead of `⚠️` / `🚨`.
  * Use `%.1f PSI LOW` instead of `%.1f PSI ⚠️`.
  * Use `DEG` or `°` instead of non-standard symbols.

---

## 7. ESP32-S3 Physical Display Firmware Synchronization Checklist

When the physical Waveshare ESP32-S3 3.5" display arrives, apply these exact mappings to `lib/obd_ble/` and `src/`:

- [ ] **ELM327 BLE Driver (`lib/obd_ble/` & `lib/obd_ble/ObdReader.cpp`)**:
  - Implement the 32-tick Speed Interleaving loop (`010D` polled on every alternate tick).
  - Implement the 25-second timer routine to send `ATSH 720`, read `222A05`..`08` (TPMS) and `222A26` (Fuel), then restore `ATSH 7E0`.
  - Set `ATST 10` (40 ms timeout) and `ATAT 2` for maximum throughput.
- [x] **UI Layer (`lib/mx5_ui/` & `AndroidMx5UI.cpp`)**:
  - Incorporate `speedNormalRightContainer_` / `speedWarningContainer_` toggle logic on Screen 0.
  - Link `mx5_rf_cal_dsc` (99x180 ARGB8888) image descriptor into Screen 1 (TPMS 3-column layout).
  - Apply the dynamic DTC Vehicle Health / Repair Guide modal logic.
  - Ensure all string formatting strictly follows ASCII rendering guidelines.
  - Wire up `SCREEN_BLE_CONFIG` (Screen 14) multi-device discovery, tap-to-pair, NVS MAC persistence, and unpair actions.

---

## 8. Bluetooth BLE OBD Scanner Discovery & Pairing Protocol

* **Multi-Adapter Support**:
  * Scans and lists all nearby BLE peripherals regardless of brand/naming (`vLinker`, `OBDLink CX`, `VEEPEAK`, `iCar Pro`, custom ELM dongles).
  * Auto-identifies OBD service GATT advertisements (`0xFFF0`, `0xFFE0`, `0x18F0`, `e7810a71-73ae-499d-8c15-faa9aef0c3f2`).
* **Persistent NVS Pairing**:
  * Saves paired device MAC address and name to ESP32 NVS namespace `sys_prefs` (`paired_mac`, `paired_name`).
  * Boots directly into fast reconnect mode targeting saved MAC address.
* **Unpairing & Switcher**:
  * `FORGET / UNPAIR SCANNER` clears stored NVS keys and initiates fresh discovery cycle.
  * Tapping any discovered device immediately saves new target MAC and initiates ELM handshake.

---

## 9. 3-Foot Automotive Driving Ergonomics & Typography Rules

* **Target Driving Eye Distance**: 36 inches ($3\text{ feet} \approx 91.4\text{ cm}$) from eye to screen in sports car cockpit.
* **Pixel Density & Visual Angle**: 3.5" $480 \times 320$ screen $\approx 165\text{ PPI}$ ($0.153\text{ mm/pixel}$). At 36 inches, $1\text{ px} \approx 0.575\text{ arcminutes}$.
* **Glance Thresholds for Driving Screens (0–5)**:
  * **Hero Speed / Main Value**: 48–120px ($\ge 27\text{ arcmin}$) — instant sub-second recognition.
  * **Primary Dial & Timer Values**: 24–40px ($\ge 14\text{–}23\text{ arcmin}$) — glanceable without squinting.
  * **Secondary Values & Temps**: 16–20px ($\ge 9\text{–}11\text{ arcmin}$).
  * **Card & Channel Labels**: 12–16px bold (`C_CHROME` `#DCE0E8` / `C_DIM` `#B6BCC8`).
  * **Strict Minimum**: $\ge 12\text{px}$ on all driving screens (10px strictly reserved for handheld diagnostic screens 6, 8–16).
* **Anti-Glare Cockpit Contrast**:
  * `C_DIM` upgraded to `#B6BCC8` ($74\%$ luminance) to eliminate open-top sunlight glare washout.
  * `C_CHROME` upgraded to `#DCE0E8` ($88\%$ luminance).
  * Pedal Input channels decoupled into clear vertical stacks (`THR`/`BRK` headers $\rightarrow$ level bars $\rightarrow$ bold `%` numbers).

