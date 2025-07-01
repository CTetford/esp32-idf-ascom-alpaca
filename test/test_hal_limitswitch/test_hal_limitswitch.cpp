#include <unity.h>
#include "hal/LimitSwitch.h"
#include "esp_idf_mocks.h" // For mocking ESP-IDF functions
#include "driver/gpio.h"   // For GPIO types
#include "pinmap.h"        // Not strictly needed here, but good for consistency

// --- Mock Control for GPIO ---
typedef struct {
    gpio_num_t pin;
    gpio_config_t config;
    int current_level; // To simulate GPIO input level
    int call_count_config;
    int call_count_get_level;
} gpio_mock_state_t;

static gpio_mock_state_t mock_gpio_state;

// --- Mock ESP-IDF GPIO functions ---
extern "C" {

esp_err_t gpio_config(const gpio_config_t *pGPIOConfig) {
    // Assuming only one pin is configured by LimitSwitch at a time for simplicity in mock
    mock_gpio_state.pin = (gpio_num_t)__builtin_ctzll(pGPIOConfig->pin_bit_mask); // Get the single pin from mask
    mock_gpio_state.config = *pGPIOConfig;
    mock_gpio_state.call_count_config++;
    return ESP_OK;
}

int gpio_get_level(gpio_num_t gpio_num) {
    if (gpio_num == mock_gpio_state.pin) {
        mock_gpio_state.call_count_get_level++;
        return mock_gpio_state.current_level;
    }
    // Should not happen if test is set up correctly
    printf("ERROR: gpio_get_level called for unexpected pin %d in mock\n", gpio_num);
    return -1;
}

// No need to mock esp_log for this simple module's tests if we assume
// logging macros compile out or are redirected.
} // extern "C"


// --- Test Helper Functions ---
void reset_gpio_mock_state(gpio_num_t pin_to_mock) {
    memset(&mock_gpio_state, 0, sizeof(gpio_mock_state_t));
    mock_gpio_state.pin = pin_to_mock; // Store which pin we expect to be configured
    mock_gpio_state.current_level = 0; // Default to low level
}

// Define a GPIO for testing
const gpio_num_t TEST_SWITCH_PIN = GPIO_NUM_4;

// --- Test Cases ---
HAL::LimitSwitch test_switch;

void setUp(void) {
    reset_gpio_mock_state(TEST_SWITCH_PIN);
    // test_switch = HAL::LimitSwitch(); // Recreate if it had complex internal state
}

void tearDown(void) {
    // Clean up
}

void test_limitswitch_initialization_active_low_with_pullup(void) {
    TEST_ASSERT_EQUAL(ESP_OK, test_switch.begin(TEST_SWITCH_PIN, true /*active_low*/, true /*pull_up*/, false /*pull_down*/));

    TEST_ASSERT_EQUAL_INT(1, mock_gpio_state.call_count_config);
    TEST_ASSERT_EQUAL(TEST_SWITCH_PIN, mock_gpio_state.pin);
    TEST_ASSERT_EQUAL_UINT64((1ULL << TEST_SWITCH_PIN), mock_gpio_state.config.pin_bit_mask);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_INPUT, mock_gpio_state.config.mode);
    TEST_ASSERT_EQUAL(GPIO_PULLUP_ENABLE, mock_gpio_state.config.pull_up_en);
    TEST_ASSERT_EQUAL(GPIO_PULLDOWN_DISABLE, mock_gpio_state.config.pull_down_en);
}

void test_limitswitch_initialization_active_high_with_pulldown(void) {
    TEST_ASSERT_EQUAL(ESP_OK, test_switch.begin(TEST_SWITCH_PIN, false /*active_low*/, false /*pull_up*/, true /*pull_down*/));

    TEST_ASSERT_EQUAL_INT(1, mock_gpio_state.call_count_config);
    TEST_ASSERT_EQUAL(TEST_SWITCH_PIN, mock_gpio_state.pin);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_INPUT, mock_gpio_state.config.mode);
    TEST_ASSERT_EQUAL(GPIO_PULLUP_DISABLE, mock_gpio_state.config.pull_up_en);
    TEST_ASSERT_EQUAL(GPIO_PULLDOWN_ENABLE, mock_gpio_state.config.pull_down_en);
}

void test_limitswitch_is_active_active_low(void) {
    test_switch.begin(TEST_SWITCH_PIN, true /*active_low*/); // Default pull-up true

    // Simulate switch active (pin is LOW)
    mock_gpio_state.current_level = 0;
    TEST_ASSERT_TRUE(test_switch.isActive());
    TEST_ASSERT_TRUE(test_switch.isAtHome()); // Alias
    TEST_ASSERT_EQUAL_INT(2, mock_gpio_state.call_count_get_level); // isActive + isAtHome

    // Simulate switch inactive (pin is HIGH)
    mock_gpio_state.current_level = 1;
    mock_gpio_state.call_count_get_level = 0; // Reset for this check
    TEST_ASSERT_FALSE(test_switch.isActive());
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_state.call_count_get_level);
}

void test_limitswitch_is_active_active_high(void) {
    test_switch.begin(TEST_SWITCH_PIN, false /*active_low*/); // Default pull-up true, pull-down false

    // Simulate switch active (pin is HIGH)
    mock_gpio_state.current_level = 1;
    TEST_ASSERT_TRUE(test_switch.isActive());
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_state.call_count_get_level);

    // Simulate switch inactive (pin is LOW)
    mock_gpio_state.current_level = 0;
    mock_gpio_state.call_count_get_level = 0; // Reset
    TEST_ASSERT_FALSE(test_switch.isActive());
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_state.call_count_get_level);
}

void test_limitswitch_uninitialized_returns_false(void) {
    HAL::LimitSwitch uninit_switch; // Create a new, uninitialized switch
    // Don't call begin()
    TEST_ASSERT_FALSE(uninit_switch.isActive());
    // The mock gpio_get_level should not be called for an uninitialized switch.
    // The current mock doesn't distinguish instances, so this test would be hard to make specific
    // without instance tracking in the mock or a more complex mock setup.
    // However, the class itself logs an error and returns false.
    // For this test, we just check the return value.
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_limitswitch_initialization_active_low_with_pullup);
    RUN_TEST(test_limitswitch_initialization_active_high_with_pulldown);
    RUN_TEST(test_limitswitch_is_active_active_low);
    RUN_TEST(test_limitswitch_is_active_active_high);
    RUN_TEST(test_limitswitch_uninitialized_returns_false);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
