// TFTs.h — Display manager for six ST7789 TFT screens
//
// Architecture: All six 135x240 ST7789 displays share a single SPI bus (MOSI,
// SCLK, DC, RST). Individual displays are addressed by asserting their CS line
// through the 74HC595 shift register (see ChipSelect). The TFTs class inherits
// from TFT_eSPI so it can directly call graphics primitives (fillScreen,
// pushImage, etc.) which transmit over SPI to whichever display currently has
// its CS line asserted.
//
// Image pipeline:
//   1. Clock face images (digits 0-9 for each "font") are stored on SPIFFS
//      as either .bmp or .clk files, named by index (e.g., "10.clk" = font 1, digit 0).
//   2. LoadImageIntoBuffer() reads a file into the static output_buffer (a full
//      135x240 pixel framebuffer in RAM). Dimming is applied during loading.
//   3. DrawImage() pushes the buffer to the currently-selected display via SPI.
//   4. Pre-loading: after drawing the seconds-ones digit, the next second's
//      image is speculatively loaded into the buffer (LoadNextImage), so when
//      the second changes, the image is already in RAM and only the fast SPI
//      transfer is needed. This hides the slower SPIFFS read latency.
//
// Memory: output_buffer is 135 * 240 * 2 = 64,800 bytes — a significant chunk
// of the ESP32's 520 KB SRAM. Only one buffer is used (not double-buffered)
// because RAM is tight.

#ifndef TFTS_H
#define TFTS_H

#include "GLOBAL_DEFINES.h"

// FS_NO_GLOBALS prevents the SPIFFS library from polluting the global namespace
// with File/Dir types that would clash with other libraries.
#define FS_NO_GLOBALS
#include <FS.h>
#include "SPIFFS.h"

#include <TFT_eSPI.h>
#include "ChipSelect.h"

class TFTs : public TFT_eSPI {
public:
  TFTs() : TFT_eSPI(), chip_select(), enabled(false) {
    for (uint8_t digit = 0; digit < NUM_DIGITS; digit++) digits[digit] = 0;
  }

  // Display update policy: no = don't redraw, yes = redraw if value changed,
  // force = always redraw (used after changing clock face / dimming)
  enum show_t { no, yes, force };

  // Magic value meaning "display is blanked" (no digit shown, screen is black)
  const static uint8_t blanked = 255;

  // Pixel dimming factor: 255 = full brightness, 0 = black.
  // Applied during image loading by scaling each RGB channel.
  uint8_t dimming = 255;

  // Index of the currently selected clock face (font set). Each clock face
  // contains 10 digit images (0-9). File index = current_graphic * 10 + digit.
  uint8_t current_graphic = 1;

  void begin();
  void clear();

  void setDigit(uint8_t digit, uint8_t value, show_t show = yes);
  uint8_t getDigit(uint8_t digit) { return digits[digit]; }

  void showDigit(uint8_t digit);

  // TFT power control via MOSFET — affects all six displays simultaneously
  void enableAllDisplays()  { digitalWrite(TFT_ENABLE_PIN, HIGH); enabled = true; }
  void disableAllDisplays() { digitalWrite(TFT_ENABLE_PIN, LOW); enabled = false; }
  void toggleAllDisplays()  { if (enabled) disableAllDisplays(); else enableAllDisplays(); }
  bool isEnabled()          { return enabled; }

  // Public chip_select so other modules can address individual displays
  ChipSelect chip_select;

  uint8_t NumberOfClockFaces = 0;
  void LoadNextImage();         // Pre-load the next seconds-ones image into buffer
  void InvalidateImageInBuffer(); // Force a reload on next draw (e.g., after font change)

private:
  uint8_t digits[NUM_DIGITS];   // Current digit value for each display position
  bool enabled;

  bool FileExists(const char* path);
  int8_t CountNumberOfClockFaces();
  void FillBufferFromRGB565(const uint8_t *pixels, int16_t w, int16_t h);
  bool LoadImageIntoBuffer(uint8_t file_index);
  void DrawImage(uint8_t file_index);
  uint16_t read16(fs::File &f);
  uint32_t read32(fs::File &f);

  // Full-frame pixel buffer shared across all displays.
  // Static because only one image is in memory at a time (we draw to one
  // display, then reload the buffer for the next display if needed).
  static uint16_t output_buffer[TFT_HEIGHT][TFT_WIDTH];

  // Tracks which file is currently in the buffer to avoid redundant reloads
  uint8_t file_in_buffer = 255;     // 255 = no valid image buffered
  uint8_t next_file_required = 0;   // File index to pre-load for the next second
};

extern TFTs tfts;

#endif // TFTS_H
