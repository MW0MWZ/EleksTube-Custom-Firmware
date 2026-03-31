# Windows Build Environment Setup

Complete guide for setting up a command-line build environment on Windows to compile, flash, and iterate on the EleksTube IPS clock firmware.

**Shell note:** All commands below are written for **PowerShell**. They also work in **Git Bash** with minor path adjustments. If you use `cmd.exe`, replace forward slashes in paths with backslashes.

## 1. Install arduino-cli

Several options:

**Windows Installer (MSI):**
Download the latest `.msi` from https://arduino.github.io/arduino-cli/latest/installation/ and run it. The installer adds `arduino-cli` to your PATH automatically.

**Package managers:**
```powershell
# winget (built into Windows 11 and recent Windows 10)
winget install ArduinoSA.ArduinoCLI

# Chocolatey
choco install arduino-cli

# Scoop
scoop install arduino-cli
```

Verify (open a new terminal after installing):
```powershell
arduino-cli version
```

## 2. Configure and Install ESP32 Board Support

```powershell
arduino-cli config init
arduino-cli config add board_manager.additional_urls `
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

## 3. Install Required Libraries

```powershell
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

```powershell
# Find the library install directory
$libJson = arduino-cli lib list --format json | ConvertFrom-Json
$tftDir = ($libJson.installed_libraries | Where-Object {
    $_.library.name -eq "TFT_eSPI"
}).library.install_dir

Write-Host "TFT_eSPI location: $tftDir"

# Replace User_Setup.h with a single include pointing to our config
@"
// Redirected to EleksTube project configuration
// All TFT driver, pin, and SPI settings are in GLOBAL_DEFINES.h
#include "GLOBAL_DEFINES.h"
"@ | Set-Content "$tftDir\User_Setup.h" -Encoding UTF8
```

**Git Bash alternative:**
```bash
TFTESPI_DIR=$(arduino-cli lib list --format json | python3 -c "
import json, sys
for lib in json.load(sys.stdin)['installed_libraries']:
    if lib['library']['name'] == 'TFT_eSPI':
        print(lib['library']['install_dir'])
        break
")

cat > "$TFTESPI_DIR/User_Setup.h" << 'SETUP'
// Redirected to EleksTube project configuration
// All TFT driver, pin, and SPI settings are in GLOBAL_DEFINES.h
#include "GLOBAL_DEFINES.h"
SETUP
```

**Important:** This `#include` uses a relative path. It works because arduino-cli adds the sketch directory to the include path during compilation. If compilation fails with "GLOBAL_DEFINES.h not found", use the absolute path instead:

```cpp
#include "C:/full/path/to/EleksTubeIPS/GLOBAL_DEFINES.h"
```

Note: Use forward slashes in the `#include` path even on Windows -- the compiler handles them correctly and it avoids backslash-escape issues.

## 5. USB Driver Setup

The ESP32 board in the EleksTube connects via a USB-to-serial chip. Which driver you need depends on the chip.

### CP2102/CP2104 (Silicon Labs)

Usually installs automatically via Windows Update when you first plug in the device. If it does not appear in Device Manager under "Ports (COM & LPT)":

1. Download the driver from https://www.silabs.com/developers/usb-to-uart-bridge-vcp-drivers
2. Run the installer
3. Unplug and replug the USB cable

### CH340/CH341 (WCH)

Windows does **not** include this driver by default. You must install it manually:

1. Download from http://www.wch-ic.com/downloads/CH341SER_EXE.html
2. Run `CH341SER.EXE` and click "Install"
3. Unplug and replug the USB cable

### Find Your COM Port

After connecting the clock via USB:

1. Open **Device Manager** (`devmgmt.msc`)
2. Expand **Ports (COM & LPT)**
3. Look for "Silicon Labs CP210x" or "CH340" -- note the COM port number (e.g., `COM3`)

Or from the command line:
```powershell
arduino-cli board list
```

## 6. Compile

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

To see detailed output (useful for debugging include/library issues):
```powershell
arduino-cli compile -v --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

### Partition Scheme

This project uses a custom partition scheme (`PartitionScheme=custom`). The custom partition table is located in the sketch's `partitions.csv`.

To explore other built-in partition options:
```powershell
arduino-cli board details -b esp32:esp32:esp32
```

## 7. Flash Firmware

```powershell
arduino-cli upload -p COM3 -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

Replace `COM3` with your actual port from step 5.

### Compile and Flash in One Step

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/; `
if ($LASTEXITCODE -eq 0) { `
  arduino-cli upload -p COM3 -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/ `
}
```

**Git Bash alternative:**
```bash
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/ && \
arduino-cli upload -p COM3 -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/
```

## 8. Upload Clock Face Images to SPIFFS

The clock face images in `EleksTubeIPS/data/` must be uploaded to the ESP32's SPIFFS partition separately from the firmware.

### Install Python and esptool

If Python is not installed, download it from https://www.python.org/downloads/ and ensure "Add Python to PATH" is checked during installation.

Then install esptool:
```powershell
pip install esptool
```

Verify:
```powershell
esptool.py version
```

If `esptool.py` is not recognized, try `python -m esptool` instead, or ensure Python's `Scripts` directory is on your PATH.

### Create and Flash the SPIFFS Image

```powershell
# Locate mkspiffs (installed with ESP32 core)
$mkspiffs = Get-ChildItem "$env:LOCALAPPDATA\Arduino15\packages\esp32\tools\mkspiffs" `
  -Recurse -Filter "mkspiffs.exe" | Select-Object -First 1 -ExpandProperty FullName

# Create SPIFFS image from the data/ directory
# Size 0x2A0000 (2752512 bytes) matches the custom partition's spiffs size
& $mkspiffs -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin

# Flash to ESP32 (offset 0x150000 for custom partition scheme)
esptool.py --chip esp32 --port COM3 --baud 460800 `
  write_flash -z 0x150000 spiffs.bin

# Clean up
Remove-Item spiffs.bin
```

**Git Bash alternative:**
```bash
MKSPIFFS=$(find "$LOCALAPPDATA/Arduino15/packages/esp32/tools/mkspiffs" -name "mkspiffs.exe" | head -1)

$MKSPIFFS -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin

esptool.py --chip esp32 --port COM3 --baud 460800 \
  write_flash -z 0x150000 spiffs.bin

rm spiffs.bin
```

**Partition offsets:** The offset `0x150000` and size `0x2A0000` are for the `custom` partition scheme used by this project. If you change partition schemes, check the partition table CSV for the `spiffs` row and use its offset and size values.

## 9. Serial Monitor

To watch debug output from the clock:

```powershell
arduino-cli monitor -p COM3 -c baudrate=115200
```

Or with PuTTY:
1. Download PuTTY from https://www.putty.org/
2. Select "Serial" as connection type
3. Set "Serial line" to `COM3` and "Speed" to `115200`
4. Click "Open"

## Quick Reference

| Task | Command |
|------|---------|
| Compile | `arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Flash firmware | `arduino-cli upload -p PORT -b 460800 --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/` |
| Create SPIFFS image | `mkspiffs.exe -c EleksTubeIPS/data -b 4096 -p 256 -s 0x2A0000 spiffs.bin` |
| Flash SPIFFS | `esptool.py --chip esp32 --port PORT --baud 460800 write_flash -z 0x150000 spiffs.bin` |
| Serial monitor | `arduino-cli monitor -p PORT -c baudrate=115200` |
| List ports | `arduino-cli board list` |
| List libraries | `arduino-cli lib list` |
| Arduino15 path | `%LOCALAPPDATA%\Arduino15\` |
