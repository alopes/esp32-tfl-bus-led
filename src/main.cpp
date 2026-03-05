#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <DNSServer.h>
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
DNSServer    dnsServer;
bool         provisioningActive = false;
unsigned long buttonPressStart  = 0;
bool         buttonWasPressed   = false;

Adafruit_NeoPixel led(LED_COUNT, LED_PIN, NEO_GRB + NEO_KHZ800);

char currentLine[8]  = "";
char currentDest[32] = "";
bool flashState      = false;
unsigned long lastPoll      = 0;
unsigned long lastLedUpdate = 0;
int           currentMinutes = -1;

// ─── NVS helpers ───────────────────────────────────────────────────────────

void loadConfig() {
    if (!prefs.begin("busled", false)) {
        Serial.println("NVS not available, using compile-time defaults");
        strncpy(cfgWifiSsid, WIFI_SSID, sizeof(cfgWifiSsid) - 1);
        cfgWifiSsid[sizeof(cfgWifiSsid) - 1] = '\0';
        strncpy(cfgWifiPassword, WIFI_PASSWORD, sizeof(cfgWifiPassword) - 1);
        cfgWifiPassword[sizeof(cfgWifiPassword) - 1] = '\0';
        strncpy(cfgStopId, TFL_STOP_ID, sizeof(cfgStopId) - 1);
        cfgStopId[sizeof(cfgStopId) - 1] = '\0';
        strncpy(cfgTrackedLines, TRACKED_LINES, sizeof(cfgTrackedLines) - 1);
        cfgTrackedLines[sizeof(cfgTrackedLines) - 1] = '\0';
        return;
    }

    auto loadStr = [](Preferences &p, const char* key, char* dest, size_t destSize, const char* fallback) {
        String val = p.isKey(key) ? p.getString(key) : String(fallback);
        strncpy(dest, val.c_str(), destSize - 1);
        dest[destSize - 1] = '\0';
    };

    loadStr(prefs, "deviceName",   cfgDeviceName,    sizeof(cfgDeviceName),    "");
    loadStr(prefs, "wifiSsid",     cfgWifiSsid,      sizeof(cfgWifiSsid),      WIFI_SSID);
    loadStr(prefs, "wifiPassword", cfgWifiPassword,   sizeof(cfgWifiPassword),  WIFI_PASSWORD);
    loadStr(prefs, "stopId",       cfgStopId,         sizeof(cfgStopId),        TFL_STOP_ID);
    loadStr(prefs, "trackedLines", cfgTrackedLines,   sizeof(cfgTrackedLines),  TRACKED_LINES);

    prefs.end();
}

bool saveDeviceConfig() {
    if (!prefs.begin("busled", false)) {
        Serial.println("NVS open failed for device config save");
        return false;
    }
    prefs.putString("deviceName",   cfgDeviceName);
    prefs.putString("wifiSsid",     cfgWifiSsid);
    prefs.putString("wifiPassword", cfgWifiPassword);
    prefs.end();
    return true;
}

bool saveTrackingConfig() {
    if (!prefs.begin("busled", false)) {
        Serial.println("NVS open failed for tracking config save");
        return false;
    }
    prefs.putString("stopId",        cfgStopId);
    prefs.putString("trackedLines",  cfgTrackedLines);
    prefs.end();
    return true;
}

// ─── LED helpers ───────────────────────────────────────────────────────────

void setLED(uint8_t r, uint8_t g, uint8_t b) {
    led.setPixelColor(0, led.Color(r, g, b));
    led.show();
}

void ledOff() {
    setLED(0, 0, 0);
}

// ─── Provisioning HTML ────────────────────────────────────────────────────

static const char PROVISIONING_PAGE[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Bus Indicator Setup</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Johnston','Gill Sans',system-ui,sans-serif;background:#003888;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}
.card{background:#fff;border-radius:8px;padding:2rem;width:100%;max-width:400px;box-shadow:0 4px 24px rgba(0,0,0,.2);color:#1c3f94}
.roundel{width:48px;height:48px;margin:0 auto 1rem;position:relative}
.roundel .bar{position:absolute;top:50%;left:0;width:100%;height:12px;margin-top:-6px;background:#dc241f;border-radius:2px}
.roundel .ring{width:48px;height:48px;border:5px solid #dc241f;border-radius:50%;position:relative}
h1{font-size:1.2rem;text-align:center;margin-bottom:.2rem;color:#1c3f94;font-weight:700;letter-spacing:-.02em}
p.sub{font-size:.8rem;color:#666;margin-bottom:1.5rem;text-align:center}
label{display:block;font-size:.8rem;margin-bottom:.3rem;color:#1c3f94;font-weight:600}
input,select{width:100%;padding:.55rem;border:2px solid #ccd6e0;border-radius:4px;background:#f6f8fa;color:#1c3f94;font-size:.9rem;margin-bottom:.8rem;font-family:inherit}
select{cursor:pointer}select option{background:#fff;color:#1c3f94}
input:focus,select:focus{outline:none;border-color:#003888}
.req::after{content:" *";color:#dc241f}
button{width:100%;padding:.65rem;background:#dc241f;color:#fff;border:none;border-radius:4px;font-size:.95rem;cursor:pointer;margin-top:.3rem;font-family:inherit;font-weight:600;letter-spacing:.02em}
button:hover{background:#b71c1c}
.btn-sm{width:auto;display:inline-block;padding:.35rem .7rem;font-size:.75rem;margin:0 0 .8rem 0;background:#fff;color:#003888;border:2px solid #003888;font-weight:600}
.btn-sm:hover{background:#003888;color:#fff}
.err{color:#dc241f;font-size:.8rem;margin-bottom:.5rem;display:none}
.sep{border:none;border-top:1px solid #e0e4e8;margin:1rem 0}
</style></head><body><div class="card">
<div class="roundel"><div class="ring"></div><div class="bar"></div></div>
<h1>Bus Indicator Setup</h1><p class="sub">Connect to Wi-Fi and configure bus tracking.</p>
<form id="f" method="POST" action="/provision">
<label class="req">Wi-Fi Network</label>
<select id="sel" onchange="toggleManual()"><option value="">Scanning...</option></select>
<input id="s" name="ssid" maxlength="63" autocomplete="off" placeholder="Enter network name" style="display:none">
<button type="button" class="btn-sm" onclick="doScan()">Rescan networks</button>
<label class="req" for="p">Wi-Fi Password</label><input id="p" name="password" type="password" maxlength="63">
<hr class="sep">
<label for="st">TfL Stop ID</label><input id="st" name="stopId" maxlength="63" placeholder="e.g. 490008660N">
<label for="l">Bus Lines (comma-separated)</label><input id="l" name="lines" maxlength="127" placeholder="e.g. 55,243,N55">
<label for="dn">Device Name</label><input id="dn" name="deviceName" maxlength="63" placeholder="e.g. Kitchen">
<div class="err" id="e"></div>
<button type="submit">Save &amp; Connect</button>
</form></div>
<script>
function bars(rssi){if(rssi>=-50)return'\u2588\u2588\u2588\u2588';if(rssi>=-60)return'\u2588\u2588\u2588\u2591';if(rssi>=-70)return'\u2588\u2588\u2591\u2591';return'\u2588\u2591\u2591\u2591';}
function toggleManual(){var sel=document.getElementById('sel'),inp=document.getElementById('s');if(sel.value===''){var isManual=sel.options[sel.selectedIndex]&&sel.options[sel.selectedIndex].dataset.manual;if(isManual){inp.style.display='';inp.value='';inp.focus();return;}}inp.style.display='none';inp.value=sel.value;}
function doScan(){var sel=document.getElementById('sel');sel.innerHTML='<option value="">Scanning...</option>';document.getElementById('s').style.display='none';
fetch('/scan').then(function(r){return r.json();}).then(function(d){
sel.innerHTML='';var nets=d.networks||[];
if(nets.length===0){sel.innerHTML='<option value="">No networks found</option>';document.getElementById('s').style.display='';document.getElementById('s').value='';return;}
for(var i=0;i<nets.length;i++){var o=document.createElement('option');o.value=nets[i].ssid;o.textContent=nets[i].ssid+(nets[i].open?' (open)':'')+' '+bars(nets[i].rssi);sel.appendChild(o);}
var m=document.createElement('option');m.value='';m.dataset.manual='1';m.textContent='Other (enter manually)';sel.appendChild(m);
document.getElementById('s').value=sel.value;
}).catch(function(){sel.innerHTML='<option value="">Scan failed</option>';document.getElementById('s').style.display='';document.getElementById('s').value='';});}
document.getElementById('f').onsubmit=function(ev){var inp=document.getElementById('s');if(!inp.value.trim()){ev.preventDefault();var e=document.getElementById('e');e.textContent='Wi-Fi network name is required.';e.style.display='block';return false;}};
doScan();
</script>
</body></html>)rawhtml";

static const char PROVISIONING_SUCCESS[] PROGMEM = R"rawhtml(
<!DOCTYPE html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Setup Complete</title><style>
*{box-sizing:border-box;margin:0;padding:0}
body{font-family:'Johnston','Gill Sans',system-ui,sans-serif;background:#003888;color:#fff;min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1rem}
.card{background:#fff;border-radius:8px;padding:2rem;width:100%;max-width:400px;box-shadow:0 4px 24px rgba(0,0,0,.2);text-align:center;color:#1c3f94}
.tick{font-size:2.5rem;margin-bottom:.5rem}
h1{font-size:1.2rem;margin-bottom:.5rem;color:#1c3f94;font-weight:700}p{font-size:.85rem;color:#666;margin-top:.5rem}
</style></head><body><div class="card"><div class="tick">&#10003;</div><h1>Setup Complete</h1><p>Configuration saved. The device will now restart and connect to your Wi-Fi network.</p><p>This page will stop responding shortly.</p></div></body></html>)rawhtml";

// ─── Provisioning LED feedback ────────────────────────────────────────────

void ledProvisioningPulse() {
    static unsigned long lastToggle = 0;
    static bool on = false;
    if (millis() - lastToggle >= 1000) {
        lastToggle = millis();
        on = !on;
        if (on) setLED(0, 0, 255);
        else    ledOff();
    }
}

void ledFactoryResetWarning() {
    static unsigned long lastToggle = 0;
    static bool on = false;
    if (millis() - lastToggle >= 150) {
        lastToggle = millis();
        on = !on;
        if (on) setLED(255, 255, 255);
        else    ledOff();
    }
}

// ─── mDNS ─────────────────────────────────────────────────────────────────

void startMDNS() {
    MDNS.end();

    char hostname[64] = "";
    if (cfgDeviceName[0] != '\0') {
        snprintf(hostname, sizeof(hostname), "busled-%.56s", cfgDeviceName);
        for (char* p = hostname + 7; *p; p++) {
            char c = (*p == ' ') ? '-' : tolower((unsigned char)*p);
            if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) c = '-';
            *p = c;
        }
        size_t len = strlen(hostname);
        while (len > 7 && hostname[7] == '-') { memmove(hostname + 7, hostname + 8, len - 7); --len; }
        while (len > 7 && hostname[len - 1] == '-') hostname[--len] = '\0';
    }

    if (strlen(hostname) <= 7) {
        uint8_t mac[6];
        WiFi.macAddress(mac);
        snprintf(hostname, sizeof(hostname), "busled-%02x%02x%02x", mac[3], mac[4], mac[5]);
    }

    if (MDNS.begin(hostname)) {
        MDNS.addService("http", "tcp", 80);
        Serial.printf("mDNS started: %s.local\n", hostname);
    } else {
        Serial.println("mDNS failed to start");
    }
}

// ─── Wi-Fi ─────────────────────────────────────────────────────────────────

bool connectWiFi() {
    if (WiFi.status() == WL_CONNECTED) return true;

    WiFi.mode(WIFI_STA);
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
        startMDNS();
        return true;
    }
    Serial.println("\nWi-Fi connection failed");
    return false;
}

// ─── Wi-Fi scan ──────────────────────────────────────────────────────────

void handleWifiScan() {
    int n = WiFi.scanNetworks(false, false);

    JsonDocument doc;
    JsonArray networks = doc["networks"].to<JsonArray>();

    if (n > 0) {
        struct Network { String ssid; int rssi; bool open; };
        Network unique[32];
        int count = 0;

        for (int i = 0; i < n && count < 32; i++) {
            String ssid = WiFi.SSID(i);
            if (ssid.length() == 0) continue;

            bool found = false;
            for (int j = 0; j < count; j++) {
                if (unique[j].ssid == ssid) {
                    if (WiFi.RSSI(i) > unique[j].rssi) {
                        unique[j].rssi = WiFi.RSSI(i);
                        unique[j].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                    }
                    found = true;
                    break;
                }
            }
            if (!found) {
                unique[count].ssid = ssid;
                unique[count].rssi = WiFi.RSSI(i);
                unique[count].open = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN);
                count++;
            }
        }

        for (int i = 0; i < count - 1; i++) {
            for (int j = i + 1; j < count; j++) {
                if (unique[j].rssi > unique[i].rssi) {
                    Network tmp = unique[i];
                    unique[i] = unique[j];
                    unique[j] = tmp;
                }
            }
        }

        for (int i = 0; i < count; i++) {
            JsonObject net = networks.add<JsonObject>();
            net["ssid"] = unique[i].ssid;
            net["rssi"] = unique[i].rssi;
            net["open"] = unique[i].open;
        }
    }

    WiFi.scanDelete();

    String body;
    serializeJson(doc, body);
    server.send(200, "application/json", body);
}

// ─── Provisioning ─────────────────────────────────────────────────────────

void startProvisioning() {
    uint8_t mac[6];
    WiFi.macAddress(mac);
    char apSsid[32];
    snprintf(apSsid, sizeof(apSsid), "BusIndicator-%02X%02X%02X", mac[3], mac[4], mac[5]);

    WiFi.mode(WIFI_AP_STA);
    WiFi.softAP(apSsid);
    delay(100);
    Serial.printf("Provisioning AP started: %s (IP: %s)\n", apSsid, WiFi.softAPIP().toString().c_str());

    dnsServer.start(53, "*", WiFi.softAPIP());

    server.on("/", HTTP_GET, []() {
        server.send_P(200, "text/html", PROVISIONING_PAGE);
    });

    server.on("/scan", HTTP_GET, handleWifiScan);

    server.on("/provision", HTTP_POST, []() {
        String ssid = server.arg("ssid");
        ssid.trim();
        if (ssid.length() == 0) {
            server.send(400, "text/plain", "Wi-Fi SSID is required");
            return;
        }

        strncpy(cfgWifiSsid, ssid.c_str(), sizeof(cfgWifiSsid) - 1);
        cfgWifiSsid[sizeof(cfgWifiSsid) - 1] = '\0';

        String pass = server.arg("password");
        strncpy(cfgWifiPassword, pass.c_str(), sizeof(cfgWifiPassword) - 1);
        cfgWifiPassword[sizeof(cfgWifiPassword) - 1] = '\0';

        String stopId = server.arg("stopId");
        stopId.trim();
        strncpy(cfgStopId, stopId.c_str(), sizeof(cfgStopId) - 1);
        cfgStopId[sizeof(cfgStopId) - 1] = '\0';

        String lines = server.arg("lines");
        lines.trim();
        strncpy(cfgTrackedLines, lines.c_str(), sizeof(cfgTrackedLines) - 1);
        cfgTrackedLines[sizeof(cfgTrackedLines) - 1] = '\0';

        String devName = server.arg("deviceName");
        devName.trim();
        strncpy(cfgDeviceName, devName.c_str(), sizeof(cfgDeviceName) - 1);
        cfgDeviceName[sizeof(cfgDeviceName) - 1] = '\0';

        saveDeviceConfig();
        saveTrackingConfig();

        server.send_P(200, "text/html", PROVISIONING_SUCCESS);
        Serial.println("Provisioning complete, restarting...");
        provisioningActive = false;
    });

    auto redirectToRoot = []() {
        server.sendHeader("Location", "http://192.168.4.1/");
        server.send(302, "text/plain", "");
    };
    server.on("/generate_204",       HTTP_GET, redirectToRoot);
    server.on("/hotspot-detect.html", HTTP_GET, redirectToRoot);
    server.on("/connecttest.txt",    HTTP_GET, redirectToRoot);
    server.on("/ncsi.txt",           HTTP_GET, redirectToRoot);
    server.onNotFound(redirectToRoot);

    server.begin();
    provisioningActive = true;

    while (provisioningActive) {
        dnsServer.processNextRequest();
        server.handleClient();
        ledProvisioningPulse();
        delay(1);
    }

    delay(1000);
    server.stop();
    dnsServer.stop();
    setLED(0, 255, 0);
    delay(500);
    ledOff();
    ESP.restart();
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

    if (cfgTrackedLines[0] == '\0') {
        Serial.println("No tracked lines configured");
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
    currentLine[sizeof(currentLine) - 1] = '\0';
    strncpy(currentDest, nearestDest, sizeof(currentDest) - 1);
    currentDest[sizeof(currentDest) - 1] = '\0';
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
// TODO: add X-Api-Key authentication — this API is currently unauthenticated (preview only).

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

    if (!doc["deviceName"].isNull()) {
        if (!doc["deviceName"].is<const char*>()) {
            server.send(400, "application/json", "{\"error\":\"deviceName must be a string\"}");
            return;
        }
        strncpy(cfgDeviceName, doc["deviceName"].as<const char*>(), sizeof(cfgDeviceName) - 1);
        cfgDeviceName[sizeof(cfgDeviceName) - 1] = '\0';
    }

    if (!doc["wifi"].isNull()) {
        if (!doc["wifi"].is<JsonObject>()) {
            server.send(400, "application/json", "{\"error\":\"wifi must be an object\"}");
            return;
        }

        if (!doc["wifi"]["ssid"].isNull()) {
            if (!doc["wifi"]["ssid"].is<const char*>()) {
                server.send(400, "application/json", "{\"error\":\"wifi.ssid must be a string\"}");
                return;
            }
            const char* newSsid = doc["wifi"]["ssid"].as<const char*>();
            if (newSsid[0] == '\0') {
                server.send(400, "application/json", "{\"error\":\"wifi.ssid must not be empty\"}");
                return;
            }
            if (strcmp(cfgWifiSsid, newSsid) != 0) {
                strncpy(cfgWifiSsid, newSsid, sizeof(cfgWifiSsid) - 1);
                cfgWifiSsid[sizeof(cfgWifiSsid) - 1] = '\0';
                wifiChanged = true;
            }
        }

        if (!doc["wifi"]["password"].isNull()) {
            if (!doc["wifi"]["password"].is<const char*>()) {
                server.send(400, "application/json", "{\"error\":\"wifi.password must be a string\"}");
                return;
            }
            const char* newPass = doc["wifi"]["password"].as<const char*>();
            if (strcmp(cfgWifiPassword, newPass) != 0) {
                strncpy(cfgWifiPassword, newPass, sizeof(cfgWifiPassword) - 1);
                cfgWifiPassword[sizeof(cfgWifiPassword) - 1] = '\0';
                wifiChanged = true;
            }
        }
    }

    if (!saveDeviceConfig()) {
        server.send(500, "application/json", "{\"error\":\"Failed to persist config\"}");
        return;
    }
    server.send(200, "application/json", "{}");

    if (wifiChanged) {
        delay(100);
        WiFi.disconnect(true);
        unsigned long start = millis();
        while (WiFi.status() == WL_CONNECTED && millis() - start < 5000) {
            delay(50);
        }
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

    if (stops.size() > 1) {
        server.send(400, "application/json", "{\"error\":\"Only one stop is supported\"}");
        return;
    }

    if (stops.size() > 0) {
        JsonObject stop = stops[0].as<JsonObject>();

        if (!stop["stopId"].is<const char*>() || !stop["lines"].is<JsonArray>()) {
            server.send(400, "application/json", "{\"error\":\"Each stop must have stopId and lines\"}");
            return;
        }

        const char* stopId = stop["stopId"].as<const char*>();
        if (stopId[0] == '\0') {
            server.send(400, "application/json", "{\"error\":\"stopId must not be empty\"}");
            return;
        }

        strncpy(cfgStopId, stopId, sizeof(cfgStopId) - 1);
        cfgStopId[sizeof(cfgStopId) - 1] = '\0';

        JsonArray lines = stop["lines"].as<JsonArray>();
        String linesStr = "";
        bool first = true;
        for (JsonVariant line : lines) {
            if (!line.is<const char*>()) {
                server.send(400, "application/json", "{\"error\":\"Each line must be a non-empty string\"}");
                return;
            }
            const char* lineStr = line.as<const char*>();
            if (!lineStr || lineStr[0] == '\0') {
                server.send(400, "application/json", "{\"error\":\"Each line must be a non-empty string\"}");
                return;
            }
            if (!first) linesStr += ",";
            linesStr += lineStr;
            first = false;
        }
        if (linesStr.length() >= sizeof(cfgTrackedLines)) {
            server.send(400, "application/json", "{\"error\":\"Tracked lines list too long\"}");
            return;
        }
        strncpy(cfgTrackedLines, linesStr.c_str(), sizeof(cfgTrackedLines) - 1);
        cfgTrackedLines[sizeof(cfgTrackedLines) - 1] = '\0';
    } else {
        cfgStopId[0]        = '\0';
        cfgTrackedLines[0]  = '\0';
    }

    if (!saveTrackingConfig()) {
        server.send(500, "application/json", "{\"error\":\"Failed to persist config\"}");
        return;
    }
    server.send(200, "application/json", "{}");
}

void handleGetStatus() {
    JsonDocument doc;

    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = (WiFi.status() == WL_CONNECTED);
    wifi["ip"]        = WiFi.localIP().toString();

    JsonObject tracking = doc["tracking"].to<JsonObject>();
    tracking["msSinceLastPoll"]     = (lastPoll > 0) ? millis() - lastPoll : 0;
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

    pinMode(BOOT_BUTTON_PIN, INPUT_PULLUP);

    led.begin();
    led.setBrightness(LED_BRIGHTNESS);
    ledOff();

    setLED(0, 0, 255); delay(300);
    setLED(255, 255, 0); delay(300);
    setLED(255, 0, 0); delay(300);
    ledOff();
    Serial.println("LED self-test complete");

    loadConfig();

    if (cfgWifiSsid[0] == '\0') {
        Serial.println("No Wi-Fi credentials configured, entering provisioning mode");
        startProvisioning();
        return;
    }

    if (!connectWiFi()) {
        Serial.println("Wi-Fi connection failed, entering provisioning mode");
        startProvisioning();
        return;
    }

    setupServer();
}

void loop() {
    bool buttonPressed = (digitalRead(BOOT_BUTTON_PIN) == LOW);
    if (buttonPressed && !buttonWasPressed) {
        buttonPressStart = millis();
    }
    if (buttonPressed) {
        ledFactoryResetWarning();
        if (millis() - buttonPressStart >= FACTORY_RESET_HOLD_MS) {
            Serial.println("Factory reset: clearing Wi-Fi credentials");
            if (prefs.begin("busled", false)) {
                prefs.putString("wifiSsid", "");
                prefs.putString("wifiPassword", "");
                prefs.end();
            }
            setLED(255, 255, 255);
            delay(500);
            ledOff();
            ESP.restart();
        }
    }
    buttonWasPressed = buttonPressed;

    connectWiFi();
    server.handleClient();

    unsigned long now = millis();

    if (now - lastPoll >= POLL_INTERVAL_MS || lastPoll == 0) {
        lastPoll = now;
        currentMinutes = fetchNearestBusMinutes();
    }

    unsigned long ledInterval = (currentMinutes >= 0 && currentMinutes < THRESHOLD_RED) ? 500 : 1000;

    if (now - lastLedUpdate >= ledInterval) {
        lastLedUpdate = now;
        updateLED(currentMinutes);

        if (currentMinutes >= 0 && currentMinutes < THRESHOLD_RED) {
            Serial.printf("%s to %s — %d min [flashing red]\n", currentLine, currentDest, currentMinutes);
        } else if (currentMinutes >= 0) {
            const char* colour = currentMinutes >= THRESHOLD_BLUE ? "blue" :
                                 currentMinutes >= THRESHOLD_YELLOW ? "yellow" : "red";
            Serial.printf("%s to %s — %d min [%s]\n", currentLine, currentDest, currentMinutes, colour);
        } else {
            Serial.println("No buses — LED off");
        }
    }
}

