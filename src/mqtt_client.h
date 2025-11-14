#pragma once
#include <Arduino.h>

void mqtt_init(const char* host, uint16_t port, const char* user, const char* pass);
void mqtt_loop();
void mqtt_publish(const String& topic, const String& payload);

extern String mqtt_last_json;
