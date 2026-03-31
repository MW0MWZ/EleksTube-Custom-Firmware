// GLOBAL_DEFINES.h — Hardware abstraction layer for the EleksTube IPS clock
//
// This file maps logical pin names to physical ESP32 GPIO numbers and configures
// the TFT_eSPI graphics library at compile time. Three hardware variants exist
// (Si Hai, NovelLife SE, original EleksTube) that share the same ST7789 displays
// but differ in PCB layout, pin assignments, and RTC chip. The correct variant
// is selected by a single #define in USER_DEFINES.h; everything else adapts
// automatically via the conditional blocks below.
//
// TFT_eSPI integration: TFT_eSPI normally reads its pin config from a
// User_Setup.h file. By defining USER_SETUP_LOADED here, we override that
// mechanism and inject our own pin definitions directly, keeping all hardware
// config in one place per variant.

#ifndef GLOBAL_DEFINES_H_
#define GLOBAL_DEFINES_H_

#include <stdint.h>
#include <Arduino.h>

#include "USER_DEFINES.h"

// NVS (non-volatile storage) namespace for persisting user settings across reboots
#define SAVED_CONFIG_NAMESPACE "configs"

// -----------------------------------------------------------------------
// Digit index constants — map each TFT display position to an index 0..5.
// Index 0 is the rightmost display (seconds ones), index 5 is the leftmost
// (hours tens). This ordering matches the physical wiring of the 74HC595
// shift register that drives the chip-select lines.
// -----------------------------------------------------------------------
#define SECONDS_ONES (0)
#define SECONDS_TENS (1)
#define MINUTES_ONES (2)
#define MINUTES_TENS (3)
#define HOURS_ONES   (4)
#define HOURS_TENS   (5)
#define NUM_DIGITS   (6)

// =======================================================================
// Si Hai variant — uses a DS1302 RTC (3-wire serial interface, less
// accurate than the DS3231 I2C RTC used by the other two variants).
// Pin assignments differ significantly from the EleksTube/NovelLife boards.
// =======================================================================
#ifdef HARDWARE_SI_HAI_CLOCK

  // WS2812 addressable RGB LED data pin (one LED behind each display tube)
  #define BACKLIGHTS_PIN (32)

  // Buttons, active low, externally pulled up
  #define BUTTON_LEFT_PIN  (35)
  #define BUTTON_MODE_PIN  (34)
  #define BUTTON_RIGHT_PIN (39)
  #define BUTTON_POWER_PIN (36)

  // DS1302 RTC uses a proprietary 3-wire protocol (not I2C)
  #define DS1302_SCLK (33)
  #define DS1302_IO   (25)
  #define DS1302_CE   (26)

  // 74HC595 shift register pins for TFT chip-select multiplexing.
  // Only 3 GPIO pins are needed to address all 6 displays.
  #define CSSR_DATA_PIN  (4)
  #define CSSR_CLOCK_PIN (22)
  #define CSSR_LATCH_PIN (21)

  // A MOSFET controls power to all six TFT displays simultaneously.
  // HIGH = displays powered on. Used for power saving / blanking.
  #define TFT_ENABLE_PIN (2)

  // --- TFT_eSPI configuration for ST7789 135x240 IPS displays ---
  #define ST7789_DRIVER
  #define TFT_WIDTH  135
  #define TFT_HEIGHT 240
  #define CGRAM_OFFSET      // ST7789 has a 240x320 framebuffer; offset needed for 135px width
  #define TFT_SDA_READ      // Enable reading the display via SPI MOSI pin
  #define TFT_MOSI 19
  #define TFT_SCLK 18
  #define TFT_DC   16       // Data/Command selection pin
  #define TFT_RST  23       // Hardware reset pin (active low)

  // Font subsets to compile into flash (saves memory vs loading all fonts)
  #define LOAD_FONT2
  #define LOAD_FONT4

  #define SMOOTH_FONT
  #define SPI_FREQUENCY  40000000  // 40 MHz SPI clock — near the ST7789 max of 62.5 MHz

  // Tell TFT_eSPI to use our defines instead of its own User_Setup.h
  #define USER_SETUP_LOADED
#endif

// =======================================================================
// NovelLife SE variant — uses the better DS3231 I2C RTC.
// PCB has a gesture sensor connector (not supported in this firmware).
// Pin mappings are close to the original EleksTube but not identical.
// =======================================================================
#ifdef HARDWARE_NovelLife_SE_CLOCK

  #define BACKLIGHTS_PIN (12)

  // Buttons (gesture sensor not included in code)
  #define BUTTON_LEFT_PIN  (33)
  #define BUTTON_MODE_PIN  (32)
  #define BUTTON_RIGHT_PIN (35)
  #define BUTTON_POWER_PIN (34)

  // DS3231 RTC uses I2C — more accurate (built-in temperature compensation)
  #define RTC_SCL_PIN (22)
  #define RTC_SDA_PIN (21)

  // 74HC595 shift register for chip-select multiplexing
  #define CSSR_DATA_PIN  (14)
  #define CSSR_CLOCK_PIN (13)
  #define CSSR_LATCH_PIN (15)

  #define TFT_ENABLE_PIN (4)

  // --- TFT_eSPI configuration ---
  #define ST7789_DRIVER
  #define TFT_WIDTH  135
  #define TFT_HEIGHT 240
  #define CGRAM_OFFSET
  #define TFT_SDA_READ
  #define TFT_MOSI 23       // Note: MOSI pin differs from Si Hai variant
  #define TFT_SCLK 18
  #define TFT_DC   25
  #define TFT_RST  26

  #define LOAD_FONT2
  #define LOAD_FONT4

  #define SMOOTH_FONT
  #define SPI_FREQUENCY  40000000

  #define USER_SETUP_LOADED
#endif

// =======================================================================
// Original EleksTube variant — the most common version.
// Uses DS3231 I2C RTC. Pin layout is nearly identical to NovelLife SE
// except for the shift register clock/latch and TFT enable pins.
// =======================================================================
#ifdef HARDWARE_Elekstube_CLOCK

  #define BACKLIGHTS_PIN (12)

  // Buttons, active low, externally pulled up
  #define BUTTON_LEFT_PIN  (33)
  #define BUTTON_MODE_PIN  (32)
  #define BUTTON_RIGHT_PIN (35)
  #define BUTTON_POWER_PIN (34)

  // DS3231 RTC via I2C
  #define RTC_SCL_PIN (22)
  #define RTC_SDA_PIN (21)

  // 74HC595 shift register — note different clock/latch pins vs NovelLife
  #define CSSR_DATA_PIN  (14)
  #define CSSR_CLOCK_PIN (16)
  #define CSSR_LATCH_PIN (17)

  #define TFT_ENABLE_PIN (27)

  // --- TFT_eSPI configuration ---
  #define ST7789_DRIVER
  #define TFT_WIDTH  135
  #define TFT_HEIGHT 240
  #define CGRAM_OFFSET
  #define TFT_SDA_READ
  #define TFT_MOSI 23
  #define TFT_SCLK 18
  #define TFT_DC   25
  #define TFT_RST  26

  #define LOAD_FONT2
  #define LOAD_FONT4

  #define SMOOTH_FONT
  #define SPI_FREQUENCY  40000000

  #define USER_SETUP_LOADED

#endif

#endif // GLOBAL_DEFINES_H_
