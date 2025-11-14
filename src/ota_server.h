#pragma once
#include <Arduino.h>

void OTA_begin(const char* hostname = "esp32-device", uint16_t otaPort = 3232,
               const char* web_user = nullptr, const char* web_pass = nullptr);
void OTA_handle();
void OTA_watchdog();
