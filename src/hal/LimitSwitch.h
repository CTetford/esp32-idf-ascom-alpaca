#pragma once
#ifndef LIMIT_SWITCH_H
#define LIMIT_SWITCH_H

#include "driver/gpio.h"
#include "esp_err.h"

namespace HAL {

class LimitSwitch {
public:
    LimitSwitch();
    ~LimitSwitch();

    /**
     * @brief Initializes the limit switch.
     * @param pin GPIO pin connected to the limit switch.
     * @param active_low True if the switch signal is LOW when active, false if HIGH when active.
     * @param pull_up Enable internal pull-up resistor if active_low is true and no external pull-up.
     * @param pull_down Enable internal pull-down resistor if active_low is false and no external pull-down.
     * @return esp_err_t ESP_OK on success, or an error code.
     */
    esp_err_t begin(gpio_num_t pin, bool active_low = true, bool pull_up = true, bool pull_down = false);

    /**
     * @brief Checks if the limit switch is currently active (e.g., pressed).
     * @return True if the switch is active, false otherwise.
     */
    bool isActive();

    /**
     * @brief Alias for isActive(), often used for home switches.
     * @return True if the switch is active, false otherwise.
     */
    bool isAtHome(); // Convenience alias for isActive()

private:
    gpio_num_t _pin;
    bool _active_low;
    bool _initialized;
};

} // namespace HAL

#endif // LIMIT_SWITCH_H
