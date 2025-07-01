#include "EspNowManager.h"
#include "esp_wifi.h" // Required for esp_wifi_init, esp_wifi_start, esp_wifi_set_channel
#include "esp_log.h"
#include "string.h" // For memcpy in C-style callbacks
#include <vector>
#include "cJSON.h" // For creating JSON commands

static const char *TAG = "EspNowManager";

namespace Communication {

// Initialize static member
ShutterStatusCallback EspNowManager::_status_callback = nullptr;

EspNowManager::EspNowManager() :
    _peer_added(false),
    _initialized(false),
    _esp_now_channel(1) { // Default channel
    memset(&_shutter_peer, 0, sizeof(_shutter_peer));
}

EspNowManager::~EspNowManager() {
    if (_initialized) {
        esp_now_deinit();
        ESP_LOGI(TAG, "ESP-NOW de-initialized.");
    }
}

esp_err_t EspNowManager::begin(uint8_t channel) {
    if (_initialized) {
        ESP_LOGW(TAG, "ESP-NOW already initialized.");
        return ESP_OK;
    }
    _esp_now_channel = channel;

    ESP_LOGI(TAG, "Initializing ESP-NOW on channel %d...", _esp_now_channel);

    // ESP-NOW requires WiFi to be started.
    // Initialize WiFi in station mode (or AP mode, but station is common for this)
    // This doesn't connect to an AP, just starts the WiFi hardware.
    // If WiFi is already managed elsewhere (e.g. for MQTT), this might need adjustment
    // or to rely on that initialization. For now, assume we manage it here minimally.

    // Check current WiFi status if needed. For simplicity, we proceed with init.
    // Minimal WiFi setup for ESP-NOW:
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_err_t ret = esp_wifi_init(&cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_set_storage(WIFI_STORAGE_RAM);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_storage() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set mode to STA. ESP-NOW can work in STA, AP, or STA+AP modes.
    ret = esp_wifi_set_mode(WIFI_MODE_STA);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode(STA) failed: %s", esp_err_to_name(ret));
        return ret;
    }

    ret = esp_wifi_start();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start() failed: %s", esp_err_to_name(ret));
        return ret;
    }

    // Set the specific channel for ESP-NOW
    // This should be done after esp_wifi_start()
    // The primary channel must be set before esp_now_init()
    ret = esp_wifi_set_channel(_esp_now_channel, WIFI_SECOND_CHAN_NONE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_channel(%d) failed: %s", _esp_now_channel, esp_err_to_name(ret));
        // esp_wifi_stop(); // Clean up WiFi if channel set fails
        // esp_wifi_deinit();
        return ret;
    } else {
        ESP_LOGI(TAG, "WiFi channel set to %d", _esp_now_channel);
    }


    // Initialize ESP-NOW
    ret = esp_now_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW Init failed: %s", esp_err_to_name(ret));
        // esp_wifi_stop(); // Clean up WiFi if ESP-NOW init fails
        // esp_wifi_deinit();
        return ret;
    }
    ESP_LOGI(TAG, "ESP-NOW initialized successfully.");

    // Register send and receive callbacks
    ret = esp_now_register_send_cb(EspNowManager::onDataSent);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW send CB: %s", esp_err_to_name(ret));
        esp_now_deinit();
        // esp_wifi_stop();
        // esp_wifi_deinit();
        return ret;
    }

    ret = esp_now_register_recv_cb(EspNowManager::onDataRecv);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register ESP-NOW recv CB: %s", esp_err_to_name(ret));
        esp_now_unregister_send_cb();
        esp_now_deinit();
        // esp_wifi_stop();
        // esp_wifi_deinit();
        return ret;
    }

    _initialized = true;
    ESP_LOGI(TAG, "ESP-NOW callbacks registered.");
    return ESP_OK;
}

esp_err_t EspNowManager::addPeer(const uint8_t* mac_address) {
    if (!_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized. Call begin() first.");
        return ESP_ERR_INVALID_STATE;
    }
    if (mac_address == nullptr) {
        ESP_LOGE(TAG, "Invalid MAC address (null).");
        return ESP_ERR_INVALID_ARG;
    }

    memcpy(_shutter_peer.peer_addr, mac_address, ESP_NOW_ETH_ALEN);
    _shutter_peer.channel = _esp_now_channel; // Use the channel set during begin()
    _shutter_peer.encrypt = false;          // No encryption for simplicity, can be enabled
    _shutter_peer.ifidx = WIFI_IF_STA;      // Use STA interface

    // Check if peer already exists
    if (esp_now_is_peer_exist(mac_address)) {
        ESP_LOGI(TAG, "Peer %02X:%02X:%02X:%02X:%02X:%02X already exists. Modifying.",
                 mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5]);
        esp_err_t ret = esp_now_mod_peer(&_shutter_peer);
         if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to modify peer: %s", esp_err_to_name(ret));
            _peer_added = false;
        } else {
            ESP_LOGI(TAG, "Peer modified successfully.");
            _peer_added = true;
        }
        return ret;

    } else {
        esp_err_t ret = esp_now_add_peer(&_shutter_peer);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to add ESP-NOW peer %02X:%02X:%02X:%02X:%02X:%02X : %s",
                     mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5],
                     esp_err_to_name(ret));
            _peer_added = false;
        } else {
            ESP_LOGI(TAG, "ESP-NOW peer %02X:%02X:%02X:%02X:%02X:%02X added successfully on channel %d.",
                     mac_address[0], mac_address[1], mac_address[2], mac_address[3], mac_address[4], mac_address[5], _esp_now_channel);
            _peer_added = true;
        }
        return ret;
    }
}


esp_err_t EspNowManager::sendShutterCommand(const std::string& command_json) {
    if (!_initialized) {
        ESP_LOGE(TAG, "ESP-NOW not initialized for send.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!_peer_added) {
        ESP_LOGE(TAG, "No peer added to send command to.");
        return ESP_ERR_NOT_FOUND; // Or ESP_ERR_ESPNOW_PEER_NOT_EXIST
    }
    if (command_json.length() > ESP_NOW_MAX_DATA_LEN) {
        ESP_LOGE(TAG, "Command JSON too long (len: %d, max: %d)", command_json.length(), ESP_NOW_MAX_DATA_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    esp_err_t ret = esp_now_send(_shutter_peer.peer_addr, (const uint8_t*)command_json.c_str(), command_json.length());
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "ESP-NOW send failed: %s", esp_err_to_name(ret));
    } else {
        ESP_LOGD(TAG, "ESP-NOW command sent: %s", command_json.c_str());
    }
    return ret;
}

esp_err_t EspNowManager::sendShutterCommand(const char* command_str) {
    if (command_str == nullptr) return ESP_ERR_INVALID_ARG;

    cJSON *root = cJSON_CreateObject();
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to create cJSON object for command.");
        return ESP_FAIL;
    }
    cJSON_AddStringToObject(root, "command", command_str);

    char *json_string = cJSON_PrintUnformatted(root);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to print cJSON object to string.");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    std::string cmd_payload = json_string;
    cJSON_free(json_string); // cJSON_Print allocates memory
    cJSON_Delete(root);

    return sendShutterCommand(cmd_payload);
}


void EspNowManager::registerShutterStatusCallback(ShutterStatusCallback callback) {
    _status_callback = callback;
    ESP_LOGI(TAG, "Shutter status callback registered.");
}

// Static callback handlers
void EspNowManager::onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status) {
    if (mac_addr == NULL) {
        ESP_LOGE(TAG, "Send CB: MAC address is NULL");
        return;
    }
    ESP_LOGD(TAG, "ESP-NOW Send CB to %02X:%02X:%02X:%02X:%02X:%02X, Status: %s",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
             (status == ESP_NOW_SEND_SUCCESS) ? "Success" : "Fail");
    // Add retry logic or error handling if needed
}

void EspNowManager::onDataRecv(const uint8_t *mac_addr, const uint8_t *incoming_data, int len) {
     if (mac_addr == NULL || incoming_data == NULL || len <= 0) {
        ESP_LOGE(TAG, "Recv CB: Invalid arguments (mac_addr: %p, data: %p, len: %d)", mac_addr, incoming_data, len);
        return;
    }

    // Create a null-terminated string from the received data for safety
    // Max len ESP_NOW_MAX_DATA_LEN (250)
    char* data_str = (char*)malloc(len + 1);
    if (!data_str) {
        ESP_LOGE(TAG, "Recv CB: Failed to allocate memory for data string.");
        return;
    }
    memcpy(data_str, incoming_data, len);
    data_str[len] = '\0';

    ESP_LOGD(TAG, "ESP-NOW Recv CB from %02X:%02X:%02X:%02X:%02X:%02X, Len: %d, Data: %s",
             mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5],
             len, data_str);

    if (_status_callback) {
        // The callback expects a const char* that is null-terminated.
        _status_callback(mac_addr, data_str, len);
    } else {
        ESP_LOGW(TAG, "No shutter status callback registered to handle received data.");
    }
    free(data_str);
}


} // namespace Communication
