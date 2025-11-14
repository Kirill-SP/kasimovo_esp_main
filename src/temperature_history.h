#pragma once
#include <Arduino.h>

void addTemperatureToHistory(float value);
String getTemperatureHistoryJson();
