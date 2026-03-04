#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "config.h"

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

char currentLine[8] = "";
char currentDest[32] = "";

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

void ledOff() {
    setLED(0, 0, 0);
}

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("Connecting to %s", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
    } else {
        Serial.println("\nWi-Fi connection failed");
    }
}

void setup() {
    Serial.begin(115200);
    delay(1000);

    led.begin();
    led.setBrightness(LED_BRIGHTNESS);
    ledOff();

    setLED(0, 0, 255); delay(300);
    setLED(255, 255, 0); delay(300);
    setLED(255, 0, 0); delay(300);
    ledOff();
    Serial.println("LED self-test complete");

    connectWiFi();
}

int fetchNearestBusMinutes() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi not connected");
        return -1;
    }

    HTTPClient http;
    http.begin(TFL_STOP_URL);
    http.setTimeout(10000);

    int httpCode = http.GET();
    if (httpCode < 0) {
        Serial.printf("Connection failed (error %d)\n", httpCode);
        http.end();
        return -1;
    }
    if (httpCode != 200) {
        Serial.printf("HTTP %d\n", httpCode);
        http.end();
        return -1;
    }

    String payload = http.getString();
    http.end();

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, payload);
    if (err) {
        Serial.printf("JSON parse error: %s\n", err.c_str());
        return -1;
    }

    JsonArray arrivals = doc.as<JsonArray>();
    if (arrivals.size() == 0) {
        Serial.println("No arrivals");
        return -1;
    }

    int minSeconds = INT_MAX;
    const char* nearestLine = "";
    const char* nearestDest = "";

    for (JsonObject bus : arrivals) {
        const char* line = bus["lineName"] | "?";

        bool tracked = false;
        char buf[sizeof(TRACKED_LINES)];
        strncpy(buf, TRACKED_LINES, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            if (strcmp(line, tok) == 0) {
                tracked = true;
                break;
            }
            tok = strtok(NULL, ",");
        }
        if (!tracked) continue;

        int t = bus["timeToStation"] | INT_MAX;
        if (t < minSeconds) {
            minSeconds = t;
            nearestLine = line;
            nearestDest = bus["destinationName"] | "?";
        }
    }

    if (minSeconds == INT_MAX) {
        Serial.println("No tracked arrivals");
        return -1;
    }

    int minutes = minSeconds / 60;
    strncpy(currentLine, nearestLine, sizeof(currentLine) - 1);
    strncpy(currentDest, nearestDest, sizeof(currentDest) - 1);
    Serial.printf("Nearest: %s to %s in %d min\n", currentLine, currentDest, minutes);
    return minutes;
}

bool flashState = false;

void updateLED(int minutes) {
    if (minutes < 0 || minutes >= THRESHOLD_OFF) {
        ledOff();
    } else if (minutes >= THRESHOLD_BLUE) {
        setLED(0, 0, 255);
    } else if (minutes >= THRESHOLD_YELLOW) {
        setLED(255, 255, 0);
    } else if (minutes >= THRESHOLD_RED) {
        setLED(255, 0, 0);
    } else {
        flashState = !flashState;
        if (flashState) {
            setLED(255, 0, 0);
        } else {
            ledOff();
        }
    }
}

unsigned long lastPoll = 0;
int currentMinutes = -1;

void loop() {
    connectWiFi();

    unsigned long now = millis();

    if (now - lastPoll >= POLL_INTERVAL_MS || lastPoll == 0) {
        lastPoll = now;
        currentMinutes = fetchNearestBusMinutes();
    }

    updateLED(currentMinutes);

    if (currentMinutes >= 0 && currentMinutes < THRESHOLD_RED) {
        Serial.printf("%s to %s — %d min [flashing red]\n", currentLine, currentDest, currentMinutes);
        delay(500);
    } else if (currentMinutes >= 0) {
        const char* colour = currentMinutes >= THRESHOLD_BLUE ? "blue" :
                             currentMinutes >= THRESHOLD_YELLOW ? "yellow" : "red";
        Serial.printf("%s to %s — %d min [%s]\n", currentLine, currentDest, currentMinutes, colour);
        delay(1000);
    } else {
        Serial.println("No buses — LED off");
        delay(1000);
    }
}
