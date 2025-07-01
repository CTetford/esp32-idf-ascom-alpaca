#pragma once

// This file is intended to be a place for more complex mock setups,
// FakeFunctionFramework (FFF) declarations, or includes for CMock generated mocks.

// For now, it's empty as basic mocks are defined directly in the test .cpp file.

// Example of how you might disable logging for tests if not using a mock logger:
/*
#ifdef UNIT_TESTING // This define would be set by the build system for test builds
// Undefine ESP_LOGx macros if they cause linking issues or unwanted output during tests
#undef ESP_LOGI
#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGD
#undef ESP_LOGV

// Define them as no-ops or simple printf
#include <stdio.h>
#define ESP_LOGI(tag, format, ...) printf("I [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGE(tag, format, ...) printf("E [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGW(tag, format, ...) printf("W [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGD(tag, format, ...) printf("D [%s] " format "\n", tag, ##__VA_ARGS__)
#define ESP_LOGV(tag, format, ...) printf("V [%s] " format "\n", tag, ##__VA_ARGS__)

#endif
*/
