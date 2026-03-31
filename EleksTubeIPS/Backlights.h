// Backlights.h — WS2812 RGB LED controller for tube backlighting
//
// Each of the six display tubes has a WS2812 addressable LED mounted behind
// it, providing coloured backlighting visible through the translucent tube
// housing. This class inherits from Adafruit_NeoPixel to drive the LEDs
// and adds pattern generation (rainbow, pulse, breath), colour phase control,
// and night-mode dimming.
//
// Colour model: colours are specified as a "phase" value (0..767) that maps
// to a position on a continuous RGB colour wheel. See phaseToColor() for
// the mapping. This makes it easy to create smooth rainbow effects by
// advancing the phase over time.
//
// Configuration is stored via a pointer to the StoredConfig struct, so
// settings persist across reboots in NVS flash.

#ifndef BACKLIGHTS_H
#define BACKLIGHTS_H

#include "GLOBAL_DEFINES.h"
#include <stdint.h>
#include "StoredConfig.h"
#include <Adafruit_NeoPixel.h>

class Backlights : public Adafruit_NeoPixel {
public:
  // Initialize NeoPixel with 6 LEDs (one per tube), GRB colour order
  // (standard for WS2812), 800 kHz data rate
  Backlights() : config(NULL), pattern_needs_init(true), off(true),
    Adafruit_NeoPixel(NUM_DIGITS, BACKLIGHTS_PIN, NEO_GRB + NEO_KHZ800)
    {}

  // Available lighting patterns — dark turns LEDs off, test cycles through
  // individual LEDs and colours for hardware debugging
  enum patterns { dark, test, constant, rainbow, pulse, breath, num_patterns };
  const static String patterns_str[num_patterns];

  void begin(StoredConfig::Config::Backlights *config_);
  void loop();  // Call every iteration of the main loop — drives animation

  // Power toggle — sets pattern_needs_init to force a full refresh
  void togglePower() { off = !off; pattern_needs_init = true; }
  void PowerOn()     { off = false; pattern_needs_init = true; }
  void PowerOff()    { off = true; pattern_needs_init = true; }

  void setPattern(patterns p) { config->pattern = uint8_t(p); pattern_needs_init = true; }
  patterns getPattern()       { return patterns(config->pattern); }
  String getPatternStr()      { return patterns_str[config->pattern]; }
  void setNextPattern(int8_t i = 1);

  void setPulseRate(uint8_t bpm)      { config->pulse_bpm = bpm; }
  void setBreathRate(uint8_t per_min) { config->breath_per_min = per_min; }

  // Colour phase: 0..767 maps around the RGB colour wheel (see phaseToColor)
  void setColorPhase(uint16_t phase)  { config->color_phase = phase % max_phase; pattern_needs_init = true; }
  void adjustColorPhase(int16_t adj);
  uint16_t getColorPhase()            { return config->color_phase; }
  uint32_t getColor()                 { return phaseToColor(config->color_phase); }

  // Intensity uses a logarithmic scale (0..7) via bit-shifting — see setIntensity()
  void setIntensity(uint8_t intensity);
  void adjustIntensity(int16_t adj);
  uint8_t getIntensity()              { return config->intensity; }

  // Night-mode dimming — when dimming=true, patterns use dimmed_intensity
  // instead of config->intensity for brightness calculations
  bool dimming = false;
  uint8_t dimmed_intensity = 0;
  void forceRefresh() { pattern_needs_init = true; }

private:
  // When true, the next loop() call reinitializes the current pattern
  // (recalculates colours, resets brightness). Set on any config change.
  bool pattern_needs_init;
  bool off;

  StoredConfig::Config::Backlights *config;

  // Pattern generators — each called from loop() every iteration
  void testPattern();
  void rainbowPattern();
  void pulsePattern();
  void breathPattern();

  // Colour wheel helpers
  uint8_t phaseToIntensity(uint16_t phase);  // Single-channel triangle wave
  uint32_t phaseToColor(uint16_t phase);     // Full RGB from phase position

  // Colour wheel has 768 steps (256 per primary colour transition)
  const uint16_t max_phase = 768;

  // 8 brightness levels (0..7), mapped to NeoPixel brightness via bit-shifting
  const uint8_t max_intensity = 8;

  // Test pattern advances every 250ms
  const uint32_t test_ms_delay = 250;
};

#endif // BACKLIGHTS_H
