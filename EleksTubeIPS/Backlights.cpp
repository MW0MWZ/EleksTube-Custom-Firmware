// Backlights.cpp — Pattern generation and animation for WS2812 tube backlights
//
// Animation approach: all patterns are non-blocking. Instead of using delays,
// each pattern computes its current state from millis() on every loop() call.
// This ensures the main loop stays responsive for button presses, WiFi, etc.

#include "Backlights.h"
#include <math.h>

// Gamma correction for WS2812 LEDs. Displays (phones, monitors) apply gamma
// internally, but WS2812 LEDs are linear — a value of 128 is roughly half
// the light output of 255. Without correction, colours picked on screen look
// washed out and wrong on the LEDs. Gamma 2.8 is the standard for WS2812.
static uint8_t gamma_correct(uint8_t v) {
  return (uint8_t)(pow((float)v / 255.0f, 2.8f) * 255.0f + 0.5f);
}

// Apply gamma correction to a 0xRRGGBB colour value
static uint32_t gamma_color(uint32_t color) {
  uint8_t r = gamma_correct((color >> 16) & 0xFF);
  uint8_t g = gamma_correct((color >> 8) & 0xFF);
  uint8_t b = gamma_correct(color & 0xFF);
  return ((uint32_t)r << 16) | ((uint32_t)g << 8) | b;
}

void Backlights::begin(StoredConfig::Config::Backlights *config_) {
  config = config_;

  // On first boot (or after NVS erase), the stored config will be invalid.
  // Load sensible defaults so the clock lights up immediately.
  if (config->is_valid != StoredConfig::valid) {
    Serial.println("Loaded Backlights config is invalid, using default.  This is normal on first boot.");
    setPattern(rainbow);
    setColorPhase(0);
    setIntensity(max_intensity - 1);  // Second-highest brightness (level 6 of 0..7)
    setPulseRate(72);                  // 72 BPM — roughly resting heart rate
    setBreathRate(10);                 // 10 breaths per minute — calm breathing rhythm
    config->fixed_color = 0;          // 0 = use color_phase instead of a fixed RGB value
    config->is_valid = StoredConfig::valid;
  }

  off = false;
}

void Backlights::setNextPattern(int8_t i) {
  // Wrap around in both directions (i can be negative for "previous")
  int8_t next_pattern = (config->pattern + i) % num_patterns;
  while (next_pattern < 0) {
    next_pattern += num_patterns;  // C++ modulo can return negative for negative inputs
  }
  setPattern(patterns(next_pattern));
}

void Backlights::adjustColorPhase(int16_t adj) {
  // Wrapping arithmetic on the colour wheel (0..767)
  int16_t new_phase = (int16_t(config->color_phase % max_phase) + adj) % max_phase;
  while (new_phase < 0) {
    new_phase += max_phase;
  }
  setColorPhase(new_phase);
}

void Backlights::adjustIntensity(int16_t adj) {
  int16_t new_intensity = (int16_t(config->intensity) + adj) % max_intensity;
  while (new_intensity < 0) {
    new_intensity += max_intensity;
  }
  setIntensity(new_intensity);
}

void Backlights::setIntensity(uint8_t intensity) {
  config->intensity = intensity;
  // Convert intensity level (0..7) to NeoPixel brightness (0..255) using
  // bit-shifting for a quasi-logarithmic scale. This matches human perception
  // of brightness better than a linear scale.
  // Level 7: 0xFF >> 0 = 255, Level 6: 0xFF >> 1 = 127, ... Level 0: 0xFF >> 7 = 1
  setBrightness(0xFF >> (max_intensity - config->intensity - 1));
  pattern_needs_init = true;
}

void Backlights::loop() {
  // Pattern dispatch — each pattern handles its own animation timing.
  // pattern_needs_init tells the pattern to do a full setup (set colours,
  // brightness) before starting animation. This flag is cleared at the end.
  if (off || config->pattern == dark) {
    if (pattern_needs_init) {
      clear();  // NeoPixel clear — sets all pixels to 0
      show();   // Push to LEDs via the WS2812 protocol
    }
  } else if (config->pattern == test) {
    testPattern();
  } else if (config->pattern == constant) {
    // Constant colour — only update on init (no animation needed)
    if (pattern_needs_init) {
      if (dimming) {
        setBrightness(0xFF >> (max_intensity - dimmed_intensity - 1));
      } else {
        setBrightness(0xFF >> (max_intensity - config->intensity - 1));
      }
      // fixed_color overrides the colour wheel if set (e.g., from web UI)
      if (config->fixed_color != 0) {
        fill(gamma_color(config->fixed_color));
      } else {
        fill(phaseToColor(config->color_phase));
      }
      show();
    }
  } else if (config->pattern == rainbow) {
    rainbowPattern();
  } else if (config->pattern == pulse) {
    pulsePattern();
  } else if (config->pattern == breath) {
    breathPattern();
  }

  pattern_needs_init = false;
}

// -----------------------------------------------------------------------
// Pulse pattern — a sine-wave brightness oscillation at a configurable BPM.
// Uses abs(sin()) so brightness goes: bright -> dim -> bright with no
// fully-off period, creating a heartbeat-like pulse effect.
// -----------------------------------------------------------------------
void Backlights::pulsePattern() {
  if (pattern_needs_init) {
    fill(config->fixed_color != 0 ? gamma_color(config->fixed_color) : phaseToColor(config->color_phase));
  }

  // Convert BPM to milliseconds per full cycle
  float pulse_length_millis = (60.0f * 1000) / config->pulse_bpm;
  // abs(sin()) keeps brightness always positive; range is 1..255
  float val = 1 + abs(sin(2 * M_PI * millis() / pulse_length_millis)) * 254;
  if (dimming) {
    val = val * dimmed_intensity / 7;  // Scale down for night mode
  }
  setBrightness((uint8_t)val);
  show();
}

// -----------------------------------------------------------------------
// Breath pattern — a more organic, asymmetric brightness curve using
// exp(sin(t)). This produces a slow rise and gentle fall that mimics
// natural breathing. The constants normalize the output to 0..255:
//   exp(sin(t)) ranges from exp(-1)=0.368 to exp(1)=2.718
//   Subtracting 0.368 and multiplying by 108 maps this to ~0..254
// -----------------------------------------------------------------------
void Backlights::breathPattern() {
  if (pattern_needs_init) {
    fill(config->fixed_color != 0 ? gamma_color(config->fixed_color) : phaseToColor(config->color_phase));
  }

  float pulse_length_millis = (60.0f * 1000) / config->breath_per_min;
  float val = (exp(sin(2 * M_PI * millis() / pulse_length_millis)) - 0.36787944f) * 108.0f;

  if (dimming) {
    val = val * dimmed_intensity / 7;
  }

  // Clamp minimum to 1 — NeoPixel brightness of 0 causes division-by-zero
  // issues in the Adafruit library's internal scaling math
  uint8_t brightness = (uint8_t)val;
  if (brightness < 1) { brightness = 1; }
  setBrightness(brightness);
  show();
}

// -----------------------------------------------------------------------
// Test pattern — cycles through each LED individually, showing red, green,
// blue, then off for each position. Useful for verifying wiring order and
// identifying dead LEDs.
// -----------------------------------------------------------------------
void Backlights::testPattern() {
  const uint8_t num_colors = 4;
  uint8_t num_states = NUM_DIGITS * num_colors;
  uint8_t state = (millis() / test_ms_delay) % num_states;

  uint8_t digit = state / num_colors;
  // Cycle through 0xFF0000 (red), 0x00FF00 (green), 0x0000FF (blue), 0x000000 (off)
  // by right-shifting 0xFF0000 by 8 bits per colour step
  uint32_t color = 0xFF0000 >> (state % num_colors) * 8;

  clear();
  setPixelColor(digit, color);
  show();
}

// -----------------------------------------------------------------------
// Colour wheel implementation — maps a phase value (0..767) to an RGB colour.
//
// The wheel is divided into three 256-step segments:
//   Phase   0..255: Red fades up,   Green is off,    Blue fades down
//   Phase 256..511: Red fades down,  Green fades up,  Blue is off
//   Phase 512..767: Red is off,      Green fades down, Blue fades up
//
// Each channel's contribution is a triangle wave offset by 256 steps (120 degrees).
// -----------------------------------------------------------------------

// Returns a single channel's intensity (0..255) for a given phase.
// Triangle wave: rises linearly 0->255 over phase 0..255,
// falls linearly 255->0 over phase 256..511, then stays at 0 for 512..767.
uint8_t Backlights::phaseToIntensity(uint16_t phase) {
  uint16_t color = 0;
  if (phase <= 255) {
    color = phase;          // Rising edge
  } else if (phase <= 511) {
    color = 511 - phase;    // Falling edge
  } else {
    color = 0;              // Off segment (remaining third of the wheel)
  }
  return uint8_t(color);
}

// Compose full RGB colour from three phase-offset triangle waves
uint32_t Backlights::phaseToColor(uint16_t phase) {
  uint8_t red = phaseToIntensity(phase);
  uint8_t green = phaseToIntensity((phase + 256) % max_phase);  // Offset by 1/3 of wheel
  uint8_t blue = phaseToIntensity((phase + 512) % max_phase);   // Offset by 2/3 of wheel
  return (uint32_t(red) << 16 | uint32_t(green) << 8 | uint32_t(blue));
}

// -----------------------------------------------------------------------
// Rainbow pattern — each LED shows a different colour from the wheel,
// and the whole pattern rotates over time. The phase offset between
// adjacent LEDs is (768 / 6) / 3 = ~42 steps, giving a subtle colour
// gradient across the six tubes.
// -----------------------------------------------------------------------
void Backlights::rainbowPattern() {
  // Phase spacing between adjacent digits — divided by 3 for a gentle gradient
  // (full spacing would make adjacent tubes very different colours)
  const uint16_t phase_per_digit = (max_phase / NUM_DIGITS) / 3;

  // Advance phase based on time: dividing millis by 16 gives ~60 colour
  // wheel rotations per minute — a slow, smooth colour shift
  uint16_t phase = millis() / 16 % max_phase;

  for (uint8_t digit = 0; digit < NUM_DIGITS; digit++) {
    uint16_t my_phase = (phase + digit * phase_per_digit) % max_phase;
    setPixelColor(digit, phaseToColor(my_phase));
  }
  if (dimming) {
    setBrightness((uint8_t)dimmed_intensity);
  } else {
    setBrightness(0xFF >> (max_intensity - config->intensity - 1));
  }
  show();
}

// Human-readable pattern names for display in the web config UI
const String Backlights::patterns_str[Backlights::num_patterns] =
  { "Dark", "Test", "Constant", "Rainbow", "Pulse", "Breath" };
