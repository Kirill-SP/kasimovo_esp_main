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

void logOTA(const String &msg) {
  String entry = "[" + getTimeString() + "] " + msg;
  Serial.println(entry);

  if (!SPIFFS.begin(true)) return;
  File f = SPIFFS.open("/ota.log", FILE_APPEND);
  if (f) {
    f.println(entry);
    f.close();
  }
}


// --- Authentication helper (HTTP Basic) ---
bool checkAuth() {
  if (_webUser.length() == 0) return true; // no auth configured
  if (!server.hasHeader("Authorization")) {
    server.requestAuthentication();
    return false;
  }
  return server.authenticate(_webUser.c_str(), _webPass.c_str());
}

// --- favicon SVG handler (ESP32 outline minimal SVG) ---
void handleFavicon() {
  // Small inline SVG with ESP32-like outline
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

// --- Serve favicon.ico redirect to svg (some browsers request ico) ---
void handleFaviconIco() {
  server.sendHeader("Location", "/favicon.svg");
  server.send(302, "text/plain", "");
}

extern String mqtt_last_json;  // объявим глобально в mqtt_client.cpp

void handleMqttStatus() {
  if (mqtt_last_json.length() == 0) {
    server.send(200, "application/json", "{\"status\":\"no data\"}");
  } else {
    server.send(200, "application/json", mqtt_last_json);
  }
}


// --- Root and status handlers ---
void handleRoot() {
  if (!SPIFFS.begin(true)) {
    server.send(500, "text/plain", "SPIFFS error");
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
  JsonDocument doc;
  doc["ip"] = WiFi.localIP().toString();
  doc["rssi"] = WiFi.RSSI();
  doc["uptime"] = formatTime(millis());
  doc["temp"] = temperatureRead();
  String json;
  serializeJson(doc, json);
  server.send(200, "application/json", json);
}

// --- Logs pages ---
void handleViewLogs() {
  if (!checkAuth()) return; // require auth
  if (!SPIFFS.begin(true)) {
    server.send(500, "text/plain", "SPIFFS error");
    return;
  }
  File f = SPIFFS.open("/ota.log", FILE_READ);
  String html = "<html><head><meta charset='UTF-8'><title>Logs</title>"
                "<style>body{background:#111;color:#eee;font-family:sans-serif;padding:20px;}pre{white-space:pre-wrap;background:#1d1d1d;padding:15px;border-radius:10px;}</style>"
                "</head><body><h2>OTA Logs</h2><pre>";
  if (f) {
    while (f.available()) html += (char)f.read();
    f.close();
  } else {
    html += "Log file empty or missing.";
  }
  html += "</pre><p><a href='/'>Back</a></p></body></html>";
  server.send(200, "text/html", html);
}

// --- clear logs (protected) ---
void handleClearLogs() {
  if (!checkAuth()) return;
  if (!SPIFFS.begin(true)) {
    server.send(500, "text/plain", "SPIFFS error");
    return;
  }
  SPIFFS.remove("/ota.log");
  logOTA("Logs cleared by web");
  server.send(200, "text/plain", "Logs cleared");
}

// --- restart (protected) ---
void handleRestart() {
  if (!checkAuth()) return;
  server.send(200, "text/plain", "ESP32 restarting...");
  logOTA("Restart requested by web");
  delay(200);
  ESP.restart();
}

void OTA_begin(const char* hostname, uint16_t otaPort, const char* web_user, const char* web_pass) {
  _hostname = hostname;
  _otaPort = otaPort;
  _webUser = web_user ? String(web_user) : String();
  _webPass = web_pass ? String(web_pass) : String();

  logOTA("=== OTA Init ===");
  if (!SPIFFS.begin(true)) {
    logOTA("[SPIFFS] mount failed");
  }

  ArduinoOTA.setHostname(_hostname.c_str());
  ArduinoOTA.setPort(_otaPort);

  if (_webPass.length() > 0) {
    // protect ArduinoOTA with password (for espota auth)
    ArduinoOTA.setPassword(_webPass.c_str());
  }

  ArduinoOTA.onStart([]() { logOTA("🔄 OTA start"); });
  ArduinoOTA.onEnd([]() { logOTA("✅ OTA complete"); });
  ArduinoOTA.onProgress([](unsigned int p, unsigned int t) {
    // p = bytes, t = total
    // print percent
    if (t) {
      int pct = (int)(100.0 * p / t);
      Serial.printf("[OTA] %d%%\r", pct);
    }
  });
  ArduinoOTA.onError([](ota_error_t e) { logOTA("❌ OTA error: " + String(e)); });

  ArduinoOTA.begin();
  otaRunning = true;
  logOTA("ArduinoOTA started on port " + String(_otaPort));

  // Register routes
  server.on("/", handleRoot);
  server.on("/status", handleStatus);
  server.on("/favicon.svg", handleFavicon);
  server.on("/favicon.ico", handleFaviconIco);
  server.on("/clearlogs", handleClearLogs);
  server.on("/restart", handleRestart);
  server.on("/mqtt_status", handleMqttStatus);
  server.on("/mqtt_cmd", HTTP_POST, []() {
    if (!checkAuth()) return;

    String topic = server.arg("topic");
    String payload = server.arg("payload");

    extern void mqtt_publish(const String&, const String&);
    mqtt_publish(topic, payload);

    server.send(200, "text/plain", "OK");
  });
  server.on("/temp_history.json", HTTP_GET, []() {
    String json = getTemperatureHistoryJson();
    server.send(200, "application/json", json);
  });
// LOGS
  server.on("/logs", []() {
    if (!SPIFFS.begin(true)) {
      server.send(500, "text/plain", "SPIFFS ERROR");
      return;
    }

    File f = SPIFFS.open("/ota.log", "r");
    if (!f) {
      server.send(200, "text/plain", "Log empty");
      return;
    }

    // читаем весь файл
    String all = f.readString();
    f.close();

    // разбиваем на строки
    std::vector<String> lines;
    int start = 0;
    while (true) {
      int idx = all.indexOf('\n', start);
      if (idx == -1) break;
      lines.push_back(all.substring(start, idx));
      start = idx + 1;
    }

    // собираем строки в обратном порядке
    String out;
    for (int i = lines.size() - 1; i >= 0; i--) {
      out += lines[i] + "\n";
    }

    server.send(200, "text/plain", out);
  });




  // ElegantOTA (with basic auth if provided)
  if (_webPass.length() > 0) {
    // ElegantOTA supports basic auth via begin(server, username, password)
    ElegantOTA.begin(&server, _webUser.c_str(), _webPass.c_str());
  } else {
    ElegantOTA.begin(&server);
  }
  server.begin();
  logOTA("ElegantOTA web server started on port 80");
}

void OTA_handle() {
  if (otaRunning) {
    ArduinoOTA.handle();
    server.handleClient();
  }
}

void OTA_watchdog() {
  if (millis() - lastCheck < 15000) return;
  lastCheck = millis();

  if (WiFi.status() != WL_CONNECTED) {
    otaRunning = false;
    logOTA("⚠️ Wi-Fi lost");
    return;
  }

  WiFiClient test;
  if (!test.connect(WiFi.localIP(), _otaPort, 200)) {
    logOTA("⚠️ OTA port closed, restarting OTA...");
    OTA_begin(_hostname.c_str(), _otaPort, _webUser.c_str(), _webPass.c_str());
  }
  test.stop();
}
