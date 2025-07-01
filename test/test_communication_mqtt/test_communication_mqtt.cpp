#include <unity.h>
#include "communication/MqttClientWrapper.h"
#include "dome_control/DomeController.h" // For DomeController type
#include "esp_idf_mocks.h"
#include "mqtt_client.h"
#include "esp_system.h" // For esp_reboot
#include <string.h>      // For strcmp, strncpy
#include <vector>
#include <map>

// --- Mock DomeController ---
// We need a mock DomeController to provide status data for MQTT publishing.
class MockDomeController {
public:
    float current_azimuth = 0.0f;
    bool at_home = false;
    bool slewing = false;
    bool parked = false;
    const char* state_str = "IDLE";

    // Methods that MqttClientWrapper calls
    float getCurrentAzimuth() const { return current_azimuth; }
    bool isAtHome() const { return at_home; }
    bool isSlewing() const { return slewing; }
    bool isParked() const { return parked; }
    const char* getCurrentStateStr() const { return state_str; }

    // Dummy methods to satisfy linkage if MqttClientWrapper included full DomeController.h
    // and not just forward declaration. These won't be called by MqttClientWrapper.
    MockDomeController(HAL::Motor& m, HAL::Encoder& e, HAL::LimitSwitch& l) {} // Dummy constructor
    void findHome() {}
    void slewToAzimuth(float target_azimuth) {}
    // ... other methods
};


// --- Mock Control for ESP-MQTT ---
typedef struct {
    int call_count_init;
    int call_count_register_event;
    int call_count_start;
    int call_count_stop;
    int call_count_destroy;
    int call_count_publish;
    int call_count_subscribe;

    esp_mqtt_client_config_t last_config;
    esp_event_handler_t event_handler; // The one registered with esp_mqtt_client_register_event
    void* event_handler_arg; // Argument passed to esp_mqtt_client_register_event

    std::string last_published_topic;
    std::string last_published_payload;
    int last_published_qos;
    int last_published_retain;

    std::string last_subscribed_topic;
    int last_subscribed_qos;

    // Store all published messages for more detailed verification
    std::vector<std::pair<std::string, std::string>> all_published_messages;


} esp_mqtt_mock_state_t;

static esp_mqtt_mock_state_t mock_mqtt_state;
static esp_mqtt_client_handle_t mock_client_handle = (esp_mqtt_client_handle_t)0x12345678; // Dummy handle

// --- Mock for esp_reboot ---
static int mock_esp_reboot_calls = 0;
extern "C" void esp_reboot(void) {
    mock_esp_reboot_calls++;
}

// --- Mock ESP-IDF MQTT Client functions ---
extern "C" {

esp_mqtt_client_handle_t esp_mqtt_client_init(const esp_mqtt_client_config_t *config) {
    mock_mqtt_state.call_count_init++;
    mock_mqtt_state.last_config = *config;
    return mock_client_handle;
}

esp_err_t esp_mqtt_client_register_event(esp_mqtt_client_handle_t client, esp_mqtt_event_id_t event,
                                       esp_event_handler_t event_handler, void* event_handler_arg) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_register_event++;
    mock_mqtt_state.event_handler = event_handler;
    mock_mqtt_state.event_handler_arg = event_handler_arg; // Should be client handle
    return ESP_OK;
}

esp_err_t esp_mqtt_client_start(esp_mqtt_client_handle_t client) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_start++;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_stop(esp_mqtt_client_handle_t client) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_stop++;
    return ESP_OK;
}

esp_err_t esp_mqtt_client_destroy(esp_mqtt_client_handle_t client) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_destroy++;
    return ESP_OK;
}

int esp_mqtt_client_publish(esp_mqtt_client_handle_t client, const char *topic, const char *data,
                            int len, int qos, int retain) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_publish++;
    mock_mqtt_state.last_published_topic = topic ? topic : "";
    // If len is 0, data might be NULL, but c_str() on empty string is fine.
    // If len > 0, data is payload. If len is 0, payload is effectively empty.
    mock_mqtt_state.last_published_payload = data ? std::string(data, (len == 0 && data[0] == '\0') ? 0 : (len == 0 ? strlen(data) : len) ) : "";
    mock_mqtt_state.last_published_qos = qos;
    mock_mqtt_state.last_published_retain = retain;
    mock_mqtt_state.all_published_messages.push_back({mock_mqtt_state.last_published_topic, mock_mqtt_state.last_published_payload});
    return 99; // Dummy message ID
}

int esp_mqtt_client_subscribe(esp_mqtt_client_handle_t client, const char *topic, int qos) {
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, client);
    mock_mqtt_state.call_count_subscribe++;
    mock_mqtt_state.last_subscribed_topic = topic ? topic : "";
    mock_mqtt_state.last_subscribed_qos = qos;
    return 100; // Dummy message ID
}

} // extern "C"


// --- Global Test Objects & Helper Functions ---
// Dummy HAL objects for MockDomeController constructor (not actually used by its mocked methods)
HAL::Motor g_dummy_motor_for_mock_dome;
HAL::Encoder g_dummy_encoder_for_mock_dome;
HAL::LimitSwitch g_dummy_ls_for_mock_dome;

MockDomeController g_mock_dome_controller(g_dummy_motor_for_mock_dome, g_dummy_encoder_for_mock_dome, g_dummy_ls_for_mock_dome);
Communication::MqttClientWrapper g_mqtt_uut; // Unit Under Test

static bool test_reboot_callback_invoked = false;
void test_reboot_cb() {
    test_reboot_callback_invoked = true;
}

void reset_all_mqtt_mocks_and_uut() {
    memset(&mock_mqtt_state, 0, sizeof(esp_mqtt_mock_state_t));
    mock_mqtt_state.all_published_messages.clear();
    mock_esp_reboot_calls = 0;
    test_reboot_callback_invoked = false;

    // Reset internal state of UUT if possible, or recreate.
    // MqttClientWrapper's destructor calls stop(), which calls destroy().
    // If g_mqtt_uut is global, its destructor runs at end.
    // For isolated tests, better to new/delete or have a reinit method.
    // For now, rely on global and ensure stop() is tested to reset mock counts if needed.
    // Communication::MqttClientWrapper new_uut; // This would require _instance to be updated in UUT.
    // g_mqtt_uut = new_uut; // Not possible with global.
    // Simplest for now: assume tests don't interact badly through shared UUT state if begin() is called.
}


// --- Test Cases ---
void setUp(void) {
    reset_all_mqtt_mocks_and_uut();
}

void tearDown(void) {
    if (g_mqtt_uut.isConnected()) { // Ensure client is stopped to avoid issues between tests.
         g_mqtt_uut.stop(); // This will call mock stop/destroy
    }
}

void test_mqtt_begin_initializes_and_starts_client(void) {
    TEST_ASSERT_EQUAL(ESP_OK, g_mqtt_uut.begin("mqtt://testbroker", "client1", "user", "pass", "/OCS/DomeTest", &g_mock_dome_controller));
    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_init);
    TEST_ASSERT_EQUAL_STRING("mqtt://testbroker", mock_mqtt_state.last_config.broker.address.uri);
    TEST_ASSERT_EQUAL_STRING("client1", mock_mqtt_state.last_config.credentials.client_id);
    TEST_ASSERT_EQUAL_STRING("user", mock_mqtt_state.last_config.credentials.username);
    // TEST_ASSERT_EQUAL_STRING("pass", mock_mqtt_state.last_config.credentials.authentication.password); // Password field name might differ by IDF version

    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_register_event);
    TEST_ASSERT_NOT_NULL(mock_mqtt_state.event_handler);
    TEST_ASSERT_EQUAL_PTR(mock_client_handle, mock_mqtt_state.event_handler_arg); // event_handler_arg is client

    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_start);
}

void test_mqtt_stop_stops_and_destroys_client(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller); // Initialize first
    mock_mqtt_state.call_count_stop = 0; // Reset after begin
    mock_mqtt_state.call_count_destroy = 0;

    g_mqtt_uut.stop();
    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_stop);
    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_destroy);
    TEST_ASSERT_FALSE(g_mqtt_uut.isConnected());
}

void test_mqtt_publish_status_when_not_connected(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller);
    // Don't simulate MQTT_EVENT_CONNECTED, so isConnected() remains false

    TEST_ASSERT_EQUAL(ESP_ERR_INVALID_STATE, g_mqtt_uut.publishStatus());
    TEST_ASSERT_EQUAL_INT(0, mock_mqtt_state.call_count_publish);
}

void test_mqtt_publish_status_when_connected(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller);

    // Simulate MQTT_EVENT_CONNECTED by directly calling the event handler
    esp_mqtt_event_t connect_event = {};
    connect_event.event_id = MQTT_EVENT_CONNECTED;
    connect_event.client = mock_client_handle; // Pass the client handle
    mock_mqtt_state.event_handler(&connect_event); // This should set _connected = true in UUT
    TEST_ASSERT_TRUE(g_mqtt_uut.isConnected());

    // Setup mock dome controller state
    g_mock_dome_controller.current_azimuth = 90.55f;
    g_mock_dome_controller.at_home = false;
    g_mock_dome_controller.slewing = true;
    g_mock_dome_controller.parked = false;
    g_mock_dome_controller.state_str = "SLEWING";

    TEST_ASSERT_EQUAL(ESP_OK, g_mqtt_uut.publishStatus());
    // Expect 5 publishes: azimuth, athome, slewing, parked, dome_state
    TEST_ASSERT_EQUAL_INT(5, mock_mqtt_state.call_count_publish);

    // Verify one of the publishes (e.g., azimuth)
    bool found_azimuth = false;
    for (const auto& msg : mock_mqtt_state.all_published_messages) {
        if (msg.first == "/OCS/DomeTest/azimuth") {
            found_azimuth = true;
            char expected_payload[10];
            snprintf(expected_payload, sizeof(expected_payload), "%.2f", 90.55f);
            TEST_ASSERT_EQUAL_STRING(expected_payload, msg.second.c_str());
            break;
        }
    }
    TEST_ASSERT_TRUE(found_azimuth);

    bool found_state = false;
    for (const auto& msg : mock_mqtt_state.all_published_messages) {
        if (msg.first == "/OCS/DomeTest/dome_state") {
            found_state = true;
            TEST_ASSERT_EQUAL_STRING("SLEWING", msg.second.c_str());
            break;
        }
    }
    TEST_ASSERT_TRUE(found_state);
}

void test_mqtt_event_connected_subscribes_to_reboot(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller);

    esp_mqtt_event_t event = {};
    event.event_id = MQTT_EVENT_CONNECTED;
    event.client = mock_client_handle;
    mock_mqtt_state.event_handler(&event); // Call the event handler

    TEST_ASSERT_TRUE(g_mqtt_uut.isConnected());
    TEST_ASSERT_EQUAL_INT(1, mock_mqtt_state.call_count_subscribe);
    TEST_ASSERT_EQUAL_STRING("/OCS/DomeTest/command/reboot", mock_mqtt_state.last_subscribed_topic.c_str());
}

void test_mqtt_event_data_on_reboot_topic_calls_reboot_cb(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller);
    g_mqtt_uut.registerRebootCommandCallback(test_reboot_cb);

    std::string reboot_topic = "/OCS/DomeTest/command/reboot";
    std::string reboot_payload = "reboot";

    esp_mqtt_event_t event = {};
    event.event_id = MQTT_EVENT_DATA;
    event.client = mock_client_handle;
    event.topic = (char*)reboot_topic.c_str();
    event.topic_len = reboot_topic.length();
    event.data = (char*)reboot_payload.c_str();
    event.data_len = reboot_payload.length();

    mock_mqtt_state.event_handler(&event); // Call the event handler

    TEST_ASSERT_TRUE(test_reboot_callback_invoked);
    TEST_ASSERT_EQUAL_INT(0, mock_esp_reboot_calls); // Custom callback called, not default reboot
}

void test_mqtt_event_data_on_reboot_topic_calls_default_reboot(void) {
    g_mqtt_uut.begin("mqtt://testbroker", "client1", "", "", "/OCS/DomeTest", &g_mock_dome_controller);
    // No custom reboot callback registered

    std::string reboot_topic = "/OCS/DomeTest/command/reboot";
    std::string reboot_payload = "1"; // Another valid payload

    esp_mqtt_event_t event = {};
    event.event_id = MQTT_EVENT_DATA;
    event.client = mock_client_handle;
    event.topic = (char*)reboot_topic.c_str();
    event.topic_len = reboot_topic.length();
    event.data = (char*)reboot_payload.c_str();
    event.data_len = reboot_payload.length();

    mock_mqtt_state.event_handler(&event);

    TEST_ASSERT_FALSE(test_reboot_callback_invoked); // No custom CB
    TEST_ASSERT_EQUAL_INT(1, mock_esp_reboot_calls); // Default esp_reboot mock called
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_mqtt_begin_initializes_and_starts_client);
    RUN_TEST(test_mqtt_stop_stops_and_destroys_client);
    RUN_TEST(test_mqtt_publish_status_when_not_connected);
    RUN_TEST(test_mqtt_publish_status_when_connected);
    RUN_TEST(test_mqtt_event_connected_subscribes_to_reboot);
    RUN_TEST(test_mqtt_event_data_on_reboot_topic_calls_reboot_cb);
    RUN_TEST(test_mqtt_event_data_on_reboot_topic_calls_default_reboot);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
