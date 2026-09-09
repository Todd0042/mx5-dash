# MX-5 Dash — Working Notes (direct read-handoff for continuing work)

This file is the "where we left off" reference. It is meant to be read before
continuing development so you (or an agent) can pick up exactly where the last
hardware bring-up session ended. For long-term architecture/engineering rules
see `GEMINI.md`.

---

## Current Hardware Situation

- **Board**: Waveshare ESP32-S3-Touch-LCD-3.5 **"C"** (Standard ST7796 SPI +
  FT6336 capacitive touch, AXP2101 PMIC, TCA9554 port expander, BMA421 IMU).
  This is the sibling of the 3.5" **"B"** (AXS15231B QSPI + AXS5106L touch).
- **Car**: 2022 Mazda MX-5 RF GT (ND2 / SkyActiv-G).
- **BLE OBD adapter**: **vLinker MC+** (Vgate), plugged into the OBD port of the
  **running** car. Android Bluetooth settings shows its name as **"vLinker MS
  08449"**. Vgate units only advertise while **unpaired/unbound**; once a phone
  has bonded them they withhold advertisements from all other scanners. See the
  BLE session notes below — this bond is the leading theory for why our radios
  have never caught it.
- The "3.5-C" variant was added as a firmware-supported sibling of the existing
  "3.5-B" build (auto-detected by I2C probing for 0x3B vs 0x38 touch IC).

--- 

## BLE Discovery Session (2026-09-08) — Two Core Bugs Found & Fixed

### Bug 1 — the core-0 OBD task silently never ran ("auto-scan death")
- `ObdService` spins up a FreeRTOS task on **core 0** (prio 2, 8 KB stack) that
  drives all background BLE scanning / reconnect. It used to be created **after**
  `NimBLEDevice::init()` in `ObdService::start()`.
- On the ESP32-S3 the display/LVGL init (`ui.begin()`) consumes ~250 KB of
  internal RAM. Measured free internal RAM right before BLE init: **~57 KB**;
  after `NimBLEDevice::init()`: **7,180 B**.
- `xTaskCreatePinnedToCore` therefore returned **pdFAIL (-1)** — no room for the
  8 KB stack + TCB — and the task **never ran**. UI-triggered rescans still
  worked (they call the NimBLE scan API directly), so the failure was invisible:
  symptoms were "can't find / never connects" when the real problem was "no
  background scanner exists at all".

### Bug 2 — Naive "task first" fix made `NimBLEDevice::init()` hang
- Reordering so the task ran first caused `NimBLEDevice::init()` to **hang
  forever**: the 8 KB task had already consumed the heap NimBLE needs for its own
  host task/buffers → deadlock. Verified with step prints: `begin step0 core=1`
  printed, `begin step1 init-ok` never did, while the task stayed alive and
  ticking the whole time.

### The real fix — start OBD before the display (main.cpp `setup()`)
- Internal RAM at setup start is **~310 KB** (measured: `int=309964 dma=302180`)
  because nothing has initialized the UI yet. So `main.cpp` now does:
  `UserPrefs::loadAll()` → `obd.setBlePrefix()/setBleScanTimeout()` →
  `obd.start()` → `display.begin()` → `ui.begin()`. BLE + 8 KB task both fit
  trivially in the pre-UI heap.
- `BleElm::loop()` is gated on a `volatile bool initReady_` set at the end of
  `begin()`, so the task never touches NimBLE before init completes.
- Verified post-fix boot chain:
  ```
  [main] heap at setup start: int=309964 dma=302180
  [bleElm] xTaskCreate -> 1 (pdPASS=1) stack/prio 8192/2 on core 0
  [bleElm] begin step0 core=1 / step1 init-ok / step2 config-ok
  [bleElm] initialized BLE subsystem (paired MAC: '', prefix: 'vLinker')
  [bleElm] starting BLE scan ...        <- auto-scan now runs continuously
  [bleElm] probe: conn=0 init=0 scan=1 ... implRdy=1
  [bleElm] UI scan done: N device(s), core0 tick age = 12-19ms
  ```
  Cross-core loop health is reported as `core0 tick age` (3–19 ms) on the BT
  screen and in the scan-done dump.

### BLE Discovery & Connectivity Root Causes Resolved (2026-09-08 Session)
- **Root Cause 1 — ESP32 Controller HCI Duplicate Filtering (`filter_duplicates`)**:
  - `startScanInternal()` was calling `pScan->setAdvertisedDeviceCallbacks(scanCbs_, false)` with `wantDuplicates = false`.
  - In NimBLE, `wantDuplicates = false` enables hardware controller duplicate filtering (`filter_duplicates = 1`).
  - When the vLinker adapter first advertised, its initial `ADV_IND` packet arrived without the local device name (names are delivered in the `SCAN_RSP` scan response packet). `onResult()` evaluated the empty name, returned without adding it to `discoveredDevices_`, and then the ESP32 HCI controller **permanently suppressed all subsequent advertising packets and scan responses** from that adapter MAC! `onResult()` was never called again when the name packet arrived.
  - **Fix**: Set `wantDuplicates = true` (`pScan->setAdvertisedDeviceCallbacks(scanCbs_, true)`). `BleElm`'s `onResult()` already deduplicates and updates existing entries in `discoveredDevices_`.
- **Root Cause 2 — False Device Matching & Phone MAC Loops**:
  - `isVLinkerName()` included broad substring keywords (`car`, `link`, `vr`, `mcu`) which matched cabin Bluetooth devices (such as Android Auto, phone names, or headsets).
  - Tapping an unnamed/phone entry on the display saved its MAC (`07:0A:71:97:28:BB`) to NVS.
  - `discoverGatt()` possessed a loose generic fallback loop that matched Google Nearby Share (`0xFEF3`) on the phone, treating it as an OBD service. `BleElm` sent `ATZ\r` to the phone's Nearby Share service, timed out waiting for an ELM prompt (`>`), disconnected, and retried infinitely—preventing the ESP32 from ever connecting to the real vLinker adapter in pairing mode.
  - **Fix**: Removed ambiguous keywords (`car`, `link`, `vr`, `mcu`) from `isVLinkerName()` and eliminated the generic fallback loop from `discoverGatt()`, strictly requiring recognized OBD service UUIDs (`e781...`, `FFF0`, `FFE0`, `18F0`). Erased NVS to clear the phone MAC.
- **Root Cause 3 — MITM Passkey Security Requirement (`mitm = true`)**:
  - `begin()` called `NimBLEDevice::setSecurityAuth(true, true, true)`.
  - The second parameter `mitm = true` required Man-In-The-Middle passkey/PIN entry authentication. Headless BLE OBD adapters (vLinker MC+, OBDLink CX, Veepeak) do not have displays or keypads for PIN entry and use "Just Works" pairing. Demanding MITM protection caused security negotiation (SMP) to fail on connection.
  - **Fix**: Set `NimBLEDevice::setSecurityAuth(true, false, true)` (`mitm = false`) and `NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT)`.
- **Root Cause 4 — Scan Window Duty Cycle Gap**:
  - `setInterval(97)` and `setWindow(67)` set 97 ms interval and 67 ms window (arguments in ms in NimBLE).
  - This left a 30 ms blind gap every 97 ms (~31% radio off-time). OBD adapter advertisements falling in that gap were missed.
  - **Fix**: Set `setInterval(100)` and `setWindow(99)` for continuous 100% duty cycle active scanning.

### TEMP diagnostic instrumentation still in place (remove after first connect)
- `BleElm.cpp`: `begin()` step prints, `loop()` probe print, RX advertising dump
  in `onResult` (`[bleElm] RX adv: ...`), `diagBuffer_/diagLen_/diagCount_`.
- `ObdService.cpp/.h`: task-entered + 5 s tick prints, xTaskCreate/heap prints,
  `volatile uint32_t implTickMs_`, `diagBuffer()/diagCount()/core0TickAgeMs()`
  overrides.
- `ObdSource.h` (lib/mx5_config): diagnostic virtuals so the native build
  compiles.
- `Mx5UI.cpp`: scan-done diag dump + 2 s core0-age print on the BT screen
  (all `#if defined(ARDUINO)`-guarded).
- `main.cpp`: `heap at setup start` print.

---

## Where We Left Off / Current Status

Everything below is **done and verified in the latest build/flash on the 3.5-C
board**, and is committed to GitHub:

1. **Display works on 3.5-C**: ST7796 detected, initialized, and rendering.
   - Red now renders **red** (fixed by flushing the LVGL buffer through the
     byte-swap-aware `draw16bitBeRGBBitmap` on the ST7796 branch — the "B"
     panel path uses `draw16bitBeRGBBitmapR1` instead).
   - The earlier "display blinks" issue was caused by *repeated flashing*
     during bring-up; with a normal boot it does **not** blink.
2. **Rotation defaulted to USB Left**: `MX5_LCD_ROTATION 1`.
   - Boot now **unconditionally** re-applies the saved rotation in
     `src/main.cpp:43` (`display.setRotation(savedRot);`) so the boot state is
     byte-identical to choosing "USB LEFT" in the Settings screen. The settings
     buttons are wired: USB LEFT → rotation 1, USB RIGHT → rotation 3
     (`Mx5UI.cpp` cases 101/102). Touch mapping is rotation-aware in
     `Waveshare35B.cpp` `my_touchpad_read`.
   - **Note**: the physical mapping of rotation value → USB side is relative to
     how you hold the panel; if the panel still looks "USB right" on this board
     with rotation 1, flip `MX5_LCD_ROTATION` in `lib/mx5_config/Config.h`
     between 1 and 3 and re-test (both values are symmetric MADCTL swaps on
     ST7796).
3. **BLE scan filter rewritten** (`lib/obd_ble/BleElm.cpp`) so the dash only
   finds the **vLinker** instead of phantom/unusable devices:
   - `isVLinkerName()` accepts "v…linker" across any separators/case
     (vLinker MC, V-LINKER, V_LINKER, V Linker, etc.).
   - Fallback: any advertiser advertising an OBD-ish service UUID
     (`FFF0` / `FFE0` / `18F0`) is also listed.
   - Auto-connect only ever targets the **explicitly paired MAC** (from the
     wizard / BT setup); the old "save the first OBD-looking device" auto-bond
     block was removed.
4. **NVS state on the board** (as of last flash):
   `rot=1 units=1 theme=0 bri=95 ble='vLinker' mac='' configured=0`
   - `configured=0` means the **initial setup wizard will run** on next boot,
     taking you through OBD2 pairing. That's exactly the state we want for
     in-vehicle testing.
   - To return to the wizard any time: `pio run -e esp32s3_touch_lcd_3_5 -t erase`
     then re-upload.
5. **Backlight**: moved to GPIO6 (`MX5_PIN_BACKLIGHT 6`); auto-dim active
   (day 95% / night 25%, night = battery < 12.8 V). Selftest disabled
   (`MX5_DISPLAY_SELFTEST 0`).
6. **Builds**: `esp32s3_touch_lcd_3_5` (the 3.5-C env, extends the 3.5-B env),
   `esp32s3_touch_lcd_3_5b`, and `native_preview` all compile. Arduino_GFX +
   XPowersLib + TCA9554 + SensorLib are **vendored locally** under `lib/`
   (GFX removed from registry `lib_deps` because the 3.5-C board needs the ST7796
   driver from a pinned version).
7. **Android side**: continued in lockstep (AndroidMx5UI.cpp overhaul,
   BluetoothSerialManager.kt rework, MainActivity + Mx5RenderView tweaks). It is
   **not** the current focus — hardware test phase is.
8. **BLE driver fixed**: the core-0 background scan task now actually runs
   (`obd.start()` moved before display init, see session notes above) — auto
   scan, scan pacing, and result delivery are all live on the board. The only
   open piece is the **vLinker MC+ not advertising to us** (bond-cache theory,
   see above).

## Verified Boot Log (last 3.5-C flash, sanity reference)

```
[prefs] loaded: rot=1 units=1 theme=0 bri=95 ble='vLinker' mac='' configured=0
[display] detected Waveshare 3.5 / 3.5-C (ST7796 SPI + FT6336 Touch at 0x38)
[display] TCA9554 found at 0x20, pulsing pin 1 (LCD Reset)...
[display] LCD hardware reset via TCA9554 complete.
[main] applied rotation 1
[bleElm] initialized BLE subsystem (paired MAC: '', prefix: 'vLinker')
[main] ready
[display] flush #1: x1=0 y1=0 x2=479 y2=319   (full-frame, no partial flushes)
```

## Next Steps (in-vehicle, in order)

0. **Unbind the MC+ from the phone so it advertises again.** Per the session
   notes: verify whether "vLinker MS 08449" is live or bond-cache, long-press
   the MC+ button to unbind if needed, then re-press for a fresh pairing window.
   Only then will the board's auto-scan (and laptop bleak) see it.
1. **Run the setup wizard** (board is at `configured=0`).
   - Pair the vLinker (BLE MAC shown in the discovery list during the wizard).
   - Confirm USB Left orientation is correct on boot; if it comes up "USB right"
     instead, flip `MX5_LCD_ROTATION` 1 ↔ 3 in `Config.h`, rebuild, flash, and
     re-check (see rotation note above).
2. **Confirm live OBD2 data** on the dashboard once paired (Speed/RPM/etc.).
3. **Verify the full screen carousel** works with touch swipes
   (Speed → TPMS → Temps → Diag → Track → RPM → Trip → Menu + sub-dashboards).
4. **Then, the big pending task: full per-screen UI redesign for 3.5" 480x320**.
   - The current fonts are compiled for the smaller 320x240-ish scale and look
     "garbled"/wrong at 480×320.
   - Candidates: `lib/mx5_ui/lv_font_mono_120` (used at `Mx5UI.cpp:704`),
     `lv_font_mono_96`, `lv_font_mono_48`; LVGL conf in `lib/mx5_ui/lv_conf.h`
     (`LV_COLOR_16_SWAP 1`, Montserrat 8–48 default 14).
   - Start with the **Wizard** and **Speed** screens, establish a consistent
     type scale for 480×320, rebuild/flash per screen batch.
   - Colors & auto-dim live at `Mx5UI.cpp:33-49` and `Mx5UI.cpp:3237-3262`.
   - Every screen is built once in `ui.begin()` and updated each `loop()`; the
     screen list/order is defined in `GEMINI.md` Rule 2.

## Build / Flash / Log Workflow

- **Build**:
  ```bash
  ~/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5
  ```
  Build all targets at once per GUIDELINES (`GEMINI.md` Rule 7):
  ```bash
  ~/.platformio/penv/bin/pio run -e native_preview -e esp32s3_touch_lcd_3_5 -e esp32s3_touch_lcd_3_5b
  ```
- **Upload** (must run under the `uucp` group in this shell):
  ```bash
  newgrp uucp -c "~/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5 -t upload --upload-port /dev/ttyACM0"
  ```
- **Erase NVS then re-upload** (returns to the wizard):
  ```bash
  newgrp uucp -c "~/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5 -t erase"
  ```
  then upload again as above.
- **Serial log**: `pio device monitor` fails with termios "Inappropriate ioctl"
  in the bash tool. Use raw pyserial instead:
  ```bash
  ~/.platformio/penv/bin/python -c "import serial,time; s=serial.Serial('/dev/ttyACM0',115200,timeout=2); s.setDTR(False); s.setRTS(False); start=time.time()
  while time.time()-start<9:
      d=s.read(s.in_waiting or 1)
      if d: print(d.decode('utf-8','replace'),end='',flush=True)"
  ```
  (mind `newgrp uucp` for permission on the port).
- **Helper scripts**: `tools/flash_esp32.sh` and `tools/scan_esp32.sh` exist;
  note `flash_esp32.sh` currently hardcodes the old `_3_5b` env — update to the
  active env if you use it.

## Recurring Gotchas

- **vLinker advertising**: only advertises while **unpaired/unbound** — once any
  phone bonded it, it hides from all other scanners. If the dash "finds nothing"
  while the phone still lists it under *Paired devices*, the phone listing is
  **bond cache**, not a live advertisement; unbind the **adapter** (long-press
  its button) and then test with fresh pairing.
- **ESP32-S3 heap under NimBLE**: `NimBLEDevice::init()` eats ~50 KB of internal
  RAM, and display/LVGL init eats ~250 KB **before** that. On this board you get
  exactly one shot — start BLE and its 8 KB core-0 task **before** `ui.begin()`,
  or either the task creation returns `pdFAIL` or BLE init hangs. Keep the
  pre-UI ~310 KB window for all of it.
- **Old "B" model paths still exist** (`Arduino_AXS15231B` driver files were
  deleted in favor of vendored Arduino_GFX which provides it). The 3.5B env
  still builds; if the B panel was re-introduced, re-verify the QSPI R1 flush
  path (`draw16bitBeRGBBitmapR1`).
- **Rotation vs touch**: changing rotation 1↔3 must keep
  `my_touchpad_read`'s rotation cases consistent with the chosen MADCTL.
- **GPIO pins**: backlight is GPIO6 on the 3.5-C. SDA=8 / SCL=7 (with SCL=9
  fallback probe in `Waveshare35B.cpp::initPower`).

## Git

- Remote: `origin  https://github.com/Todd0042/mx5-dash.git` (branch `main`).
- Follow the repo's conventional-commit style (see `git log --oneline`).
- After any change, `git add -A && git commit && git push` — the laptop clone
  should pull before/after working sessions.