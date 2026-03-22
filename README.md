# ESP32 TfL Bus Indicator

![ESP32 TfL Bus Indicator](esp32-tfl-bus-indicator.jpg)

Real-time bus departure board powered by an ESP32-S3 with a 2.8" TFT touchscreen and RGB LED. Polls the [TfL Unified API](https://api.tfl.gov.uk/) every 30 seconds and displays up to 7 upcoming arrivals with colour-coded countdown timers.

## Features

- **Live departures** — line number, destination, and minutes until arrival, updated every second between API polls
- **Colour-coded urgency** — both the TFT colour bars and onboard RGB LED reflect how soon the next bus arrives
- **Touchscreen navigation** — tap the settings cog to view device info (QR code, hostname, config); tap the back arrow to return
- **Display themes** — three selectable themes (Classic, Dark, Light) that apply instantly
- **Web settings page** — configure Wi-Fi, stop ID, tracked lines, display theme, and status bar visibility from any browser
- **API key authentication** — all write endpoints require an API key; the key is auto-generated and shown during initial setup
- **QR code access** — the device info screen shows a QR code linking directly to the settings page with the API key pre-filled
- **NVS persistence** — all settings survive reboots
- **Captive portal provisioning** — first-boot Wi-Fi setup via a hotspot with automatic portal redirect

## LED Colours

| Nearest Bus | LED |
|---|---|
| >= 15 min or no data | Off |
| 10–14 min | Blue |
| 5–9 min | Yellow |
| 2–4 min | Red |
| 0–1 min | Flashing red |

## TFT Display

The ILI9341 2.8" TFT (240×320) has two screens:

**Departures** — up to 7 rows showing line, destination, and countdown minutes with colour-coded urgency bars. An optional status bar at the bottom shows Wi-Fi status and time since last update.

**Device Info** — QR code linking to the settings page, plus hostname, stop ID, tracked lines, and API key.

<p align="center">
  <img src="display-preview.svg" alt="Display preview" width="280">
</p>

### Display Themes

| Theme | Description |
|---|---|
| Classic | Dark background with blue headers |
| Dark | Pure black background with grey headers, inverted QR code |
| Light | White background with blue headers, dark text |

Themes can be changed from the web settings page and take effect immediately.

### Wiring

| ILI9341 Pin | ESP32-S3 GPIO |
|---|---|
| CS | GPIO 10 |
| RST | GPIO 9 |
| DC | GPIO 8 |
| MOSI | GPIO 11 |
| SCK | GPIO 12 |
| LED | 3V3 |
| VCC | 3V3 |
| GND | GND |

| XPT2046 Touch Pin | ESP32-S3 GPIO |
|---|---|
| T_CS | GPIO 15 |
| T_CLK | GPIO 18 |
| T_DIN | GPIO 16 |
| T_DO | GPIO 17 |

## How It Works

The device connects to Wi-Fi and polls the TfL Unified API every 30 seconds for live arrivals at your configured stop. It filters the response to only the bus lines you're tracking, collects up to 8 arrivals sorted by time, and displays them on the TFT screen. The nearest arrival also drives the LED colour (see table above). Between polls, displayed minutes count down in real time. Serial output logs each update with the line, destination, and colour.

Once connected, the device runs a web server with a settings page for runtime configuration. All changes (Wi-Fi, tracking, display) are applied immediately and persisted to NVS.

## Hardware

- [ESP32-S3-DevKitC-1](https://amzn.to/40KIAMw) (any variant with onboard WS2812 RGB LED on GPIO48)
- [ILI9341 2.8" SPI TFT display with XPT2046 touch](https://amzn.to/3YQ4Lkw) (240×320, 4-wire SPI)
- USB-C cable for power and flashing

## Setup

1. Install [PlatformIO CLI](https://docs.platformio.org/en/latest/core/installation.html)

2. Build and flash:
   ```bash
   pio run -t upload
   ```

3. The device starts a Wi-Fi hotspot called **BusIndicator-XXYYZZ** (the LED pulses blue). Connect to it with your phone or laptop — a setup page opens automatically.

   <img src="setup.jpg" alt="Setup portal" width="300">

4. Enter your Wi-Fi credentials, TfL stop ID, and bus lines. Save the API key shown on the setup page — you will need it to access settings later. The device saves the config, restarts, and connects to your network.

5. Once connected, tap the settings cog on the TFT or scan the QR code on the device info screen to open the web settings page.

To find your stop's NaptanId, search the [TfL API](https://api.tfl.gov.uk/) at `https://api.tfl.gov.uk/StopPoint/Search/{query}` and use the `naptanId` field. Line names are case-sensitive and must match the `lineName` field exactly (e.g. `73,390,N73`).

### Compile-time credentials (optional)

If you prefer to bake credentials into the firmware instead of using the setup portal, copy the example and fill in your values:

```bash
cp secrets.example.ini secrets.ini
# Edit secrets.ini with your Wi-Fi credentials, stop ID, and bus lines
```

## HTTP API

All POST endpoints require an `X-Api-Key` header. The API key is auto-generated on first boot and shown on the provisioning page.

| Method | Endpoint | Description |
|---|---|---|
| GET | `/` | Settings page (HTML) |
| GET | `/scan` | Scan for Wi-Fi networks |
| GET | `/config/device` | Device name, Wi-Fi SSID |
| POST | `/config/device` | Update device name, Wi-Fi credentials |
| GET | `/config/tracking` | Stop ID, tracked lines |
| POST | `/config/tracking` | Update stop ID, tracked lines |
| GET | `/config/display` | Theme, status bar visibility |
| POST | `/config/display` | Update theme, status bar visibility |
| GET | `/status` | Wi-Fi status, IP address, uptime |

## Factory Reset

Hold the **BOOT** button for 5 seconds (LED flashes white while held). The device clears its Wi-Fi credentials and API key, restarts, and re-enters the setup portal. A new API key is generated during setup. Stop ID, tracked lines, and display settings are preserved.

## Configuration

All settings (Wi-Fi, stop ID, tracked lines, display theme, status bar) can be changed at runtime via the web settings page or HTTP API without reflashing. Thresholds, LED settings, pin assignments, and poll interval can be adjusted in `include/config.h`.

## Project Structure

```
├── src/main.cpp           — application logic (single file)
├── include/config.h       — pins, thresholds, LED, poll interval
├── secrets.ini            — Wi-Fi and TfL credentials (not committed)
├── secrets.example.ini    — template for secrets.ini
└── platformio.ini         — PlatformIO build config and library deps
```

## Troubleshooting

- **LED pulses blue on boot** — the device has no Wi-Fi credentials and is in setup mode. Connect to the BusIndicator hotspot to configure it.
- **LED stays off** — check that your stop ID is correct and the line names match the TfL API response.
- **Wi-Fi connection failed** — if the device cannot connect within 10 seconds, it falls back to setup mode automatically.
- **"No tracked departures" on screen** — line names must match the TfL `lineName` field exactly (case-sensitive). Open `https://api.tfl.gov.uk/StopPoint/{your-stop-id}/Arrivals` in a browser to check.
- **Touch not responding** — verify the XPT2046 wiring matches the pin table above. Touch calibration values can be adjusted in `config.h`.
- **Viewing serial output** — run `pio device monitor` to see live logs from the device.

## Licence

MIT
