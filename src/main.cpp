#include <WiFi.h>
#include "ota_server.h"
#include "mqtt_client.h"
#include "temperature_history.h"

const char* ssid = "kasimovo";
const char* pass = "53354591297";

const char* web_user = "kirill";
const char* web_pass = "esp5335"; // <- поменяй на свой

float currentTemperature = 0;

void tempHistoryTask(void *pvParameters) {
  for (;;) {
    float temp = currentTemperature; // Берём последнее измерение
    addTemperatureToHistory(temp);
    vTaskDelay(pdMS_TO_TICKS(10000)); // каждые 10 секунд
  }
}
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  xTaskCreate(tempHistoryTask, "TempHistory", 4096, NULL, 1, NULL);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  mqtt_init("192.168.76.50", 1883, "openhabian", "Mqtt5335");
  OTA_begin("ESP32-Main", 3232, web_user, web_pass);
}

void loop() {
  OTA_handle();
  OTA_watchdog();
  mqtt_loop();
  delay(10);
}
