// WebConfig.h -- Embedded web server with authentication and OTA updates
//
// Serves an SPA from PROGMEM with REST API endpoints for clock configuration.
// Includes dashboard password authentication, CSRF protection, rate limiting,
// and HTTP OTA firmware updates via file upload from the browser.

#ifndef WEBCONFIG_H
#define WEBCONFIG_H

#include <FS.h>
using fs::FS;
#include <WebServer.h>
#include <DNSServer.h>
#include <Update.h>
#include "StoredConfig.h"

class WebConfig {
public:
  WebConfig() : server(80), config(NULL) {}

  void begin(StoredConfig *stored_config);
  void loop();
  void startDNS();
  void stopDNS();

private:
  WebServer server;
  DNSServer dnsServer;
  StoredConfig *storedConfig;
  StoredConfig::Config *config;
  bool dns_running = false;

  // --- Authentication ---
  // Session token: generated on successful login, stored in a cookie.
  // Empty string means no active session. Regenerated on each login.
  char session_token[17];
  bool auth_required();          // True if a dashboard password is set
  bool check_auth();             // Verify session cookie, send 401 if invalid
  void handleLogin();            // POST /api/login
  void handleSetPassword();      // POST /api/set_password
  void sha256_hex(const char *input, char *output);  // SHA-256 hash to hex string

  // --- CSRF ---
  char csrf_token[17];
  void generate_csrf_token();
  bool validate_csrf_token();
  bool validate_request();       // Rate limit + Host header + auth + CSRF

  // --- Rate limiter ---
  float rate_tokens = 10.0f;
  uint32_t rate_last_update = 0;
  bool check_rate_limit();

  // --- OTA ---
  void handleOTAUpload();        // POST /api/ota (multipart file upload)
  void handleOTAUploadData();    // Upload data handler (called per chunk)
  size_t ota_bytes_written = 0;
  bool ota_success = false;
  bool ota_authenticated = false;
  bool ota_is_spiffs = false;       // true if uploading SPIFFS image, false for firmware

  // --- REST API handlers ---
  void handleRoot();
  void handleGetConfig();
  void handleSetConfig();
  void handleScanWifi();
  void handleGetStatus();
  void handleNTPSync();
  void handleFacePreview();
  void handlePreview();
  void handleFactoryReset();
  void handleNotFound();
};

#endif // WEBCONFIG_H
