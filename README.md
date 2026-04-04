# EleksTubeIPS - Custom Firmware

[![Release](https://img.shields.io/github/v/release/MW0MWZ/EleksTubeIPS?style=flat-square)](https://github.com/MW0MWZ/EleksTubeIPS/releases/latest)
[![Build](https://img.shields.io/github/actions/workflow/status/MW0MWZ/EleksTubeIPS/build-release.yml?style=flat-square&label=build)](https://github.com/MW0MWZ/EleksTubeIPS/actions)
[![Downloads](https://img.shields.io/github/downloads/MW0MWZ/EleksTubeIPS/total?style=flat-square)](https://github.com/MW0MWZ/EleksTubeIPS/releases)
[![License](https://img.shields.io/github/license/MW0MWZ/EleksTubeIPS?style=flat-square)](LICENSE)

Custom firmware for the EleksTube IPS clock and compatible clones, based on the ESP32 microcontroller. Each of the six tubes contains a 135x240 ST7789 TFT display driven via SPI through a 74HC595 shift register, with WS2812 RGB LEDs providing configurable backlighting.

This firmware is a fork of [aly-fly/EleksTubeHAX](https://github.com/aly-fly/EleksTubeHAX) (originally [SmittyHalibut/EleksTubeHAX](https://github.com/SmittyHalibut/EleksTubeHAX)), significantly reworked with a web-based configuration interface, automatic timezone/DST support, OTA firmware updates, and captive portal setup.

## Supported Hardware

Three clock variants are supported, selected at compile time in `USER_DEFINES.h`:

| Variant | Define | RTC Chip |
|---------|--------|----------|
| Original EleksTube IPS | `HARDWARE_Elekstube_CLOCK` | DS3231 |
| SI HAI clone | `HARDWARE_SI_HAI_CLOCK` | DS1302 |
| NovelLife SE clone | `HARDWARE_NovelLife_SE_CLOCK` | DS3231 |

Each variant has different GPIO pin assignments configured automatically in `GLOBAL_DEFINES.h`.

## Features

### Web Configuration Interface
- Built-in web server accessible from any browser on your local network
- Light and dark theme with automatic detection of system preference
- Responsive design that works on mobile and desktop
- All settings configurable without reflashing — no code editing required
- Live preview — display brightness, clock face, hour format, LED colour and pattern all update instantly as you adjust them

### WiFi & Networking
- **Captive portal setup** — on first boot the clock creates an AP (`EleksTubeIPS-XXXX`) and serves a setup page automatically
- **WiFi network scanning** with signal strength display
- **AP mode toggle** — enable/disable the onboard access point from the web UI or physical buttons
- **Hostname** set to match the AP name for easy identification on your network
- **mDNS discovery** — clock advertises as `elekstubeips-xxxx.local` on the network, accessible from any device without relying on router DNS
- **Enterprise WiFi compatible** — 802.11g/n, WPA2, PMF, full channel scan, no modem sleep
- **Dynamic TX power** — automatically adjusts between 11-20 dBm based on signal strength
- **Proactive roaming** — rescans for a better AP if signal stays weak
- **Connection watchdog** — detects and recovers stale connections

### Time & Timezone
- **NTP time sync** with hardware RTC fallback (syncs hourly, uses RTC between syncs, force-redraws all displays on time correction)
- **POSIX timezone support** with automatic DST handling — select your region from a dropdown and DST transitions are handled automatically
- **Browser timezone detection** — the web UI detects your browser's timezone and offers to apply it
- **Manual NTP sync** button in the web UI
- **45+ timezone presets** covering all major regions worldwide

### Display
- **Multiple clock face graphics** stored on SPIFFS, with clickable live previews in the web UI
- **Display hue shift** — rotate the colour of any clock face through 360 degrees (e.g., warm nixie orange → cool blue or green) using a real-time RGB rotation matrix, without modifying image files
- **Configurable display brightness** via percentage slider (0-100%)
- **12/24 hour mode** and leading zero blanking
- **Image pre-loading** for smooth second transitions

### Filament Glow / Rear LEDs
- **Backlight patterns**: Off, Constant, Rainbow, Pulse, Breath
- **Full RGB colour picker** with 252-colour swatch grid
- **Gamma-corrected** WS2812 output for accurate colour reproduction
- **Configurable brightness** (0-7)

### Night Mode
- **Scheduled dimming** with configurable on/off hours
- **Independent display and LED brightness** sliders for night mode (including full off)
- **Enable/disable toggle** — turn night mode on or off without losing your schedule

### Dashboard Authentication
- **Optional password protection** — set a dashboard password to prevent unauthorised access
- **SHA-256 hashed** password storage with constant-time comparison — never stored in plaintext, resistant to timing attacks
- **Session cookies** (HttpOnly, SameSite=Strict) — secure session management
- **Login overlay** — clean full-screen login form when password is set
- **Logout button** — one-click session logout from the dashboard header
- **Password management** — set, change (requires current password), or remove from the dashboard
- **Confirm password** field with client-side match validation
- **Physical factory reset** — accessible from the button menu to recover from a forgotten password

### OTA Updates
- **Update over WiFi** — no USB cable needed after initial setup
- **Firmware and SPIFFS** — upload either a firmware `.bin` or a SPIFFS image through the same upload button; the file type is auto-detected from the ESP32 magic byte
- **Progress bar** with percentage during upload
- **Safe dual-partition** — new firmware is written to the inactive partition; old firmware is preserved if the update fails
- **Image validation** — partition size checks prevent oversized uploads; firmware magic byte detection routes to the correct flash partition
- **Security wipe** — all stored config (WiFi credentials, passwords) is erased after firmware OTA to prevent malicious firmware from inheriting credentials (SPIFFS updates preserve config)
- **Version display** — current firmware version shown in the status panel
- **CI/CD** — GitHub Actions builds firmware automatically from tagged releases

### Status & Monitoring
- **Live status panel** showing current time, WiFi status, IP address, signal strength with quality indicator (Excellent/Good/Fair/Poor), NTP sync status, uptime, AP status, and firmware version
- **Toast notifications** for save/error feedback
- **Auto-refreshing** every 5 seconds

### Security
- **CSRF protection** — per-session token required on all state-changing requests; token withheld from unauthenticated page loads
- **Rate limiting** — token bucket algorithm prevents brute-force and denial-of-service
- **Host header validation** — exact hostname matching (case-insensitive) blocks DNS rebinding attacks
- **Input validation** — all user-supplied parameters validated and sanitised; WiFi password length enforced
- **PMF deauth protection** — Protected Management Frames (802.11w) enabled where supported
- **POST body size limits** — incoming request bodies capped to prevent memory exhaustion
- **Timezone whitelist** — timezone selections checked against a compiled-in whitelist of valid POSIX TZ strings
- **Dashboard authentication** — optional password with SHA-256 hashing, constant-time comparison, and secure session cookies
- **OTA validation** — auto-detection of firmware vs SPIFFS images, partition size checks, and magic byte validation prevent malicious or corrupt uploads
- **Endpoint protection** — config, scan, and face preview endpoints require authentication; status endpoint returns limited info when unauthenticated

### Persistent Settings
- All settings saved to ESP32 NVS flash and survive power cycles

## Getting Started

### First Boot

1. Flash the firmware and SPIFFS images (see [Building](#building) below)
2. The clock will create a WiFi access point named `EleksTubeIPS-XXXX`
3. Connect to this network from your phone or laptop — a setup page will appear automatically
4. Enter your WiFi credentials and select your timezone
5. Hit **Save** — the clock will reboot and connect to your network
6. Access the configuration page anytime at `http://elekstubeips-xxxx/` or the clock's IP address

### Updating Firmware (OTA)

Once the clock is running, future firmware updates can be done over WiFi — no USB cable needed:

1. Download `EleksTubeIPS.ino.bin` from the latest [GitHub Release](../../releases)
2. Open the clock's web dashboard in your browser
3. Scroll to **Firmware Update**
4. Select the `.bin` file and click **Upload & Install**
5. The progress bar will show upload status
6. The clock will reboot with the new firmware and enter AP setup mode

**Note:** For security, all stored settings (WiFi credentials, dashboard password, timezone) are wiped after a firmware OTA update. You will need to reconfigure via the captive portal.

### Updating Clock Faces (OTA)

New clock face images can also be uploaded over WiFi without a USB cable:

1. Build a SPIFFS image: `mkspiffs -c EleksTubeIPS/data -b 4096 -p 256 -s 0x120000 spiffs.bin`
2. Upload `spiffs.bin` through the same **Firmware Update** section in the dashboard
3. The file type is detected automatically — SPIFFS uploads preserve all settings

### Building

For complete command-line build environment setup, see the platform-specific guides:

- [macOS](docs/BUILD_SETUP_MACOS.md)
- [Linux](docs/BUILD_SETUP_LINUX.md)
- [Windows](docs/BUILD_SETUP_WINDOWS.md)

#### Prerequisites

- [Arduino IDE](https://www.arduino.cc/en/software) or [arduino-cli](https://arduino.github.io/arduino-cli/) with ESP32 board support
- Required libraries:
  - `TFT_eSPI`
  - `Adafruit NeoPixel`
  - `Time` (provides TimeLib.h)
  - `DS1307RTC` (for EleksTube/NovelLife) or `Rtc by Makuna` (for SI HAI)
  - `ArduinoJson`

#### TFT_eSPI Configuration

**Important:** The `TFT_eSPI` library's `User_Setup.h` file must be replaced with a single line pointing to this project's `GLOBAL_DEFINES.h`:

```cpp
#include "/path/to/EleksTubeIPS/GLOBAL_DEFINES.h"
```

#### Hardware Selection

Edit `EleksTubeIPS/USER_DEFINES.h` and uncomment the `#define` for your hardware variant (only one at a time).

#### Compile & Flash

```bash
# Compile (custom partition scheme is required)
arduino-cli compile --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/

# Flash firmware
arduino-cli upload -p /dev/cu.usbserial-XXXXX --fqbn esp32:esp32:esp32:PartitionScheme=custom EleksTubeIPS/

# Create and flash SPIFFS image (clock face images)
mkspiffs -c EleksTubeIPS/data -b 4096 -p 256 -s 0x120000 spiffs.bin
esptool.py --chip esp32 --port /dev/cu.usbserial-XXXXX --baud 460800 write_flash -z 0x2D0000 spiffs.bin
```

A custom partition table is included (`partitions.csv`) with dual app slots for OTA support:
- **app0 + app1**: 1.38 MB each (active firmware + OTA staging)
- **SPIFFS**: 1.13 MB (compressed clock face images + PNG previews)

### Clock Face Images

Images are stored on SPIFFS as zlib-compressed CLK files with the naming convention `{set * 10 + digit}.clk`. For example, graphic set 1 uses files `10.clk` through `19.clk` for digits 0-9. PNG preview thumbnails (`p1.png`, `p2.png`, etc.) are included for the web UI. Multiple sets can be loaded and selected from the dashboard.

## Button Controls

| Button | Idle Mode | Menu Mode |
|--------|-----------|-----------|
| **Mode** | Enter menu | Next menu option |
| **Left** | Enter menu | Decrease value |
| **Right** | Enter menu | Increase value |
| **Power** | Toggle displays on/off | Exit menu (saves settings) |

The menu automatically returns to idle after 10 seconds of inactivity, saving any changes.

### Physical Menu Options

1. Backlight pattern
2. Backlight colour
3. Backlight intensity
4. 12/24 hour format
5. Blank leading zero
6. Clock face selection
7. WiFi AP on/off
8. Factory reset (press left or right to confirm)

## License

This project is licensed under the [GNU General Public License v3.0](LICENSE) — see the LICENSE file for details.

Based on work by [SmittyHalibut](https://github.com/SmittyHalibut/EleksTubeHAX) and [aly-fly](https://github.com/aly-fly/EleksTubeHAX).

## Author

Andy Taylor ([MW0MWZ](https://github.com/MW0MWZ))
