#include <Arduino.h>

#define TEMP_HISTORY_SIZE 360  // 360 точек = 1 час, шаг 10 сек

struct TempEntry {
  uint32_t timestamp;
  float temperature;
};

TempEntry tempHistory[TEMP_HISTORY_SIZE];
uint16_t tempWriteIndex = 0;

void addTemperatureToHistory(float value) {
  tempHistory[tempWriteIndex].timestamp = millis() / 1000;
  tempHistory[tempWriteIndex].temperature = value;
  tempWriteIndex = (tempWriteIndex + 1) % TEMP_HISTORY_SIZE;
}

String getTemperatureHistoryJson() {
  String json = "[";
  for (int i = 0; i < TEMP_HISTORY_SIZE; i++) {
    const TempEntry &e = tempHistory[(tempWriteIndex + i) % TEMP_HISTORY_SIZE];
    if (e.timestamp > 0) {
      json += "{\"t\":" + String(e.timestamp) +
              ",\"v\":" + String(e.temperature) + "},";
    }
  }
  if (json.endsWith(",")) json.remove(json.length() - 1);
  json += "]";
  return json;
}
