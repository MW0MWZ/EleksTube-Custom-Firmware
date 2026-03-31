// Menu.cpp -- Menu state machine driven by button edge events
//
// The menu responds only to down_edge events (the instant a button is
// first pressed), not sustained holds. This gives crisp one-action-per-press
// behavior. Each loop() call produces at most one state transition, enforced
// by early returns after each handled case.

#include "Menu.h"

void Menu::loop(Buttons& buttons) {
  // Snapshot all button states. These are from Button::loop() which
  // must have already been called this iteration (order matters).
  Button::state left_state  = buttons.left.get_state();   // decrement
  Button::state mode_state  = buttons.mode.get_state();   // next menu
  Button::state right_state = buttons.right.get_state();  // increment
  Button::state power_state = buttons.power.get_state();  // exit menu

  // Reset per-loop outputs. Callers check these after loop() returns.
  change = 0;
  state_changed = false;

  // Fast path: if we're idle and no buttons are active, skip all logic.
  // This is the common case (clock just ticking) and avoids unnecessary work.
  if (state == idle && left_state == Button::idle
      && right_state == Button::idle && mode_state == Button::idle) {
    return;
  }

  // Auto-timeout: return to idle if no button has been pressed for a while.
  // This prevents the menu from permanently obscuring the clock display.
  if (state != idle && millis() - millis_last_button_press > idle_timeout_ms) {
    state = idle;
    state_changed = true;
    return;
  }

  // Wake the menu: any button edge while idle enters the first menu item.
  // We enter states(1) -- the first real menu item, skipping idle (0).
  if (state == idle && (left_state == Button::down_edge
      || right_state == Button::down_edge || mode_state == Button::down_edge)) {
    state = states(1);
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Cycle to next menu item with mode button.
  // Uses modular arithmetic to wrap from the last item back to the first
  // (but skipping idle=0, since that would exit the menu).
  if (state != idle && mode_state == Button::down_edge) {
    uint8_t new_state = (uint8_t(state) + 1) % num_states;
    if (new_state == 0) {
      new_state = 1; // skip idle when cycling
    }
    state = states(new_state);
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }

  // Exit menu immediately with the power button
  if (state != idle && power_state == Button::down_edge) {
    state = idle;
    state_changed = true;
    return;
  }

  // Left/right buttons adjust the current setting's value.
  // Both can fire simultaneously (hardware allows it), so we check
  // each independently and accumulate the net change.
  if (state != idle && (left_state == Button::down_edge || right_state == Button::down_edge)) {
    if (left_state == Button::down_edge) {
      change--;
    }
    if (right_state == Button::down_edge) {
      change++;
    }
    millis_last_button_press = millis();
    state_changed = true;
    return;
  }
}
