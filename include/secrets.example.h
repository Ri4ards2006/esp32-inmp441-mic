/**
 * @file secrets.example.h
 * @brief Template for WiFi / UDP target settings.
 *
 * Copy to include/secrets.h (gitignored) and fill in. Without secrets.h the firmware
 * builds and runs with network transport disabled (serial telemetry only).
 */
#pragma once

#define MIC_WIFI_SSID "your-ssid"
#define MIC_WIFI_PASS "your-password"
// IPv4 of the PC running tools/receiver/mic_receiver.py, or "255.255.255.255" for
// LAN broadcast (works without knowing the PC's address, but floods the segment).
#define MIC_UDP_HOST  "192.168.1.100"
#define MIC_UDP_PORT  5005
