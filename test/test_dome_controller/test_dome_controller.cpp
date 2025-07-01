#include <unity.h>
#include "dome_control/DomeController.h"
#include "hal/Motor.h"       // Include real headers for types
#include "hal/Encoder.h"
#include "hal/LimitSwitch.h"
#include "esp_idf_mocks.h" // For any specific ESP-IDF mocks for this test file

// --- Mocking the HAL Components ---
// We need to provide mock implementations for Motor, Encoder, and LimitSwitch.
// This can be done using a mocking framework (like GoogleMock, FakeFunctionFramework)
// or by manually creating mock classes that inherit from the HAL interfaces (if they were interfaces)
// or by providing mock C-style functions if the HAL components are used via C APIs.
// Since HAL components are classes, we can create mock versions of these classes.

// For simplicity in this environment, I will use a technique where we can redirect
// the behavior of the HAL objects by controlling their state from the test functions.
// This means the actual HAL classes will be instantiated but their ESP-IDF calls
// would need to be mocked if we were testing HAL + DomeController.
// However, for pure DomeController logic tests, we mock the HAL *behavior*.

// --- Global Mock States / Control Variables ---
static float mock_motor_current_speed = 0.0f;
static bool mock_motor_enabled = false;
static int32_t mock_encoder_current_pulses = 0;
static bool mock_limit_switch_is_active = false;
static uint64_t mock_current_time_us = 0; // For esp_timer_get_time()

// Mock HAL function call counters (optional, but good for verifying interactions)
static int mock_motor_setSpeed_calls = 0;
static int mock_motor_stop_calls = 0;
static int mock_motor_enable_calls = 0;
static int mock_encoder_getPosition_calls = 0;
static int mock_encoder_resetPosition_calls = 0;
static int mock_limitswitch_isActive_calls = 0;


// --- Mock Implementations for HAL classes ---
// These will be linked instead of the actual HAL implementations for these tests.
// This requires a bit of build system magic (PlatformIO's test framework handles this).

namespace HAL {

// --- Motor Mock ---
Motor::Motor() : _initialized(false), _enabled(false) {} // Simplified constructor
Motor::~Motor() {}
esp_err_t Motor::begin(gpio_num_t pwm_a_pin, gpio_num_t pwm_b_pin, gpio_num_t en_pin,
                        mcpwm_unit_t pwm_unit, mcpwm_timer_t pwm_timer) {
    _initialized = true;
    mock_motor_enabled = false; // Default to disabled on begin
    return ESP_OK;
}
void Motor::enable() {
    mock_motor_enabled = true;
    mock_motor_enable_calls++;
}
void Motor::disable() { mock_motor_enabled = false; }

esp_err_t Motor::setSpeed(float speed_percent) {
    if (!mock_motor_enabled && speed_percent != 0.0f) {
        // Real motor class might prevent this or log warning. Mock just records.
        // Or, for stricter test, printf("Warning: setSpeed called while motor mock disabled\n");
    }
    mock_motor_current_speed = speed_percent;
    mock_motor_setSpeed_calls++;
    if (speed_percent == 0.0f) { // Treat setSpeed(0) as a stop for counting purposes
        mock_motor_stop_calls++;
    }
    return ESP_OK;
}
esp_err_t Motor::stop() {
    mock_motor_current_speed = 0.0f;
    mock_motor_stop_calls++;
    return ESP_OK;
}

// --- Encoder Mock ---
Encoder::Encoder() : _initialized(false), _software_overflow_count(0) {
     vPortCPUInitializeMutex(&_isr_mux); // Still need this if class uses it internally
}
Encoder::~Encoder() {}
esp_err_t Encoder::begin(gpio_num_t pin_a, gpio_num_t pin_b, pcnt_unit_t pcnt_unit, uint16_t filter_val) {
    _initialized = true;
    mock_encoder_current_pulses = 0; // Reset mock position on begin
    _software_overflow_count = 0;    // Reset internal state of mock too
    return ESP_OK;
}
esp_err_t Encoder::getPosition(int32_t* count) {
    if (!_initialized) return ESP_ERR_INVALID_STATE;
    *count = mock_encoder_current_pulses;
    mock_encoder_getPosition_calls++;
    return ESP_OK;
}
esp_err_t Encoder::resetPosition() {
    mock_encoder_current_pulses = 0;
    _software_overflow_count = 0;
    mock_encoder_resetPosition_calls++;
    return ESP_OK;
}
esp_err_t Encoder::pause() { return ESP_OK; }
esp_err_t Encoder::resume() { return ESP_OK; }
// The static ISR handler is part of the class but won't be directly called by these tests
// unless we specifically invoke it. We control `mock_encoder_current_pulses` directly.
void IRAM_ATTR Encoder::pcnt_intr_handler(void *arg) { /* no-op in mock */ }


// --- LimitSwitch Mock ---
LimitSwitch::LimitSwitch() : _initialized(false) {}
LimitSwitch::~LimitSwitch() {}
esp_err_t LimitSwitch::begin(gpio_num_t pin, bool active_low, bool pull_up, bool pull_down) {
    _initialized = true;
    // Store active_low if needed by mock, but test directly sets mock_limit_switch_is_active
    return ESP_OK;
}
bool LimitSwitch::isActive() {
    if (!_initialized) return false;
    mock_limitswitch_isActive_calls++;
    return mock_limit_switch_is_active;
}
bool LimitSwitch::isAtHome() { return isActive(); }

} // namespace HAL


// --- Mock for esp_timer_get_time ---
// This allows controlling time progression in tests.
extern "C" {
    uint64_t esp_timer_get_time() {
        return mock_current_time_us;
    }
}
void advance_mock_time_ms(uint32_t ms) {
    mock_current_time_us += (uint64_t)ms * 1000;
}
void set_mock_time_us(uint64_t time_us) {
    mock_current_time_us = time_us;
}


// --- Global Test Objects ---
// These use the mocked HAL implementations defined above.
HAL::Motor g_mock_motor;
HAL::Encoder g_mock_encoder;
HAL::LimitSwitch g_mock_home_switch;

// Instance of DomeController using mocked HAL
DomeControl::DomeController g_dome_controller_uut(g_mock_motor, g_mock_encoder, g_mock_home_switch);

// Default parameters for DomeController begin()
const float TEST_HOME_AZ = 0.0f;
const float TEST_PARK_AZ = 180.0f;
const float TEST_PPD = 100.0f; // Pulses per degree
const float TEST_PID_KP = 1.0f, TEST_PID_KI = 0.1f, TEST_PID_KD = 0.05f;
const int TEST_HOMING_SPEED = 30; // percent
const int TEST_HOMING_DIR = 1;
const bool TEST_HS_ACTIVE_LOW = true;


// --- Test Helper Functions ---
void reset_all_mocks_and_uut() {
    mock_motor_current_speed = 0.0f;
    mock_motor_enabled = false; // Motor should be enabled by DomeController::begin
    mock_encoder_current_pulses = 0;
    mock_limit_switch_is_active = false;
    set_mock_time_us(0);

    mock_motor_setSpeed_calls = 0;
    mock_motor_stop_calls = 0;
    mock_motor_enable_calls = 0;
    mock_encoder_getPosition_calls = 0;
    mock_encoder_resetPosition_calls = 0;
    mock_limitswitch_isActive_calls = 0;

    // Re-initialize UUT with default parameters
    g_dome_controller_uut.begin(TEST_HOME_AZ, TEST_PARK_AZ, TEST_PPD,
                                TEST_PID_KP, TEST_PID_KI, TEST_PID_KD,
                                TEST_HOMING_SPEED, TEST_HOMING_DIR, TEST_HS_ACTIVE_LOW);
    g_mock_motor.enable(); // DomeController.begin() enables motor
}

// Helper to run update N times, advancing time
void run_updates(int count, uint32_t time_advance_ms_per_update = PID_SAMPLE_TIME_MS) {
    for (int i = 0; i < count; ++i) {
        g_dome_controller_uut.update();
        advance_mock_time_ms(time_advance_ms_per_update);
    }
}


// --- Test Cases ---
void setUp(void) {
    reset_all_mocks_and_uut();
}

void tearDown(void) {
    // Ensure motor is stopped after each test if it was running
    g_mock_motor.stop();
}

void test_initial_state_is_idle(void) {
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FALSE(g_dome_controller_uut.isHomed());
}

void test_find_home_starts_homing_sequence(void) {
    g_dome_controller_uut.findHome();
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_START, g_dome_controller_uut.getCurrentState());
    run_updates(1); // Process HOMING_START
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_MOVING_TO_SWITCH, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, TEST_HOMING_SPEED * TEST_HOMING_DIR, mock_motor_current_speed);
    TEST_ASSERT_TRUE(mock_motor_enabled);
    TEST_ASSERT_EQUAL_INT(1, mock_encoder_resetPosition_calls); // Encoder reset at start of homing
}

void test_homing_sequence_happy_path(void) {
    g_dome_controller_uut.findHome(); // -> HOMING_START
    run_updates(1); // -> HOMING_MOVING_TO_SWITCH
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_MOVING_TO_SWITCH, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, TEST_HOMING_SPEED * TEST_HOMING_DIR, mock_motor_current_speed);

    // Simulate moving and finding switch
    advance_mock_time_ms(1000); // Move for 1 sec
    mock_encoder_current_pulses += TEST_HOMING_SPEED * TEST_PPD / 10; // Simulate some encoder change
    mock_limit_switch_is_active = true;
    run_updates(1); // -> HOMING_FOUND_SWITCH_STOPPING
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_FOUND_SWITCH_STOPPING, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed); // Motor should be stopped

    run_updates( (MOTOR_STOP_TIME_MS / PID_SAMPLE_TIME_MS) + 1 ); // Wait for motor to stop -> HOMING_MOVING_OFF_SWITCH
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_MOVING_OFF_SWITCH, g_dome_controller_uut.getCurrentState());
    float expected_slow_speed = TEST_HOMING_SPEED > 20 ? TEST_HOMING_SPEED / 2.0f : (TEST_HOMING_SPEED > 10 ? 10.0f : TEST_HOMING_SPEED);
    if (expected_slow_speed < 5) expected_slow_speed = 5;
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected_slow_speed * -TEST_HOMING_DIR, mock_motor_current_speed); // Moving off slowly

    // Simulate moving off switch
    advance_mock_time_ms(500);
    mock_encoder_current_pulses -= expected_slow_speed * TEST_PPD / 20;
    mock_limit_switch_is_active = false;
    run_updates(1); // -> HOMING_STOPPING_OFF_SWITCH
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_STOPPING_OFF_SWITCH, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed);

    run_updates( (MOTOR_STOP_TIME_MS / PID_SAMPLE_TIME_MS) + 1 ); // -> HOMING_REAPPROACHING_SWITCH
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_REAPPROACHING_SWITCH, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, expected_slow_speed * TEST_HOMING_DIR, mock_motor_current_speed); // Re-approaching slowly

    // Simulate re-activating switch
    advance_mock_time_ms(500);
    mock_encoder_current_pulses += expected_slow_speed * TEST_PPD / 20;
    int32_t final_encoder_pos = mock_encoder_current_pulses; // Capture this for checking home offset
    mock_limit_switch_is_active = true;
    run_updates(1); // -> HOMING_FINAL_STOP
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_FINAL_STOP, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed);

    run_updates( (MOTOR_STOP_TIME_MS / PID_SAMPLE_TIME_MS) + 1 ); // -> HOMING_COMPLETE
    TEST_ASSERT_EQUAL(DomeControl::DomeState::HOMING_COMPLETE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_TRUE(g_dome_controller_uut.isHomed());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, TEST_HOME_AZ, g_dome_controller_uut.getCurrentAzimuth());
    // Check if _home_encoder_offset_pulses was set to final_encoder_pos
    // This requires access to internal state or a getter, or inferring from behavior.
    // For now, we trust isHomed and getCurrentAzimuth after homing.

    run_updates(1); // -> IDLE
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
}

void test_homing_timeout_moving_to_switch(void) {
    g_dome_controller_uut.findHome();
    run_updates(1); // To HOMING_MOVING_TO_SWITCH

    // Simulate timeout
    advance_mock_time_ms(HOMING_TIMEOUT_MS + 1000);
    run_updates(1);
    TEST_ASSERT_EQUAL(DomeControl::DomeState::ERROR_HOMING_TIMEOUT, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed); // Motor stopped
}


void test_slew_to_azimuth_when_not_homed(void) {
    TEST_ASSERT_FALSE(g_dome_controller_uut.isHomed());
    g_dome_controller_uut.slewToAzimuth(90.0f);
    // Should remain in IDLE and not move, log an error.
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_EQUAL_INT(0, mock_motor_setSpeed_calls); // No motor movement
}

void test_slew_to_azimuth_happy_path(void) {
    // First, complete homing
    test_homing_sequence_happy_path(); // This will leave UUT in IDLE and homed.
    TEST_ASSERT_TRUE(g_dome_controller_uut.isHomed());
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    mock_motor_setSpeed_calls = 0; // Reset after homing

    float target_az = 90.0f;
    g_dome_controller_uut.slewToAzimuth(target_az);
    TEST_ASSERT_EQUAL(DomeControl::DomeState::SLEWING_TO_AZIMUTH, g_dome_controller_uut.getCurrentState());

    // Simulate slewing for a while, PID should be active
    // Let's say current azimuth is 0 (home), target is 90.
    // Target encoder = current_encoder_at_home + (90 * PPD)
    // Error will be positive, motor speed should be positive.
    run_updates(5); // Run a few PID cycles
    TEST_ASSERT_GREATER_THAN_INT(0, mock_motor_setSpeed_calls);
    TEST_ASSERT_GREATER_THAN_FLOAT(0.0f, mock_motor_current_speed); // Should be moving forward

    // Simulate reaching target
    // Need to know _home_encoder_offset_pulses after homing to calculate this correctly.
    // From test_homing_sequence_happy_path, assume _home_encoder_offset_pulses is `final_encoder_pos`.
    // Let's assume `final_encoder_pos` was, for example, 1000 after homing.
    // So, `_home_encoder_offset_pulses = 1000`. `_current_encoder_pulses = 1000`. `current_az = 0`.
    // Target az 90 -> target_encoder_pulses = 1000 + (90 * TEST_PPD) = 1000 + 9000 = 10000.
    // This requires more careful setup of encoder value after homing or a getter for home_offset.
    // For simplicity, let's assume homing sets current encoder to 0 and home_offset to 0.
    reset_all_mocks_and_uut(); // Fresh start
    g_dome_controller_uut.syncToAzimuth(0.0f); // "Manually" home at encoder 0
    TEST_ASSERT_TRUE(g_dome_controller_uut.isHomed());
    mock_encoder_current_pulses = 0; // Set mock encoder after sync
    mock_motor_setSpeed_calls = 0;

    g_dome_controller_uut.slewToAzimuth(target_az);
    run_updates(1);
    TEST_ASSERT_EQUAL(DomeControl::DomeState::SLEWING_TO_AZIMUTH, g_dome_controller_uut.getCurrentState());

    // Simulate reaching the target
    mock_encoder_current_pulses = (int32_t)(target_az * TEST_PPD); // Exactly at target
    run_updates(1); // Should detect target reached
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed); // Motor stopped
    TEST_ASSERT_FLOAT_WITHIN(0.1f, target_az, g_dome_controller_uut.getCurrentAzimuth());
}

void test_abort_slew(void) {
    reset_all_mocks_and_uut();
    g_dome_controller_uut.syncToAzimuth(0.0f);
    mock_encoder_current_pulses = 0;

    g_dome_controller_uut.slewToAzimuth(180.0f);
    run_updates(1); // Start slewing
    TEST_ASSERT_EQUAL(DomeControl::DomeState::SLEWING_TO_AZIMUTH, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_NOT_EQUAL(0.0f, mock_motor_current_speed);

    g_dome_controller_uut.abortSlew();
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, mock_motor_current_speed); // Motor stopped
}

void test_sync_to_azimuth(void) {
    // Initial state: not homed, encoder at some arbitrary value
    mock_encoder_current_pulses = 5000;
    TEST_ASSERT_FALSE(g_dome_controller_uut.isHomed());

    float sync_az = 45.0f;
    g_dome_controller_uut.syncToAzimuth(sync_az);

    TEST_ASSERT_TRUE(g_dome_controller_uut.isHomed());
    TEST_ASSERT_EQUAL(DomeControl::DomeState::IDLE, g_dome_controller_uut.getCurrentState());
    TEST_ASSERT_FLOAT_WITHIN(0.1f, sync_az, g_dome_controller_uut.getCurrentAzimuth());
    // Check that _home_encoder_offset_pulses was correctly calculated.
    // If current_az = 45 (synced), home_az = 0 (default), current_enc = 5000
    // delta_from_defined_home = 45 - 0 = 45
    // expected_home_offset = 5000 - (45 * TEST_PPD) = 5000 - 4500 = 500
    // To verify this, we'd need a getter for _home_encoder_offset_pulses or observe behavior:
    // If we now set mock_encoder_current_pulses to 500 (the calculated offset), current azimuth should be TEST_HOME_AZ (0.0)
    mock_encoder_current_pulses = 500;
    run_updates(1); // To update internal current_azimuth based on new mock_encoder_current_pulses
    TEST_ASSERT_FLOAT_WITHIN(0.1f, TEST_HOME_AZ, g_dome_controller_uut.getCurrentAzimuth());
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_initial_state_is_idle);
    RUN_TEST(test_find_home_starts_homing_sequence);
    RUN_TEST(test_homing_sequence_happy_path);
    RUN_TEST(test_homing_timeout_moving_to_switch);
    RUN_TEST(test_slew_to_azimuth_when_not_homed);
    RUN_TEST(test_slew_to_azimuth_happy_path);
    RUN_TEST(test_abort_slew);
    RUN_TEST(test_sync_to_azimuth);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
