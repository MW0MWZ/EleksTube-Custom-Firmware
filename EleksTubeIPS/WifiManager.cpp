// WifiManager.cpp -- WiFi connection and access point management
//
// Uses the ESP32 Arduino WiFi library's event-driven model: rather than
// polling WiFi.status() in a loop, we register callbacks for connection
// state changes. This lets the main loop run freely while WiFi events
// update the global WifiState asynchronously.
//
// The AP mode creates an open (no password) access point that serves
// the clock's web configuration interface. The AP name includes the
// last two octets of the MAC address to make each clock uniquely
// identifiable when multiple clocks are nearby.

#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>
#include "StoredConfig.h"
#include "TFTs.h"
#include "WifiManager.h"

extern StoredConfig stored_config;

// ---- Global State ----
WifiState_t WifiState = disconnected;
bool APRunning = false;
char WifiAPName[32] = "";

// Rate-limit reconnection attempts to avoid busy-looping when the
// network is down (WIFI_RETRY_CONNECTION_SEC is defined in USER_DEFINES.h)
uint32_t reconnect_attempt_time = 0;

// ---- WiFi Event Callbacks ----
// These are called asynchronously by the ESP32 WiFi driver from a system
// task, not from loop(). They update global state that the main loop reads.

void WiFiStaConnected(arduino_event_id_t event) {
  Serial.println("Connected to AP: " + String(WiFi.SSID()));
}

void WiFiStaGotIP(arduino_event_id_t event) {
  // STA connection isn't "complete" until DHCP assigns an IP address.
  // Only then can we reach NTP servers and serve web pages.
  Serial.print("Got IP: ");
  Serial.println(WiFi.localIP());
  WifiState = connected;

  // Register mDNS so the clock is reachable at elekstubeips-xxxx.local
  // from any device on the network without relying on the router's DNS.
  // Re-registering on every IP acquisition handles WiFi reconnections.
  if (MDNS.begin(WifiAPName)) {
    MDNS.addService("http", "tcp", 80);
    Serial.print("mDNS: ");
    Serial.print(WifiAPName);
    Serial.println(".local");
  }
}

void WiFiStaDisconnected(arduino_event_id_t event) {
  // Triggered on signal loss, AP reboot, or authentication failure.
  // WifiReconnect() will attempt to restore the connection.
  WifiState = disconnected;
  Serial.println("WiFi lost connection");
}

// ---- Access Point Control ----
// AP mode creates a local WiFi network that clients can join directly.
// Combined with a web server (elsewhere), this acts as a captive portal
// for configuring WiFi credentials, timezone, and display settings.

void WifiStartAP() {
  if (APRunning) return;

  // WIFI_AP_STA runs both AP and STA simultaneously -- the clock can
  // serve its config page while still connected to the internet
  WiFi.mode(WIFI_AP_STA);

  // Start the AP on channel 1 (non-overlapping 2.4GHz channel).
  // Open network (no password) -- intentional for a config portal.
  // Max 4 connections is plenty for a config UI.
  WiFi.softAP(WifiAPName, NULL, 11, 0, 4);

  // Force 20MHz channel bandwidth instead of default 40MHz.
  // 20MHz is sufficient for a config web UI, uses less airtime,
  // causes less interference with neighbouring networks, and
  // reduces radio power consumption.
  esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);

  Serial.print("AP started: ");
  Serial.println(WifiAPName);
  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  APRunning = true;
  // Persist AP state so it survives reboot
  stored_config.config.wifi.ap_enabled = true;
  stored_config.save();
}

void WifiStopAP() {
  if (!APRunning) return;

  // true = disconnect any connected clients
  WiFi.softAPdisconnect(true);
  // Drop back to STA-only mode if we have a station connection,
  // freeing the radio from AP beacon overhead
  if (WifiState == connected) {
    WiFi.mode(WIFI_STA);
  }

  Serial.println("AP stopped");
  APRunning = false;
  stored_config.config.wifi.ap_enabled = false;
  stored_config.save();
}

void WifiToggleAP() {
  if (APRunning) WifiStopAP();
  else WifiStartAP();
}

// ---- WiFi Initialization ----

void WifiBegin(StoredConfig::Config::Wifi *wifi_config) {
  WifiState = disconnected;

  // Read the factory-programmed MAC address from the ESP32's eFuse
  // (one-time-programmable memory). This address is globally unique
  // and stable across reboots, making it ideal for generating a
  // device-specific AP name.
  uint8_t mac[6];
  esp_read_mac(mac, ESP_MAC_WIFI_STA);
  char mac_str[18];
  snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
           mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  Serial.print("MAC: ");
  Serial.println(mac_str);

  // Use last two MAC octets for a short but unique AP name suffix
  snprintf(WifiAPName, sizeof(WifiAPName), "EleksTubeIPS-%02X%02X", mac[4], mac[5]);

  // Determine if AP should run at startup
  bool should_start_ap = false;
  if (wifi_config->is_valid != StoredConfig::valid) {
    // First boot (no saved config) -- always start AP so user can configure
    wifi_config->ap_enabled = true;
    wifi_config->dashboard_password_hash[0] = '\0';  // No auth on first boot
    should_start_ap = true;
  } else {
    // Use the persisted AP preference from last session
    should_start_ap = wifi_config->ap_enabled;
  }

  // --- WiFi configuration for enterprise AP compatibility ---
  // These settings target well-managed enterprise WiFi (e.g., Cisco 9120
  // with WLC) which expects clients to behave per modern 802.11 standards.

  // Use 802.11g only — no 11b (enterprise APs reject it) and no 11n.
  // 11g at 6 Mbps BPSK/OFDM has the best receiver sensitivity (~-82 dBm)
  // of any non-11b rate, giving maximum range. 11n adds HT overhead that
  // slightly reduces sensitivity and gains us nothing — a clock doesn't
  // need throughput, it needs reliability at the edge of range.
  // Also avoids HT capability negotiation complexity with enterprise APs.
  esp_wifi_set_protocol(WIFI_IF_STA, WIFI_PROTOCOL_11G);

  // Start at maximum TX power (20dBm). The dynamic TX power adjustment
  // in WifiReconnect() will scale this back once connected and RSSI is
  // known. Starting high ensures the weakest devices can associate.
  WiFi.setTxPower(WIFI_POWER_20dBm);

  // Disable WiFi power save on the STA interface. Enterprise APs (especially
  // Cisco) track client power-save state and may timeout or deauth clients
  // that miss too many beacons while sleeping. For a mains-powered clock,
  // the ~80mA savings isn't worth the connectivity risk.
  esp_wifi_set_ps(WIFI_PS_NONE);

  // 20MHz bandwidth on STA — sufficient for our data needs (NTP, web UI)
  // and avoids 40MHz channel bonding issues that some enterprise configs
  // restrict or penalise.
  esp_wifi_set_bandwidth(WIFI_IF_STA, WIFI_BW_HT20);

  // Enable 802.11v BSS Transition Management and 802.11k Radio Resource
  // Management. These allow the AP to guide the client to a better AP
  // (load balancing, band steering) rather than forcibly disconnecting it.
  // Without these, the AP may use harder measures like deauth.
  esp_wifi_set_rssi_threshold(-75);  // Report to AP when signal drops
  // Note: full 802.11v/k/r support requires ESP-IDF menuconfig options
  // that aren't exposed in Arduino. The PMF and threshold settings are
  // the best we can do from the Arduino layer.

  // Start in AP+STA or STA-only mode
  if (should_start_ap) {
    WiFi.mode(WIFI_AP_STA);
    // Channel 1 is a non-overlapping 2.4GHz channel (1, 6, 11 are the
    // three non-overlapping channels). Open network, max 4 clients.
    WiFi.softAP(WifiAPName, NULL, 11, 0, 4);
    // 20MHz bandwidth reduces airtime, interference, and power draw.
    // 40MHz is unnecessary for a config portal serving ~10KB pages.
    esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW_HT20);
    APRunning = true;
    Serial.print("AP started: ");
    Serial.println(WifiAPName);
    Serial.print("AP IP: ");
    Serial.println(WiFi.softAPIP());

    // Show AP connection info on the clock's TFT displays so the user
    // knows which network to join and what IP to visit
    tfts.chip_select.set_all();
    tfts.fillScreen(TFT_BLACK);
    tfts.chip_select.set_hours_tens();
    tfts.setTextColor(TFT_WHITE, TFT_BLACK);
    tfts.setCursor(0, 0, 2);
    tfts.println("AP Mode:");
    tfts.println(WifiAPName);
    tfts.println(WiFi.softAPIP());
  } else {
    WiFi.mode(WIFI_STA);
  }

  // Register event callbacks instead of polling WiFi.status() in loop().
  // The ESP32 WiFi driver fires these from a FreeRTOS system task.
  WiFi.onEvent(WiFiStaConnected, ARDUINO_EVENT_WIFI_STA_CONNECTED);
  WiFi.onEvent(WiFiStaGotIP, ARDUINO_EVENT_WIFI_STA_GOT_IP);
  WiFi.onEvent(WiFiStaDisconnected, ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

  // Connect to saved network if credentials exist
  if (wifi_config->is_valid == StoredConfig::valid &&
      wifi_config->ssid[0] != '\0') {
    Serial.print("Connecting to: ");
    Serial.println(wifi_config->ssid);
    tfts.print("WiFi: ");
    tfts.println(wifi_config->ssid);

    // Hostname setup quirk on ESP32: WiFi.setHostname() only works if called
    // AFTER WiFi.config() but BEFORE WiFi.begin(). The config() call with all
    // INADDR_NONE tells the stack to use DHCP but also enables hostname setting.
    // We also set it via the ESP-IDF netif API as a belt-and-suspenders approach
    // because the Arduino wrapper doesn't always propagate the hostname to the
    // DHCP client (depends on ESP-IDF version).
    WiFi.config(INADDR_NONE, INADDR_NONE, INADDR_NONE, INADDR_NONE);
    WiFi.setHostname(WifiAPName);
    esp_netif_t *netif = esp_netif_get_handle_from_ifkey("WIFI_STA_DEF");
    if (netif) esp_netif_set_hostname(netif, WifiAPName);
    Serial.print("Hostname set: ");
    Serial.println(WifiAPName);

    // Enable Protected Management Frames (802.11w) before connecting.
    // PMF prevents deauthentication attacks where an attacker spoofs disconnect
    // frames to knock the clock off WiFi. Setting capable=true (not required)
    // means we use PMF with supporting routers and fall back gracefully for older ones.
    // Must be configured before WiFi.begin() — the ESP32 rejects config changes
    // while a connection is in progress.
    wifi_config_t wifi_conf;
    memset(&wifi_conf, 0, sizeof(wifi_conf));
    strncpy((char *)wifi_conf.sta.ssid, wifi_config->ssid, sizeof(wifi_conf.sta.ssid));
    strncpy((char *)wifi_conf.sta.password, wifi_config->password, sizeof(wifi_conf.sta.password));

    // 802.11w Protected Management Frames: advertise capability so
    // enterprise APs know we support it. Not required (for backward compat).
    wifi_conf.sta.pmf_cfg.capable = true;
    wifi_conf.sta.pmf_cfg.required = false;

    // Use a full all-channel scan before connecting, rather than stopping at
    // the first matching SSID. Enterprise deployments often have multiple APs
    // on the same SSID — full scan lets the ESP32 pick the strongest one.
    wifi_conf.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    wifi_conf.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;

    // Accept WPA2 and WPA3 (enterprise APs may use either)
    wifi_conf.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;

    esp_wifi_set_config(WIFI_IF_STA, &wifi_conf);

    WiFi.begin();  // Uses the config we just set via esp_wifi_set_config

    // Block during initial connection (at boot only). This ensures NTP
    // can run immediately after WifiBegin returns. The timeout prevents
    // hanging forever if the saved network is unavailable.
    unsigned long start_time = millis();
    while (WiFi.status() != WL_CONNECTED) {
      delay(500);
      tfts.print(".");
      Serial.print(".");
      if ((millis() - start_time) > (WIFI_CONNECT_TIMEOUT_SEC * 1000)) {
        Serial.println("\r\nWiFi connection timeout!");
        tfts.println("\nTIMEOUT!");
        // Set reconnect timer so WifiReconnect() backs off rather than
        // immediately retrying and potentially interrupting a background
        // connection attempt that's still in progress.
        reconnect_attempt_time = millis();
        return;
      }
    }

    WifiState = connected;
    tfts.println("\n Connected!");
    tfts.println(WiFi.localIP());
    Serial.println("");
    Serial.print("Connected to ");
    Serial.println(WiFi.SSID());
    Serial.print("IP address: ");
    Serial.println(WiFi.localIP());
    delay(200);
  } else {
    Serial.println("No WiFi credentials configured");
  }
}

// Called from main loop() every iteration. Handles three things:
// 1. Reconnection after disconnects (with backoff)
// 2. Dynamic TX power adjustment based on RSSI
// 3. Proactive roaming to a better AP when signal is weak
// 4. Watchdog — force reconnect if connected but no IP traffic
void WifiReconnect() {
  static uint32_t last_tx_adjust = 0;
  static uint32_t last_roam_check = 0;
  static uint32_t last_watchdog_check = 0;
  static int consecutive_low_rssi = 0;

  // --- 1. Reconnection ---
  if (WifiState == disconnected &&
      stored_config.config.wifi.is_valid == StoredConfig::valid &&
      stored_config.config.wifi.ssid[0] != '\0' &&
      (millis() - reconnect_attempt_time) > WIFI_RETRY_CONNECTION_SEC * 1000) {
    WiFi.setTxPower(WIFI_POWER_20dBm);  // Max power for association
    Serial.println("Attempting WiFi reconnection...");
    WiFi.disconnect();   // Clean slate — release any stale association
    delay(100);
    WiFi.reconnect();
    reconnect_attempt_time = millis();
  }

  // --- 2. Dynamic TX power ---
  // Adjust every 30 seconds. Uses our RSSI (how well we hear the AP)
  // as a proxy — if we can barely hear them, they can barely hear us.
  if (WifiState == connected && (millis() - last_tx_adjust) > 30000) {
    int rssi = WiFi.RSSI();
    wifi_power_t tx;
    if (rssi > -55) {
      tx = WIFI_POWER_11dBm;       // Strong — save power
    } else if (rssi > -65) {
      tx = WIFI_POWER_15dBm;       // Good — moderate
    } else if (rssi > -75) {
      tx = WIFI_POWER_17dBm;       // Fair — boost
    } else {
      tx = WIFI_POWER_20dBm;       // Weak — maximum
    }
    WiFi.setTxPower(tx);
    last_tx_adjust = millis();
  }

  // --- 3. Proactive roaming ---
  // If signal has been poor for 3 consecutive checks (90 seconds),
  // disconnect and re-scan to find a better AP. The WIFI_ALL_CHANNEL_SCAN
  // and WIFI_CONNECT_AP_BY_SIGNAL settings in WifiBegin() ensure reconnect
  // picks the strongest AP available.
  if (WifiState == connected && (millis() - last_roam_check) > 30000) {
    int rssi = WiFi.RSSI();
    if (rssi < -78) {
      consecutive_low_rssi++;
      if (consecutive_low_rssi >= 3) {
        Serial.printf("Roaming: RSSI %d dBm for 90s, scanning for better AP\n", rssi);
        WiFi.setTxPower(WIFI_POWER_20dBm);
        WiFi.disconnect();
        delay(100);
        WiFi.reconnect();   // Will do full scan and pick strongest AP
        consecutive_low_rssi = 0;
        reconnect_attempt_time = millis();
      }
    } else {
      consecutive_low_rssi = 0;
    }
    last_roam_check = millis();
  }

  // --- 4. Watchdog ---
  // Every 5 minutes, verify the connection is genuinely alive by checking
  // if we still have an IP. Catches "zombie" connections where WifiState
  // is connected but the AP has silently dropped us.
  if (WifiState == connected &&
      (millis() - last_watchdog_check) > 300000) {
    last_watchdog_check = millis();
    if (WiFi.localIP() == IPAddress(0, 0, 0, 0)) {
      Serial.println("Watchdog: connection stale, forcing reconnect");
      WifiState = disconnected;
      WiFi.disconnect();
      delay(100);
      WiFi.reconnect();
      reconnect_attempt_time = millis();
    }
  }
}
