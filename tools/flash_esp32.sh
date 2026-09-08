#!/usr/bin/env bash
# ==============================================================================
# MX-5 Dash: ESP32-S3 One-Step Build & Flash Tool
# ==============================================================================
set -e

# Ensure user has uucp permissions active
if [ -z "$IN_UUCP_SUB" ] && ! id -Gn | grep -qw uucp; then
    export IN_UUCP_SUB=1
    exec newgrp uucp <<EOF
$0 "$@"
EOF
fi

PIO="$HOME/.platformio/penv/bin/pio"

echo "========================================================"
echo "    MX-5 Dash - ESP32-S3 Build & Flash Tool"
echo "========================================================"

# Detect port
PORTS=($(ls /dev/ttyACM* /dev/ttyUSB* 2>/dev/null || true))
if [ ${#PORTS[@]} -eq 0 ]; then
    echo "[!] No ESP32 device detected on /dev/ttyACM* or /dev/ttyUSB*."
    echo "[i] Please connect your ESP32-S3 board via USB-C and try again."
    exit 1
fi

PORT="${PORTS[0]}"
echo "[+] Target port: $PORT"

echo ""
echo "[*] Building and uploading firmware to $PORT..."
$PIO run -e esp32s3_touch_lcd_3_5b -t upload --upload-port "$PORT"

echo ""
echo "========================================================"
echo "[+] Flash successful! Starting serial monitor (Ctrl+C to exit)..."
echo "========================================================"
$PIO device monitor --port "$PORT" --baud 115200 --rts 0 --dtr 0
