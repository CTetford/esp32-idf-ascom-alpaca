#include <unity.h>
#include "device_drivers/AlpacaDomeImpl.h"
#include "dome_control/DomeController.h"    // For DomeState enum and real class structure
#include "communication/EspNowManager.h" // For real class structure
#include "alpaca_server/api.h"           // For Alpaca error codes
#include "esp_idf_mocks.h"
#include "cJSON.h" // For creating JSON payloads for shutter status tests
#include <string.h>

// --- Mock Implementations for DomeController and EspNowManager ---
// These will be simplified versions focusing on the interface AlpacaDomeImpl uses.

// --- Mock DomeController ---
class MockDomeControllerForAlpaca : public DomeControl::DomeController {
public:
    // Need to provide dummy HAL objects for the base DomeController constructor
    // These dummies won't actually be used if we override all relevant methods.
    class DummyMotor : public HAL::Motor { public: DummyMotor() {} esp_err_t begin(gpio_num_t,gpio_num_t,gpio_num_t,mcpwm_unit_t,mcpwm_timer_t) override {return ESP_OK;} void enable(){} void disable(){} esp_err_t setSpeed(float) override {return ESP_OK;} esp_err_t stop() override {return ESP_OK;} };
    class DummyEncoder : public HAL::Encoder { public: DummyEncoder() {} esp_err_t begin(gpio_num_t,gpio_num_t,pcnt_unit_t,uint16_t) override {return ESP_OK;} esp_err_t getPosition(int32_t*c) override {*c=0; return ESP_OK;} esp_err_t resetPosition() override {return ESP_OK;} esp_err_t pause() override {return ESP_OK;} esp_err_t resume() override {return ESP_OK;} };
    class DummyLimitSwitch : public HAL::LimitSwitch { public: DummyLimitSwitch() {} esp_err_t begin(gpio_num_t,bool,bool,bool) override {return ESP_OK;} bool isActive() override {return false;} };

    static DummyMotor s_dummy_motor;
    static DummyEncoder s_dummy_encoder;
    static DummyLimitSwitch s_dummy_ls;

    MockDomeControllerForAlpaca() : DomeControl::DomeController(s_dummy_motor, s_dummy_encoder, s_dummy_ls) {}

    // Control variables for mock behavior
    float mock_current_azimuth = 0.0f;
    bool mock_is_at_home = false;
    bool mock_is_parked = false;
    bool mock_is_slewing = false;
    bool mock_is_homed = true; // Assume homed by default for many tests
    DomeControl::DomeState mock_current_state = DomeControl::DomeState::IDLE;

    // Call counters
    int calls_findHome = 0;
    int calls_slewToAzimuth = 0;
    float last_slew_target_az = 0.0f;
    int calls_park = 0;
    int calls_setParkPosition = 0;
    int calls_abortSlew = 0;
    int calls_syncToAzimuth = 0;
    float last_sync_target_az = 0.0f;

    // Override methods used by AlpacaDomeImpl
    float getCurrentAzimuth() const override { return mock_current_azimuth; }
    bool isAtHome() const override { return mock_is_at_home; }
    bool isParked() const override { return mock_is_parked; }
    bool isSlewing() const override { return mock_is_slewing; }
    bool isHomed() const override { return mock_is_homed; } // Crucial for many operations
    DomeControl::DomeState getCurrentState() const override { return mock_current_state; }
    const char* getCurrentStateStr() const override { return DomeControl::domeStateToString(mock_current_state); }

    void findHome() override { calls_findHome++; }
    void slewToAzimuth(float target_azimuth) override { calls_slewToAzimuth++; last_slew_target_az = target_azimuth; }
    void park() override { calls_park++; }
    void setParkPosition() override { calls_setParkPosition++; }
    void abortSlew() override { calls_abortSlew++; mock_is_slewing = false; } // Simulate slew stopping
    void syncToAzimuth(float current_azimuth) override { calls_syncToAzimuth++; last_sync_target_az = current_azimuth; mock_is_homed = true; /* Sync implies homed */ }
};
// Define static dummies
MockDomeControllerForAlpaca::DummyMotor MockDomeControllerForAlpaca::s_dummy_motor;
MockDomeControllerForAlpaca::DummyEncoder MockDomeControllerForAlpaca::s_dummy_encoder;
MockDomeControllerForAlpaca::DummyLimitSwitch MockDomeControllerForAlpaca::s_dummy_ls;


// --- Mock EspNowManager ---
class MockEspNowManager : public Communication::EspNowManager {
public:
    MockEspNowManager() {}

    int calls_sendShutterCommand_str = 0;
    std::string last_sent_command_json_str;
    int calls_sendShutterCommand_char = 0;
    std::string last_sent_command_char;

    Communication::ShutterStatusCallback registered_status_cb = nullptr;

    // Override methods used by AlpacaDomeImpl
    esp_err_t sendShutterCommand(const std::string& command_json) override {
        calls_sendShutterCommand_str++;
        last_sent_command_json_str = command_json;
        return ESP_OK;
    }
    esp_err_t sendShutterCommand(const char* command) override {
        calls_sendShutterCommand_char++;
        last_sent_command_char = command;
        // Simulate JSON creation for verification if needed, or just check command string
        return ESP_OK;
    }
    void registerShutterStatusCallback(Communication::ShutterStatusCallback callback) override {
        registered_status_cb = callback;
    }
    // Dummy begin/addPeer for constructor if base class needs them.
    // EspNowManager constructor is empty, begin/addPeer are separate.
};

// --- Global Mock Objects & UUT ---
MockDomeControllerForAlpaca g_mock_dome_ctrl;
MockEspNowManager g_mock_esp_now_mgr;

// AlpacaDomeImpl UUT - must be pointer to allow recreation if AlpacaServer::Device part of constructor is tricky
DeviceDrivers::AlpacaDomeImpl* g_alpaca_dome_uut = nullptr;

// --- Mock for esp_timer_get_time ---
static uint64_t mock_current_time_us_alpaca = 0;
extern "C" {
    uint64_t esp_timer_get_time() {
        return mock_current_time_us_alpaca;
    }
}
void advance_mock_time_ms_alpaca(uint32_t ms) {
    mock_current_time_us_alpaca += (uint64_t)ms * 1000;
}


// --- Test Helper Functions ---
void reset_mocks_and_uut() {
    // Reset MockDomeController state
    g_mock_dome_ctrl.mock_current_azimuth = 0.0f;
    g_mock_dome_ctrl.mock_is_at_home = false;
    g_mock_dome_ctrl.mock_is_parked = false;
    g_mock_dome_ctrl.mock_is_slewing = false;
    g_mock_dome_ctrl.mock_is_homed = true; // Default to homed for most tests
    g_mock_dome_ctrl.mock_current_state = DomeControl::DomeState::IDLE;
    g_mock_dome_ctrl.calls_findHome = 0;
    g_mock_dome_ctrl.calls_slewToAzimuth = 0;
    g_mock_dome_ctrl.calls_park = 0;
    g_mock_dome_ctrl.calls_setParkPosition = 0;
    g_mock_dome_ctrl.calls_abortSlew = 0;
    g_mock_dome_ctrl.calls_syncToAzimuth = 0;

    // Reset MockEspNowManager state
    g_mock_esp_now_mgr.calls_sendShutterCommand_str = 0;
    g_mock_esp_now_mgr.last_sent_command_json_str.clear();
    g_mock_esp_now_mgr.calls_sendShutterCommand_char = 0;
    g_mock_esp_now_mgr.last_sent_command_char.clear();
    g_mock_esp_now_mgr.registered_status_cb = nullptr;

    mock_current_time_us_alpaca = 0;

    // Recreate UUT
    if (g_alpaca_dome_uut) delete g_alpaca_dome_uut;
    g_alpaca_dome_uut = new DeviceDrivers::AlpacaDomeImpl(
        "test-dome", "Test Alpaca Dome", "Mock Driver Info", "0.1",
        g_mock_dome_ctrl, g_mock_esp_now_mgr
    );
    // Constructor of AlpacaDomeImpl calls esp_now_mgr.sendShutterCommand("status_request");
    // So reset those counts after construction if the test doesn't care about constructor's call.
    g_mock_esp_now_mgr.calls_sendShutterCommand_char = 0;
    g_mock_esp_now_mgr.last_sent_command_char.clear();
}


// --- Test Cases ---
void setUp(void) {
    reset_mocks_and_uut();
}

void tearDown(void) {
    delete g_alpaca_dome_uut;
    g_alpaca_dome_uut = nullptr;
}

// Common Device Methods
void test_get_connected(void) {
    bool connected;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_connected(&connected));
    TEST_ASSERT_TRUE(connected); // Default connected state

    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->set_connected(false));
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_connected(&connected));
    TEST_ASSERT_FALSE(connected);
    TEST_ASSERT_EQUAL_INT(1, g_mock_dome_ctrl.calls_abortSlew); // Setting disconnected should abort slew
}

// Dome Specific Getters
void test_get_azimuth_when_homed(void) {
    double az;
    g_mock_dome_ctrl.mock_is_homed = true;
    g_mock_dome_ctrl.mock_current_azimuth = 123.45f;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_azimuth(&az));
    TEST_ASSERT_DOUBLE_WITHIN(0.01, 123.45, az);
}

void test_get_azimuth_when_not_homed(void) {
    double az;
    g_mock_dome_ctrl.mock_is_homed = false;
    TEST_ASSERT_EQUAL(AlpacaServer::ALPACA_ERR_VALUE_NOT_SET, g_alpaca_dome_uut->get_azimuth(&az));
}

void test_get_athome(void) {
    bool at_home;
    g_mock_dome_ctrl.mock_is_at_home = true;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_athome(&at_home));
    TEST_ASSERT_TRUE(at_home);
}

void test_get_slewing(void) {
    bool slewing;
    g_mock_dome_ctrl.mock_is_slewing = true;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_slewing(&slewing));
    TEST_ASSERT_TRUE(slewing);
}

// Dome Specific Actions (PUT methods)
void test_put_findhome(void) {
    g_mock_dome_ctrl.mock_is_slewing = false;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->put_findhome());
    TEST_ASSERT_EQUAL_INT(1, g_mock_dome_ctrl.calls_findHome);
}

void test_put_findhome_when_slewing_returns_error(void) {
    g_mock_dome_ctrl.mock_is_slewing = true;
    TEST_ASSERT_EQUAL(AlpacaServer::ALPACA_ERR_INVALID_OPERATION, g_alpaca_dome_uut->put_findhome());
    TEST_ASSERT_EQUAL_INT(0, g_mock_dome_ctrl.calls_findHome);
}

void test_put_slewtoazimuth_when_homed(void) {
    g_mock_dome_ctrl.mock_is_homed = true;
    g_mock_dome_ctrl.mock_is_slewing = false;
    double target_az = 90.0;
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->put_slewtoazimuth(target_az));
    TEST_ASSERT_EQUAL_INT(1, g_mock_dome_ctrl.calls_slewToAzimuth);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, (float)target_az, g_mock_dome_ctrl.last_slew_target_az);
}

void test_put_slewtoazimuth_when_not_homed_returns_error(void) {
    g_mock_dome_ctrl.mock_is_homed = false;
    TEST_ASSERT_EQUAL(AlpacaServer::ALPACA_ERR_INVALID_OPERATION, g_alpaca_dome_uut->put_slewtoazimuth(45.0));
    TEST_ASSERT_EQUAL_INT(0, g_mock_dome_ctrl.calls_slewToAzimuth);
}

void test_put_abortslew(void) {
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->put_abortslew());
    TEST_ASSERT_EQUAL_INT(1, g_mock_dome_ctrl.calls_abortSlew);
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_now_mgr.calls_sendShutterCommand_char);
    TEST_ASSERT_EQUAL_STRING("stop", g_mock_esp_now_mgr.last_sent_command_char.c_str());
}

// Shutter methods
void test_put_openshutter(void) {
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->put_openshutter());
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_now_mgr.calls_sendShutterCommand_char);
    TEST_ASSERT_EQUAL_STRING("open", g_mock_esp_now_mgr.last_sent_command_char.c_str());
}

void test_put_closeshutter(void) {
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->put_closeshutter());
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_now_mgr.calls_sendShutterCommand_char);
    TEST_ASSERT_EQUAL_STRING("close", g_mock_esp_now_mgr.last_sent_command_char.c_str());
}

void test_get_shutterstatus_initial_is_error_requests_update(void) {
    AlpacaServer::Dome::ShutterState status;
    // Constructor already called sendShutterCommand("status_request")
    // Resetting counts to check this specific call
    g_mock_esp_now_mgr.calls_sendShutterCommand_char = 0;

    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_shutterstatus(&status));
    TEST_ASSERT_EQUAL(AlpacaServer::Dome::ShutterState::Error, status); // Initial state
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_now_mgr.calls_sendShutterCommand_char); // Requested update
    TEST_ASSERT_EQUAL_STRING("status_request", g_mock_esp_now_mgr.last_sent_command_char.c_str());
}

void test_get_shutterstatus_stale_requests_update(void) {
    AlpacaServer::Dome::ShutterState status;
    // Simulate some time passed
    advance_mock_time_ms_alpaca(10000); // 10 seconds
    g_mock_esp_now_mgr.calls_sendShutterCommand_char = 0; // Reset count

    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_shutterstatus(&status));
    TEST_ASSERT_EQUAL_INT(1, g_mock_esp_now_mgr.calls_sendShutterCommand_char); // Requested update
    TEST_ASSERT_EQUAL_STRING("status_request", g_mock_esp_now_mgr.last_sent_command_char.c_str());
}

void test_update_shutter_status_callback_parses_json(void) {
    TEST_ASSERT_NOT_NULL(g_mock_esp_now_mgr.registered_status_cb); // Callback should be registered by UUT constructor

    const char* json_payload = "{\"status\":\"open\", \"error_message\":\"none\"}";
    uint8_t dummy_mac[] = {1,2,3,4,5,6};
    // Call the registered callback (which is AlpacaDomeImpl::updateShutterStatus)
    g_mock_esp_now_mgr.registered_status_cb(dummy_mac, json_payload, strlen(json_payload));

    AlpacaServer::Dome::ShutterState status;
    g_mock_esp_now_mgr.calls_sendShutterCommand_char = 0; // Prevent get_shutterstatus from sending another request
    TEST_ASSERT_EQUAL(ESP_OK, g_alpaca_dome_uut->get_shutterstatus(&status));
    TEST_ASSERT_EQUAL(AlpacaServer::Dome::ShutterState::Open, status);
    TEST_ASSERT_EQUAL_INT(0, g_mock_esp_now_mgr.calls_sendShutterCommand_char); // No new request sent
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_get_connected);
    RUN_TEST(test_get_azimuth_when_homed);
    RUN_TEST(test_get_azimuth_when_not_homed);
    RUN_TEST(test_get_athome);
    RUN_TEST(test_get_slewing);
    RUN_TEST(test_put_findhome);
    RUN_TEST(test_put_findhome_when_slewing_returns_error);
    RUN_TEST(test_put_slewtoazimuth_when_homed);
    RUN_TEST(test_put_slewtoazimuth_when_not_homed_returns_error);
    RUN_TEST(test_put_abortslew);
    RUN_TEST(test_put_openshutter);
    RUN_TEST(test_put_closeshutter);
    RUN_TEST(test_get_shutterstatus_initial_is_error_requests_update);
    RUN_TEST(test_get_shutterstatus_stale_requests_update);
    RUN_TEST(test_update_shutter_status_callback_parses_json);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
