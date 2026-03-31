# macOS Build Environment Setup

Complete guide for setting up a command-line build environment on macOS to compile, flash, and iterate on the EleksTube IPS clock firmware.

## 1. Install arduino-cli

```bash
brew install arduino-cli
```

Verify:
```bash
arduino-cli version
```

## 2. Configure and Install ESP32 Board Support

```bash
arduino-cli config init
arduino-cli config add board_manager.additional_urls \
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## 3. Install Required Libraries

```bash
arduino-cli lib install "TFT_eSPI"
arduino-cli lib install "Adafruit NeoPixel"
arduino-cli lib install "Time"
arduino-cli lib install "DS1307RTC"
arduino-cli lib install "Rtc by Makuna"
arduino-cli lib install "ArduinoJson"
```

Library name mapping:
| Code `#include` | Arduino Library Name |
|-----------------|---------------------|
| `TimeLib.h` | `Time` |
| `DS1307RTC.h` | `DS1307RTC` |
| `RtcDS1302.h`, `ThreeWire.h` | `Rtc by Makuna` |
| `Adafruit_NeoPixel.h` | `Adafruit NeoPixel` |
| `TFT_eSPI.h` | `TFT_eSPI` |
| `ArduinoJson.h` | `ArduinoJson` |

## 4. Configure TFT_eSPI

The TFT_eSPI library must be pointed at this project's `GLOBAL_DEFINES.h`, which contains all the display driver, pin, and SPI configuration.

Find the library's `User_Setup.h` and replace its contents:

```bash
# Find the file
TFTESPI_DIR=$(arduino-cli lib list --format json | python3 -c "
import json, sys
for lib in json.load(sys.stdin)['installed_libraries']:
    if lib['library']['name'] == 'TFT_eSPI':
        print(lib['library']['install_dir'])
        break
")

echo "TFT_eSPI location: $TFTESPI_DIR"

# Replace User_Setup.h with a single include pointing to our config
cat > "$TFTESPI_DIR/User_Setup.h" << 'SETUP'
// Redirected to EleksTube project configuration
// All TFT driver, pin, and SPI settings are in GLOBAL_DEFINES.h
#include "GLOBAL_DEFINES.h"
SETUP
```

**Important:** This `#include` uses a relative path. It works because arduino-cli adds the sketch directory to the include path during compilation. If compilation fails with "GLOBAL_DEFINES.h not found", use the absolute path instead:

```cpp
#include "/full/path/to/EleksTubeIPS/GLOBAL_DEFINES.h"
```

## 5. USB Driver Setup

The ESP32 board in the EleksTube connects via a USB-to-serial chip. Which driver you need depends on the chip:

**CP2102/CP2104 (Silicon Labs):** Works natively on macOS — no driver needed.

**CH340/CH341 (WCH):** Requires a driver:
1. Download from https://github.com/WCHSoftGroup/ch34xser_macos
2. Install the `.pkg`
3. Go to **System Settings > General > Login Items & Extensions**
4. Under "Driver Extensions", find `CH34xVCPDriver` and toggle it **ON**
5. Reboot

After connecting the clock via USB, verify it appears:
```bash
ls /dev/cu.usb*
```

You should see something like `/dev/cu.usbserial-XXXXX` or `/dev/cu.usbmodem-XXXXX`.

## 6. Compile

This project uses a custom partition table (`EleksTubeIPS/partitions.csv`), so the `PartitionScheme=custom` option is required for all compile and upload commands.

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

To see detailed output (useful for debugging include/library issues):
```bash
arduino-cli compile -v --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

## 7. Flash Firmware

```bash
arduino-cli upload -p /dev/cu.usbserial-XXXXX --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

Replace `/dev/cu.usbserial-XXXXX` with your actual port from `ls /dev/cu.usb*`.

To find the port automatically:
```bash
arduino-cli board list
```

### Compile and Flash in One Step

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/ && \
arduino-cli upload -p /dev/cu.usbserial-XXXXX --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

## 8. Upload Clock Face Images to SPIFFS

The clock face images in `EleksTubeIPS/data/` must be uploaded to the ESP32's SPIFFS partition separately from the firmware.

### Install esptool

```bash
pip3 install esptool
```

### Create and Flash the SPIFFS Image

The SPIFFS offset and size must match the custom partition table in `EleksTubeIPS/partitions.csv`. The SPIFFS partition starts at offset `0x150000` with a size of `0x2A0000` (2,752,512 bytes).

```bash
# Locate mkspiffs (installed with ESP32 core)
MKSPIFFS=$(find ~/Library/Arduino15/packages/esp32/tools/mkspiffs/*/mkspiffs -type f | head -1)

# Create SPIFFS image from the data/ directory
$MKSPIFFS -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin

# Flash to ESP32
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXXX --baud 921600 \
  write_flash -z 0x150000 spiffs.bin

# Clean up
rm spiffs.bin
```

You can verify these values directly from the partition table:
```bash
cat EleksTubeIPS/partitions.csv
```

Look for the `spiffs` row — use its `Offset` and `Size` values.

## 9. Serial Monitor

To watch debug output from the clock:

```bash
arduino-cli monitor -p /dev/cu.usbserial-XXXXX -c baudrate=115200
```

Or with screen:
```bash
screen /dev/cu.usbserial-XXXXX 115200
```

(Exit screen with `Ctrl-A` then `K`, then `Y`.)

## Quick Reference

| Task | Command |
|------|---------|
| Compile | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Flash firmware | `arduino-cli upload -p PORT --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Build SPIFFS | `mkspiffs -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin` |
| Flash SPIFFS | `esptool.py --chip esp32 --port PORT --baud 921600 write_flash -z 0x150000 spiffs.bin` |
| Serial monitor | `arduino-cli monitor -p PORT -c baudrate=115200` |
| List ports | `arduino-cli board list` |
| List libraries | `arduino-cli lib list` |
