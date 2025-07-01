#pragma once
#ifndef ENCODER_H
#define ENCODER_H

#include "driver/pcnt.h"
#include "esp_err.h"

namespace HAL {

class Encoder {
public:
    Encoder();
    ~Encoder();

    /**
     * @brief Initializes the Quadrature Encoder using PCNT peripheral.
     * @param pin_a GPIO pin for Channel A of the encoder.
     * @param pin_b GPIO pin for Channel B of the encoder.
     * @param pcnt_unit PCNT unit to use (PCNT_UNIT_0 to PCNT_UNIT_7).
     * @param filter_val Value for glitch filter (0-1023), 0 to disable.
     *                     1023 means 1023 * APB_CLK cycles.
     * @param max_val Maximum pulse count before reset (e.g. for rollover, or use PCNT_H_LIM_VAL).
     * @param min_val Minimum pulse count (e.g. for rollover, or use PCNT_L_LIM_VAL).
     * @return esp_err_t ESP_OK on success, or an error code.
     */
    esp_err_t begin(gpio_num_t pin_a, gpio_num_t pin_b,
                    pcnt_unit_t pcnt_unit = PCNT_UNIT_0,
                    uint16_t filter_val = 100 // Filter value ~12.5us with 80MHz APB clock
                    // PCNT_H_LIM_VAL (e.g. 32767) and PCNT_L_LIM_VAL (e.g. -32768) are used for overflow.
                    );

    /**
     * @brief Gets the current accumulated 32-bit pulse count from the encoder.
     * @param[out] count Pointer to store the 32-bit pulse count.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t getPosition(int32_t* count);


    /**
     * @brief Resets the encoder's pulse count (both hardware and accumulated 32-bit) to zero.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t resetPosition();

    /**
     * @brief Pauses the PCNT unit.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t pause();

    /**
     * @brief Resumes the PCNT unit.
     * @return esp_err_t ESP_OK on success.
     */
    esp_err_t resume();


private:
    pcnt_unit_t _pcnt_unit;
    bool _initialized;

    volatile int32_t _software_overflow_count; // Stores net overflows of the 16-bit hardware counter
    portMUX_TYPE _isr_mux; // Mutex for protecting access to _software_overflow_count

    static void IRAM_ATTR pcnt_intr_handler(void *arg);
};

} // namespace HAL

#endif // ENCODER_H
