#pragma once

// For LimitSwitch tests, only GPIO mocks are primarily needed.

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
