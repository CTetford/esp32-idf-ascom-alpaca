#include <unity.h>
#include "communication/EspNowManager.h"
#include "esp_idf_mocks.h"
#include "esp_now.h"
#include "esp_wifi.h"
#include "cJSON.h" // For verifying JSON payloads
#include <string.h> // For memcpy, memcmp

// --- Mock Control for ESP-NOW & WiFi ---
typedef struct {
    int call_count_wifi_init;
    int call_count_wifi_set_storage;
    int call_count_wifi_set_mode;
    int call_count_wifi_start;
    int call_count_wifi_set_channel;
    uint8_t set_channel_val;

    int call_count_esp_now_init;
    int call_count_esp_now_deinit;
    int call_count_esp_now_register_send_cb;
    int call_count_esp_now_register_recv_cb;
    int call_count_esp_now_add_peer;
    int call_count_esp_now_mod_peer;
    int call_count_esp_now_is_peer_exist;
    int call_count_esp_now_send;

    esp_now_send_cb_t send_cb;
    esp_now_recv_cb_t recv_cb;

    esp_now_peer_info_t added_peer;
    bool peer_exists_return_val;

    uint8_t last_sent_mac[ESP_NOW_ETH_ALEN];
    uint8_t last_sent_data[ESP_NOW_MAX_DATA_LEN];
    int last_sent_data_len;

} espnow_wifi_mock_state_t;

static espnow_wifi_mock_state_t mock_state;

// --- Mock ESP-IDF functions ---
extern "C" {

// WiFi Mocks
esp_err_t esp_wifi_init(const wifi_init_config_t *config) {
    mock_state.call_count_wifi_init++;
    return ESP_OK;
}
esp_err_t esp_wifi_set_storage(wifi_storage_t storage) {
    mock_state.call_count_wifi_set_storage++;
    return ESP_OK;
}
esp_err_t esp_wifi_set_mode(wifi_mode_t mode) {
    mock_state.call_count_wifi_set_mode++;
    return ESP_OK;
}
esp_err_t esp_wifi_start() {
    mock_state.call_count_wifi_start++;
    return ESP_OK;
}
esp_err_t esp_wifi_stop() { return ESP_OK; } // Not directly used by class, but good to have
esp_err_t esp_wifi_deinit() { return ESP_OK; }
esp_err_t esp_wifi_set_channel(uint8_t primary, wifi_second_chan_t second) {
    mock_state.call_count_wifi_set_channel++;
    mock_state.set_channel_val = primary;
    return ESP_OK;
}


// ESP-NOW Mocks
esp_err_t esp_now_init(void) {
    mock_state.call_count_esp_now_init++;
    return ESP_OK;
}
esp_err_t esp_now_deinit(void) {
    mock_state.call_count_esp_now_deinit++;
    return ESP_OK;
}
esp_err_t esp_now_register_send_cb(esp_now_send_cb_t cb) {
    mock_state.call_count_esp_now_register_send_cb++;
    mock_state.send_cb = cb;
    return ESP_OK;
}
esp_err_t esp_now_register_recv_cb(esp_now_recv_cb_t cb) {
    mock_state.call_count_esp_now_register_recv_cb++;
    mock_state.recv_cb = cb;
    return ESP_OK;
}
esp_err_t esp_now_unregister_send_cb(void) { return ESP_OK; }
esp_err_t esp_now_unregister_recv_cb(void) { return ESP_OK; }

esp_err_t esp_now_add_peer(const esp_now_peer_info_t *peer_info) {
    mock_state.call_count_esp_now_add_peer++;
    memcpy(&mock_state.added_peer, peer_info, sizeof(esp_now_peer_info_t));
    return ESP_OK;
}
esp_err_t esp_now_mod_peer(const esp_now_peer_info_t *peer_info) {
    mock_state.call_count_esp_now_mod_peer++;
    memcpy(&mock_state.added_peer, peer_info, sizeof(esp_now_peer_info_t));
    return ESP_OK;
}

bool esp_now_is_peer_exist(const uint8_t *peer_addr) {
    mock_state.call_count_esp_now_is_peer_exist++;
    // Check if peer_addr matches mock_state.added_peer.peer_addr if a peer was added
    if (mock_state.call_count_esp_now_add_peer > 0 || mock_state.call_count_esp_now_mod_peer > 0) {
        if (memcmp(peer_addr, mock_state.added_peer.peer_addr, ESP_NOW_ETH_ALEN) == 0) {
            return mock_state.peer_exists_return_val; // Controlled by test
        }
    }
    return false; // Default to not existing if not the one we "added"
}

esp_err_t esp_now_send(const uint8_t *peer_addr, const uint8_t *data, size_t len) {
    mock_state.call_count_esp_now_send++;
    if (peer_addr) memcpy(mock_state.last_sent_mac, peer_addr, ESP_NOW_ETH_ALEN);
    if (data && len < ESP_NOW_MAX_DATA_LEN) {
        memcpy(mock_state.last_sent_data, data, len);
        mock_state.last_sent_data[len] = '\0'; // Null terminate for string comparison in tests
    }
    mock_state.last_sent_data_len = len;
    return ESP_OK;
}

// cJSON Mocks (if we were testing cJSON usage itself, not strictly needed if we trust cJSON)
// For now, assume cJSON functions work as expected.

} // extern "C"


// --- Test Helper Functions ---
void reset_espnow_wifi_mock_state() {
    memset(&mock_state, 0, sizeof(espnow_wifi_mock_state_t));
}

// Test callback for shutter status
static bool test_callback_invoked = false;
static uint8_t test_received_mac[ESP_NOW_ETH_ALEN];
static char test_received_data_str[ESP_NOW_MAX_DATA_LEN + 1];
static int test_received_data_len = 0;

void test_shutter_status_cb(const uint8_t *mac_addr, const char *data, int len) {
    test_callback_invoked = true;
    if (mac_addr) memcpy(test_received_mac, mac_addr, ESP_NOW_ETH_ALEN);
    if (data && len < ESP_NOW_MAX_DATA_LEN) {
        memcpy(test_received_data_str, data, len);
        test_received_data_str[len] = '\0';
    }
    test_received_data_len = len;
}

void reset_test_callback_state() {
    test_callback_invoked = false;
    memset(test_received_mac, 0, ESP_NOW_ETH_ALEN);
    memset(test_received_data_str, 0, sizeof(test_received_data_str));
    test_received_data_len = 0;
}

// --- Test Cases ---
Communication::EspNowManager g_esp_now_uut; // Unit Under Test

void setUp(void) {
    reset_espnow_wifi_mock_state();
    reset_test_callback_state();
    // g_esp_now_uut = Communication::EspNowManager(); // Recreate if complex state
}

void tearDown(void) {
    // g_esp_now_uut.EspNowManager::~EspNowManager(); // Call destructor for deinit if needed.
    // The mock esp_now_deinit will be called if UUT destructor calls it.
}

void test_espnow_begin_initializes_wifi_and_espnow(void) {
    uint8_t test_channel = 6;
    TEST_ASSERT_EQUAL(ESP_OK, g_esp_now_uut.begin(test_channel));

    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_wifi_init);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_wifi_set_storage);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_wifi_set_mode);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_wifi_start);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_wifi_set_channel);
    TEST_ASSERT_EQUAL_UINT8(test_channel, mock_state.set_channel_val);

    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_init);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_register_send_cb);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_register_recv_cb);
    TEST_ASSERT_NOT_NULL(mock_state.send_cb);
    TEST_ASSERT_NOT_NULL(mock_state.recv_cb);
}

void test_espnow_add_new_peer(void) {
    g_esp_now_uut.begin(); // Default channel
    uint8_t peer_mac[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    mock_state.peer_exists_return_val = false; // Simulate peer does not exist

    TEST_ASSERT_EQUAL(ESP_OK, g_esp_now_uut.addPeer(peer_mac));
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_is_peer_exist);
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_add_peer);
    TEST_ASSERT_EQUAL_INT(0, mock_state.call_count_esp_now_mod_peer);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(peer_mac, mock_state.added_peer.peer_addr, ESP_NOW_ETH_ALEN);
    TEST_ASSERT_EQUAL_UINT8(mock_state.set_channel_val, mock_state.added_peer.channel); // Channel should match
}

void test_espnow_add_existing_peer_modifies_it(void) {
    g_esp_now_uut.begin();
    uint8_t peer_mac[] = {0x0A, 0x0B, 0x0C, 0x0D, 0x0E, 0x0F};
    // First "add" it so our mock knows about it for esp_now_is_peer_exist
    g_esp_now_uut.addPeer(peer_mac);
    mock_state.call_count_esp_now_add_peer = 0; // Reset for this specific check

    mock_state.peer_exists_return_val = true; // Simulate peer now exists

    TEST_ASSERT_EQUAL(ESP_OK, g_esp_now_uut.addPeer(peer_mac)); // Try to add again
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_is_peer_exist); // Called once for the second add
    TEST_ASSERT_EQUAL_INT(0, mock_state.call_count_esp_now_add_peer); // Not called again
    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_mod_peer); // Modify called
}


void test_espnow_send_shutter_command_string(void) {
    g_esp_now_uut.begin();
    uint8_t peer_mac[] = {0x11, 0x22, 0x33, 0x44, 0x55, 0x66};
    g_esp_now_uut.addPeer(peer_mac);

    std::string test_json_cmd = "{\"key\":\"value\"}";
    TEST_ASSERT_EQUAL(ESP_OK, g_esp_now_uut.sendShutterCommand(test_json_cmd));

    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_send);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(peer_mac, mock_state.last_sent_mac, ESP_NOW_ETH_ALEN);
    TEST_ASSERT_EQUAL_STRING(test_json_cmd.c_str(), (const char*)mock_state.last_sent_data);
    TEST_ASSERT_EQUAL_INT(test_json_cmd.length(), mock_state.last_sent_data_len);
}

void test_espnow_send_shutter_command_char_ptr_builds_json(void) {
    g_esp_now_uut.begin();
    uint8_t peer_mac[] = {0x1A, 0x2B, 0x3C, 0x4D, 0x5E, 0x6F};
    g_esp_now_uut.addPeer(peer_mac);

    const char* cmd = "open";
    TEST_ASSERT_EQUAL(ESP_OK, g_esp_now_uut.sendShutterCommand(cmd));

    TEST_ASSERT_EQUAL_INT(1, mock_state.call_count_esp_now_send);

    // Verify JSON payload
    cJSON *root = cJSON_Parse((const char*)mock_state.last_sent_data);
    TEST_ASSERT_NOT_NULL(root);
    cJSON *cmd_item = cJSON_GetObjectItemCaseSensitive(root, "command");
    TEST_ASSERT_NOT_NULL(cmd_item);
    TEST_ASSERT_TRUE(cJSON_IsString(cmd_item));
    TEST_ASSERT_EQUAL_STRING(cmd, cmd_item->valuestring);
    cJSON_Delete(root);
}

void test_espnow_receive_callback_fires(void) {
    g_esp_now_uut.begin();
    g_esp_now_uut.registerShutterStatusCallback(test_shutter_status_cb);
    TEST_ASSERT_NOT_NULL(mock_state.recv_cb); // Check if EspNowManager registered its static cb

    // Simulate ESP-NOW receiving data by directly calling the static recv_cb
    // that EspNowManager registered with the ESP-NOW IDF mock.
    uint8_t source_mac[] = {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF};
    const char* received_payload = "{\"status\":\"closed\"}";
    int payload_len = strlen(received_payload);

    // This calls EspNowManager::onDataRecv (static)
    mock_state.recv_cb(source_mac, (const uint8_t*)received_payload, payload_len);

    TEST_ASSERT_TRUE(test_callback_invoked);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(source_mac, test_received_mac, ESP_NOW_ETH_ALEN);
    TEST_ASSERT_EQUAL_STRING(received_payload, test_received_data_str);
    TEST_ASSERT_EQUAL_INT(payload_len, test_received_data_len);
}


// --- Test Runner ---
void app_main_test_runner(void) {
    UNITY_BEGIN();
    RUN_TEST(test_espnow_begin_initializes_wifi_and_espnow);
    RUN_TEST(test_espnow_add_new_peer);
    RUN_TEST(test_espnow_add_existing_peer_modifies_it);
    RUN_TEST(test_espnow_send_shutter_command_string);
    RUN_TEST(test_espnow_send_shutter_command_char_ptr_builds_json);
    RUN_TEST(test_espnow_receive_callback_fires);
    UNITY_END();
}

extern "C" void app_main() {
    vTaskDelay(pdMS_TO_TICKS(2000));
    app_main_test_runner();
}
