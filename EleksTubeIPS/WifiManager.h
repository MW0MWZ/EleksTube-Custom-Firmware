// WifiManager.h -- WiFi connection lifecycle and AP mode management
//
// Manages two WiFi operating modes on the ESP32:
//   - STA (Station): Connects to the user's home WiFi for NTP and web access
//   - AP (Access Point): Creates a captive portal so the user can configure
//     WiFi credentials from a phone/laptop when no network is saved
//
// The ESP32 can run both modes simultaneously (WIFI_AP_STA), which allows
// the clock to serve its config portal while still connected to the internet.
//
// Connection lifecycle:
//   1. On first boot (no saved config): start AP mode for setup
//   2. On subsequent boots: connect to saved WiFi, optionally also run AP
//   3. If WiFi drops, WifiReconnect() retries periodically
//   4. AP can be toggled at runtime via button press

#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include "GLOBAL_DEFINES.h"
#include "StoredConfig.h"

// Simple two-state model: either we have an IP address or we don't
enum WifiState_t { disconnected, connected, num_wifi_states };

void WifiBegin(StoredConfig::Config::Wifi *wifi_config);
void WifiReconnect();
void WifiStartAP();
void WifiStopAP();
void WifiToggleAP();

// Global state visible to other modules (Clock checks WifiState before NTP)
extern WifiState_t WifiState;
extern bool APRunning;
// Device-unique AP name, e.g. "EleksTubeIPS-A1B2" (derived from MAC)
extern char WifiAPName[32];

#endif // WIFI_MANAGER_H
