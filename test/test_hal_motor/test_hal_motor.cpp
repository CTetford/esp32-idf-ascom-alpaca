#include <unity.h>
#include "hal/Motor.h"
#include "esp_idf_mocks.h" // For mocking ESP-IDF functions
#include "driver/mcpwm.h"  // For MCPWM types
#include "driver/gpio.h"   // For GPIO types
#include "pinmap.h"        // For actual pin numbers, though we'll use fixed ones for tests

// --- Mock Control ---
// These will be used by the mock functions to check calls and provide return values.
typedef struct {
    gpio_num_t pin;
    gpio_config_t config;
    int set_level_val;
    int call_count_config;
    int call_count_set_level;
} gpio_mock_state_t;

typedef struct {
    mcpwm_unit_t unit;
    mcpwm_io_signals_t io_signal_opA; // For MCPWM_OPR_A
    mcpwm_io_signals_t io_signal_opB; // For MCPWM_OPR_B
    gpio_num_t gpio_num_opA;
    gpio_num_t gpio_num_opB;
    mcpwm_timer_t timer;
    mcpwm_config_t config;
    float duty_opA;
    float duty_opB;
    mcpwm_duty_type_t duty_type_opA;
    mcpwm_duty_type_t duty_type_opB;
    int call_count_gpio_init_opA;
    int call_count_gpio_init_opB;
    int call_count_mcpwm_init;
    int call_count_set_duty_opA;
    int call_count_set_duty_opB;
    int call_count_set_signal_low_opA;
    int call_count_set_signal_low_opB;
    // Add more fields as needed for other MCPWM functions
} mcpwm_mock_state_t;

// Declare mock states for GPIO and MCPWM
// We need one for each pin potentially used.
// For Motor: en_pin, pwm_a_pin, pwm_b_pin
static gpio_mock_state_t mock_gpio_en_pin_state;
static mcpwm_mock_state_t mock_mcpwm_state;


// --- Mock ESP-IDF functions ---
// These need to be linked instead of the real ones during testing.
// This usually involves linker script manipulation or weak symbols if the build system supports it easily.
// For PlatformIO unit testing, we can define them, and the test build might pick them.
// Or, use a proper mocking framework like FakeFunctionFramework (fff) or CMock.
// For simplicity here, I'll define basic mocks.

extern "C" {

// GPIO Mocks
esp_err_t gpio_config(const gpio_config_t *pGPIOConfig) {
    // For now, assume only EN pin uses gpio_config directly in Motor class
    // This is a simplified mock. A real one would handle multiple pins.
    if ((1ULL << mock_gpio_en_pin_state.pin) & pGPIOConfig->pin_bit_mask) {
        mock_gpio_en_pin_state.config = *pGPIOConfig;
        mock_gpio_en_pin_state.call_count_config++;
    }
    // Add more sophisticated pin checking if testing multiple GPIO configs
    return ESP_OK;
}

esp_err_t gpio_set_level(gpio_num_t gpio_num, uint32_t level) {
    if (gpio_num == mock_gpio_en_pin_state.pin) {
        mock_gpio_en_pin_state.set_level_val = level;
        mock_gpio_en_pin_state.call_count_set_level++;
    }
    // Add more pin checks if needed
    return ESP_OK;
}

// MCPWM Mocks
esp_err_t mcpwm_gpio_init(mcpwm_unit_t mcpwm_num, mcpwm_io_signals_t io_signal, int gpio_num) {
    mock_mcpwm_state.unit = mcpwm_num;
    if (io_signal == MCPWM0A || io_signal == MCPWM1A || io_signal == MCPWM2A) { // Simplified check for OpA
        mock_mcpwm_state.io_signal_opA = io_signal;
        mock_mcpwm_state.gpio_num_opA = (gpio_num_t)gpio_num;
        mock_mcpwm_state.call_count_gpio_init_opA++;
    } else if (io_signal == MCPWM0B || io_signal == MCPWM1B || io_signal == MCPWM2B) { // Simplified check for OpB
        mock_mcpwm_state.io_signal_opB = io_signal;
        mock_mcpwm_state.gpio_num_opB = (gpio_num_t)gpio_num;
        mock_mcpwm_state.call_count_gpio_init_opB++;
    }
    // This logic for io_signal to operator needs to match how Motor.cpp determines MCPWM_OPR_A/B based on pins.
    // The current Motor.cpp directly uses MCPWM_OPR_A and MCPWM_OPR_B for the calls,
    // and mcpwm_gpio_init takes the mcpwm_io_signals_t which are like MCPWM0A, MCPWM0B.
    // Let's assume the test will pass specific io_signals that map to OPR_A and OPR_B for the unit/timer.
    // The Motor class uses MCPWM_OPR_A and MCPWM_OPR_B, which are enum values 0 and 1.
    // The mcpwm_gpio_init function takes an mcpwm_io_signals_t (e.g. MCPWM0A).
    // The mock should correctly associate these.
    // For this mock: we'll assume the test calls mcpwm_gpio_init with io_signals that implicitly map.
    // A better mock would check unit and map io_signal to an operator index for storage.
    // For the Motor class as written, it calls:
    // mcpwm_gpio_init(_pwm_unit, MCPWM_OPR_A, _pwm_a_pin); -> This is incorrect. mcpwm_gpio_init expects an io_signal like MCPWM0A.
    // The Motor.cpp needs to be fixed. It should be:
    // mcpwm_gpio_init(_pwm_unit, mcpwm_io_signals_t corresponding to OPR_A of _pwm_timer, _pwm_a_pin);
    // For MCPWM_TIMER_0, OPR_A is MCPWM0A. For MCPWM_TIMER_1, OPR_A is MCPWM1A etc.
    // Let's adjust the mock to assume io_signal directly implies operator for simplicity of the mock,
    // but acknowledge Motor.cpp needs a fix if it passes MCPWM_OPR_A directly to mcpwm_gpio_init's io_signal arg.

    // Re-checking Motor.cpp: `mcpwm_gpio_init(_pwm_unit, MCPWM_OPR_A, _pwm_a_pin);`
    // This is indeed an API misuse. MCPWM_OPR_A is an enum for operator, not mcpwm_io_signals_t.
    // It should be e.g. `mcpwm_gpio_init(_pwm_unit, (mcpwm_io_signals_t)(MCPWM0A + _pwm_timer*2), _pwm_a_pin);` (this mapping is also simplified)
    // Or more correctly:
    // if (_pwm_timer == MCPWM_TIMER_0) { mcpwm_gpio_init(_pwm_unit, MCPWM0A, _pwm_a_pin); } etc.

    // For the sake of progressing the test, I'll assume the mock can interpret MCPWM_OPR_A/B as specific signals for now.
    // This means the unit tests might pass due to a "compensating error" in the mock logic if Motor.cpp is not fixed.
    // I will fix Motor.cpp first.

    return ESP_OK;
}

esp_err_t mcpwm_init(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num, const mcpwm_config_t *mcpwm_conf) {
    mock_mcpwm_state.unit = mcpwm_num;
    mock_mcpwm_state.timer = timer_num;
    mock_mcpwm_state.config = *mcpwm_conf;
    mock_mcpwm_state.call_count_mcpwm_init++;
    return ESP_OK;
}

esp_err_t mcpwm_set_duty(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num, mcpwm_operator_t op_num, float duty) {
    mock_mcpwm_state.unit = mcpwm_num;
    mock_mcpwm_state.timer = timer_num;
    if (op_num == MCPWM_OPR_A) {
        mock_mcpwm_state.duty_opA = duty;
        mock_mcpwm_state.call_count_set_duty_opA++;
    } else if (op_num == MCPWM_OPR_B) {
        mock_mcpwm_state.duty_opB = duty;
        mock_mcpwm_state.call_count_set_duty_opB++;
    }
    return ESP_OK;
}

esp_err_t mcpwm_set_duty_type(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num, mcpwm_operator_t op_num, mcpwm_duty_type_t duty_type) {
    mock_mcpwm_state.unit = mcpwm_num;
    mock_mcpwm_state.timer = timer_num;
     if (op_num == MCPWM_OPR_A) {
        mock_mcpwm_state.duty_type_opA = duty_type;
    } else if (op_num == MCPWM_OPR_B) {
        mock_mcpwm_state.duty_type_opB = duty_type;
    }
    return ESP_OK;
}

esp_err_t mcpwm_set_signal_low(mcpwm_unit_t mcpwm_num, mcpwm_timer_t timer_num, mcpwm_operator_t op_num) {
    mock_mcpwm_state.unit = mcpwm_num;
    mock_mcpwm_state.timer = timer_num;
    if (op_num == MCPWM_OPR_A) {
        // Simulate setting duty to 0 when signal is low for verification
        mock_mcpwm_state.duty_opA = 0.0f;
        mock_mcpwm_state.call_count_set_signal_low_opA++;
    } else if (op_num == MCPWM_OPR_B) {
        mock_mcpwm_state.duty_opB = 0.0f;
        mock_mcpwm_state.call_count_set_signal_low_opB++;
    }
    return ESP_OK;
}
} // extern "C"


// --- Test Helper Functions ---
void reset_gpio_mock_state(gpio_mock_state_t& state, gpio_num_t pin_to_mock) {
    memset(&state, 0, sizeof(gpio_mock_state_t));
    state.pin = pin_to_mock;
}

void reset_mcpwm_mock_state() {
    memset(&mock_mcpwm_state, 0, sizeof(mcpwm_mock_state_t));
}

// Define some GPIOs for testing
const gpio_num_t TEST_EN_PIN = GPIO_NUM_1;
const gpio_num_t TEST_PWM_A_PIN = GPIO_NUM_2; // RPWM
const gpio_num_t TEST_PWM_B_PIN = GPIO_NUM_3; // LPWM


// --- Test Cases ---
HAL::Motor test_motor; // Test motor instance

void setUp(void) {
    // Reset mocks before each test
    reset_gpio_mock_state(mock_gpio_en_pin_state, TEST_EN_PIN);
    reset_mcpwm_mock_state();
    // Re-initialize motor for each test to have a clean state.
    // test_motor = HAL::Motor(); // If default constructor was more useful or if it needs specific re-init logic
}

void tearDown(void) {
    // Clean up resources if any were allocated
}

void test_motor_initialization(void) {
    TEST_ASSERT_EQUAL(ESP_OK, test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN));

    // Check EN Pin GPIO config
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_en_pin_state.call_count_config);
    TEST_ASSERT_EQUAL_UINT64((1ULL << TEST_EN_PIN), mock_gpio_en_pin_state.config.pin_bit_mask);
    TEST_ASSERT_EQUAL_INT(GPIO_MODE_OUTPUT, mock_gpio_en_pin_state.config.mode);
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_en_pin_state.call_count_set_level); // Disabled initially
    TEST_ASSERT_EQUAL_INT(0, mock_gpio_en_pin_state.set_level_val);      // Level should be 0 (disabled)

    // Check MCPWM GPIO init
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_gpio_init_opA);
    TEST_ASSERT_EQUAL(TEST_PWM_A_PIN, mock_mcpwm_state.gpio_num_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_gpio_init_opB);
    TEST_ASSERT_EQUAL(TEST_PWM_B_PIN, mock_mcpwm_state.gpio_num_opB);

    // Check MCPWM init
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_mcpwm_init);
    TEST_ASSERT_EQUAL(MCPWM_UNIT_0, mock_mcpwm_state.unit); // Default unit
    TEST_ASSERT_EQUAL(MCPWM_TIMER_0, mock_mcpwm_state.timer); // Default timer
    TEST_ASSERT_EQUAL_INT(10000, mock_mcpwm_state.config.frequency); // Default freq

    // Check initial state (both signals low after init)
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opB);
}

void test_motor_enable_disable(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    mock_gpio_en_pin_state.call_count_set_level = 0; // Reset after begin's initial disable

    test_motor.enable();
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_en_pin_state.call_count_set_level);
    TEST_ASSERT_EQUAL_INT(1, mock_gpio_en_pin_state.set_level_val);

    test_motor.disable();
    TEST_ASSERT_EQUAL_INT(2, mock_gpio_en_pin_state.call_count_set_level);
    TEST_ASSERT_EQUAL_INT(0, mock_gpio_en_pin_state.set_level_val);
}

void test_motor_set_speed_forward(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    test_motor.enable(); // Motor must be enabled to set speed effectively

    // Reset call counts for relevant functions after begin/enable
    mock_mcpwm_state.call_count_set_duty_opA = 0;
    mock_mcpwm_state.call_count_set_signal_low_opB = 0; // This was set during begin()

    TEST_ASSERT_EQUAL(ESP_OK, test_motor.setSpeed(50.0f));
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_duty_opA);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, mock_mcpwm_state.duty_opA);
    TEST_ASSERT_EQUAL(MCPWM_DUTY_MODE_0, mock_mcpwm_state.duty_type_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opB);
}

void test_motor_set_speed_reverse(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    test_motor.enable();

    mock_mcpwm_state.call_count_set_signal_low_opA = 0; // This was set during begin()
    mock_mcpwm_state.call_count_set_duty_opB = 0;

    TEST_ASSERT_EQUAL(ESP_OK, test_motor.setSpeed(-75.0f));
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_duty_opB);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 75.0f, mock_mcpwm_state.duty_opB); // Speed is abs for duty
    TEST_ASSERT_EQUAL(MCPWM_DUTY_MODE_0, mock_mcpwm_state.duty_type_opB);
}

void test_motor_set_speed_stop(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    test_motor.enable();

    // Move it first
    test_motor.setSpeed(50.0f);

    // Reset call counts for signal_low before testing stop
    // Note: begin() calls set_signal_low for OPA and OPB.
    // setSpeed(50.0f) calls set_signal_low for OPB.
    // So, we need to be careful about the exact call counts or reset them strategically.
    mock_mcpwm_state.call_count_set_signal_low_opA = 0;
    mock_mcpwm_state.call_count_set_signal_low_opB = 0;

    TEST_ASSERT_EQUAL(ESP_OK, test_motor.setSpeed(0.0f));
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opB);
}

void test_motor_stop_method(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    test_motor.enable();
    test_motor.setSpeed(30.0f); // Move it

    mock_mcpwm_state.call_count_set_signal_low_opA = 0;
    mock_mcpwm_state.call_count_set_signal_low_opB = 0;

    TEST_ASSERT_EQUAL(ESP_OK, test_motor.stop());
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opB);
}

void test_motor_speed_clamping(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    test_motor.enable();

    mock_mcpwm_state.call_count_set_duty_opA = 0;
    test_motor.setSpeed(150.0f); // Should be clamped to 100
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, mock_mcpwm_state.duty_opA);

    mock_mcpwm_state.call_count_set_duty_opB = 0;
    test_motor.setSpeed(-120.0f); // Should be clamped to 100 for duty_opB
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, mock_mcpwm_state.duty_opB);
}

void test_motor_speed_when_disabled(void) {
    test_motor.begin(TEST_PWM_A_PIN, TEST_PWM_B_PIN, TEST_EN_PIN);
    // Motor is disabled by default after begin()

    mock_mcpwm_state.call_count_set_duty_opA = 0;
    mock_mcpwm_state.call_count_set_duty_opB = 0;
    // Reset signal low counts that happened during begin()
    mock_mcpwm_state.call_count_set_signal_low_opA = 0;
    mock_mcpwm_state.call_count_set_signal_low_opB = 0;

    TEST_ASSERT_EQUAL(ESP_OK, test_motor.setSpeed(50.0f));
    // Should set signals to low because motor is disabled
    TEST_ASSERT_EQUAL_INT(0, mock_mcpwm_state.call_count_set_duty_opA); // No duty set
    TEST_ASSERT_EQUAL_INT(0, mock_mcpwm_state.call_count_set_duty_opB);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opA);
    TEST_ASSERT_EQUAL_INT(1, mock_mcpwm_state.call_count_set_signal_low_opB);
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_motor_initialization);
    RUN_TEST(test_motor_enable_disable);
    RUN_TEST(test_motor_set_speed_forward);
    RUN_TEST(test_motor_set_speed_reverse);
    RUN_TEST(test_motor_set_speed_stop);
    RUN_TEST(test_motor_stop_method);
    RUN_TEST(test_motor_speed_clamping);
    RUN_TEST(test_motor_speed_when_disabled);
    UNITY_END();
}

extern "C" void app_main() {
    // This function is the entry point for PlatformIO test runner
    // Delay for a moment to allow USB monitor to connect
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
