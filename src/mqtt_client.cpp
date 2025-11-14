#include "mqtt_client.h"
#include <AsyncMqttClient.h>
#include <ArduinoJson.h>

AsyncMqttClient mqtt;
String mqtt_last_json;

static String _host;
static uint16_t _port;
static String _user;
static String _pass;

void mqtt_publish(const String& topic, const String& payload) {
  mqtt.publish(topic.c_str(), 0, false, payload.c_str());
}

// -----------------------------------------------------------
// MESSAGE HANDLER
// -----------------------------------------------------------
void onMessage(char* topic, char* payload,
               AsyncMqttClientMessageProperties properties,
               size_t len, size_t index, size_t total) 
{
  String msg = String(payload).substring(0, len);

  // ArduinoJson 7: используем JsonDocument
  JsonDocument doc;
  doc["topic"] = topic;
  doc["payload"] = msg;

  mqtt_last_json = "";
  serializeJson(doc, mqtt_last_json);

  extern void logOTA(const String &);
  logOTA("MQTT: " + String(topic) + " = " + msg);
}

// -----------------------------------------------------------
// CONNECTED
// -----------------------------------------------------------
void onConnect(bool sessionPresent) {
  mqtt.subscribe("cmd/esp32/#", 0);
  extern void logOTA(const String &);
  logOTA("MQTT connected");
}

// -----------------------------------------------------------
// INIT
// -----------------------------------------------------------
void mqtt_init(const char* host, uint16_t port, const char* user, const char* pass) {
  _host = host;
  _port = port;
  _user = user ? user : "";
  _pass = pass ? pass : "";

  mqtt.onConnect(onConnect);
  mqtt.onMessage(onMessage);

  mqtt.setServer(_host.c_str(), _port);
  if (_user.length()) {
    mqtt.setCredentials(_user.c_str(), _pass.c_str());
  }

  mqtt.connect();
}

void mqtt_loop() {
  // AsyncMqttClient: ничего не нужно
}
