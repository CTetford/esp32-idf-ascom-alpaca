#pragma once
#ifndef MOTOR_H
#define MOTOR_H

#include "driver/mcpwm.h"
#include "soc/mcpwm_periph.h" // For MCPWM_GEN_FORCE_LOW / HIGH if needed

namespace HAL {

class Motor {
public:
    Motor();
    ~Motor();

    /**
     * @brief Initializes the motor controller.
     * @param pwm_a_pin GPIO pin for PWM A (e.g., RPWM on BTS7960).
     * @param pwm_b_pin GPIO pin for PWM B (e.g., LPWM on BTS7960).
     * @param en_pin GPIO pin for Enable (e.g., common EN for L_EN/R_EN on BTS7960, or specific R_EN/L_EN).
     *                 If using dual enable pins, this setup might need adjustment or en_pin_b.
     * @param pwm_unit MCPWM unit to use (MCPWM_UNIT_0 or MCPWM_UNIT_1).
     * @param pwm_timer MCPWM timer to use (MCPWM_TIMER_0, MCPWM_TIMER_1, MCPWM_TIMER_2).
     * @return esp_err_t ESP_OK on success, or an error code.
     */
    esp_err_t begin(gpio_num_t pwm_a_pin, gpio_num_t pwm_b_pin, gpio_num_t en_pin,
                    mcpwm_unit_t pwm_unit = MCPWM_UNIT_0, mcpwm_timer_t pwm_timer = MCPWM_TIMER_0);

    /**
     * @brief Sets the speed and direction of the motor.
     * @param speed Speed from -100 (full reverse) to 100 (full forward). 0 for stop.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t setSpeed(float speed_percent);

    /**
     * @brief Stops the motor (coast).
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t stop();

    /**
     * @brief Enables the motor driver.
     */
    void enable();

    /**
     * @brief Disables the motor driver.
     */
    void disable();

private:
    mcpwm_unit_t _pwm_unit;
    mcpwm_timer_t _pwm_timer;
    gpio_num_t _en_pin;
    gpio_num_t _pwm_a_pin; // Connected to RPWM for forward, LPWM for reverse typically
    gpio_num_t _pwm_b_pin; // Connected to LPWM for forward, RPWM for reverse typically
    bool _initialized;
    bool _enabled;

    // Helper to configure BTS7960 style drive: one PWM for speed, direction by which pin (A or B) gets PWM.
    // For BTS7960:
    // Forward: PWM on RPWM (e.g. _pwm_a_pin), LPWM LOW
    // Reverse: PWM on LPWM (e.g. _pwm_b_pin), RPWM LOW
    // Brake:   RPWM LOW, LPWM LOW (or HIGH, HIGH - check datasheet)
    // We will use one pin as the primary PWM signal and the other as a simple GPIO for direction,
    // or use two MCPWM operators if more complex driving is needed.
    // For simplicity, this initial version will use one MCPWM operator for speed,
    // and set the other pin to LOW or HIGH.
    // A more robust BTS7960 implementation would use two MCPWM operators,
    // one for RPWM and one for LPWM, and control them independently.
    // This example will use one operator for RPWM and set LPWM to low for forward,
    // and one operator for LPWM and set RPWM to low for reverse.
    // This requires re-initializing the MCPWM operator for the other pin if switching.
    //
    // A simpler approach for BTS7960:
    // - R_EN and L_EN tied together and controlled by _en_pin (HIGH to enable).
    // - To go forward: R_PWM gets PWM signal, L_PWM is LOW.
    // - To go reverse: L_PWM gets PWM signal, R_PWM is LOW.
    // - To brake: R_PWM is LOW, L_PWM is LOW (or both HIGH for high-side braking).
    // This class will use MCPWM for one signal (e.g. R_PWM) and simple GPIO for the other (L_PWM).
    // This is not ideal as it doesn't allow easy switching of which pin gets PWM.
    //
    // Corrected approach for typical BTS7960:
    // - Use two MCPWM operators (e.g., MCPWM_OPR_A and MCPWM_OPR_B) on the same timer.
    // - _pwm_a_pin assigned to MCPWM_OPR_A (e.g., for RPWM)
    // - _pwm_b_pin assigned to MCPWM_OPR_B (e.g., for LPWM)
};

} // namespace HAL

#endif // MOTOR_H
