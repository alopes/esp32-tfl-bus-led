#pragma once

#ifndef WIFI_SSID
#error "WIFI_SSID not configured — create secrets.ini (see secrets.example.ini)"
#endif

#ifndef WIFI_PASSWORD
#error "WIFI_PASSWORD not configured — create secrets.ini (see secrets.example.ini)"
#endif

#ifndef TFL_STOP_ID
#error "TFL_STOP_ID not configured — create secrets.ini (see secrets.example.ini)"
#endif

#ifndef TRACKED_LINES
#error "TRACKED_LINES not configured — create secrets.ini (see secrets.example.ini)"
#endif

#define TFL_STOP_URL "https://api.tfl.gov.uk/StopPoint/" TFL_STOP_ID "/Arrivals"

const unsigned long POLL_INTERVAL_MS = 30000;

const int LED_PIN       = 48;
const int LED_COUNT     = 1;
const int LED_BRIGHTNESS = 30;

const int THRESHOLD_OFF    = 15;
const int THRESHOLD_BLUE   = 10;
const int THRESHOLD_YELLOW = 5;
const int THRESHOLD_RED    = 2;
