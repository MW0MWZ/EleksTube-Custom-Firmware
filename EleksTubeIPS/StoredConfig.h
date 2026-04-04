// StoredConfig.h -- Persistent configuration storage using ESP32 NVS
//
// All user-configurable settings (WiFi credentials, timezone, display
// preferences, backlight patterns) are stored in a single Config struct
// that is persisted to the ESP32's NVS (Non-Volatile Storage) flash
// partition via the Arduino Preferences library.
//
// Persistence strategy: The entire Config struct is read/written as a
// single binary blob (Preferences getBytes/putBytes). This is simpler
// than storing individual keys but means ANY config change rewrites the
// whole blob. This is fine for infrequent writes (settings changes),
// but would be problematic for high-frequency data (NVS flash has
// limited write endurance, typically ~100K cycles).
//
// Versioning / validity: Each sub-struct has an is_valid field that acts
// as a sentinel. On first boot, NVS contains 0x00 or 0xFF (erased flash).
// The sentinel value 0x55 is chosen specifically because it is neither
// 0x00 nor 0xFF, so we can reliably detect whether config has ever been
// written. Each module checks is_valid on startup and applies defaults
// if the config is uninitialized. When adding new fields to a sub-struct,
// existing fields remain valid as long as the struct only grows at the end.

#ifndef STORED_CONFIG_H
#define STORED_CONFIG_H

#include "GLOBAL_DEFINES.h"
#include <Preferences.h>

class StoredConfig {
public:
  StoredConfig() : prefs(), config_size(sizeof(config)), loaded(false) {}

  void begin() {
    // Open (or create) the NVS namespace. false = read-write mode.
    // Namespaces partition NVS so different libraries don't collide.
    prefs.begin(SAVED_CONFIG_NAMESPACE, false);
    Serial.print("Config size: ");
    Serial.println(config_size);
  }

  void load() {
    // Read the entire Config struct as a binary blob from NVS.
    // If the key doesn't exist yet (first boot), config remains zeroed.
    prefs.getBytes(SAVED_CONFIG_NAMESPACE, &config, config_size);
    loaded = true;
  }

  void save() {
    // Write the entire Config struct as a binary blob to NVS.
    // NVS handles wear leveling internally, but frequent saves
    // should still be avoided to preserve flash lifetime.
    prefs.putBytes(SAVED_CONFIG_NAMESPACE, &config, config_size);
  }

  void factory_reset() {
    // Zero the in-memory config and erase the NVS namespace.
    // On next boot, is_valid will be 0x00 (not 0x55), so all subsystems
    // will detect uninitialised config and load defaults.
    memset(&config, 0, config_size);
    prefs.clear();
  }

  bool is_loaded() { return loaded; }

  const static uint8_t str_buffer_size = 32;

  // ---- Configuration Data Structure ----
  // Laid out as sub-structs for logical grouping. Each sub-struct has its
  // own is_valid sentinel so modules can independently detect first-boot.
  // WARNING: Do not reorder or remove fields -- this would corrupt saved
  // configs on devices that have already written data. New fields should
  // be added at the end of each sub-struct.

  struct Config {
    struct Backlights {
      uint8_t  pattern;
      uint16_t color_phase;          // hue offset for rainbow patterns (0-65535)
      uint8_t  intensity;            // brightness 0-7 (WS2812 global)
      uint8_t  pulse_bpm;
      uint8_t  breath_per_min;
      uint32_t fixed_color;          // RGB color for constant pattern (0xRRGGBB)
      uint8_t  is_valid;             // must equal StoredConfig::valid if initialized
    } backlights;

    struct Clock {
      bool     twelve_hour;
      time_t   time_zone_offset;     // legacy manual offset in seconds (superseded by tz_string)
      bool     blank_hours_zero;     // suppress leading zero on hours display
      int8_t   selected_graphic;     // which clock face graphic set to use (1-based)
      char     tz_string[48];        // POSIX TZ string, e.g. "EST5EDT,M3.2.0,M11.1.0"
      uint8_t  night_time;           // hour to start night dimming (0-23)
      uint8_t  day_time;             // hour to end night dimming (0-23)
      bool     night_mode_enabled;
      uint8_t  day_tft_intensity;    // daytime TFT brightness (0-255)
      uint8_t  night_tft_intensity;  // night TFT brightness (0-255)
      uint8_t  night_led_intensity;  // night backlight LED brightness (0-7)
      uint16_t hue_shift;            // display hue rotation in degrees (0-359, 0 = off)
      uint8_t  is_valid;
    } uclock;

    struct Wifi {
      char     ssid[str_buffer_size];
      // SECURITY: password is stored in plaintext in NVS (unavoidable on ESP32).
      // It must NEVER be included in any API response -- it is write-only via
      // the web UI. See handleGetConfig() which deliberately omits this field.
      char     password[str_buffer_size];
      bool     ap_enabled;           // whether to start AP mode on boot
      // Dashboard login password stored as SHA-256 hex digest (64 chars + null).
      // Empty string means no auth required (first-boot / unconfigured state).
      char     dashboard_password_hash[65];
      uint8_t  is_valid;
    } wifi;
  } config;

  // Sentinel value for detecting initialized config.
  // 0x55 (binary 01010101) is chosen because erased flash reads as either
  // 0x00 or 0xFF depending on the flash technology, so this value cannot
  // appear by accident in unwritten memory.
  const static uint8_t valid = 0x55;

private:
  Preferences prefs;
  uint16_t config_size;
  bool loaded;
};

#endif // STORED_CONFIG_H
