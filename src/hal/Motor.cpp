#include "Motor.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "../pinmap.h" // To get actual pin numbers if not passed, though prefer passing them.
                       // For now, assume pins are passed in `begin`.

static const char *TAG = "HAL_Motor";

namespace HAL {

Motor::Motor() :
    _pwm_unit(MCPWM_UNIT_0),
    _pwm_timer(MCPWM_TIMER_0),
    _en_pin(GPIO_NUM_NC),
    _pwm_a_pin(GPIO_NUM_NC),
    _pwm_b_pin(GPIO_NUM_NC),
    _initialized(false),
    _enabled(false) {
}

Motor::~Motor() {
    if (_initialized) {
        // Consider stopping MCPWM unit if it's no longer needed by any motor
        // mcpwm_stop(_pwm_unit, _pwm_timer);
        // For now, assume it might be shared or managed elsewhere if multiple motors
    }
}

esp_err_t Motor::begin(gpio_num_t pwm_a_pin, gpio_num_t pwm_b_pin, gpio_num_t en_pin,
                        mcpwm_unit_t pwm_unit, mcpwm_timer_t pwm_timer) {
    _pwm_a_pin = pwm_a_pin; // e.g., MOTOR_R_PWM_PIN, for forward motion
    _pwm_b_pin = pwm_b_pin; // e.g., MOTOR_L_PWM_PIN, for reverse motion
    _en_pin = en_pin;       // e.g., MOTOR_EN_PIN
    _pwm_unit = pwm_unit;
    _pwm_timer = pwm_timer;

    ESP_LOGI(TAG, "Initializing motor: PWM_A(RPWM)=%d, PWM_B(LPWM)=%d, EN=%d on Unit %d, Timer %d",
             _pwm_a_pin, _pwm_b_pin, _en_pin, _pwm_unit, _pwm_timer);

    esp_err_t ret = ESP_OK;

    // Configure Enable Pin
    if (_en_pin != GPIO_NUM_NC) {
        gpio_config_t en_gpio_config = {
            .pin_bit_mask = (1ULL << _en_pin),
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE
        };
        ret = gpio_config(&en_gpio_config);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to configure EN pin %d: %s", _en_pin, esp_err_to_name(ret));
            return ret;
        }
        disable(); // Start with motor disabled
    }

    // Configure MCPWM
    // Pin _pwm_a_pin will be Operator A, Pin _pwm_b_pin will be Operator B
    // Determine the correct mcpwm_io_signals_t based on timer and operator
    mcpwm_io_signals_t io_signal_a, io_signal_b;
    if (_pwm_timer == MCPWM_TIMER_0) {
        io_signal_a = MCPWM0A;
        io_signal_b = MCPWM0B;
    } else if (_pwm_timer == MCPWM_TIMER_1) {
        io_signal_a = MCPWM1A;
        io_signal_b = MCPWM1B;
    } else if (_pwm_timer == MCPWM_TIMER_2) {
        io_signal_a = MCPWM2A;
        io_signal_b = MCPWM2B;
    } else {
        ESP_LOGE(TAG, "Unsupported MCPWM timer: %d", _pwm_timer);
        return ESP_ERR_INVALID_ARG;
    }

    ret = mcpwm_gpio_init(_pwm_unit, io_signal_a, _pwm_a_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init MCPWM signal %d (OpA) on pin %d: %s", io_signal_a, _pwm_a_pin, esp_err_to_name(ret));
        return ret;
    }
    ret = mcpwm_gpio_init(_pwm_unit, io_signal_b, _pwm_b_pin);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init MCPWM signal %d (OpB) on pin %d: %s", io_signal_b, _pwm_b_pin, esp_err_to_name(ret));
        // Consider de-init OPR_A if OPR_B fails
        return ret;
    }

    mcpwm_config_t pwm_config = {
        .frequency = 10000, // 10 kHz PWM frequency - adjust as needed for motor/driver
        .cmpr_a = 0,        // duty cycle for A (0%)
        .cmpr_b = 0,        // duty cycle for B (0%)
        .duty_mode = MCPWM_DUTY_MODE_0, // Active high duty cycle
        .counter_mode = MCPWM_UP_COUNTER
    };
    ret = mcpwm_init(_pwm_unit, _pwm_timer, &pwm_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to init MCPWM unit %d timer %d: %s", _pwm_unit, _pwm_timer, esp_err_to_name(ret));
        return ret;
    }

    // Set both operators to do nothing initially (output low)
    // This means the motor is "braked" if L_EN and R_EN are active.
    mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_A);
    mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_B);

    _initialized = true;
    ESP_LOGI(TAG, "Motor initialized successfully.");
    return ESP_OK;
}

void Motor::enable() {
    if (_en_pin != GPIO_NUM_NC) {
        gpio_set_level(_en_pin, 1); // Assuming active high enable
        ESP_LOGD(TAG, "Motor enabled (EN pin HIGH)");
    }
    _enabled = true;
}

void Motor::disable() {
    if (_en_pin != GPIO_NUM_NC) {
        gpio_set_level(_en_pin, 0); // Assuming active high enable
        ESP_LOGD(TAG, "Motor disabled (EN pin LOW)");
    }
    _enabled = false;
}

esp_err_t Motor::setSpeed(float speed_percent) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Motor not initialized.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!_enabled && _en_pin != GPIO_NUM_NC) {
        // ESP_LOGW(TAG, "Motor is disabled, speed command ignored. Call enable() first.");
        // Do not return error, just don't move if disabled. Or, auto-enable? For now, require explicit enable.
        // For safety, ensure motor is stopped if it's disabled.
        mcpwm_set_duty(_pwm_unit, _pwm_timer, MCPWM_OPR_A, 0);
        mcpwm_set_duty(_pwm_unit, _pwm_timer, MCPWM_OPR_B, 0);
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_A); // Ensure output is low
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_B); // Ensure output is low
        return ESP_OK;
    }


    float duty_cycle = fabsf(speed_percent);
    if (duty_cycle > 100.0f) duty_cycle = 100.0f;
    if (duty_cycle < 0.0f) duty_cycle = 0.0f; // Should be handled by fabsf already

    ESP_LOGD(TAG, "Set speed: %.2f%%", speed_percent);

    if (speed_percent > 0) { // Forward
        // PWM on _pwm_a_pin (RPWM), _pwm_b_pin (LPWM) LOW
        mcpwm_set_duty(_pwm_unit, _pwm_timer, MCPWM_OPR_A, duty_cycle);
        mcpwm_set_duty_type(_pwm_unit, _pwm_timer, MCPWM_OPR_A, MCPWM_DUTY_MODE_0); // Ensure active high
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_B); // Set LPWM to LOW
        ESP_LOGV(TAG, "Forward: RPWM set to %.2f%%, LPWM set to LOW", duty_cycle);
    } else if (speed_percent < 0) { // Reverse
        // PWM on _pwm_b_pin (LPWM), _pwm_a_pin (RPWM) LOW
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_A); // Set RPWM to LOW
        mcpwm_set_duty(_pwm_unit, _pwm_timer, MCPWM_OPR_B, duty_cycle);
        mcpwm_set_duty_type(_pwm_unit, _pwm_timer, MCPWM_OPR_B, MCPWM_DUTY_MODE_0); // Ensure active high
        ESP_LOGV(TAG, "Reverse: LPWM set to %.2f%%, RPWM set to LOW", duty_cycle);
    } else { // Stop (Brake)
        // Both RPWM and LPWM LOW
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_A);
        mcpwm_set_signal_low(_pwm_unit, _pwm_timer, MCPWM_OPR_B);
        ESP_LOGV(TAG, "Stop: RPWM and LPWM set to LOW (Brake)");
        // Alternative: Coast by disabling EN pin, but this class uses explicit enable/disable
        // If you want coast on speed 0, you'd call disable() or modify EN pin logic here.
    }
    return ESP_OK;
}

esp_err_t Motor::stop() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Motor not initialized for stop command.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Motor stop requested.");
    return setSpeed(0); // Sets both PWMs to 0, effectively braking.
                        // If coast is desired, call disable() or implement specific coast logic.
}

} // namespace HAL
