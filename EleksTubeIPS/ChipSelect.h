// ChipSelect.h — 74HC595 shift register driver for TFT chip-select multiplexing
//
// Problem: Six ST7789 TFT displays share a single SPI bus (MOSI, SCLK, DC, RST)
// but each needs its own chip-select (CS) line. The ESP32 doesn't have enough
// free GPIOs, so the hardware uses a 74HC595 8-bit shift register to generate
// six independent CS signals from only three GPIO pins (data, clock, latch).
//
// How it works:
//   1. The ESP32 shifts 8 bits into the 74HC595 via shiftOut() (bit-banged SPI).
//   2. Pulsing the latch pin transfers the shift register contents to the output pins.
//   3. Each output pin (Q0-Q5) connects to one TFT's CS line.
//   4. CS is active-low on the ST7789, so a 0 bit selects a display and 1 deselects it.
//   5. The class internally stores a "digits_map" where 1 = enabled, and inverts
//      it during update() so the user-facing API is intuitive (1 = selected).
//
// Bit ordering: The 74HC595 outputs Q0-Q7 map to the displays as follows:
//   Q0 = Hours Tens, Q1 = Hours Ones, ... Q5 = Seconds Ones, Q6-Q7 = unused.
//   But digits_map bit 0 = Seconds Ones (index 0) and bit 5 = Hours Tens (index 5),
//   so update() shifts the bits left by 2 to align with the Q0-Q5 wiring.

#ifndef CHIP_SELECT_H
#define CHIP_SELECT_H

#include "GLOBAL_DEFINES.h"

class ChipSelect {
public:
  ChipSelect() : digits_map(all_off) {}

  void begin();
  void update();

  // Set the raw digit selection bitmap.
  // Bit 0 (LSB) = SECONDS_ONES, bit 5 = HOURS_TENS.
  // 1 = enabled, 0 = disabled (active-low inversion handled in update())
  void set_digit_map(uint8_t map, bool update_ = true) { digits_map = map; if (update_) update(); }
  uint8_t get_digit_map() { return digits_map; }

  // Convenience functions — select a single digit, all digits, or none
  void set_digit(uint8_t digit, bool update_ = true) { set_digit_map(0x01 << digit, update_); }
  void set_all(bool update_ = true)                   { set_digit_map(all_on, update_); }
  void clear(bool update_ = true)                     { set_digit_map(all_off, update_); }

  // Named accessors for readability in calling code
  void set_seconds_ones()                             { set_digit(SECONDS_ONES); }
  void set_seconds_tens()                             { set_digit(SECONDS_TENS); }
  void set_minutes_ones()                             { set_digit(MINUTES_ONES); }
  void set_minutes_tens()                             { set_digit(MINUTES_TENS); }
  void set_hours_ones()                               { set_digit(HOURS_ONES); }
  void set_hours_tens()                               { set_digit(HOURS_TENS); }

private:
  uint8_t digits_map;
  const uint8_t all_on = 0x3F;   // Lower 6 bits set = all 6 displays selected
  const uint8_t all_off = 0x00;  // No displays selected
};

#endif // CHIP_SELECT_H
