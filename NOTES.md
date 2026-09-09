# MX-5 Dash — Working Notes & Hardware Handoff

This file is the authoritative "where we left off" reference for the **mx5-dash**
project. Read it before continuing development so you (or an agent) can pick up
exactly where the last bring-up session ended. For long-term architecture rules,
see `GEMINI.md`.

**Status: WORKING.** The dash boots, connects to the OBDLink CX over BLE, and
streams live telemetry (RPM, speed, coolant, oil temp, ambient temp, fuel, TCM
gear for the 6AT, TPMS). Last verified session: 2026-09-09.

---

## 1. Current Hardware & System Profile

- **Board**: Waveshare ESP32-S3-Touch-LCD-3.5B (ESP32-S3R8, 16 MB Flash, 8 MB
  Octal PSRAM, 480x320 viewable, FT6336 capacitive touch).
- **Panel driver**: `esp32s3_touch_lcd_3_5` PlatformIO env; Waveshare 3.5-C
  ST7796 SPI via QSPI + HV511/other touch. Rotation applied post-begin from NVS.
- **Power & Peripherals**:
  - **AXP2101 PMIC** (I2C `0x34`): rails ALDO/BLDO/DLDO energized at boot.
  - **TCA9554 Port Expander** (I2C `0x20`): pulses LCD hardware reset on Pin 1.
  - **Touch**: FT6336 / AXS5106L (I2C `0x38`).
- **Vehicle**: 2022 Mazda MX-5 RF GT (ND2, SkyActiv-G 2.0L, **6-speed automatic**).
- **OBD adapter**: OBDLink CX (STN1170) — the ONLY scanner that works (see §4).

---

## 2. Software Architecture

```
core 1 : display + LVGL (Arduino loop)     — Mx5UI, Waveshare35B
core 0 : ObdService FreeRTOS task          — BLE ELM327 polling
```

- Cross-core communication is a critical-section-protected `VehicleData`
  snapshot; the UI calls `obd.snapshot()` every frame, never touching BLE.
- **OBD service starts BEFORE display/LVGL init** in `main.cpp` `setup()`. On the
  S3, the UI's LVGL buffers eat ~250 KB internal RAM; starting NimBLE after that
  left too little heap for the 8 KB OBD task stack and NimBLE init (**pdFAIL /
  init hang**). Starting BLE first keeps ~310 KB free.
- BLE prefix and scan timeout are loaded from NVS and passed before `start()`.

---

## 3. Build, Flash & Serial Diagnostics

```bash
# Build (must be in the uucp group for serial access afterwards)
newgrp uucp -c "/home/todd/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5"

# Flash
newgrp uucp -c "/home/todd/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5 \
    -t upload --upload-port /dev/ttyACM0"
```

- Serial permission: member of `uucp` group (or `sudo chmod 666 /dev/ttyACM0`).
- **Kill any serial capture by PID before flashing** (the port is in use):
  `pgrep -af live_capture.py | grep -v pgrep` then `kill <pid>`.
  Never `pkill -f live_capture.py` inside the tool's own shell.
- Logging is UART @ 115200. Boot log includes `[main]`, `[display]`, `[bleElm]`,
  `[obd]`, `[tcm]`, `[prefs]`.

---

## 4. Connecting the OBDLink CX Scanner (BLE)

### 4.1 Scanner hardware facts (official OBDLink docs)

- **BLE 5.1 only** — no Classic BT. The ESP32-S3 can drive it (vLinker MS cannot).
- Custom UART service `0000FFF0`: **FFF1 = Notify**, **FFF2 = Write / Write-without-Response**.
- MTU max **247**, no queued writes.
- **Bonding is only accepted during the first 5 minutes after the CX powers on.**
- **Pairing is triggered by subscribing FFF1 or writing FFF2** (per the official
  app flow), NOT by a scan-level pair.
- **Always uses built-in BLE encrypted communications.** PIN for legacy
  standards = `123456`.
- **LED**: fast blink = "paging its bonded host" (directed advertising); slow
  blink = normal advertising.
- Address type is **public** (`0`).

### 4.2 Why it failed originally — the six gotchas (all fixed)

1. **CCCD must be written WITH a write response.** NimBLE-Arduino
   `subscribe(true, cb)` writes the CCCD without response; the CX silently drops
   it (the call looks successful, so notifications never arrive and `ATZ`
   responses never come). Fix: `pNotifyChar_->subscribe(useNotify, cb, true)`
   AND **read the CCCD back** (`readValue<uint16_t>() == 0x0001`) to prove it.
2. **Don't `deleteAllBonds()` on every boot.** That erases the dash↔CX LTK; the
   CX then needs a fresh 5-minute bonding window after EVERY reboot. Keep the
   bond — a resumed bond re-encrypts in 0 ms and works.
3. **The CX does not undirected-advertise to non-bonded hosts**, and after
   bonding it fast-blinks *directed* advertising to its known bond. The scan may
   never surface it, so pure scan+connect fails forever. Fix: **forced
   direct-address connect** every ~5 s when the scan hasn't surfaced the known
   adapter — poke `pairedMac_` from NVS, else the hardcoded fallback
   `48:23:35:57:99:16` (BleElm.cpp `g_forcedAdapterMac`, TEMP DIAGNOSTIC).
4. **Stale resumed enc** (`bonded=1 enc=1 in 0ms`) can look healthy yet not
   flow data — the link is only truly functional after a *fresh* pairing
   replaces the old LTK.
5. **Post-bond link drop is expected** (~1 s after a new bond is applied). Keep
   the target (don't blacklist), reconnect — the second connect re-encrypts in
   0 ms and holds.
6. **Only blacklist unnamed junk** (earbuds/neighbor BLE physically reject with
   error 13 and were eating connect budget). Never blacklist a device matched by
   name/OUI/GATT UUID on a data-path failure.

### 4.3 Connection algorithm (boot → live telemetry)

1. **BleElm::begin("OBDLink")**: loads `paired_mac`/`paired_name` from NVS;
   NimBLE init; NO `deleteAllBonds()`; `setPower(P9)` max TX for cabin range;
   security IO = no-input-no-output, bonding + secure-connect (`setSecurityAuth(true,false,true)`);
   passkey callback auto-returns `123456`; MTU 247; client connect timeout 8 s.
2. **Idle loop while unconnected** (BleElm.cpp `loop()`):
   - No target yet → if the scan hasn't surfaced the known adapter and ≥5 s
     since the last poke, **force a direct-address connect** to the paired MAC
     (or the hardcoded fallback). Otherwise run a 5 s **active scan**
     (100 ms interval/window, 100% duty, async completion callback) and accept a
     candidate if it matches the name prefix (`OBDLink`, `vLinker`, `VEEPEAK`,
     `V-gate`…), advertises the FFF0 service UUID, is an exact paired-MAC match,
     — or, when unpaired, is connectable with RSSI > −80.
   - Junk that physically rejects the connection gets blacklisted for the boot.
3. **Connect** (public addr type) → discover GATT on FFF0 → find the notify
   char (FFF1) and write char (FFF2) → **subscribe FFF1 with response** →
   **CCCD readback** → ELM init handshake (`ATZ` etc.) → `connected_ = true`.
4. If this device had no stored MAC yet, `pairDevice()` saves the MAC+name to NVS.
5. Handle the expected post-bond drop: reconnect immediately, don't blacklist.

### 4.4 On-device pairing wizard (what the driver does)

- Boot with `configured=1` → goes straight to the driver cluster (Speed screen).
  Connection progress shows in the top banner ("SEARCHING…" → "LIVE OBD-II •
  CONNECTED" → "DISCONNECTED • RECONNECTING…").
- First-run / re-setup → **Wizard** (`SCREEN_WIZARD`, 4 steps:
  `1. CONNECT → 2. CONFIG → 3. TPMS → 4. READY`):
  - **Step 1 CONNECT**: shows "SEARCHING FOR OBDLINK CX ADAPTER…" and
    auto-connects; on CAN handshake OK it flips to "OBDLINK CX CONNECTED & CAN
    HANDSHAKE OK!" and auto-advances to Step 2 (Transmission & Units, then TPMS
    calibration, then READY).
- Wizard freeze fix: `ObdService::loopTask()`'s `frozen_` path (wizard up) did
  not update `data.connected`, so the wizard always showed "SEARCHING" even when
  connected. The frozen branch now sets `connected = isInit` (and
  `lastUpdateMs`) under the mux before continuing.

### 4.5 Re-pairing / recovering a dead bond

- **Symptom**: CX fast-blinking, or dash has blank `paired_mac`, or data never
  flows despite `bonded=1 enc=1`.
- **Fix**: power-cycle the CX (pull and re-seat in the OBD port), then within its
  5-minute bonding window let the dash reconnect (wizard Step 1 or a reboot). It
  pokes the MAC directly and answers the passkey as `123456` automatically. A
  successful new pairing **replaces** the old single bond.
- The dash's stored bond survives re-flashes (`paired_mac` lives in NVS).
- **Laptop isolation tool** (proves the CX + car are healthy while the dash is
  suspect): `bluetoothctl pair` (enter PIN `123456`), or a small bleak script
  that subscribes FFF1, writes `ATZ` / `0100` to FFF2 and prints notifications.
  Use `bluetoothctl remove <mac>` afterwards to free the CX's single-bond slot.

### 4.6 Troubleshooting quick reference

| Symptom | Cause / fix |
| --- | --- |
| Never connects, CX fast-blinking | CX is directed-adv to its old bond; power-cycle it and re-pair within 5 min |
| Connects but no data, `bonded=1 enc=1` | Stale LTK — force a fresh pairing to replace it |
| Scan shows nothing at all | CX won't undirected-advertise; rely on the forced direct-address poke |
| CCCD readback ≠ 0x0001 | Notifications will never fire; subscriber wrote CCCD without response |
| Many "error 13" rejections | Unnamed BLE junk — blacklist logic should be eating these |
| Wizard stuck on "SEARCHING" | Old bug (fixed); verify `data.connected` is set in the frozen branch |

---

## 5. Telemetry — PIDs, DIDs & Decoders

| Signal | Query | Value | Cadence | Notes |
| --- | --- | --- | --- | --- |
| RPM | `010C` | 16-bit /4 | 100 ms | PCM 7E0 |
| Speed | `010D` | 1 byte (km/h) | 300 ms | PCM 7E0 |
| Coolant | `0105` | byte − 40 | 300 ms | PCM 7E0 |
| Load | `0104` | byte % | 1200 ms | PCM 7E0 |
| Throttle | `0111` | byte % | 1200 ms | PCM 7E0 |
| Fuel level | `012F` | byte % | 1200 ms | PCM 7E0; linear calibrate (see §5.3) |
| Intake air | `010F` | byte − 40 | 1200 ms | PCM 7E0 |
| Battery | `0142` | *0.1 V | 1200 ms | PCM 7E0 |
| Ambient temp | `0146` | byte − 40 | 25 s | PCM 7E0 (Mode 01; the Mode-22 "220146" listed in old docs is NOT used) |
| Oil temp | `221310` | 16-bit /100 − 40 | 3 s | PCM 7E0, best-effort |
| TCM gear | `221E12` | 1 byte | on AT only | Header **7E1** (see §5.1) |
| TPMS pressure | `222A05…2A08` | 1 byte | per TPMS interval | Header **720** (BCM, MS-CAN) (see §5.2) |
| TPMS temp | `222A0A…2A0D` | 1 byte | per TPMS interval | Header 720 (see §5.2) |

Header switching uses `ATSH <id>` before the query (7E0 default, 7E1 TCM, 720 BCM).

### 5.1 TCM gear / PRND — ND 6AT (DID 221E12, header 7E1)

Captured empirically on-road 2026-09-09 (P→R→N→D→N→R→P sequence):
`46 → 3C → 32 → 01 → 32 → 3C → 46`.

| Byte | Meaning |
| --- | --- |
| `0x46` | P |
| `0x3C` | R |
| `0x32` | N |
| `0x01…0x06` | D1…D6 |
| anything else | `D` + `-` (e.g. "D–" while shifting) |

`estimateGear()` at standstill falls back to the decoded TCM PRND instead of
assuming P. **Do not 'fix' this table to 0x70/0x60/0x50 guesses — those were the
original bug** (P read "6" and R read "–").

### 5.2 TPMS — ND formulas (DID 222A05–08 + 222A0A–0D, header 720)

- Pressure DIDs (`2A05..2A08`) return a **single data byte**:
  `psi = ((A * 1373) / 1000) * 0.145037738` (Miata.net ND thread).
- Temperature is a **separate** DID pair (`2A0A..2A0D`): `C = A − 40`.
- The old code parsed 2 bytes — that is why nothing displayed. `pollTpmsCorner()`
  reads one byte per DID.
- TPMS calibration (wizard Step 3) round-robins all four `222Axx` candidates and
  binds a DID to the active corner by catching which one changes.

### 5.3 Fuel level (012F)

- `parseCalibratedFuel(raw)` is linear: `((raw − 8) * 100) / 216`. Averaging raw
  == averaging percent.
- **Display**: a U-shaped dot trough inside a card (left side up 2.5→10%, top
  row 10→90%, right side down 90→97.5%) + two readouts:
  - `C x.x gal` — current fuel (white), tank = **11 gal**, `gal = pct/100 * 11`.
  - `M y.y gal` — missing fuel (Soul Red `0xC41230` accent): `11 − cur`.
- **Averaging** (`applyFuelSample`): first sample passes immediately; afterwards
  it's the **median** of all samples in a trailing 30 s ring (median ignores
  slosh spikes a mean would drag), plus **fast-fill detection** (a new raw ≥ 8
  points above the median resets the window and jumps to the fresh value).

---

## 6. UI Architecture (Mx5UI)

- Screens: `SCREEN_SPEED` (0), `SCREEN_TPMS` (1), `SCREEN_WIZARD`; dynamic
  view-driven polling via `ObdService::setActiveScreen()`.
- Speed screen right column (166 px): `rpmSeg_` = `buildDottedArc(166×166)` at
  (0,0); `fuelSeg_` = `buildFuelGauge(166×72)` at (0,178) (U-trough, no caption).
- `SegArc` struct carries `wrap`, `dots[24]`, `count`, `pct10[24]` (fuel
  thresholds in percent×10), `val`, `sub`. **Gotcha:** `pct10` must be
  `uint16_t` (values up to 975) and `count` must be
  `sizeof(arr)/sizeof(arr[0])` — `sizeof(uint16_t[15])` is 30, which previously
  overran `dots[24]` and caused a `LoadProhibited` boot loop.
- Warning system: speed/rpm warning card + banner; `warningMutedForDrive_`
  **re-arms when the condition clears** (once muted it used to stay muted
  forever); whole card + value are tappable; sub-text is condition-aware
  ("PULL OVER • ENGINE OVERHEATING" / "REFUEL SOON").

---

## 7. Session Log (2026-09-09)

- Fixed 6AT gear decode via on-road capture (see §5.1); fixed speed/gear/rpm/ambient
  arc readouts that never updated; added `012F` polling on the automatic path.
- Fixed TPMS: single-byte parse + temp DIDs + `pollTpmsCorner()`.
- Flashed; user confirmed speed, gear, rpm, fuel, ambient temp, TPMS all working.
- Removed ambient card from speed screen; fuel became a wide bar, then a U-trough
  with C/M gallon readouts.
- Warning system fixes (§6). Boot-loop fixed (§6 SegArc gotcha) and verified on
  serial; removed the "FUEL" caption from the fuel card.

## 8. Pending / Noted For Later

- **TEMP DIAGNOSTICS still in tree to trim**: `[bleElm]` probe prints, task-tick
  print, `RX notify len=` dump + `RX adv:`, heap print, `[tcm]` line,
  `g_forcedAdapterMac` (keep as an unpaired fallback but consider making it
  NVS-configurable), and `[main]` startup heap prints.
- First `AT\r` in `initAdapter` may return `?` (wake/race); `ATZ` then resets
  cleanly. Consider sending ATZ first.
- Scan/junk race: an OBD-looking candidate captured before the paired-MAC poke is
  harmless now (pair poke wins on fallback) but candidate selection could prefer
  OBD-flagged devices.
- `qr/` directory (untracked) holds Wi-Fi-AP-offload QR screenshots — not part of
  the firmware.

---

## 9. Deployment notes for pushing

- Remote: `https://github.com/Todd0042/mx5-dash.git` (git credential helper =
  `store`; `gh` authenticated as **Todd0042**).
- **Do not commit without being asked.** When committed, this Notes file itself
  should be in the commit.
- Never commit secrets: OBD PIN `123456` is a published factory default and the
  CX MAC is a diagnostic fallback — fine to keep, but no tokens/keys ever.