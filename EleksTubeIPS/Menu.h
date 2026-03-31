// Menu.h -- Button-driven settings menu with idle timeout
//
// Implements a simple linear menu that cycles through clock settings
// using the physical buttons. The menu overlays onto the hours-tens TFT
// display while active, and automatically returns to normal clock mode
// after a period of inactivity.
//
// Button mapping:
//   mode  -> enter menu / advance to next menu item (cycles through all)
//   left  -> decrement the current setting
//   right -> increment the current setting
//   power -> exit menu immediately (returns to idle)
//
// The menu does not directly modify hardware settings. Instead, it
// exposes get_state() and get_change() which the main loop reads to
// apply the appropriate changes to backlights, clock, WiFi, etc.
// This separation keeps the Menu class hardware-agnostic.

#ifndef MENU_H
#define MENU_H

#include <Arduino.h>
#include "Buttons.h"

class Menu {
public:
  Menu() : state(idle), change(0), millis_last_button_press(0) {}
  void begin() {}
  void loop(Buttons& buttons);

  // Menu states correspond 1:1 with configurable settings.
  // The order here defines the order the user cycles through them
  // by pressing the mode button. idle (0) is the default non-menu state.
  enum states {
    idle = 0,
    backlight_pattern,       // LED pattern: off, constant, rainbow, pulse, breath
    pattern_color,           // LED color hue (for patterns that use a fixed color)
    backlight_intensity,     // LED brightness level (0-7)
    twelve_hour,             // Toggle 12-hour vs 24-hour display
    blank_hours_zero,        // Toggle whether leading zero on hours is shown
    selected_graphic,        // Choose which clock face graphics set to use
    toggle_ap,               // Toggle the WiFi access point on/off
    factory_reset,           // Factory reset: left/right to confirm, wipes config and reboots
    num_states               // Sentinel: total count, used for wraparound math
  };

  states get_state()    { return state; }
  // Returns -1, 0, or +1 indicating the direction of the most recent adjustment
  int8_t get_change()   { return change; }
  // True if the menu state changed this loop iteration (enter, exit, navigate, or adjust)
  bool state_changed_q() { return state_changed; }

private:
  // Auto-return to idle after 10 seconds with no button press.
  // Prevents the menu from staying on-screen indefinitely if the
  // user walks away, which would block normal clock display.
  const uint16_t idle_timeout_ms = 10000;
  states state;
  int8_t change;                    // Accumulated left/right adjustment this loop
  uint32_t millis_last_button_press;  // Timestamp for idle timeout calculation
  bool state_changed;
};

#endif // MENU_H
