// ota_server.cpp — исправленная стабильная версия
#include "ota_server.h"
#include "temperature_history.h"
#include <WiFi.h>
#include <ArduinoOTA.h>
#include <WebServer.h>
#include <ElegantOTA.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

static WebServer server(80);
static bool otaRunning = false;
static uint32_t lastCheck = 0;
static uint16_t _otaPort = 3232;
static String _hostname;
static String _webUser;
static String _webPass;
static bool spiffsMounted = false;

String formatTime(uint32_t ms) {
  uint32_t s = ms / 1000;
  uint16_t h = s / 3600;
  uint8_t m = (s % 3600) / 60;
  uint8_t sec = s % 60;
  char buf[16];
  sprintf(buf, "%02u:%02u:%02u", h, m, sec);
  return String(buf);
}

String getTimeString() {
  time_t now = time(nullptr);
  struct tm* t = localtime(&now);
  char buf[32];
  if (t)
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", t);
  else
    sprintf(buf, "TIME_ERR");
  return String(buf);
}

// ---- LOGGING (uses mounted SPIFFS) ----
void logOTA(const String &msg) {
  String entry = "[" + getTimeString() + "] " + msg;
  Serial.println(entry);

  if (!spiffsMounted) return;
  File f = SPIFFS.open("/ota.log", FILE_APPEND);
  if (!f) return;
  f.println(entry);
  f.close();
}

// ---- AUTH helper ----
bool checkAuth() {
  if (_webUser.length() == 0) return true;
  if (!server.authenticate(_webUser.c_str(), _webPass.c_str())) {
    server.requestAuthentication();
    return false;
  }
  return true;
}

// ---- favicon handlers ----
void handleFavicon() {
  const char *svg =
    "<?xml version='1.0' encoding='UTF-8' standalone='no'?>"
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 64 64' width='64' height='64'>"
    "<rect x='6' y='10' width='52' height='44' rx='6' fill='#1d1d1d' stroke='#4fc3f7' stroke-width='2'/>"
    "<circle cx='20' cy='24' r='3' fill='#4fc3f7'/>"
    "<circle cx='32' cy='24' r='3' fill='#4fc3f7'/>"
    "<circle cx='44' cy='24' r='3' fill='#4fc3f7'/>"
    "<rect x='18' y='34' width='28' height='6' rx='2' fill='#4fc3f7' opacity='0.12'/>"
    "</svg>";
  server.sendHeader("Cache-Control", "public, max-age=86400");
  server.send(200, "image/svg+xml", svg);
}

void handleFaviconIco() {
  server.sendHeader("Location", "/favicon.svg");
  server.send(302, "text/plain", "");
}

// ---- Root/status handlers ----
void handleRoot() {
  if (!spiffsMounted) {
    server.send(500, "text/plain", "SPIFFS not mounted");
    return;
  }
  File f = SPIFFS.open("/index.html", FILE_READ);
  if (!f) {
    server.send(404, "text/plain", "index.html not found");
    return;
  }
  server.streamFile(f, "text/html");
  f.close();
}

void handleStatus() {
  DynamicJsonDocument doc(256);
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = formatTime(millis());
  doc["temp"] = temperatureRead();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// ---- Logs (single authoritative handler; latest on top) ----
void handleLogs() {
  if (!checkAuth()) return;
  if (!spiffsMounted) {
    server.send(500, "text/plain", "SPIFFS not mounted");
    return;
  }

  File f = SPIFFS.open("/ota.log", FILE_READ);
  if (!f) {
    server.send(200, "text/plain", "Log empty");
    return;
  }

  // read file to String (ok for typical small logs)
  String content = f.readString();
  f.close();

  // split lines and reverse order
  std::vector<String> lines;
  int start = 0;
  while (true) {
    int idx = content.indexOf('\n', start);
    if (idx == -1) break;
    lines.push_back(content.substring(start, idx));
    start = idx + 1;
  }

  String out = "<html><head><meta charset='utf-8'><title>Logs</title>"
               "<style>body{background:#111;color:#eee;font-family:sans-serif;padding:20px;}pre{white-space:pre-wrap;background:#1d1d1d;padding:15px;border-radius:10px;}</style>"
               "</head><body><h2>OTA Logs</h2><pre>";

  for (int i = (int)lines.size() - 1; i >= 0; i--) {
    out += lines[i];
    out += "\n";
  }
  out += "</pre><p><a href='/'>Back</a></p></body></html>";

  server.send(200, "text/html; charset=utf-8", out);
}

// ---- Clear logs / restart ----
void handleClearLogs() {
  if (!checkAuth()) return;
  if (spiffsMounted) SPIFFS.remove("/ota.log");
  logOTA("Logs cleared by web");
  server.sendHeader("Location", "/logs");
  server.send(302, "text/plain", "");
}

void handleRestart() {
  if (!checkAuth()) return;
  logOTA("Restart requested by web");
  server.send(200, "text/plain", "Rebooting...");
  delay(250);
  ESP.restart();
}

// ---- Temp history endpoint (uses temperature_history.c) ----
void handleTempHistory() {
  String json = getTemperatureHistoryJson();
  server.send(200, "application/json", json);
}

// ---- MQTT status endpoint (if mqtt_client defines mqtt_last_json) ----
extern String mqtt_last_json;
void handleMqttStatus() {
  if (mqtt_last_json.length() == 0) server.send(200, "application/json", "{\"status\":\"no data\"}");
  else server.send(200, "application/json", mqtt_last_json);
}

// ---- MQTT publish endpoint (protected) ----
void handleMqttCmd() {
  if (!checkAuth()) return;
  String topic = server.arg("topic");
  String payload = server.arg("payload");
  extern void mqtt_publish(const String&, const String&);
  mqtt_publish(topic, payload);
  server.send(200, "text/plain", "OK");
}

// ---- OTA begin: mount SPIFFS once, init ArduinoOTA + ElegantOTA + routes ----
void OTA_begin(const char* hostname, uint16_t otaPort, const char* web_user, const char* web_pass) {
  _hostname = hostname ? String(hostname) : String("esp32");
  _webUser = web_user ? String(web_user) : String();
  _webPass = web_pass ? String(web_pass) : String();
  _otaPort = otaPort;

  // mount SPIFFS once
  if (!spiffsMounted) {
    spiffsMounted = SPIFFS.begin(true);
    if (spiffsMounted) {
      logOTA("SPIFFS mounted");
    } else {
      Serial.println("SPIFFS mount failed");
    }
  }

  // ArduinoOTA (if desired) — configure but don't call begin if not needed
  ArduinoOTA.setHostname(_hostname.c_str());
  ArduinoOTA.setPort(_otaPort);
  if (_webPass.length() > 0) ArduinoOTA.setPassword(_webPass.c_str());
  ArduinoOTA.onStart([](){ logOTA("🔄 OTA start"); });
  ArduinoOTA.onEnd([](){ logOTA("✅ OTA complete"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t){
    if (t) Serial.printf("[OTA] %d%%\r", (int)(100.0 * p / t));
  });
  ArduinoOTA.onError([](ota_error_t e){ logOTA("OTA error: " + String(e)); });
  ArduinoOTA.begin();

  otaRunning = true;
  logOTA("ArduinoOTA started on port " + String(_otaPort));

  // register routes (single, clear set)
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/favicon.svg", handleFavicon);
  server.on("/favicon.ico", handleFaviconIco);
  server.on("/logs", handleLogs);
  server.on("/clearlogs", handleClearLogs);
  server.on("/clear_logs", handleClearLogs); // backwards compatibility
  server.on("/restart", handleRestart);
  server.on("/temp_history.json", handleTempHistory);
  server.on("/mqtt_status", handleMqttStatus);
  server.on("/mqtt_cmd", HTTP_POST, handleMqttCmd);

  // ElegantOTA classic supports optional auth
  if (_webUser.length() > 0) {
    ElegantOTA.setAuth(_webUser.c_str(), _webPass.c_str());
  }
  ElegantOTA.begin(&server);

  server.begin();
  logOTA("ElegantOTA web server started on port 80");
}

// ---- main loop helpers ----
void OTA_handle() {
  if (otaRunning) {
    ArduinoOTA.handle();
    server.handleClient();
  }
}

// ---- safe watchdog (non-blocking, no self-connect) ----
void OTA_watchdog() {
  if (millis() - lastCheck < 15000) return;
  lastCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    logOTA("⚠️ Wi-Fi lost");
    otaRunning = false;
    return;
  }

  // simple: if ota not running, re-init it (no blocking)
  if (!otaRunning) {
    logOTA("⚠️ OTA not running, reinitializing");
    OTA_begin(_hostname.c_str(), _otaPort, _webUser.c_str(), _webPass.c_str());
  }
}
