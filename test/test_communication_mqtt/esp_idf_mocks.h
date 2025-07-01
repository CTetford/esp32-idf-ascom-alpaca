#pragma once

// This file can be used for mocks specific to MqttClientWrapper tests,
// or for includes of more general ESP-IDF/FreeRTOS mocks if needed.

// We will need to mock esp_mqtt_client_... functions and esp_reboot.
// Also, DomeController methods if we don't mock the whole class.

// Example of how you might disable logging for tests:
/*
#ifdef UNIT_TESTING
#undef ESP_LOGI
#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGD
#undef ESP_LOGV

#include <stdio.h> // For printf
#define ESP_LOGI(tag, format, ...) printf("I [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) printf("E [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("W [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("D [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("V [%s] " format "\n", tag, ##__VA_ARGS__)
#endif
*/

// Forward declare a mock DomeController if needed by tests directly
namespace DomeControl {
    // Simplified mock or real class included by test_communication_mqtt.cpp
    // class DomeController;
}
