// TFTs.cpp — Display rendering engine: image loading, dimming, and SPI output
//
// Two image format paths are compiled in depending on USE_CLK_FILES:
//   - BMP: Standard Windows bitmap. Supports 1/4/8/24-bit colour depth with
//     palette. Bottom-up row order (last row stored first). Heavier parsing.
//   - CLK: Custom raw format. 6-byte header (magic + width + height) followed
//     by raw RGB565 pixels in top-down order. Much faster to load.

#include "TFTs.h"
#include "WifiManager.h"
#include <miniz.h>
#include <math.h>

void TFTs::begin() {
  // Initialize the 74HC595 shift register, then select ALL displays so the
  // subsequent TFT_eSPI init() sends the ST7789 initialization sequence to
  // every display simultaneously (saves time vs initializing one by one).
  chip_select.begin();
  chip_select.set_all();

  pinMode(TFT_ENABLE_PIN, OUTPUT);
  enableAllDisplays();
  InvalidateImageInBuffer();

  // TFT_eSPI::init() — sends the ST7789 startup command sequence over SPI.
  // Because all CS lines are asserted, all six displays receive it at once.
  init();

  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS initialization failed!");
  }

  // Scan SPIFFS to find how many clock face sets are available
  NumberOfClockFaces = CountNumberOfClockFaces();
}

// Precompute the 3x3 RGB hue rotation matrix for a given angle.
// The rotation is around the grey axis (1,1,1) in RGB colour space,
// which shifts hue while preserving luminance. Black/white pixels
// are unaffected (no saturation to rotate).
//
// Matrix entries are stored as Q8 fixed-point: multiply by the entry
// then right-shift 8 to get the result. This avoids floating-point
// math in the per-pixel inner loop (32,400 pixels per image).
void TFTs::computeHueMatrix(uint16_t angle_deg) {
  if (angle_deg == 0 || angle_deg >= 360) {
    // Identity matrix — no rotation
    hue_matrix[0] = 256; hue_matrix[1] = 0;   hue_matrix[2] = 0;
    hue_matrix[3] = 0;   hue_matrix[4] = 256; hue_matrix[5] = 0;
    hue_matrix[6] = 0;   hue_matrix[7] = 0;   hue_matrix[8] = 256;
    hue_shift = 0;
    return;
  }
  float rad = angle_deg * M_PI / 180.0f;
  float cosA = cosf(rad);
  float sinA = sinf(rad);
  float third = (1.0f - cosA) / 3.0f;
  float sqrt_third = sinA * sqrtf(1.0f / 3.0f);

  hue_matrix[0] = (int16_t)((cosA + third) * 256.0f);
  hue_matrix[1] = (int16_t)((third - sqrt_third) * 256.0f);
  hue_matrix[2] = (int16_t)((third + sqrt_third) * 256.0f);
  hue_matrix[3] = (int16_t)((third + sqrt_third) * 256.0f);
  hue_matrix[4] = (int16_t)((cosA + third) * 256.0f);
  hue_matrix[5] = (int16_t)((third - sqrt_third) * 256.0f);
  hue_matrix[6] = (int16_t)((third - sqrt_third) * 256.0f);
  hue_matrix[7] = (int16_t)((third + sqrt_third) * 256.0f);
  hue_matrix[8] = (int16_t)((cosA + third) * 256.0f);
}

void TFTs::clear() {
  // Select all displays and ensure they're powered — used during setup/reset
  chip_select.set_all();
  enableAllDisplays();
}

void TFTs::setDigit(uint8_t digit, uint8_t value, show_t show) {
  uint8_t old_value = digits[digit];
  digits[digit] = value;

  // Only redraw if the value actually changed (show == yes) or if forced
  if (show != no && (old_value != value || show == force)) {
    showDigit(digit);
  }
}

void TFTs::showDigit(uint8_t digit) {
  // Assert only this digit's CS line before sending pixel data
  chip_select.set_digit(digit);

  if (digits[digit] == blanked) {
    fillScreen(TFT_BLACK);
  } else {
    // File index encoding: clock_face_number * 10 + digit_value
    // e.g., clock face 2, digit 7 -> file "27.clk"
    uint8_t file_index = current_graphic * 10 + digits[digit];
    DrawImage(file_index);

    // Pre-loading strategy: predict the next seconds-ones digit and queue
    // its image for background loading. This means when the second ticks
    // over, the image is already in output_buffer and only the fast SPI
    // push is needed (LoadNextImage is called from the main loop idle time).
    uint8_t next_number = digits[SECONDS_ONES] + 1;
    if (next_number > 9) next_number = 0;
    next_file_required = current_graphic * 10 + next_number;
  }
}

void TFTs::LoadNextImage() {
  // Called during idle time in the main loop. Only loads if the predicted
  // next image differs from what's already in the buffer.
  if (next_file_required != file_in_buffer) {
#ifdef DEBUG_OUTPUT
    Serial.println("Preload img");
#endif
    LoadImageIntoBuffer(next_file_required);
  }
}

void TFTs::InvalidateImageInBuffer() {
  // Setting to 255 ensures the next DrawImage call will trigger a reload,
  // since no valid file index is 255.
  file_in_buffer = 255;
}

bool TFTs::FileExists(const char* path) {
  fs::File f = SPIFFS.open(path, "r");
  bool exists = ((f == true) && !f.isDirectory());
  f.close();
  return exists;
}

// -----------------------------------------------------------------------
// Static output buffer — 135 x 240 pixels x 2 bytes = 64,800 bytes.
// Allocated statically to avoid heap fragmentation on the ESP32.
// -----------------------------------------------------------------------
uint16_t TFTs::output_buffer[TFT_HEIGHT][TFT_WIDTH];

// =======================================================================
// BMP image loader — compiled only when USE_CLK_FILES is NOT defined.
// Adapted from the TFT_SPIFFS_BMP example in the TFT_eSPI library.
// Modified to buffer the entire image at once (instead of line-by-line)
// so it can be pushed to the display in a single SPI transaction.
// =======================================================================
#ifndef USE_CLK_FILES

int8_t TFTs::CountNumberOfClockFaces() {
  int8_t i, found;
  char filename[10];

  // Clock faces are numbered starting at 1. Each face has files named
  // 10.bmp through 19.bmp (face 1), 20.bmp through 29.bmp (face 2), etc.
  // We probe for the "0" digit of each face (10, 20, 30...) to count them.
  Serial.print("Searching for BMP clock files... ");
  found = 0;
  for (i = 1; i < 10; i++) {
    sprintf(filename, "/%d.bmp", i * 10);
    if (!FileExists(filename)) {
      found = i - 1;
      break;
    }
    found = i;  // All faces up to i exist
  }
  Serial.print(found);
  Serial.println(" fonts found.");
  return found;
}

bool TFTs::LoadImageIntoBuffer(uint8_t file_index) {
  uint32_t start_time = millis();

  fs::File bmpFS;
  char filename[10];
  sprintf(filename, "/%d.bmp", file_index);

  bmpFS = SPIFFS.open(filename, "r");
  if (!bmpFS) {
    Serial.print("File not found: ");
    Serial.println(filename);
    return false;
  }

  uint32_t seekOffset, headerSize, paletteSize = 0;
  int16_t w, h, row, col;
  uint16_t r, g, b, bitDepth;

  // Zero the buffer so any pixels outside the image bounds are black
  memset(output_buffer, '\0', sizeof(output_buffer));

  // BMP magic number check: 0x4D42 = "BM" in little-endian
  uint16_t magic = read16(bmpFS);
  if (magic == 0xFFFF) {
    Serial.print("Can't openfile. Make sure you upload the SPIFFs image with BMPs. : ");
    Serial.println(filename);
    bmpFS.close();
    return false;
  }

  if (magic != 0x4D42) {
    Serial.print("File not a BMP. Magic: ");
    Serial.println(magic);
    bmpFS.close();
    return false;
  }

  // Parse BMP header fields (see BMP file format specification)
  read32(bmpFS); // filesize — not needed
  read32(bmpFS); // reserved — not needed
  seekOffset = read32(bmpFS);   // Offset to pixel data from file start
  headerSize = read32(bmpFS);   // DIB header size (40 for BITMAPINFOHEADER)
  w = read32(bmpFS);            // Image width in pixels
  h = read32(bmpFS);            // Image height in pixels
  read16(bmpFS);                // Color planes — always 1
  bitDepth = read16(bmpFS);     // Bits per pixel: 1, 4, 8, or 24
#ifdef DEBUG_OUTPUT
  Serial.print("image W, H, BPP: ");
  Serial.print(w);
  Serial.print(", ");
  Serial.print(h);
  Serial.print(", ");
  Serial.println(bitDepth);
  Serial.print("dimming: ");
  Serial.println(dimming);
#endif
  // Center the image in the display if it's smaller than the screen
  int16_t x = (TFT_WIDTH - w) / 2;
  int16_t y = (TFT_HEIGHT - h) / 2;

  // Compression field must be 0 (uncompressed). Only support 1/4/8/24 bpp.
  if (read32(bmpFS) != 0 || (bitDepth != 24 && bitDepth != 1 && bitDepth != 4 && bitDepth != 8)) {
    Serial.println("BMP format not recognized.");
    bmpFS.close();
    return false;
  }

  // For paletted images (1/4/8 bpp), read the colour palette
  uint32_t palette[256];
  if (bitDepth <= 8) {
    // Skip image size, X/Y pixels-per-meter, colors-used fields
    read32(bmpFS); read32(bmpFS); read32(bmpFS);
    paletteSize = read32(bmpFS);
    if (paletteSize == 0) paletteSize = (1u << bitDepth);
    // Seek to palette start (immediately after DIB header)
    bmpFS.seek(14 + headerSize);
    for (uint16_t i = 0; i < paletteSize; i++) {
      palette[i] = read32(bmpFS);  // Each entry is BGRA (4 bytes)
    }
  }

  // Seek to the start of pixel data
  bmpFS.seek(seekOffset);

  // Validate dimensions against display size to prevent stack overflow
  if (w <= 0 || w > TFT_WIDTH || h <= 0 || h > TFT_HEIGHT) {
    Serial.println("BMP dimensions exceed display size");
    bmpFS.close();
    return false;
  }

  // BMP rows are padded to 4-byte boundaries. Fixed buffer size based on
  // validated max width to avoid VLA stack overflow from corrupt files.
  uint32_t lineSize = ((bitDepth * w + 31) >> 5) * 4;
  uint8_t lineBuffer[TFT_WIDTH * 3 + 4];  // Max: 135px * 24bpp + padding

  // BMP stores rows bottom-to-top, so we read from the last row backwards
  for (row = h - 1; row >= 0; row--) {
    bmpFS.read(lineBuffer, lineSize);
    uint8_t *bptr = lineBuffer;

    for (col = 0; col < w; col++) {
      if (bitDepth == 24) {
        // 24-bit BMP stores pixels as BGR (not RGB)
        b = *bptr++;
        g = *bptr++;
        r = *bptr++;
      } else {
        // Paletted formats: look up the colour from the palette
        uint32_t c = 0;
        if (bitDepth == 8) {
          c = palette[*bptr++];
        } else if (bitDepth == 4) {
          // Two pixels per byte: high nibble first, then low nibble
          c = palette[(*bptr >> ((col & 0x01) ? 0 : 4)) & 0x0F];
          if (col & 0x01) bptr++;
        } else {
          // 1-bit: 8 pixels per byte, MSB first
          c = palette[(*bptr >> (7 - (col & 0x07))) & 0x01];
          if ((col & 0x07) == 0x07) bptr++;
        }
        // Palette entries are stored as B, G, R, A (little-endian BGRA)
        b = c; g = c >> 8; r = c >> 16;
      }

      // --- Hue rotation ---
      // Apply the precomputed RGB rotation matrix (Q8 fixed-point).
      // int32_t intermediates prevent overflow (max: 3 * 512 * 255).
      if (hue_shift != 0) {
        int32_t nr = ((int32_t)hue_matrix[0]*r + (int32_t)hue_matrix[1]*g + (int32_t)hue_matrix[2]*b) >> 8;
        int32_t ng = ((int32_t)hue_matrix[3]*r + (int32_t)hue_matrix[4]*g + (int32_t)hue_matrix[5]*b) >> 8;
        int32_t nb = ((int32_t)hue_matrix[6]*r + (int32_t)hue_matrix[7]*g + (int32_t)hue_matrix[8]*b) >> 8;
        r = nr < 0 ? 0 : (nr > 255 ? 255 : nr);
        g = ng < 0 ? 0 : (ng > 255 ? 255 : ng);
        b = nb < 0 ? 0 : (nb > 255 ? 255 : nb);
      }

      // --- Pixel dimming ---
      // Multiply each channel by dimming/256 using integer math.
      // dimming=255 means ~99.6% brightness (effectively full), dimming=0 = black.
      // The multiply-then-shift approach avoids expensive division.
      if (dimming < 255) {
        b *= dimming;
        g *= dimming;
        r *= dimming;
        b = b >> 8;
        g = g >> 8;
        r = r >> 8;
      }
      // Convert 8-bit-per-channel RGB to RGB565 (16-bit) for the ST7789:
      //   R: 5 bits, G: 6 bits, B: 5 bits
      output_buffer[row][col] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | ((b & 0xFF) >> 3);
    }
  }
  file_in_buffer = file_index;

  bmpFS.close();
#ifdef DEBUG_OUTPUT
  Serial.print("img load : ");
  Serial.println(millis() - start_time);
#endif
  return true;
}
#endif

// =======================================================================
// CLK image loader — compiled only when USE_CLK_FILES IS defined.
// CLK is a minimal custom format: 2-byte magic ("CK" = 0x4B43), 2-byte
// width, 2-byte height, then raw RGB565 pixels in top-down, left-to-right
// order. No compression, no palette, no row padding. Much faster than BMP.
// =======================================================================
#ifdef USE_CLK_FILES

int8_t TFTs::CountNumberOfClockFaces() {
  int8_t i, found;
  char filename[10];

  // Same probing strategy as BMP: check for the "0" digit of each face
  Serial.print("Searching for CLK clock files... ");
  found = 0;
  for (i = 1; i < 10; i++) {
    sprintf(filename, "/%d.clk", i * 10);
    if (!FileExists(filename)) {
      found = i - 1;
      break;
    }
    found = i;  // All faces up to i exist
  }
  Serial.print(found);
  Serial.println(" fonts found.");
  return found;
}

// Fill the output buffer from raw (uncompressed) RGB565 pixel data.
// Handles centering and optional dimming. Used by both raw CLK and
// after decompressing ZC format.
void TFTs::FillBufferFromRGB565(const uint8_t *pixels, int16_t w, int16_t h) {
  int16_t x = (TFT_WIDTH - w) / 2;
  int16_t y = (TFT_HEIGHT - h) / 2;

  for (int16_t row = 0; row < h; row++) {
    for (int16_t col = 0; col < w; col++) {
      int ofs = (row * w + col) * 2;
      if (dimming == 255 && hue_shift == 0) {
        output_buffer[row + y][col + x] = (pixels[ofs + 1] << 8) | pixels[ofs];
      } else {
        uint8_t PixM = pixels[ofs + 1];
        uint8_t PixL = pixels[ofs];
        int16_t r = (PixM) & 0xF8;
        int16_t g = ((PixM << 5) | (PixL >> 3)) & 0xFC;
        int16_t b = (PixL << 3) & 0xF8;
        if (hue_shift != 0) {
          int32_t nr = ((int32_t)hue_matrix[0]*r + (int32_t)hue_matrix[1]*g + (int32_t)hue_matrix[2]*b) >> 8;
          int32_t ng = ((int32_t)hue_matrix[3]*r + (int32_t)hue_matrix[4]*g + (int32_t)hue_matrix[5]*b) >> 8;
          int32_t nb = ((int32_t)hue_matrix[6]*r + (int32_t)hue_matrix[7]*g + (int32_t)hue_matrix[8]*b) >> 8;
          r = nr < 0 ? 0 : (nr > 255 ? 255 : nr);
          g = ng < 0 ? 0 : (ng > 255 ? 255 : ng);
          b = nb < 0 ? 0 : (nb > 255 ? 255 : nb);
        }
        if (dimming < 255) {
          r = (r * dimming) >> 8;
          g = (g * dimming) >> 8;
          b = (b * dimming) >> 8;
        }
        output_buffer[row + y][col + x] = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
      }
    }
  }
}

bool TFTs::LoadImageIntoBuffer(uint8_t file_index) {
#ifdef DEBUG_OUTPUT
  uint32_t start_time = millis();
#endif

  char filename[20];
  sprintf(filename, "/%d.clk", file_index);

  fs::File f = SPIFFS.open(filename, "r");
  if (!f) {
    Serial.print("File not found: ");
    Serial.println(filename);
    return false;
  }

  memset(output_buffer, '\0', sizeof(output_buffer));

  // Read magic to determine format
  uint16_t magic = read16(f);
  if (magic == 0xFFFF) {
    Serial.println("Can't read file — check SPIFFS upload");
    f.close();
    return false;
  }

  int16_t w = read16(f);
  int16_t h = read16(f);

  if (magic == 0x435A) {
    // ZC format: zlib-compressed RGB565 pixel data
    // Header: magic(2) + width(2) + height(2) + compressed_size(4) + data
    uint32_t comp_size = read32(f);
    size_t raw_size = w * h * 2;

    // Read compressed data into a heap buffer (typically 20-35KB).
    // Decompress directly into the static output_buffer (64,800 bytes)
    // to avoid allocating a second 64KB heap buffer.
    uint8_t *comp_buf = (uint8_t *)malloc(comp_size);
    if (!comp_buf) {
      Serial.println("Out of memory for compressed image");
      f.close();
      return false;
    }

    f.read(comp_buf, comp_size);
    f.close();

    // Decompress into output_buffer (reinterpreted as a byte array).
    // output_buffer is uint16_t[240][135] = 64,800 bytes, which is
    // exactly w*h*2 for a 135x240 image.
    size_t out_len = tinfl_decompress_mem_to_mem(
      output_buffer, sizeof(output_buffer), comp_buf, comp_size,
      TINFL_FLAG_PARSE_ZLIB_HEADER);
    free(comp_buf);

    if (out_len == TINFL_DECOMPRESS_MEM_TO_MEM_FAILED) {
      Serial.println("Image decompression failed");
      return false;
    }

    // The raw RGB565 bytes are now in output_buffer. If dimming or hue
    // rotation is needed, unpack/transform/repack each pixel in-place.
    if (dimming != 255 || hue_shift != 0) {
      uint8_t *raw = (uint8_t *)output_buffer;
      for (size_t i = 0; i < raw_size; i += 2) {
        uint8_t lo = raw[i];
        uint8_t hi = raw[i + 1];
        int16_t r = (hi) & 0xF8;
        int16_t g = ((hi << 5) | (lo >> 3)) & 0xFC;
        int16_t b = (lo << 3) & 0xF8;
        if (hue_shift != 0) {
          int32_t nr = ((int32_t)hue_matrix[0]*r + (int32_t)hue_matrix[1]*g + (int32_t)hue_matrix[2]*b) >> 8;
          int32_t ng = ((int32_t)hue_matrix[3]*r + (int32_t)hue_matrix[4]*g + (int32_t)hue_matrix[5]*b) >> 8;
          int32_t nb = ((int32_t)hue_matrix[6]*r + (int32_t)hue_matrix[7]*g + (int32_t)hue_matrix[8]*b) >> 8;
          r = nr < 0 ? 0 : (nr > 255 ? 255 : nr);
          g = ng < 0 ? 0 : (ng > 255 ? 255 : ng);
          b = nb < 0 ? 0 : (nb > 255 ? 255 : nb);
        }
        if (dimming < 255) {
          r = (r * dimming) >> 8;
          g = (g * dimming) >> 8;
          b = (b * dimming) >> 8;
        }
        uint16_t px = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
        raw[i] = px & 0xFF;
        raw[i + 1] = px >> 8;
      }
    }

  } else if (magic == 0x4B43) {
    // CK format: raw (uncompressed) RGB565 pixel data
    size_t raw_size = w * h * 2;
    uint8_t *raw_buf = (uint8_t *)malloc(raw_size);
    if (!raw_buf) {
      Serial.println("Out of memory for image load");
      f.close();
      return false;
    }
    f.read(raw_buf, raw_size);
    f.close();

    FillBufferFromRGB565(raw_buf, w, h);
    free(raw_buf);

  } else {
    Serial.printf("Unknown image format: 0x%04X\n", magic);
    f.close();
    return false;
  }

  file_in_buffer = file_index;
#ifdef DEBUG_OUTPUT
  Serial.print("img load : ");
  Serial.println(millis() - start_time);
#endif
  return true;
}
#endif

// -----------------------------------------------------------------------
// DrawImage — push the output buffer to the currently-selected display.
// If the requested image isn't already in the buffer, load it first.
// -----------------------------------------------------------------------
void TFTs::DrawImage(uint8_t file_index) {
#ifdef DEBUG_OUTPUT
  uint32_t start_time = millis();
#endif

  // Cache hit: if the requested image is already in the buffer, skip the
  // SPIFFS read (which takes ~100ms) and go straight to the SPI push.
  if (file_index != file_in_buffer) {
    LoadImageIntoBuffer(file_index);
  }

  // TFT_eSPI byte-swap control: ST7789 expects big-endian RGB565 but our
  // buffer stores little-endian. setSwapBytes(true) tells pushImage to
  // byte-swap each 16-bit pixel during the SPI transfer.
  bool old_swap_bytes = getSwapBytes();
  setSwapBytes(true);
  pushImage(0, 0, TFT_WIDTH, TFT_HEIGHT, (uint16_t *)output_buffer);
  setSwapBytes(old_swap_bytes);

#ifdef DEBUG_OUTPUT
  Serial.print("img transfer: ");
  Serial.println(millis() - start_time);
#endif
}

// -----------------------------------------------------------------------
// Little-endian file readers — read multi-byte values byte-by-byte to
// avoid alignment issues on the ESP32 (Xtensa is picky about unaligned
// 16/32-bit memory accesses). Works for both BMP and CLK headers.
// -----------------------------------------------------------------------
uint16_t TFTs::read16(fs::File &f) {
  uint16_t result;
  ((uint8_t *)&result)[0] = f.read();
  ((uint8_t *)&result)[1] = f.read();
  return result;
}

uint32_t TFTs::read32(fs::File &f) {
  uint32_t result;
  ((uint8_t *)&result)[0] = f.read();
  ((uint8_t *)&result)[1] = f.read();
  ((uint8_t *)&result)[2] = f.read();
  ((uint8_t *)&result)[3] = f.read();
  return result;
}
