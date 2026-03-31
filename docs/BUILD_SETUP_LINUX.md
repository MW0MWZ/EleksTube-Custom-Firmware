# Linux Build Environment Setup

Complete guide for setting up a command-line build environment on Linux to compile, flash, and iterate on the EleksTube IPS clock firmware. Covers Ubuntu/Debian and Fedora/RHEL.

## 1. Install arduino-cli

Install via the official install script (do **not** use `snap install arduino-cli` or `apt install arduino-cli` -- those packages are typically outdated):

```bash
curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh
```

This installs to `./bin/arduino-cli`. Move it onto your PATH:

```bash
sudo mv bin/arduino-cli /usr/local/bin/
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

The ESP32 board in the EleksTube connects via a USB-to-serial chip. Linux has built-in support for most chips, but there are common gotchas.

### Serial Port Permissions

Your user must be in the `dialout` group to access serial ports without `sudo`:

```bash
sudo usermod -a -G dialout $USER
```

**You must log out and back in** (or reboot) for the group change to take effect. Verify with:

```bash
groups | grep dialout
```

### CP2102/CP2104 (Silicon Labs)

Works out of the box on all modern Linux kernels. No action needed.

### CH340/CH341 (WCH)

The kernel driver (`ch341`) is included in modern kernels, but on **Ubuntu 22.04+** the `brltty` service (Braille display support) claims CH340 devices and prevents serial access. Symptoms: the device appears briefly in `dmesg` then disappears, or you get "Permission denied" / "Device busy" errors.

Fix:

```bash
# Ubuntu/Debian
sudo systemctl stop brltty-udev.service
sudo systemctl disable brltty-udev.service
sudo systemctl stop brltty.service
sudo systemctl disable brltty.service

# If you don't use a Braille display, you can remove it entirely:
sudo apt remove brltty
```

Unplug and replug the USB cable after fixing this.

### Verify the Connection

After connecting the clock via USB:

```bash
ls /dev/ttyUSB* /dev/ttyACM* 2>/dev/null
```

You should see `/dev/ttyUSB0` (CP2102/CH340) or `/dev/ttyACM0` (CDC-ACM devices). You can also check `dmesg` for details:

```bash
dmesg | tail -20
```

## 6. Compile

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

To see detailed output (useful for debugging include/library issues):
```bash
arduino-cli compile -v --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

### Partition Scheme

This project uses a custom partition scheme (`PartitionScheme=custom`). The custom partition table is located in the sketch's `partitions.csv`.

To explore other built-in partition options:
```bash
arduino-cli board details -b esp32:esp32:esp32
```

## 7. Flash Firmware

```bash
arduino-cli upload -p /dev/ttyUSB0 -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

Replace `/dev/ttyUSB0` with your actual port from step 5.

To find the port automatically:
```bash
arduino-cli board list
```

### Compile and Flash in One Step

```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/ && \
arduino-cli upload -p /dev/ttyUSB0 -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

## 8. Upload Clock Face Images to SPIFFS

The clock face images in `EleksTubeIPS/data/` must be uploaded to the ESP32's SPIFFS partition separately from the firmware.

### Install esptool

```bash
pip3 install esptool
```

On newer distributions (Ubuntu 23.04+, Fedora 38+) that enforce PEP 668, pip may refuse to install globally. Options:

```bash
# Option A: Allow system-wide install (simplest)
pip3 install --break-system-packages esptool

# Option B: Use a virtual environment
python3 -m venv ~/esptools-venv
source ~/esptools-venv/bin/activate
pip install esptool

# Option C: Use pipx (installs in isolated environment, adds to PATH)
# Ubuntu/Debian: sudo apt install pipx
# Fedora: sudo dnf install pipx
pipx install esptool
```

### Create and Flash the SPIFFS Image

```bash
# Locate mkspiffs (installed with ESP32 core)
MKSPIFFS=$(find ~/.arduino15/packages/esp32/tools/mkspiffs -name mkspiffs -type f | head -1)

# Create SPIFFS image from the data/ directory
# Size 0x2A0000 (2752512 bytes) matches the custom partition's spiffs size
$MKSPIFFS -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin

# Flash to ESP32 (offset 0x150000 for custom partition scheme)
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 \
  write_flash -z 0x150000 spiffs.bin

# Clean up
rm spiffs.bin
```

**Partition offsets:** The offset `0x150000` and size `0x2A0000` are for the `custom` partition scheme used by this project. If you change partition schemes, check the partition table CSV for the `spiffs` row and use its offset and size values.

### Distro-Specific Prerequisites

If `pip3` or `python3` is not installed:

```bash
# Ubuntu/Debian
sudo apt update && sudo apt install python3 python3-pip

# Fedora/RHEL
sudo dnf install python3 python3-pip
```

## 9. Serial Monitor

To watch debug output from the clock:

```bash
arduino-cli monitor -p /dev/ttyUSB0 -c baudrate=115200
```

Or with minicom:
```bash
# Install if needed:
# Ubuntu/Debian: sudo apt install minicom
# Fedora/RHEL:   sudo dnf install minicom
minicom -D /dev/ttyUSB0 -b 115200
```
(Exit minicom with `Ctrl-A` then `X`.)

Or with screen:
```bash
screen /dev/ttyUSB0 115200
```
(Exit screen with `Ctrl-A` then `K`, then `Y`.)

## Quick Reference

| Task | Command |
|------|---------|
| Compile | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Flash firmware | `arduino-cli upload -p PORT -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Create SPIFFS image | `$MKSPIFFS -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin` |
| Flash SPIFFS | `esptool.py --chip esp32 --port PORT --baud 460800 write_flash -z 0x150000 spiffs.bin` |
| Serial monitor | `arduino-cli monitor -p PORT -c baudrate=115200` |
| List ports | `arduino-cli board list` |
| List libraries | `arduino-cli lib list` |
| Add user to dialout | `sudo usermod -a -G dialout $USER` |
