// Button.cpp -- Debounced button state machine implementation
//
// Implements edge detection and long-press timing using a simple
// two-sample comparison (current reading vs. previous reading).
//
// Note on debouncing: This design relies on the main loop period (~20 ms)
// being longer than typical switch bounce (~5-10 ms). Because we only
// sample once per loop iteration, mechanical bounce is naturally filtered
// out -- by the time the next loop() call reads the pin, the contacts
// have settled. No explicit debounce delay or capacitor filtering needed.

#include "Buttons.h"

void Button::begin() {
  millis_at_last_transition = millis();
  millis_at_last_loop = millis_at_last_transition;

  // Configure the GPIO as a digital input. The EleksTube buttons are
  // externally pulled high and short to ground when pressed (active-low).
  // INPUT mode is used (not INPUT_PULLUP) because the board has external
  // pull-up resistors already.
  pinMode(pin, INPUT);

  // Read the initial physical state so we don't generate a spurious
  // edge on the first call to loop()
  down_last_time = is_button_down();
  if (down_last_time) {
    button_state = down_edge;
  } else {
    button_state = idle;
  }
}

void Button::loop() {
  millis_at_last_loop = millis();
  bool down_now = is_button_down();

  state previous_state = button_state;

  // Determine the new state from a 2x2 matrix of (previous, current) readings.
  // This is the core of the state machine -- each combination maps to exactly
  // one outcome, making the logic exhaustive and easy to verify.

  if (down_last_time == false && down_now == false) {
    // Was up, still up -- nothing happening
    button_state = idle;
  }
  else if (down_last_time == false && down_now == true) {
    // Rising edge: button was just pressed this loop iteration.
    // Record the transition time so we can measure hold duration.
    button_state = down_edge;
    millis_at_last_transition = millis_at_last_loop;
  }
  else if (down_last_time == true && down_now == true) {
    // Button is being held. Check if we've crossed the long-press threshold.
    // The transition timestamp is NOT updated here -- it stays at the original
    // press time so the duration measurement remains accurate.
    if (millis_at_last_loop - millis_at_last_transition >= long_press_ms) {
      if (previous_state == down_long_edge || previous_state == down_long) {
        // Already past the long-press edge; stay in sustained long-press
        button_state = down_long;
      } else {
        // Just crossed the threshold -- fire the long-press edge exactly once
        button_state = down_long_edge;
      }
    } else {
      // Still within the short-press window
      button_state = down;
    }
  }
  else if (down_last_time == true && down_now == false) {
    // Falling edge: button was just released. The up_edge vs. up_long_edge
    // distinction lets callers differentiate "short tap released" from
    // "long press released" (useful for cancel-on-release patterns).
    if (previous_state == down_long_edge || previous_state == down_long) {
      button_state = up_long_edge;
    } else {
      button_state = up_edge;
    }
    millis_at_last_transition = millis_at_last_loop;
  }

  // Track whether the state changed so callers can skip processing
  // when nothing happened (common in cooperative multitasking loops)
  state_changed = previous_state != button_state;
  // Save current reading for next iteration's edge detection
  down_last_time = down_now;
}
