#include <WiFi.h>
#include "ota_server.h"
#include "mqtt_client.h"
#include "temperature_history.h"
#include "driver/adc.h"
extern "C" uint8_t temprature_sens_read();

const char* ssid = "kasimovo";
const char* pass = "53354591297";

const char* web_user    = "kirill";
const char* web_pass    = "esp5335"; // <- поменяй на свой
const char* clientName  = "esp_main";
float currentTemperature = 0;

IPAddress local_IP(192,168,76,105);
IPAddress gateway(192,168,76,1);
IPAddress subnet(255,255,255,0);
IPAddress dns(192,168,76,1);


float readCpuTemp() {
  return (temprature_sens_read() - 32) / 1.8;
}

void tempHistoryTask(void *pvParameters) {
  for (;;) {
    currentTemperature = readCpuTemp();
    float temp = currentTemperature; // Берём последнее измерение
    addTemperatureToHistory(temp);
    vTaskDelay(pdMS_TO_TICKS(10000)); // каждые 10 секунд
  }
}
void setup() {
  Serial.begin(115200);
  WiFi.mode(WIFI_STA);
  WiFi.persistent(false);        // не пишет в flash
  WiFi.setSleep(false);          // выключает модем sleep (важно!)
  WiFi.config(local_IP, gateway, subnet, dns);
  WiFi.setHostname(clientName);  // обязательно: до begin()
  WiFi.disconnect(true, true);   // чистый старт стека
  delay(200);
  WiFi.begin(ssid, pass);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.printf("\nConnected! IP: %s\n", WiFi.localIP().toString().c_str());
  xTaskCreate(tempHistoryTask, "TempHistory", 4096, NULL, 1, NULL);
  configTime(0, 0, "pool.ntp.org", "time.google.com", "time1.google.com");
  Serial.print("Waiting for NTP");
  for (int i = 0; i < 30; i++) {
      time_t now = time(nullptr);
      if (now > 1700000000) {  // 2023+
          Serial.println("\nNTP synchronized");
          break;
      }
      Serial.print(".");
      delay(300);
  }
  mqtt_init("192.168.76.50", 1883, "openhabian", "Mqtt5335");
  OTA_begin("ESP32-Main", 3232, web_user, web_pass);
}

void loop() {
  OTA_handle();
  OTA_watchdog();
  mqtt_loop();
  delay(10);
}
