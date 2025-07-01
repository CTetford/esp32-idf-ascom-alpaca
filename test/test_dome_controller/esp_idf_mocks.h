#pragma once

// This file can be used for mocks specific to DomeController tests,
// or for includes of more general ESP-IDF/FreeRTOS mocks if needed.

// For DomeController tests, we will primarily rely on mocking the HAL components
// (Motor, Encoder, LimitSwitch) rather than raw ESP-IDF functions directly.
// However, if DomeController itself used FreeRTOS primitives not encapsulated
// by HAL, those would be mocked here or included.

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

// Mock esp_timer_get_time if needed for precise timing control in tests
// static uint64_t mock_time_us = 0;
// inline uint64_t esp_timer_get_time() { return mock_time_us; }
// inline void advance_mock_time_ms(uint32_t ms) { mock_time_us += (uint64_t)ms * 1000; }

#endif
*/
