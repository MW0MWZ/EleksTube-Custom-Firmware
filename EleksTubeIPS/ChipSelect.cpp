// ChipSelect.cpp — Drives the 74HC595 shift register to select TFT displays
//
// The 74HC595 is clocked via bit-banged SPI (not the hardware SPI peripheral,
// which is reserved for the TFT data bus). This is fine because CS changes
// happen infrequently (once per digit update) and speed is not critical.

#include "ChipSelect.h"

void ChipSelect::begin() {
  // Configure the three shift register control pins as outputs
  pinMode(CSSR_LATCH_PIN, OUTPUT);
  pinMode(CSSR_DATA_PIN, OUTPUT);
  pinMode(CSSR_CLOCK_PIN, OUTPUT);

  // Start with all lines low — safe idle state before first update
  digitalWrite(CSSR_DATA_PIN, LOW);
  digitalWrite(CSSR_CLOCK_PIN, LOW);
  digitalWrite(CSSR_LATCH_PIN, LOW);

  // Push initial state (all CS lines deasserted) to the shift register
  update();
}

void ChipSelect::update() {
  // The 74HC595 has 8 outputs (Q0-Q7). Only Q0-Q5 are wired to displays.
  // Q6 and Q7 are unused, so we shift digits_map left by 2 to skip them.
  //
  // Inversion (~) is needed because:
  //   - Our digits_map uses 1 = "select this display"
  //   - But ST7789 CS is active-low: 0 on the pin = selected
  //   - So we flip all bits before shifting out
  uint8_t to_shift = (~digits_map) << 2;

  // 74HC595 latch protocol: hold latch LOW while shifting data in,
  // then pulse HIGH to transfer the shift register to the output pins.
  digitalWrite(CSSR_LATCH_PIN, LOW);
  // LSBFIRST because Q0 (Hours Tens) is the first output pin in the chain
  shiftOut(CSSR_DATA_PIN, CSSR_CLOCK_PIN, LSBFIRST, to_shift);
  // Rising edge on latch copies shift register contents to output pins
  digitalWrite(CSSR_LATCH_PIN, HIGH);
}
