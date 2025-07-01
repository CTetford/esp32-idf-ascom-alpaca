#pragma once

// For AlpacaDomeImpl tests, we primarily mock DomeController and EspNowManager.
// Direct ESP-IDF mocks might only be needed if AlpacaDomeImpl itself used them,
// or for esp_timer_get_time.

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
// static uint64_t mock_time_us_alpaca = 0;
// inline uint64_t esp_timer_get_time() { return mock_time_us_alpaca; }
// inline void advance_mock_time_ms_alpaca(uint32_t ms) { mock_time_us_alpaca += (uint64_t)ms * 1000; }
#endif
*/

// Forward declarations for mock classes if they are defined in the .cpp
// class MockDomeController;
// class MockEspNowManager;
