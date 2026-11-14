#pragma once
// Copy to include/secrets.h and replace placeholders before first USB flash.
// NEVER commit include/secrets.h.
#define SEEYA_WIFI_SSID "CHANGE_ME_WIFI"
#define SEEYA_WIFI_PASSWORD "CHANGE_ME_WIFI_PASSWORD"
// Set a non-empty password for HTTP Basic auth; use "" to disable ALL HTTP authentication.
// WARNING: empty means anyone with network access can change settings and upload OTA firmware.
#define SEEYA_ADMIN_PASSWORD "CHANGE_ME_ADMIN_PASSWORD"
