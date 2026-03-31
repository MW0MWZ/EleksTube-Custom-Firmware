// EleksTubeIPS.ino -- Main firmware for the EleksTube IPS clock
//
// System architecture overview:
// This firmware drives an ESP32-based clock with six 135x240 IPS TFT displays,
// six WS2812 RGB backlight LEDs, four navigation buttons, WiFi connectivity,
// NTP time synchronization, and a built-in web configuration server.
//
// The firmware uses a cooperative multitasking model: the main loop() runs
// at ~50 Hz (20 ms period) and polls all subsystems sequentially. There are
// no RTOS tasks, interrupts for buttons, or async callbacks -- everything
// is driven from a single execution thread. This keeps the code simple and
// avoids concurrency bugs, at the cost of requiring each subsystem's loop()
// to complete quickly (non-blocking).
//
// Subsystem execution order in loop() matters:
//   1. WifiReconnect  -- reconnects WiFi if dropped (needs to run early)
//   2. webconfig.loop -- handles HTTP requests (needs WiFi)
//   3. buttons.loop   -- samples GPIO pins (must run before menu)
//   4. menu.loop      -- processes button states (must run after buttons)
//   5. backlights.loop -- updates LED animations
//   6. uclock.loop    -- reads RTC / NTP time
//   7. EveryFullHour  -- checks for night/day transition
//   8. updateClockDisplay -- pushes digit images to TFTs
//   9. Menu handling   -- applies menu changes to settings
//  10. LoadNextImage   -- loads one image chunk from SPIFFS (time-sliced)
//
// Author: Aljaz Ogrin
// Project: Alternative firmware for EleksTube IPS clock
// Original location: https://github.com/aly-fly/EleksTubeHAX
// Hardware: ESP32
// Based on: https://github.com/SmittyHalibut/EleksTubeHAX

#include <stdint.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <esp_bt.h>
#include "GLOBAL_DEFINES.h"
#include "Buttons.h"
#include "Backlights.h"
#include "TFTs.h"
#include "Clock.h"
#include "Menu.h"
#include "StoredConfig.h"
#include "WifiManager.h"
#include "WebConfig.h"

// Increase the main loop task stack from default 8KB to 16KB.
// The tinfl decompressor (used for compressed clock face images) needs
// ~11KB of stack for its internal state. 16KB gives comfortable headroom.
SET_LOOP_TASK_STACK_SIZE(16 * 1024);

// -----------------------------------------------------------------------
// Global subsystem instances -- one of each, shared across modules.
// Several .cpp files use `extern` declarations to access these directly
// (e.g., WebConfig.cpp modifies tfts and backlights when settings change).
// -----------------------------------------------------------------------
Backlights backlights;
Buttons buttons;
TFTs tfts;
Clock uclock;
Menu menu;
StoredConfig stored_config;
WebConfig webconfig;

// Night mode tracking -- last_hour is initialized to an impossible value
// (255) so the first hour check always fires on boot.
uint8_t last_hour = 255;

// Forward declarations
void updateClockDisplay(TFTs::show_t show = TFTs::yes);
void setupMenu();
void EveryFullHour();

// -----------------------------------------------------------------------
// setup() -- One-time initialization, runs before loop()
//
// Initialization order is critical:
//   1. Serial       -- needed for debug output during remaining init
//   2. StoredConfig -- loads saved settings from NVS; other subsystems
//                      need these values during their own begin()
//   3. Backlights   -- needs config for pattern/color/intensity
//   4. Buttons      -- configures GPIO inputs (no dependencies)
//   5. TFTs         -- initializes SPI and the six ST7789 displays
//   6. WiFi         -- must be up before NTP can sync
//   7. Clock        -- syncs time via NTP (needs WiFi)
//   8. WebConfig    -- starts HTTP server (needs WiFi and StoredConfig)
// -----------------------------------------------------------------------
void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("");
  Serial.println(FIRMWARE_VERSION);
  Serial.println("In setup().");

  // --- Power management ---
  // These settings reduce peak current draw significantly, which helps
  // with marginal USB power supplies or long cables that cause voltage drop.
  // WiFi TX at 20dBm (default) draws ~300-400mA peaks; at 13dBm it's ~200mA.
  // For a clock sitting meters from a router, 13dBm is more than sufficient.

  // Disable Bluetooth radio -- saves ~30mA idle draw. The BT controller
  // memory is released back to the heap (~30KB gain).
  esp_bt_controller_disable();
  esp_bt_controller_mem_release(ESP_BT_MODE_BTDM);

  // Lower CPU frequency from 240MHz to 160MHz. A clock displaying images
  // and running a web server doesn't need 240MHz. Halves CPU core power
  // draw with no noticeable performance impact (SPI, WiFi, and timers
  // are clocked independently of the CPU).
  setCpuFrequencyMhz(160);

  // Load user settings from ESP32 NVS (non-volatile storage).
  // This must happen first because backlights, clock, and WiFi all
  // read their initial configuration from stored_config.config.
  stored_config.begin();
  stored_config.load();

  backlights.begin(&stored_config.config.backlights);
  buttons.begin();
  menu.begin();

  // Initialize all six TFT displays via SPI. The displays share a
  // single SPI bus and are addressed individually via a 74HC595 shift
  // register that drives their chip-select lines.
  tfts.begin();
  tfts.fillScreen(TFT_BLACK);
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.setCursor(0, 0, 2);
  tfts.println("setup...");

  // WiFi must be established before Clock because the clock uses NTP
  // for time synchronization, which requires network access.
  tfts.println("WiFi start");
  WifiBegin(&stored_config.config.wifi);

  // Brief delay before NTP query -- gives the WiFi stack time to
  // complete association and DHCP. The ">" characters provide visual
  // progress feedback on the TFT during boot.
  for (uint8_t ndx = 0; ndx < 5; ndx++) {
    tfts.print(">");
    delay(100);
  }
  tfts.println("");

  // Initialize the clock with NTP sync. Must happen after WiFi is up.
  tfts.println("Clock start");
  uclock.begin(&stored_config.config.uclock);

  // Start the web configuration server (HTTP on port 80).
  // If WiFi is in AP mode, this also starts the captive portal DNS.
  webconfig.begin(&stored_config);
  tfts.println("Done with setup.");

  // Leave boot messages visible so the user can read them (and see
  // any error messages). ~2 seconds total at 200 ms per tick.
  for (uint8_t ndx = 0; ndx < 10; ndx++) {
    tfts.print(">");
    delay(200);
  }

  // Set the active clock face graphics from the saved configuration
  tfts.current_graphic = uclock.getActiveGraphicIdx();

  // Clear boot messages and show the clock for the first time.
  // TFTs::force bypasses the "only update if digit changed" optimization
  // to ensure all six displays are drawn.
  tfts.fillScreen(TFT_BLACK);
  uclock.loop();
  updateClockDisplay(TFTs::force);
  Serial.println("Setup finished.");
}

// -----------------------------------------------------------------------
// loop() -- Main cooperative multitasking loop (~50 Hz)
//
// Every subsystem gets polled once per iteration. The loop aims for a
// 20 ms period: if all processing finishes early, the remaining time is
// used to pre-load the next clock face image from SPIFFS, then any
// leftover time is burned with delay() to maintain a consistent frame rate.
// -----------------------------------------------------------------------
void loop() {
  uint32_t millis_at_top = millis();

  // --- Phase 1: Subsystem maintenance ---
  WifiReconnect();     // Attempt WiFi reconnection if disconnected
  webconfig.loop();    // Handle pending HTTP requests (non-blocking)
  buttons.loop();      // Sample all four button GPIOs

  // Power button handling (only when not in menu mode).
  // When in menu mode, the power button exits the menu instead (handled
  // by Menu::loop). This dual-purpose avoids needing a dedicated exit button.
  if (buttons.power.is_down_edge() && (menu.get_state() == Menu::idle)) {
    tfts.toggleAllDisplays();
    backlights.togglePower();
  }

  // menu.loop() reads button states, so it must be called AFTER buttons.loop()
  menu.loop(buttons);
  backlights.loop();   // Update LED animation frame
  uclock.loop();       // Read current time from RTC/NTP

  // Check for hour transitions to apply night/day brightness changes
  EveryFullHour();

  // Push any changed digits to the TFT displays. In normal mode (TFTs::yes),
  // only digits that actually changed are redrawn, avoiding SPI bus overhead
  // and visible flicker.
  updateClockDisplay();

  // --- Phase 2: Menu state machine ---
  // Process menu events only if the state just changed AND displays are on.
  // Skipping when displays are off prevents writing menu text to a blanked
  // screen (which would flash briefly when displays are re-enabled).
  if (menu.state_changed_q() && tfts.isEnabled()) {
    Menu::states menu_state = menu.get_state();
    int8_t menu_change = menu.get_change();

    if (menu_state == Menu::idle) {
      // Returning to idle (menu closed): force-redraw all digits to
      // clear menu text, and persist any settings the user changed.
      updateClockDisplay(TFTs::force);
      Serial.print("Saving config...");
      stored_config.save();
      Serial.println(" Done.");
    } else {
      // --- Active menu items ---
      // Each menu state follows the same pattern:
      //   1. Apply the change (if any) to the relevant subsystem
      //   2. Call setupMenu() to prepare the display area
      //   3. Print the setting name and current value
      //
      // Menu text is rendered on the hours-tens display (leftmost),
      // in the bottom half of the 135x240 screen (y=120..240), leaving
      // the top half for the digit image.

      // Backlight pattern (off / constant / rainbow / pulse / breath)
      if (menu_state == Menu::backlight_pattern) {
        if (menu_change != 0) {
          backlights.setNextPattern(menu_change);
        }
        setupMenu();
        tfts.println("Pattern:");
        tfts.println(backlights.getPatternStr());
      }
      // LED color hue -- adjusts in steps of 16 for visible changes
      else if (menu_state == Menu::pattern_color) {
        if (menu_change != 0) {
          backlights.adjustColorPhase(menu_change * 16);
        }
        setupMenu();
        tfts.println("Color:");
        tfts.printf("%06X\n", backlights.getColor());
      }
      // Backlight brightness (0-7 range, mapped to WS2812 duty cycle)
      else if (menu_state == Menu::backlight_intensity) {
        if (menu_change != 0) {
          backlights.adjustIntensity(menu_change);
        }
        setupMenu();
        tfts.println("Intensity:");
        tfts.println(backlights.getIntensity());
      }
      // 12/24 hour toggle -- also force-redraws the hour digits since
      // the displayed value changes (e.g., 15 -> 3 PM)
      else if (menu_state == Menu::twelve_hour) {
        if (menu_change != 0) {
          uclock.toggleTwelveHour();
          tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), TFTs::force);
          tfts.setDigit(HOURS_ONES, uclock.getHoursOnes(), TFTs::force);
        }
        setupMenu();
        tfts.println("12 Hour?");
        tfts.println(uclock.getTwelveHour() ? "12 hour" : "24 hour");
      }
      // Leading zero blanking (e.g., " 9:30" vs "09:30")
      else if (menu_state == Menu::blank_hours_zero) {
        if (menu_change != 0) {
          uclock.toggleBlankHoursZero();
          // Only the tens digit is affected (it becomes blank or shows 0)
          tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), TFTs::force);
        }
        setupMenu();
        tfts.println("Blank zero?");
        tfts.println(uclock.getBlankHoursZero() ? "yes" : "no");
      }
      // Clock face selection -- each "graphic" is a numbered set of
      // digit images stored in SPIFFS (e.g., set 1 = nixie tube style,
      // set 2 = LED segment style, etc.)
      else if (menu_state == Menu::selected_graphic) {
        if (menu_change != 0) {
          uclock.adjustClockGraphicsIdx(menu_change);
          tfts.current_graphic = uclock.getActiveGraphicIdx();
          updateClockDisplay(TFTs::force);
        }
        setupMenu();
        tfts.println("Selected");
        tfts.println(" graphic:");
        tfts.printf("    %d\n", uclock.getActiveGraphicIdx());
      }
      // WiFi AP toggle -- also starts/stops the captive portal DNS
      // so newly connected clients get redirected to the config page
      else if (menu_state == Menu::toggle_ap) {
        if (menu_change != 0) {
          WifiToggleAP();
          if (APRunning) { webconfig.startDNS(); } else { webconfig.stopDNS(); }
        }
        setupMenu();
        tfts.println("WiFi AP:");
        tfts.println(APRunning ? "ON" : "OFF");
      }
      // Factory reset — press left or right to confirm and execute.
      // Single-press confirmation is intentional: the user must navigate
      // through all 7 preceding menu items to reach this option, which
      // provides sufficient protection against accidental activation.
      // The mode button skips past without triggering, and the 10-second
      // idle timeout returns to clock display if the user walks away.
      else if (menu_state == Menu::factory_reset) {
        if (menu_change != 0) {
          setupMenu();
          tfts.println("Resetting...");
          delay(500);
          stored_config.factory_reset();
          delay(500);
          ESP.restart();
        }
        setupMenu();
        tfts.println("Factory");
        tfts.println(" Reset?");
        tfts.println("L/R=Yes");
      }
    }
  }

  // --- Phase 3: Frame rate management and background image loading ---
  // If the loop completed in under 20 ms, use the remaining time to
  // incrementally load the next clock face image from SPIFFS. Image
  // loading is time-sliced: LoadNextImage() loads one chunk per call
  // to avoid blocking the main loop (a full image is ~64 KB, too large
  // to load in one shot without causing visible lag).
  uint32_t time_in_loop = millis() - millis_at_top;
  if (time_in_loop < 20) {
    tfts.LoadNextImage();

    // If there's still time left after image loading, sleep the remainder
    // to maintain the ~50 Hz cadence and reduce power consumption
    time_in_loop = millis() - millis_at_top;
    if (time_in_loop < 20) {
      delay(20 - time_in_loop);
    }
  }
#ifdef DEBUG_OUTPUT
  if (time_in_loop <= 1) Serial.print(".");
  else Serial.println(time_in_loop);
#endif
}

// Prepares the hours-tens TFT display for rendering menu text.
// Clears the bottom half of the screen (below the digit image)
// and positions the text cursor for menu output.
void setupMenu() {
  tfts.chip_select.set_hours_tens();  // Address the leftmost display via the 74HC595
  tfts.setTextColor(TFT_WHITE, TFT_BLACK);
  tfts.fillRect(0, 120, 135, 120, TFT_BLACK);  // Clear bottom half only
  tfts.setCursor(0, 124, 4);  // Font 4 = medium size, good for menu readability
}

// Determines if the current hour falls within the configured "night" window.
// Handles the wraparound case where night spans midnight (e.g., 22:00 - 07:00).
//
// Two cases based on the relationship between day_time and night_time:
//   day < night (e.g., day=7, night=22): night is 22..23,0..6 (spans midnight)
//   day >= night (e.g., day=22, night=7): night is 7..21 (within a single day)
bool isNightTime(uint8_t current_hour) {
  uint8_t day = stored_config.config.uclock.day_time;
  uint8_t night = stored_config.config.uclock.night_time;
  if (day < night) {
    // Normal case: daytime starts before night starts
    // Night window wraps around midnight: [night..23] + [0..day-1]
    return (current_hour < day) || (current_hour >= night);
  } else {
    // Inverted case: night window is contained within a single calendar day
    return (current_hour >= night) && (current_hour < day);
  }
}

// Runs once per hour transition to apply night/day brightness settings.
// Uses edge detection (last_hour != current_hour) to trigger exactly once
// when the hour changes, not every loop iteration.
void EveryFullHour() {
  uint8_t current_hour = uclock.getHour24();
  if (current_hour != last_hour) {
    Serial.print("current hour = ");
    Serial.println(current_hour);
    if (stored_config.config.uclock.night_mode_enabled && isNightTime(current_hour)) {
      // --- Night mode: dim or disable displays and backlights ---
      Serial.println("Setting night mode (dimmed)");
      uint8_t tft_dim = stored_config.config.uclock.night_tft_intensity;
      uint8_t led_dim = stored_config.config.uclock.night_led_intensity;
      tfts.dimming = tft_dim;
      if (tft_dim == 0) { tfts.disableAllDisplays(); }  // MOSFET cuts power to all TFTs
      backlights.dimmed_intensity = led_dim;
      if (led_dim == 0) { backlights.PowerOff(); }  // All WS2812 LEDs off
      // Images must be reloaded because dimming is applied during the
      // pixel-by-pixel BMP decode, not as a post-process filter
      tfts.InvalidateImageInBuffer();
      backlights.dimming = true;
      if (menu.get_state() == Menu::idle) {  // Skip redraw if menu is active
        updateClockDisplay(TFTs::force);
      }
    } else {
      // --- Daytime mode: restore full brightness ---
      Serial.println("Setting daytime mode (normal brightness)");
      tfts.dimming = stored_config.config.uclock.day_tft_intensity;
      tfts.enableAllDisplays();   // Re-enable the TFT power MOSFET
      backlights.PowerOn();       // Re-enable WS2812 LEDs
      tfts.InvalidateImageInBuffer();  // Force image reload with new brightness
      backlights.dimming = false;
      if (menu.get_state() == Menu::idle) {  // Skip redraw if menu is active
        updateClockDisplay(TFTs::force);
      }
    }
    last_hour = current_hour;  // Arm edge detection for the next hour
  }
}

// Pushes the current time digits to the six TFT displays.
// In normal mode (show=yes), only digits whose value changed since the
// last call are redrawn. In force mode, all six are redrawn unconditionally.
// Update order starts from seconds (fastest changing) to hours (slowest),
// which is cosmetically better -- the most-active digits update first.
void updateClockDisplay(TFTs::show_t show) {
  tfts.setDigit(SECONDS_ONES, uclock.getSecondsOnes(), show);
  tfts.setDigit(SECONDS_TENS, uclock.getSecondsTens(), show);
  tfts.setDigit(MINUTES_ONES, uclock.getMinutesOnes(), show);
  tfts.setDigit(MINUTES_TENS, uclock.getMinutesTens(), show);
  tfts.setDigit(HOURS_ONES, uclock.getHoursOnes(), show);
  tfts.setDigit(HOURS_TENS, uclock.getHoursTens(), show);
}
