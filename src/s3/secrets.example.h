// Copy to secrets.h (same folder) and fill in. secrets.h is git-ignored.
// If secrets.h is missing the build falls back to this file and prints a
// warning; the rover will then only be reachable through its bench AP.
#pragma once

// Arena / lab network (station mode). Issued at the technical briefing.
#define WIFI_STA_SSID ""
#define WIFI_STA_PASSWORD ""

// Bench fallback access point (8..63 characters). Change before use.
#define WIFI_AP_SSID "URRT-ROVER"
#define WIFI_AP_PASSWORD "change-me-please"

// Dashboard login (8 or more characters). Empty = no login: anyone on the
// network can drive the rover. One password for the whole team.
#define DASHBOARD_PASSWORD ""
