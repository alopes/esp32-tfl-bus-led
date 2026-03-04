#include <Arduino.h>
#include <WiFi.h>
#include <Adafruit_NeoPixel.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include <Preferences.h>
#include "config.h"

// ─── Runtime config (loaded from NVS; compile-time values are fallback defaults) ───
char cfgDeviceName[64]    = "";
char cfgWifiSsid[64]      = "";
char cfgWifiPassword[64]  = "";
char cfgStopId[64]        = "";
char cfgTrackedLines[128] = "";

Preferences prefs;
WebServer    server(80);

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

char currentLine[8]  = "";
char currentDest[32] = "";
bool flashState      = false;
unsigned long lastPoll    = 0;
int           currentMinutes = -1;

// ─── NVS helpers ───────────────────────────────────────────────────────────

void loadConfig() {
    prefs.begin("busled", true);
    String val;

    val = prefs.getString("deviceName", "");
    strncpy(cfgDeviceName, val.c_str(), sizeof(cfgDeviceName) - 1);
    cfgDeviceName[sizeof(cfgDeviceName) - 1] = '\0';

    val = prefs.getString("wifiSsid", WIFI_SSID);
    strncpy(cfgWifiSsid, val.c_str(), sizeof(cfgWifiSsid) - 1);
    cfgWifiSsid[sizeof(cfgWifiSsid) - 1] = '\0';

    val = prefs.getString("wifiPassword", WIFI_PASSWORD);
    strncpy(cfgWifiPassword, val.c_str(), sizeof(cfgWifiPassword) - 1);
    cfgWifiPassword[sizeof(cfgWifiPassword) - 1] = '\0';

    val = prefs.getString("stopId", TFL_STOP_ID);
    strncpy(cfgStopId, val.c_str(), sizeof(cfgStopId) - 1);
    cfgStopId[sizeof(cfgStopId) - 1] = '\0';

    val = prefs.getString("trackedLines", TRACKED_LINES);
    strncpy(cfgTrackedLines, val.c_str(), sizeof(cfgTrackedLines) - 1);
    cfgTrackedLines[sizeof(cfgTrackedLines) - 1] = '\0';

    prefs.end();
}

void saveDeviceConfig() {
    prefs.begin("busled", false);
    prefs.putString("deviceName",   cfgDeviceName);
    prefs.putString("wifiSsid",     cfgWifiSsid);
    prefs.putString("wifiPassword", cfgWifiPassword);
    prefs.end();
}

void saveTrackingConfig() {
    prefs.begin("busled", false);
    prefs.putString("stopId",        cfgStopId);
    prefs.putString("trackedLines",  cfgTrackedLines);
    prefs.end();
}

// ─── LED helpers ───────────────────────────────────────────────────────────

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

void ledOff() {
    setLED(0, 0, 0);
}

// ─── Wi-Fi ─────────────────────────────────────────────────────────────────

void connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return;

    Serial.printf("Connecting to %s", cfgWifiSsid);
    WiFi.begin(cfgWifiSsid, cfgWifiPassword);

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

// ─── TfL polling ───────────────────────────────────────────────────────────

int fetchNearestBusMinutes() {
    if (WiFi.status() != WL_CONNECTED) {
        Serial.println("Wi-Fi not connected");
        return -1;
    }

    if (cfgStopId[0] == '\0') {
        Serial.println("No stop ID configured");
        return -1;
    }

    String url = String("https://api.tfl.gov.uk/StopPoint/") + cfgStopId + "/Arrivals";

    HTTPClient http;
    http.begin(url);
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

    int minSeconds       = INT_MAX;
    const char* nearestLine = "";
    const char* nearestDest = "";

    for (JsonObject bus : arrivals) {
        const char* line = bus["lineName"] | "?";

        bool tracked = false;
        char buf[sizeof(cfgTrackedLines)];
        strncpy(buf, cfgTrackedLines, sizeof(buf) - 1);
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
            minSeconds  = t;
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

// ─── LED logic ─────────────────────────────────────────────────────────────

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

// ─── HTTP server ───────────────────────────────────────────────────────────
// NOTE: This API is currently unauthenticated (preview only).
// Authentication (API key via X-Api-Key header) is planned as a follow-up.

void handleGetConfigDevice() {
    JsonDocument doc;
    doc["deviceName"] = cfgDeviceName;
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["ssid"] = cfgWifiSsid;
    // Password is never returned

    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void handlePostConfigDevice() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    bool wifiChanged = false;

    if (doc["deviceName"].is<const char*>()) {
        strncpy(cfgDeviceName, doc["deviceName"].as<const char*>(), sizeof(cfgDeviceName) - 1);
        cfgDeviceName[sizeof(cfgDeviceName) - 1] = '\0';
    }

    if (doc["wifi"]["ssid"].is<const char*>()) {
        const char* newSsid = doc["wifi"]["ssid"].as<const char*>();
        if (strcmp(cfgWifiSsid, newSsid) != 0) {
            strncpy(cfgWifiSsid, newSsid, sizeof(cfgWifiSsid) - 1);
            cfgWifiSsid[sizeof(cfgWifiSsid) - 1] = '\0';
            wifiChanged = true;
        }
    }

    if (doc["wifi"]["password"].is<const char*>()) {
        const char* newPass = doc["wifi"]["password"].as<const char*>();
        if (strcmp(cfgWifiPassword, newPass) != 0) {
            strncpy(cfgWifiPassword, newPass, sizeof(cfgWifiPassword) - 1);
            cfgWifiPassword[sizeof(cfgWifiPassword) - 1] = '\0';
            wifiChanged = true;
        }
    }

    saveDeviceConfig();
    server.send(200, "application/json", "{}");

    if (wifiChanged) {
        // Brief delay to allow the HTTP response to be transmitted before
        // dropping the Wi-Fi connection.
        delay(100);
        WiFi.disconnect(false, false);
        connectWiFi();
    }
}

void handleGetConfigTracking() {
    JsonDocument doc;
    JsonArray stops = doc["stops"].to<JsonArray>();

    if (cfgStopId[0] != '\0') {
        JsonObject stop = stops.add<JsonObject>();
        stop["stopId"] = cfgStopId;
        JsonArray lines = stop["lines"].to<JsonArray>();

        char buf[sizeof(cfgTrackedLines)];
        strncpy(buf, cfgTrackedLines, sizeof(buf) - 1);
        buf[sizeof(buf) - 1] = '\0';
        char* tok = strtok(buf, ",");
        while (tok) {
            lines.add(tok);
            tok = strtok(NULL, ",");
        }
    }

    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void handlePostConfigTracking() {
    if (!server.hasArg("plain")) {
        server.send(400, "application/json", "{\"error\":\"Missing body\"}");
        return;
    }

    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, server.arg("plain"));
    if (err) {
        server.send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
    }

    JsonArray stops = doc["stops"].as<JsonArray>();
    if (stops.isNull()) {
        server.send(400, "application/json", "{\"error\":\"Missing stops array\"}");
        return;
    }

    if (stops.size() > 0) {
        JsonObject stop = stops[0].as<JsonObject>();

        if (!stop["stopId"].is<const char*>() || !stop["lines"].is<JsonArray>()) {
            server.send(400, "application/json", "{\"error\":\"Each stop must have stopId and lines\"}");
            return;
        }

        strncpy(cfgStopId, stop["stopId"].as<const char*>(), sizeof(cfgStopId) - 1);
        cfgStopId[sizeof(cfgStopId) - 1] = '\0';

        JsonArray lines = stop["lines"].as<JsonArray>();
        String linesStr = "";
        bool first = true;
        for (JsonVariant line : lines) {
            const char* lineStr = line.as<const char*>();
            if (lineStr && lineStr[0] != '\0') {
                if (!first) linesStr += ",";
                linesStr += lineStr;
                first = false;
            }
        }
        strncpy(cfgTrackedLines, linesStr.c_str(), sizeof(cfgTrackedLines) - 1);
        cfgTrackedLines[sizeof(cfgTrackedLines) - 1] = '\0';
    } else {
        cfgStopId[0]        = '\0';
        cfgTrackedLines[0]  = '\0';
    }

    saveTrackingConfig();
    server.send(200, "application/json", "{}");
}

void handleGetStatus() {
    JsonDocument doc;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = (WiFi.status() == WL_CONNECTED);
    wifi["ip"]        = WiFi.localIP().toString();

    JsonObject tracking = doc["tracking"].to<JsonObject>();
    tracking["lastPollMs"]          = lastPoll;
    tracking["nearestBusMinutes"]   = currentMinutes;
    tracking["nearestLine"]         = currentLine;
    tracking["nearestDestination"]  = currentDest;

    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

void setupServer() {
    server.on("/config/device",   HTTP_GET,  handleGetConfigDevice);
    server.on("/config/device",   HTTP_POST, handlePostConfigDevice);
    server.on("/config/tracking", HTTP_GET,  handleGetConfigTracking);
    server.on("/config/tracking", HTTP_POST, handlePostConfigTracking);
    server.on("/status",          HTTP_GET,  handleGetStatus);
    server.begin();
    Serial.println("HTTP server started on port 80");
}

// ─── Arduino lifecycle ─────────────────────────────────────────────────────

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

    loadConfig();
    connectWiFi();
    setupServer();
}

void loop() {
    connectWiFi();
    server.handleClient();

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

