#!/usr/bin/env bash
# ==============================================================================
# MX-5 Dash: ESP32-S3 Hardware Compatibility & Chip Diagnostic Scanner
# ==============================================================================
set -e

# Ensure user has uucp permissions active
if [ -z "$IN_UUCP_SUB" ] && ! id -Gn | grep -qw uucp; then
    export IN_UUCP_SUB=1
    exec newgrp uucp <<EOF
$0 "$@"
EOF
fi

ESPTOOL="$HOME/.platformio/penv/bin/esptool"
if [ ! -f "$ESPTOOL" ]; then
    ESPTOOL="$HOME/.platformio/penv/bin/esptool.py"
fi

echo "========================================================"
echo "    MX-5 Dash - ESP32-S3 Hardware Diagnostic Tool"
echo "========================================================"

# 1. Detect Serial Device
echo "[*] Scanning for connected USB serial ports..."
PORTS=($(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true))

if [ ${#PORTS[@]} -eq 0 ]; then
    echo "[!] No ESP32 device detected on /dev/ttyACM* or /dev/ttyUSB*."
    echo "[i] Please connect your ESP32-S3 board via a USB-C data cable."
    echo "    (Make sure the cable supports data, not charging only!)"
    exit 1
fi

PORT="${PORTS[0]}"
echo "[+] Detected serial device at: $PORT"
if [ ${#PORTS[@]} -gt 1 ]; then
    echo "    (Multiple devices found: ${PORTS[*]}. Using $PORT)"
fi

# 2. Check USB device descriptors
echo ""
echo "[*] Querying USB device identity (lsusb)..."
lsusb | grep -iE "espressif|ch340|cp210|silicon|ftdi|serial" || lsusb

# 3. Query Chip Details via esptool
echo ""
echo "[*] Interrogating ESP32-S3 silicon..."
$ESPTOOL --port "$PORT" chip-id

echo ""
echo "[*] Reading Flash Memory & Silicon Features..."
$ESPTOOL --port "$PORT" flash-id

echo ""
echo "[*] Reading MAC Address..."
$ESPTOOL --port "$PORT" read_mac

echo ""
echo "========================================================"
echo "           Hardware Compatibility Assessment"
echo "========================================================"
echo "[+] Serial communication: OPERATIONAL on $PORT"
echo "[+] Flashing tools: READY (~/.platformio/penv/bin/esptool)"
echo "[+] Ready to flash firmware with:"
echo "    ~/.platformio/penv/bin/pio run -e esp32s3_touch_lcd_3_5b -t upload"
echo "========================================================"
