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
- **BLE OBD adapter**: vLinker (LE version). It is currently **unpaired from the
  PC** so it will advertise and the dash can discover it. Do not pair it to a
  phone/PC while testing the dash, otherwise it stops advertising.
- The "3.5-C" variant was added as a firmware-supported sibling of the existing
  "3.5-B" build (auto-detected by I2C probing for 0x3B vs 0x38 touch IC).

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

- **vLinker advertising**: only advertises while unconnected. If the dash
  "finds nothing", check whether any other device/phone holds the BLE link.
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