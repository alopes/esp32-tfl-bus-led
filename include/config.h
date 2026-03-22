#pragma once

#ifndef WIFI_SSID
#define WIFI_SSID ""
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD ""
#endif

// TFL_STOP_ID and TRACKED_LINES are optional compile-time values.
// The firmware will fall back to NVS-stored values (configurable at runtime
// via the HTTP API) when these are not set or left empty.
#ifndef TFL_STOP_ID
#define TFL_STOP_ID ""
#endif

#ifndef TRACKED_LINES
#define TRACKED_LINES ""
#endif

const unsigned long POLL_INTERVAL_MS = 30000;

const int BOOT_BUTTON_PIN = 0;
const unsigned long FACTORY_RESET_HOLD_MS = 5000;

const int LED_PIN       = 48;
const int LED_COUNT     = 1;
const int LED_BRIGHTNESS = 30;

const int THRESHOLD_OFF    = 15;
const int THRESHOLD_BLUE   = 10;
const int THRESHOLD_YELLOW = 5;
const int THRESHOLD_RED    = 2;

const int TOUCH_PIN_CS  = 15;
const int TOUCH_PIN_CLK = 18;
const int TOUCH_PIN_DIN = 16;
const int TOUCH_PIN_DO  = 17;
const unsigned long TOUCH_DEBOUNCE_MS = 500;

const int TOUCH_X_MIN = 200;
const int TOUCH_X_MAX = 3800;
const int TOUCH_Y_MIN = 200;
const int TOUCH_Y_MAX = 3800;

const int MAX_ARRIVALS  = 8;
const int MAX_DISPLAY_ROWS = 7;
const unsigned long DISPLAY_REFRESH_MS = 1000;
