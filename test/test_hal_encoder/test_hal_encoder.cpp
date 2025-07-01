#include <unity.h>
#include "hal/Encoder.h"
#include "esp_idf_mocks.h" // For mocking ESP-IDF functions
#include "driver/pcnt.h"
#include "freertos/FreeRTOS.h" // For portMUX_TYPE
#include "pinmap.h"

// --- Mock Control for PCNT ---
typedef struct {
    pcnt_unit_t unit;
    pcnt_config_t config_ch0; // Assuming only channel 0 is used by Encoder class for now
    // pcnt_config_t config_ch1;
    uint16_t filter_val;
    bool filter_enabled;
    uint32_t event_enabled_mask; // Bitmask of enabled PCNT_EVT_x

    int16_t current_hw_counter;
    int32_t simulated_overflow_count; // Used by mock to drive the class's overflow counter

    pcnt_isr_callback_t isr_handler;
    void* isr_handler_arg;

    int call_count_unit_config;
    int call_count_set_filter_value;
    int call_count_filter_enable;
    int call_count_event_enable;
    int call_count_counter_pause;
    int call_count_counter_clear;
    int call_count_counter_resume;
    int call_count_get_counter_value;
    int call_count_isr_service_install;
    int call_count_isr_handler_add;
    int call_count_isr_handler_remove;
    int call_count_unit_get_events; // New name for pcnt_get_event_status
    int call_count_unit_clear_events; // New name for pcnt_event_clear
} pcnt_mock_state_t;

static pcnt_mock_state_t mock_pcnt;

// --- Mock ESP-IDF PCNT and ISR functions ---
// Keep track of the 'this' pointer for the Encoder instance for the ISR
static HAL::Encoder* current_encoder_instance_for_isr = nullptr;

extern "C" {

// Critical section mocks (simplified, assume single core for test logic)
// These are tricky to mock perfectly without a FreeRTOS test environment.
static portMUX_TYPE test_mux = portMUX_INITIALIZER_UNLOCKED;
void IRAM_ATTR portENTER_CRITICAL_ISR(portMUX_TYPE *mux) { taskENTER_CRITICAL_ISR(mux); } // Use real if available and safe
void IRAM_ATTR portEXIT_CRITICAL_ISR(portMUX_TYPE *mux)  { taskEXIT_CRITICAL_ISR(mux); }
void portENTER_CRITICAL(portMUX_TYPE *mux) { taskENTER_CRITICAL(mux); }
void portEXIT_CRITICAL(portMUX_TYPE *mux)  { taskEXIT_CRITICAL(mux); }
void vPortCPUInitializeMutex(portMUX_TYPE *mux) { /* no-op for this simple mock */ }


esp_err_t pcnt_unit_config(const pcnt_config_t *pcnt_config) {
    mock_pcnt.unit = pcnt_config->unit;
    if (pcnt_config->channel == PCNT_CHANNEL_0) {
        mock_pcnt.config_ch0 = *pcnt_config;
    }
    // else if (pcnt_config->channel == PCNT_CHANNEL_1) { mock_pcnt.config_ch1 = *pcnt_config; }
    mock_pcnt.call_count_unit_config++;
    return ESP_OK;
}

esp_err_t pcnt_set_filter_value(pcnt_unit_t unit, uint16_t filter_val) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.filter_val = filter_val;
    mock_pcnt.call_count_set_filter_value++;
    return ESP_OK;
}

esp_err_t pcnt_filter_enable(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.filter_enabled = true;
    mock_pcnt.call_count_filter_enable++;
    return ESP_OK;
}

esp_err_t pcnt_filter_disable(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.filter_enabled = false;
    // mock_pcnt.call_count_filter_disable++; // Add if needed
    return ESP_OK;
}

esp_err_t pcnt_event_enable(pcnt_unit_t unit, pcnt_event_type_t evt_type) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.event_enabled_mask |= (1 << evt_type);
    mock_pcnt.call_count_event_enable++;
    return ESP_OK;
}

esp_err_t pcnt_counter_pause(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.call_count_counter_pause++;
    return ESP_OK;
}

esp_err_t pcnt_counter_clear(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.current_hw_counter = 0; // Simulate HW counter clear
    // Also reset the mock's idea of overflows if clear implies full reset
    // mock_pcnt.simulated_overflow_count = 0; // The class handles its own _software_overflow_count
    mock_pcnt.call_count_counter_clear++;
    return ESP_OK;
}

esp_err_t pcnt_counter_resume(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.call_count_counter_resume++;
    return ESP_OK;
}

esp_err_t pcnt_get_counter_value(pcnt_unit_t unit, int16_t *count) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    *count = mock_pcnt.current_hw_counter;
    mock_pcnt.call_count_get_counter_value++;
    return ESP_OK;
}

// Mock for pcnt_unit_get_events (new name for pcnt_get_event_status)
esp_err_t pcnt_unit_get_events(pcnt_unit_t unit, uint32_t *events) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    // Simulate events based on mock_pcnt.current_hw_counter relative to limits
    // This is complex. For ISR testing, we'll call the ISR handler directly.
    // So this mock might just return 0 if not actively simulating an event for other paths.
    *events = 0;
    if (mock_pcnt.current_hw_counter == PCNT_H_LIM_VAL && (mock_pcnt.event_enabled_mask & (1 << PCNT_EVT_H_LIM))) {
        *events |= PCNT_EVT_H_LIM;
    }
    if (mock_pcnt.current_hw_counter == PCNT_L_LIM_VAL && (mock_pcnt.event_enabled_mask & (1 << PCNT_EVT_L_LIM))) {
        *events |= PCNT_EVT_L_LIM;
    }
    mock_pcnt.call_count_unit_get_events++;
    return ESP_OK;
}

// Mock for pcnt_unit_clear_events (new name for pcnt_event_clear)
esp_err_t pcnt_unit_clear_events(pcnt_unit_t unit, uint32_t events_to_clear) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    // No specific state change in mock for clearing, just count call
    mock_pcnt.call_count_unit_clear_events++;
    return ESP_OK;
}


esp_err_t pcnt_isr_service_install(int intr_alloc_flags) {
    mock_pcnt.call_count_isr_service_install++;
    // Allow re-installation for tests, return OK or already installed
    static bool service_installed = false;
    if (service_installed && intr_alloc_flags != -1) { // -1 hack for "force reinstall for test"
         return ESP_ERR_INVALID_STATE; // Already installed
    }
    service_installed = true;
    return ESP_OK;
}

esp_err_t pcnt_isr_handler_add(pcnt_unit_t unit, pcnt_isr_callback_t isr_handler, void *arg) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.isr_handler = isr_handler;
    mock_pcnt.isr_handler_arg = arg;
    current_encoder_instance_for_isr = static_cast<HAL::Encoder*>(arg); // Store for direct ISR call
    mock_pcnt.call_count_isr_handler_add++;
    return ESP_OK;
}

esp_err_t pcnt_isr_handler_remove(pcnt_unit_t unit) {
    TEST_ASSERT_EQUAL_INT(mock_pcnt.unit, unit);
    mock_pcnt.isr_handler = nullptr;
    mock_pcnt.isr_handler_arg = nullptr;
    current_encoder_instance_for_isr = nullptr;
    mock_pcnt.call_count_isr_handler_remove++;
    return ESP_OK;
}

} // extern "C"


// --- Test Helper Functions ---
void reset_pcnt_mock_state() {
    memset(&mock_pcnt, 0, sizeof(pcnt_mock_state_t));
    // Set default unit if tests rely on it implicitly before begin()
    mock_pcnt.unit = PCNT_UNIT_0;
}

// Simulate an ISR call for H_LIM
void simulate_pcnt_h_lim_isr() {
    if (mock_pcnt.isr_handler && current_encoder_instance_for_isr) {
        // To simulate the event status register for the ISR
        // This is a bit of a hack. The ISR usually calls pcnt_unit_get_events.
        // We need pcnt_unit_get_events to return PCNT_EVT_H_LIM.
        // The mock pcnt_unit_get_events doesn't have a way to force this state externally yet.
        // So, we'll call the handler directly, and the handler will call our mock pcnt_unit_get_events.
        // To make pcnt_unit_get_events return the right thing, we'd need to set mock_pcnt.current_hw_counter
        // to PCNT_H_LIM_VAL *before* calling the handler.
        // Or, modify the mock pcnt_unit_get_events to have a "force_event_flags" field.

        // Simpler: The ISR itself is static in Encoder. We can't call it directly with a custom status.
        // The Encoder::pcnt_intr_handler is the function.
        // HAL::Encoder::pcnt_intr_handler(mock_pcnt.isr_handler_arg); // This is how it would be called.
        // The ISR will then call our mocked pcnt_unit_get_events.

        // Forcing the event for the mock:
        uint32_t forced_event_status = PCNT_EVT_H_LIM;

        // Temporarily override pcnt_unit_get_events for this call
        // This is getting complicated. A better mocking framework (FFF) would handle this easily.
        // For now, let's assume the ISR is tested by setting hw_counter and then calling getPosition,
        // relying on the internal ISR call if the test framework actually ran it.
        // Or, we directly call the static handler method of the class instance.
        HAL::Encoder::pcnt_intr_handler(mock_pcnt.isr_handler_arg);

    }
}
// Simulate an ISR call for L_LIM
void simulate_pcnt_l_lim_isr() {
    if (mock_pcnt.isr_handler && current_encoder_instance_for_isr) {
         HAL::Encoder::pcnt_intr_handler(mock_pcnt.isr_handler_arg);
    }
}


// --- Test Cases ---
HAL::Encoder test_encoder;

void setUp(void) {
    reset_pcnt_mock_state();
    // Force ISR service to appear as "not installed" for the first test needing it
    // pcnt_isr_service_install(-1); // Hack: -1 to allow re-"installation" by mock
    // test_encoder = HAL::Encoder(); // Recreate if needed
}

void tearDown(void) {
    // test_encoder. ~Encoder(); // Call destructor if it does cleanup like isr_handler_remove
}

void test_encoder_initialization(void) {
    TEST_ASSERT_EQUAL(ESP_OK, test_encoder.begin(GPIO_NUM_4, GPIO_NUM_5, PCNT_UNIT_0, 100));

    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_unit_config);
    TEST_ASSERT_EQUAL(PCNT_UNIT_0, mock_pcnt.config_ch0.unit);
    TEST_ASSERT_EQUAL(GPIO_NUM_4, mock_pcnt.config_ch0.pulse_gpio_num);
    TEST_ASSERT_EQUAL(GPIO_NUM_5, mock_pcnt.config_ch0.ctrl_gpio_num);
    TEST_ASSERT_EQUAL_INT16(PCNT_H_LIM_VAL, mock_pcnt.config_ch0.counter_h_lim);
    TEST_ASSERT_EQUAL_INT16(PCNT_L_LIM_VAL, mock_pcnt.config_ch0.counter_l_lim);

    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_set_filter_value);
    TEST_ASSERT_EQUAL_UINT16(100, mock_pcnt.filter_val);
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_filter_enable);
    TEST_ASSERT_TRUE(mock_pcnt.filter_enabled);

    TEST_ASSERT_GREATER_THAN_INT(0, mock_pcnt.call_count_event_enable); // Called twice
    TEST_ASSERT_TRUE(mock_pcnt.event_enabled_mask & (1 << PCNT_EVT_H_LIM));
    TEST_ASSERT_TRUE(mock_pcnt.event_enabled_mask & (1 << PCNT_EVT_L_LIM));

    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_counter_pause);
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_counter_clear);
    TEST_ASSERT_EQUAL_INT(0, mock_pcnt.current_hw_counter); // Check if clear worked in mock
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_counter_resume);

    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_isr_service_install);
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_isr_handler_add);
    TEST_ASSERT_NOT_NULL(mock_pcnt.isr_handler);
    TEST_ASSERT_EQUAL_PTR(&test_encoder, mock_pcnt.isr_handler_arg);
}

void test_encoder_get_position_initial(void) {
    test_encoder.begin(GPIO_NUM_4, GPIO_NUM_5);
    int32_t pos = 123; // Non-zero initial
    mock_pcnt.current_hw_counter = 0; // Explicitly set after begin's clear
    TEST_ASSERT_EQUAL(ESP_OK, test_encoder.getPosition(&pos));
    TEST_ASSERT_EQUAL_INT32(0, pos);
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_get_counter_value);
}

void test_encoder_reset_position(void) {
    test_encoder.begin(GPIO_NUM_4, GPIO_NUM_5);
    // Simulate some movement and overflow
    mock_pcnt.current_hw_counter = 100;
    // Directly manipulate Encoder's internal overflow count for test via ISR call
    // This requires current_encoder_instance_for_isr to be set correctly.
    // And pcnt_unit_get_events mock to provide PCNT_EVT_H_LIM
    uint32_t original_event_mask = mock_pcnt.event_enabled_mask; // Save
    mock_pcnt.event_enabled_mask |= (1 << PCNT_EVT_H_LIM); // Ensure H_LIM is "enabled" for the mock get_events

    // Simulate an overflow event by setting counter to H_LIM and calling ISR
    mock_pcnt.current_hw_counter = PCNT_H_LIM_VAL;
    simulate_pcnt_h_lim_isr(); // This will increment internal _software_overflow_count

    mock_pcnt.event_enabled_mask = original_event_mask; // Restore

    // Set hw counter to something non-zero after simulated overflow
    mock_pcnt.current_hw_counter = 50;

    int32_t pos_before_reset;
    test_encoder.getPosition(&pos_before_reset);
    TEST_ASSERT_NOT_EQUAL(50, pos_before_reset); // Should be 65536 + 50 or similar based on PCNT_HW_COUNTER_RANGE

    TEST_ASSERT_EQUAL(ESP_OK, test_encoder.resetPosition());
    TEST_ASSERT_EQUAL_INT(1, mock_pcnt.call_count_counter_clear); // From reset
    TEST_ASSERT_EQUAL_INT(0, mock_pcnt.current_hw_counter); // Mock hw counter cleared

    int32_t pos_after_reset;
    TEST_ASSERT_EQUAL(ESP_OK, test_encoder.getPosition(&pos_after_reset));
    TEST_ASSERT_EQUAL_INT32(0, pos_after_reset); // Both sw and hw parts should be zero
}


void test_encoder_positive_overflow(void) {
    test_encoder.begin(GPIO_NUM_4, GPIO_NUM_5);
    int32_t pos;

    mock_pcnt.current_hw_counter = PCNT_H_LIM_VAL - 1;
    test_encoder.getPosition(&pos);
    TEST_ASSERT_EQUAL_INT32(PCNT_H_LIM_VAL - 1, pos);

    // Simulate crossing H_LIM
    mock_pcnt.current_hw_counter = PCNT_H_LIM_VAL; // At the limit
    // Manually call ISR handler as if event occurred
    // The ISR itself will call the mocked pcnt_unit_get_events, which needs to report H_LIM
    // To do this, our pcnt_unit_get_events mock needs to be smarter or we need to set a flag for it.
    // For now, let's assume simulate_pcnt_h_lim_isr sets up the conditions correctly for the mock.
    uint32_t original_event_mask = mock_pcnt.event_enabled_mask;
    mock_pcnt.event_enabled_mask |= (1 << PCNT_EVT_H_LIM); // Ensure H_LIM is "enabled" for the mock get_events

    simulate_pcnt_h_lim_isr(); // This increments _software_overflow_count inside test_encoder

    mock_pcnt.event_enabled_mask = original_event_mask;

    // After overflow, hardware counter wraps. PCNT wraps from PCNT_H_LIM_VAL to PCNT_L_LIM_VAL.
    mock_pcnt.current_hw_counter = PCNT_L_LIM_VAL;
    test_encoder.getPosition(&pos);
    // Expected: 1 (overflow) * 65536 (range) + PCNT_L_LIM_VAL
    TEST_ASSERT_EQUAL_INT32((1 * (int32_t)PCNT_HW_COUNTER_RANGE) + PCNT_L_LIM_VAL, pos);
}

void test_encoder_negative_overflow(void) {
    test_encoder.begin(GPIO_NUM_4, GPIO_NUM_5);
    int32_t pos;

    mock_pcnt.current_hw_counter = PCNT_L_LIM_VAL + 1;
    test_encoder.getPosition(&pos);
    TEST_ASSERT_EQUAL_INT32(PCNT_L_LIM_VAL + 1, pos);

    // Simulate crossing L_LIM
    mock_pcnt.current_hw_counter = PCNT_L_LIM_VAL; // At the limit
    uint32_t original_event_mask = mock_pcnt.event_enabled_mask;
    mock_pcnt.event_enabled_mask |= (1 << PCNT_EVT_L_LIM);

    simulate_pcnt_l_lim_isr(); // This decrements _software_overflow_count

    mock_pcnt.event_enabled_mask = original_event_mask;

    // After underflow, hardware counter wraps from PCNT_L_LIM_VAL to PCNT_H_LIM_VAL.
    mock_pcnt.current_hw_counter = PCNT_H_LIM_VAL;
    test_encoder.getPosition(&pos);
    // Expected: -1 (overflow) * 65536 (range) + PCNT_H_LIM_VAL
    TEST_ASSERT_EQUAL_INT32((-1 * (int32_t)PCNT_HW_COUNTER_RANGE) + PCNT_H_LIM_VAL, pos);
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_encoder_initialization);
    RUN_TEST(test_encoder_get_position_initial);
    RUN_TEST(test_encoder_reset_position);
    RUN_TEST(test_encoder_positive_overflow);
    RUN_TEST(test_encoder_negative_overflow);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
