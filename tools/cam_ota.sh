#!/usr/bin/env bash
# Wireless update of the ESP32-CAM: builds env cam_ota and sends it over Wi-Fi.
# Needs a camera build with OTA already on the board (one USB flash) and
# CAM_OTA_PASSWORD in src/cam/secrets.h.
#
# Usage: tools/cam_ota.sh [camera-ip-or-hostname]   (default: urrt-cam.local)
set -euo pipefail
cd "$(dirname "$0")/.."
host="${1:-urrt-cam.local}"
pw="$(sed -n -E 's/^#define CAM_OTA_PASSWORD "(.*)".*/\1/p' src/cam/secrets.h 2>/dev/null || true)"
if [[ ${#pw} -lt 8 ]]; then
  echo "Set CAM_OTA_PASSWORD (8+ characters) in src/cam/secrets.h first." >&2
  exit 1
fi
CAM_OTA_PASSWORD="$pw" exec tools/pio.sh run -e cam_ota -t upload --upload-port "$host"
