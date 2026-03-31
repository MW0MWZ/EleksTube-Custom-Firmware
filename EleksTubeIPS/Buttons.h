// Buttons.h -- Debounced button input with edge and long-press detection
//
// Provides a poll-based button state machine suitable for cooperative
// multitasking on ESP32 (no interrupts, no blocking delays). Each Button
// object is polled once per main loop iteration. The state machine tracks
// press edges, sustained holds, and release edges, allowing callers to
// distinguish short taps from long presses without any timing logic of
// their own.
//
// The Buttons class aggregates all four physical buttons on the EleksTube
// hardware (left, mode, right, power) and polls them together.

#ifndef BUTTONS_H
#define BUTTONS_H

#include "GLOBAL_DEFINES.h"
#include <Arduino.h>

// Tracks button state asynchronously between calls to loop().
// Only the state at the time of loop() is registered.
//
// Design choice: polling vs. interrupts
// Interrupts would give instant response but add complexity (volatile
// state, critical sections, bounce filtering in ISR context). Since the
// main loop runs every ~20 ms, polling gives perfectly adequate latency
// for human button presses while keeping the code simple and deterministic.
class Button {
public:
  // pin:           GPIO number (hardware-specific, defined in GLOBAL_DEFINES.h)
  // active_state:  LOW for active-low buttons (pulled high, grounded when pressed)
  // long_press_ms: threshold for distinguishing a tap from a hold
  Button(uint8_t pin, uint8_t active_state = LOW, uint32_t long_press_ms = 500)
    : pin(pin), active_state(active_state), long_press_ms(long_press_ms),
      down_last_time(false), state_changed(false), millis_at_last_transition(0), button_state(idle) {}

  // State machine transitions (one-way arrows):
  //
  //   idle --> down_edge --> down --> down_long_edge --> down_long
  //                          |                            |
  //                          v                            v
  //                       up_edge                    up_long_edge
  //                          |                            |
  //                          +----------------------------+
  //                                       |
  //                                       v
  //                                      idle
  //
  // "Edge" states last exactly one loop() call, providing single-shot
  // event detection. Callers check is_down_edge() to trigger an action
  // exactly once per press, avoiding repeated firing while held.
  enum state {
    idle, down_edge, down, down_long_edge, down_long,
    up_edge, up_long_edge, num_states
  };

  void begin();
  void loop();

  state get_state()       { return button_state; }
  // Returns true if the state changed during the most recent loop() call
  bool state_changed_q()  { return state_changed; }
  // How long the button has been in its current state (for UI hold timers)
  uint32_t millis_in_state() { return millis_at_last_loop - millis_at_last_transition; }

  // --- Exact state queries ---
  bool is_idle()           { return button_state == idle; }
  bool is_down_edge()      { return button_state == down_edge; }
  bool is_down()           { return button_state == down; }
  bool is_down_long_edge() { return button_state == down_long_edge; }
  bool is_down_long()      { return button_state == down_long; }
  bool is_up_edge()        { return button_state == up_edge; }
  bool is_up_long_edge()   { return button_state == up_long_edge; }

  // --- Aggregate queries for "is the button physically held?" ---
  // is_down_longy: true if long-press threshold has been reached (edge or sustained)
  bool is_down_longy()     { return button_state == down_long_edge || button_state == down_long; }
  // is_downy: true if the button is physically pressed in any sub-state
  bool is_downy()          { return button_state == down_edge || button_state == down || is_down_longy(); }
  // is_upy: true if the button is not physically pressed
  bool is_upy()            { return button_state == idle || button_state == up_edge || button_state == up_long_edge; }

private:
  const uint8_t pin;
  const uint8_t active_state;      // What digitalRead returns when pressed (LOW for active-low)
  const uint32_t long_press_ms;    // Duration threshold for long-press detection

  bool down_last_time;             // Previous loop's physical reading (for edge detection)
  bool state_changed;              // True if state transitioned this loop iteration
  uint32_t millis_at_last_transition;  // Timestamp of last state change (for long-press timing)
  uint32_t millis_at_last_loop;        // Timestamp of current loop() call
  state button_state;              // Current position in the state machine

  // Raw GPIO read, abstracted so active_state can be HIGH or LOW
  bool is_button_down() { return digitalRead(pin) == active_state; }
};

// Aggregates all four physical buttons into a single object.
// The EleksTube has four buttons: left/right (value adjust), mode (menu
// cycle), and power (display on/off or menu exit).
class Buttons {
public:
  Buttons()
    : left(BUTTON_LEFT_PIN), mode(BUTTON_MODE_PIN),
      right(BUTTON_RIGHT_PIN), power(BUTTON_POWER_PIN) {}

  void begin() { left.begin(); mode.begin(); right.begin(); power.begin(); }
  void loop()  { left.loop(); mode.loop(); right.loop(); power.loop(); }
  // True if any button changed state this loop -- used to avoid processing
  // when nothing happened (saves CPU in the cooperative main loop)
  bool state_changed() {
    return left.state_changed_q() || mode.state_changed_q()
        || right.state_changed_q() || power.state_changed_q();
  }

  Button left, mode, right, power;
};

#endif // BUTTONS_H
