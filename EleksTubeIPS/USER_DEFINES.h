// USER_DEFINES.h — User-configurable compile-time settings
//
// This is the ONE file users should edit before building. It controls:
//   1. Which hardware variant to compile for (must match your physical clock)
//   2. Debug verbosity
//   3. Image format (CLK vs BMP)
//   4. Night-mode dimming schedule and intensity
//   5. WiFi connection behaviour
//
// These are compile-time constants, not runtime settings. Changing them
// requires reflashing the firmware.

#ifndef USER_DEFINES_H_
#define USER_DEFINES_H_

// Uncomment to enable verbose serial output (image load times, pixel values, etc.)
// Increases code size and slows image rendering — disable for production.
// #define DEBUG_OUTPUT

// -----------------------------------------------------------------------
// Hardware variant selection — uncomment exactly ONE of these three lines.
// This drives all pin mappings and peripheral config in GLOBAL_DEFINES.h.
// Using the wrong variant will address wrong GPIO pins and likely produce
// a non-functional clock (or worse, short outputs on the PCB).
// -----------------------------------------------------------------------
#define HARDWARE_Elekstube_CLOCK
// #define HARDWARE_SI_HAI_CLOCK
// #define HARDWARE_NovelLife_SE_CLOCK

// Firmware version string shown in the web config UI and serial output.
// When building via GitHub Actions, this is overridden by the CI workflow
// using the git tag (e.g. -DFIRMWARE_VERSION='"EleksTubeIPS v0.9.0"').
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION    "EleksTubeIPS dev"
#endif

// Use .clk image files instead of .bmp. CLK is a simpler raw 16-bit RGB565
// format that loads faster because it skips BMP header parsing and palette
// lookup. Most community clock-face packs ship as CLK files.
#define USE_CLK_FILES

// -----------------------------------------------------------------------
// Night-mode dimming — reduces display and backlight brightness during
// sleeping hours to avoid lighting up a dark room.
// -----------------------------------------------------------------------
#define NIGHT_TIME                 1    // Hour (0-23) when dimming begins
#define DAY_TIME                   7    // Hour (0-23) when full brightness resumes
#define BACKLIGHT_DIMMED_INTENSITY 0    // WS2812 brightness during night (0..7, 0 = off)
#define TFT_DIMMED_INTENSITY      0    // TFT pixel dimming during night (0..255, 0 = black)

// -----------------------------------------------------------------------
// WiFi behaviour — the clock uses WiFi for NTP time sync. These timeouts
// prevent the clock from blocking forever if the network is unavailable.
// -----------------------------------------------------------------------
#define WIFI_CONNECT_TIMEOUT_SEC  30   // Max seconds to wait for initial connection
                                      // (enterprise APs may delay association by 10s+)
#define WIFI_RETRY_CONNECTION_SEC 30   // Seconds between reconnection attempts
                                      // (enterprise APs may request 10s+ comeback times)

#endif // USER_DEFINES_H_
