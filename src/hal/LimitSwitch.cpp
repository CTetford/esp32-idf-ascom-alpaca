#include "LimitSwitch.h"
#include "esp_log.h"

static const char *TAG = "HAL_LimitSwitch";

namespace HAL {

LimitSwitch::LimitSwitch() :
    _pin(GPIO_NUM_NC),
    _active_low(true),
    _initialized(false) {
}

LimitSwitch::~LimitSwitch() {
    if (_initialized) {
        // gpio_reset_pin(_pin); // Optional: reset pin to default state if needed
        ESP_LOGD(TAG, "LimitSwitch on pin %d resources implicitly freed.", _pin);
    }
}

esp_err_t LimitSwitch::begin(gpio_num_t pin, bool active_low, bool pull_up, bool pull_down) {
    _pin = pin;
    _active_low = active_low;

    if (_pin == GPIO_NUM_NC) {
        ESP_LOGE(TAG, "Invalid GPIO pin number for limit switch.");
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Initializing limit switch: Pin=%d, ActiveLow=%s, PullUp=%s, PullDown=%s",
             _pin,
             _active_low ? "true" : "false",
             pull_up ? "true" : "false",
             pull_down ? "true" : "false");

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << _pin),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = pull_up ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = pull_down ? GPIO_PULLDOWN_ENABLE : GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE // No interrupt by default, can be added if needed
    };

    esp_err_t ret = gpio_config(&io_conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure GPIO pin %d for limit switch: %s", _pin, esp_err_to_name(ret));
        return ret;
    }

    _initialized = true;
    ESP_LOGI(TAG, "Limit switch on pin %d initialized successfully.", _pin);
    return ESP_OK;
}

bool LimitSwitch::isActive() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Limit switch on pin %d not initialized.", _pin);
        return false; // Or handle error appropriately
    }

    int level = gpio_get_level(_pin);
    return _active_low ? (level == 0) : (level == 1);
}

bool LimitSwitch::isAtHome() {
    return isActive();
}

} // namespace HAL
