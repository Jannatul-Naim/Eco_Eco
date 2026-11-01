// Copy to secrets.h (same folder) and fill in. secrets.h is git-ignored.
#pragma once

// Primary: the arena / lab network (same one the S3 joins).
#define WIFI_STA_SSID ""
#define WIFI_STA_PASSWORD ""

// Wireless firmware updates (tools/cam_ota.sh). Empty = OTA off. Use 8+ characters.
#define CAM_OTA_PASSWORD ""

// Fallback: the S3's bench access point (must match src/s3/secrets.h).
#define WIFI_FALLBACK_SSID "URRT-ROVER"
#define WIFI_FALLBACK_PASSWORD "change-me-please"
