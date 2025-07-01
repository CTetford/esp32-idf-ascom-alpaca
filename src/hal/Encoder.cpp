#include "Encoder.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h" // For portMUX_TYPE, portENTER_CRITICAL etc.
#include "soc/pcnt_struct.h"   // For direct register access if needed (not used here yet)
#include "esp_rom_gpio.h"      // For esp_rom_gpio_pad_select_gpio if strict GPIO setup is needed

static const char *TAG = "HAL_Encoder";
// For ISR logging, use ESP_EARLY_LOGx if regular ESP_LOGx causes issues from ISR
// static const char *TAG_ISR = "HAL_Encoder_ISR";

// Define the range of the 16-bit hardware counter
#define PCNT_HW_COUNTER_RANGE (PCNT_H_LIM_VAL - PCNT_L_LIM_VAL + 1) // Should be 65536

namespace HAL {

// Static member initialization (if needed for an array of instances, not used here)
// Encoder* Encoder::isr_instances[PCNT_UNIT_MAX] = {nullptr};

Encoder::Encoder() :
    _pcnt_unit(PCNT_UNIT_MAX),
    _initialized(false),
    _software_overflow_count(0) {
    // Initialize the mutex (spinlock)
    vPortCPUInitializeMutex(&_isr_mux);
}

Encoder::~Encoder() {
    if (_initialized) {
        pcnt_isr_handler_remove(_pcnt_unit);
        // pcnt_isr_service_uninstall(); // Uninstall only if no other PCNT units are using it. This is tricky.
                                      // For now, assume it's uninstalled elsewhere or not at all if shared.
        ESP_LOGD(TAG, "Encoder on unit %d resources partially released (ISR handler removed).", _pcnt_unit);
    }
}

void IRAM_ATTR Encoder::pcnt_intr_handler(void *arg) {
    Encoder* self = static_cast<Encoder*>(arg);
    uint32_t intr_status;
    // pcnt_get_event_status is deprecated, use pcnt_unit_get_events
    pcnt_unit_get_events(self->_pcnt_unit, &intr_status);

    portENTER_CRITICAL_ISR(&self->_isr_mux);
    if (intr_status & PCNT_EVT_H_LIM) { // Hardware counter hit PCNT_H_LIM_VAL (e.g. 32767) and rolled over
        self->_software_overflow_count++;
        // ESP_EARLY_LOGD(TAG_ISR, "PCNT H_LIM! Overflow: %d", (int)self->_software_overflow_count);
    }
    if (intr_status & PCNT_EVT_L_LIM) { // Hardware counter hit PCNT_L_LIM_VAL (e.g. -32768) and rolled over
        self->_software_overflow_count--;
        // ESP_EARLY_LOGD(TAG_ISR, "PCNT L_LIM! Overflow: %d", (int)self->_software_overflow_count);
    }
    portEXIT_CRITICAL_ISR(&self->_isr_mux);

    // pcnt_event_clear is deprecated, use pcnt_unit_clear_events
    pcnt_unit_clear_events(self->_pcnt_unit, intr_status);
}


esp_err_t Encoder::begin(gpio_num_t pin_a, gpio_num_t pin_b,
                           pcnt_unit_t pcnt_unit,
                           uint16_t filter_val) {
    _pcnt_unit = pcnt_unit;
    _software_overflow_count = 0; // Reset on begin

    ESP_LOGI(TAG, "Initializing encoder: PinA=%d, PinB=%d on PCNT Unit %d for 32-bit accumulation", pin_a, pin_b, _pcnt_unit);

    pcnt_config_t pcnt_config = {
        .pulse_gpio_num = pin_a,
        .ctrl_gpio_num = pin_b,
        .lctrl_mode = PCNT_MODE_REVERSE,
        .hctrl_mode = PCNT_MODE_KEEP,
        .pos_mode = PCNT_COUNT_INC,
        .neg_mode = PCNT_COUNT_DEC,
        .counter_h_lim = PCNT_H_LIM_VAL, // Hardware counter high limit for overflow event
        .counter_l_lim = PCNT_L_LIM_VAL, // Hardware counter low limit for underflow event
        .unit = _pcnt_unit,
        .channel = PCNT_CHANNEL_0,
    };

    esp_err_t ret = pcnt_unit_config(&pcnt_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure PCNT Unit %d Channel 0: %s", _pcnt_unit, esp_err_to_name(ret));
        return ret;
    }

    // Configure Channel 1 for X4 encoding if desired (example, not enabled by default here)
    // pcnt_config.pulse_gpio_num = pin_b;
    // pcnt_config.ctrl_gpio_num = pin_a;
    // pcnt_config.channel = PCNT_CHANNEL_1;
    // pcnt_config.pos_mode = PCNT_COUNT_DEC;
    // pcnt_config.neg_mode = PCNT_COUNT_INC;
    // ret = pcnt_unit_config(&pcnt_config);
    // if (ret != ESP_OK) { ... }

    // Setup filter
    if (filter_val > 0) {
        // filter_val for deprecated pcnt_set_filter_value is APB_CLK cycles.
        // APB_CLK is typically 80MHz. So 100 cycles = 100 * 12.5ns = 1250ns.
        uint32_t filter_val_ns = filter_val * 12; // Approximate, 12.5ns per cycle at 80MHz. Use 12 for slight underestimate.
                                                 // Or more accurately: filter_val * (1000000000 / esp_clk_apb_freq())
                                                 // For simplicity, assuming filter_val is small enough that direct use is okay for deprecated.
                                                 // Or, if filter_val itself is meant to be the new `max_glitch_ns` value.
                                                 // The old `filter_val = 100` corresponds to ~1.25us.
        ESP_LOGI(TAG, "PCNT Unit %d filter enabled with value %u (deprecated, interpreted as cycles or similar magnitude for new API)", _pcnt_unit, filter_val);

        pcnt_glitch_filter_config_t filter_config = {
            .max_glitch_ns = (filter_val > 1023) ? 1023*12 : filter_val * 12, // Max for old API was 1023 cycles.
                                                                    // Using filter_val as if it were roughly in the microsecond range.
                                                                    // A value of 100 cycles -> 1250ns.
                                                                    // Let's assume filter_val from config is desired max_glitch_ns.
                                                                    // If filter_val = 100 (from default), then max_glitch_ns = 100ns. This is very short.
                                                                    // The old default of 100 cycles was 1250ns. So let's use that as a base.
                                                                    // If filter_val is the old "cycles" value:
                                                                    // uint32_t glitch_ns = filter_val * (1000000000ULL / esp_clk_apb_freq());
                                                                    // For typical 100 cycles -> 1250ns
        };
        // If filter_val = 100 (default in header), let's use it as a rough guide for a few microseconds.
        // A common value for max_glitch_ns might be a few 1000s of ns.
        // The default filter_val = 100 in header. Let's interpret this as 100 * 10 ns = 1000ns = 1us for the new API
        filter_config.max_glitch_ns = filter_val * 10;
        ESP_LOGI(TAG, "Setting glitch filter to max_glitch_ns = %lu", filter_config.max_glitch_ns);

        pcnt_unit_set_glitch_filter(pcnt_unit, &filter_config);
        pcnt_unit_glitch_filter_enable(pcnt_unit);
    } else {
        pcnt_unit_glitch_filter_disable(pcnt_unit);
    }

    // Enable events for H_LIM and L_LIM for overflow/underflow detection
    pcnt_unit_event_enable(_pcnt_unit, PCNT_UNIT_EVT_HIGH_LIM);
    pcnt_unit_event_enable(_pcnt_unit, PCNT_UNIT_EVT_LOW_LIM);

    // Initialize PCNT's counter, pause, clear, resume
    pcnt_counter_pause(_pcnt_unit);
    pcnt_counter_clear(_pcnt_unit);
    _software_overflow_count = 0; // Ensure overflow is also cleared

    // Install PCNT ISR service and add handler for this unit
    // ESP_INTR_FLAG_IRAM is good for ISRs. Level 1, 2, or 3 interrupt.
    // The 0 argument to pcnt_isr_service_install is for interrupt allocation flags.
    ret = pcnt_isr_service_install(0);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) { // ESP_ERR_INVALID_STATE means already installed
        ESP_LOGE(TAG, "Failed to install PCNT ISR service: %s", esp_err_to_name(ret));
        return ret;
    } else if (ret == ESP_ERR_INVALID_STATE) {
        ESP_LOGW(TAG, "PCNT ISR service already installed.");
    }

    ret = pcnt_isr_handler_add(_pcnt_unit, Encoder::pcnt_intr_handler, this);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to add PCNT ISR handler for unit %d: %s", _pcnt_unit, esp_err_to_name(ret));
        // pcnt_isr_service_uninstall(); // Clean up if add fails and we just installed it
        return ret;
    }

    pcnt_counter_resume(_pcnt_unit);

    _initialized = true;
    ESP_LOGI(TAG, "Encoder on PCNT Unit %d initialized successfully for 32-bit count.", _pcnt_unit);
    return ESP_OK;
}

esp_err_t Encoder::getPosition(int32_t* count) {
    if (!_initialized) {
        ESP_LOGE(TAG, "Encoder not initialized.");
        if(count) *count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    if (!count) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t hw_count;
    int32_t overflows_captured;

    portENTER_CRITICAL(&_isr_mux);
    overflows_captured = _software_overflow_count;
    esp_err_t ret = pcnt_get_counter_value(_pcnt_unit, &hw_count);
    portEXIT_CRITICAL(&_isr_mux);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get PCNT counter value for unit %d: %s", _pcnt_unit, esp_err_to_name(ret));
        *count = 0; // Or some error indicator
        return ret;
    }

    // Calculate the full 32-bit count.
    // PCNT_HW_COUNTER_RANGE is 65536.
    *count = (overflows_captured * PCNT_HW_COUNTER_RANGE) + hw_count;

    // ESP_LOGV(TAG, "getPosition: Overflows: %d, HW_Count: %d, Total: %d",
    //          (int)overflows_captured, hw_count, (int)*count);

    return ESP_OK;
}

esp_err_t Encoder::resetPosition() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Encoder not initialized for reset.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Resetting encoder position on unit %d.", _pcnt_unit);

    portENTER_CRITICAL(&_isr_mux);
    _software_overflow_count = 0;
    esp_err_t ret = pcnt_counter_clear(_pcnt_unit);
    portEXIT_CRITICAL(&_isr_mux);

    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to clear PCNT hardware counter for unit %d: %s", _pcnt_unit, esp_err_to_name(ret));
    }
    return ret;
}

esp_err_t Encoder::pause() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Encoder not initialized for pause.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Pausing encoder on unit %d.", _pcnt_unit);
    return pcnt_counter_pause(_pcnt_unit);
}

esp_err_t Encoder::resume() {
    if (!_initialized) {
        ESP_LOGE(TAG, "Encoder not initialized for resume.");
        return ESP_ERR_INVALID_STATE;
    }
    ESP_LOGD(TAG, "Resuming encoder on unit %d.", _pcnt_unit);
    return pcnt_counter_resume(_pcnt_unit);
}

} // namespace HAL
