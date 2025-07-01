#include "MqttClientWrapper.h"
#include "../dome_control/DomeController.h" // Required for DomeController methods and DomeState
#include "esp_log.h"
#include "esp_system.h" // For esp_reboot
#include <string>       // For std::to_string

static const char *TAG = "MqttClientWrapper";

namespace Communication {

// Initialize static members
RebootCommandCallback MqttClientWrapper::_reboot_callback = nullptr;
MqttClientWrapper* MqttClientWrapper::_instance = nullptr;


MqttClientWrapper::MqttClientWrapper() :
    _client(nullptr),
    _initialized(false),
    _connected(false),
    _dome_controller_ptr(nullptr) {
    _instance = this; // Initialize static instance pointer
}

MqttClientWrapper::~MqttClientWrapper() {
    stop();
    _instance = nullptr;
}

esp_err_t MqttClientWrapper::begin(const std::string& broker_uri,
                                     const std::string& client_id,
                                     const std::string& username,
                                     const std::string& password,
                                     const std::string& base_topic,
                                     DomeControl::DomeController* dome_controller) {
    if (_initialized) {
        ESP_LOGW(TAG, "MQTT client already initialized.");
        return ESP_OK;
    }

    ESP_LOGI(TAG, "Initializing MQTT client. Broker: %s, ClientID: %s", broker_uri.c_str(), client_id.c_str());

    _broker_uri = broker_uri;
    _client_id = client_id;
    _username = username;
    _password = password;
    _dome_controller_ptr = dome_controller;

    // Construct specific topics
    _status_topic_str = base_topic + "/status_json"; // Publish a single JSON with all status
    _azimuth_topic_str = base_topic + "/azimuth";
    _athome_topic_str = base_topic + "/at_home";
    _slewing_topic_str = base_topic + "/slewing";
    _parked_topic_str = base_topic + "/parked";
    _dome_state_topic_str = base_topic + "/dome_state";
    _shutter_status_topic_str = base_topic + "/shutter_status"; // Placeholder if needed
    _reboot_cmd_topic_str = base_topic + "/command/reboot";


    esp_mqtt_client_config_t mqtt_cfg = {}; // Important to zero-initialize
    mqtt_cfg.broker.address.uri = _broker_uri.c_str();
    mqtt_cfg.credentials.client_id = _client_id.c_str();
    if (!_username.empty()) {
        mqtt_cfg.credentials.username = _username.c_str();
    }
    if (!_password.empty()) {
         mqtt_cfg.credentials.authentication.password = _password.c_str();
    }
    // mqtt_cfg.event_handle = MqttClientWrapper::mqttEventHandler; // Deprecated
    // mqtt_cfg.user_context = this; // Pass instance to handler if not using static _instance

    _client = esp_mqtt_client_init(&mqtt_cfg);
    if (!_client) {
        ESP_LOGE(TAG, "Failed to initialize MQTT client.");
        return ESP_FAIL;
    }

    esp_err_t ret = esp_mqtt_client_register_event(_client,
                                MQTT_EVENT_ANY, // Or specify events: MQTT_EVENT_CONNECTED, MQTT_EVENT_DISCONNECTED, MQTT_EVENT_DATA etc.
                                MqttClientWrapper::mqttEventHandler,
                                _client); // Pass client handle as event_data to handler
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to register MQTT event handler: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(_client);
        _client = nullptr;
        return ret;
    }

    ret = esp_mqtt_client_start(_client);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(ret));
        esp_mqtt_client_destroy(_client); // Clean up client if start fails
        _client = nullptr;
        return ret;
    }

    _initialized = true;
    ESP_LOGI(TAG, "MQTT client started.");
    return ESP_OK;
}

void MqttClientWrapper::stop() {
    if (_client && _initialized) {
        ESP_LOGI(TAG, "Stopping MQTT client.");
        esp_mqtt_client_stop(_client);
        esp_mqtt_client_destroy(_client);
        _client = nullptr;
        _initialized = false;
        _connected = false;
    }
}

bool MqttClientWrapper::isConnected() const {
    return _connected;
}

esp_err_t MqttClientWrapper::publishStatus() {
    if (!_initialized || !_client || !_connected) {
        ESP_LOGW(TAG, "Cannot publish status: MQTT not initialized or not connected.");
        return ESP_ERR_INVALID_STATE;
    }
    if (!_dome_controller_ptr) {
        ESP_LOGE(TAG, "Cannot publish status: DomeController pointer is null.");
        return ESP_FAIL;
    }

    // Fetch status from DomeController
    float current_az = _dome_controller_ptr->getCurrentAzimuth();
    bool at_home = _dome_controller_ptr->isAtHome();
    bool slewing = _dome_controller_ptr->isSlewing();
    bool parked = _dome_controller_ptr->isParked();
    const char* dome_state_str = _dome_controller_ptr->getCurrentStateStr();

    int msg_id;

    // Publish individual topics
    char buffer[20]; // For float/bool to string conversion
    snprintf(buffer, sizeof(buffer), "%.2f", current_az);
    msg_id = esp_mqtt_client_publish(_client, _azimuth_topic_str.c_str(), buffer, 0, 0, 0); // QoS 0, retain 0
    ESP_LOGD(TAG, "Published to %s (msg_id=%d): %s", _azimuth_topic_str.c_str(), msg_id, buffer);

    msg_id = esp_mqtt_client_publish(_client, _athome_topic_str.c_str(), at_home ? "true" : "false", 0, 0, 0);
    ESP_LOGD(TAG, "Published to %s (msg_id=%d): %s", _athome_topic_str.c_str(), msg_id, at_home ? "true" : "false");

    msg_id = esp_mqtt_client_publish(_client, _slewing_topic_str.c_str(), slewing ? "true" : "false", 0, 0, 0);
    ESP_LOGD(TAG, "Published to %s (msg_id=%d): %s", _slewing_topic_str.c_str(), msg_id, slewing ? "true" : "false");

    msg_id = esp_mqtt_client_publish(_client, _parked_topic_str.c_str(), parked ? "true" : "false", 0, 0, 0);
    ESP_LOGD(TAG, "Published to %s (msg_id=%d): %s", _parked_topic_str.c_str(), msg_id, parked ? "true" : "false");

    msg_id = esp_mqtt_client_publish(_client, _dome_state_topic_str.c_str(), dome_state_str, 0, 0, 0);
    ESP_LOGD(TAG, "Published to %s (msg_id=%d): %s", _dome_state_topic_str.c_str(), msg_id, dome_state_str);

    // TODO: Publish consolidated JSON status to _status_topic_str if desired

    return ESP_OK;
}


void MqttClientWrapper::registerRebootCommandCallback(RebootCommandCallback callback) {
    _reboot_callback = callback;
    ESP_LOGI(TAG, "Reboot command callback registered.");
}


// Static event handler
esp_err_t MqttClientWrapper::mqttEventHandler(esp_mqtt_event_handle_t event) {
    // esp_mqtt_client_handle_t client = event->client; // Client passed as event_data in registration
    // MqttClientWrapper *wrapper = (MqttClientWrapper*)event->user_context; // If user_context was set
    MqttClientWrapper *wrapper = _instance; // Use static instance pointer

    if (!wrapper) {
        ESP_LOGE(TAG, "MQTT Event Handler: _instance is null!");
        return ESP_FAIL;
    }

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
            wrapper->_connected = true;
            handleConnected(event->client); // Pass client from event
            break;
        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
            wrapper->_connected = false;
            break;
        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_SUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_UNSUBSCRIBED:
            ESP_LOGI(TAG, "MQTT_EVENT_UNSUBSCRIBED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_PUBLISHED:
            ESP_LOGD(TAG, "MQTT_EVENT_PUBLISHED, msg_id=%d", event->msg_id);
            break;
        case MQTT_EVENT_DATA:
            ESP_LOGI(TAG, "MQTT_EVENT_DATA");
            // ESP_LOGI(TAG, "TOPIC=%.*s", event->topic_len, event->topic); // Already printed by handleData
            // ESP_LOGI(TAG, "DATA=%.*s", event->data_len, event->data);
            handleData(event);
            break;
        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
            if (event->error_handle) {
                 ESP_LOGE(TAG, "Last error code: 0x%x", event->error_handle->error_type);
                 ESP_LOGE(TAG, "Last error handle: %s", esp_err_to_name(event->error_handle->esp_tls_last_esp_err));
            }
            break;
        case MQTT_EVENT_BEFORE_CONNECT:
             ESP_LOGD(TAG, "MQTT_EVENT_BEFORE_CONNECT");
            break;
        default:
            ESP_LOGD(TAG, "Other MQTT event id: %d", event->event_id);
            break;
    }
    return ESP_OK;
}

void MqttClientWrapper::handleConnected(esp_mqtt_client_handle_t client) {
    MqttClientWrapper *wrapper = _instance;
    if (!wrapper) return;

    ESP_LOGI(TAG, "MQTT Connected. Subscribing to reboot command topic: %s", wrapper->_reboot_cmd_topic_str.c_str());
    int msg_id = esp_mqtt_client_subscribe(client, wrapper->_reboot_cmd_topic_str.c_str(), 0); // QoS 0
    if (msg_id < 0) {
        ESP_LOGE(TAG, "Failed to subscribe to reboot topic %s", wrapper->_reboot_cmd_topic_str.c_str());
    } else {
        ESP_LOGI(TAG, "Subscribed to reboot topic %s, msg_id=%d", wrapper->_reboot_cmd_topic_str.c_str(), msg_id);
    }
}

void MqttClientWrapper::handleData(esp_mqtt_event_handle_t event) {
    MqttClientWrapper *wrapper = _instance;
    if (!wrapper) return;

    std::string topic(event->topic, event->topic_len);
    std::string data(event->data, event->data_len);

    ESP_LOGI(TAG, "MQTT data received. Topic: '%s', Data: '%s'", topic.c_str(), data.c_str());

    if (topic == wrapper->_reboot_cmd_topic_str) {
        ESP_LOGW(TAG, "Reboot command received on topic %s with payload: %s", topic.c_str(), data.c_str());
        // Any payload can trigger reboot, or check for specific payload e.g. "1" or "true"
        if (data == "reboot" || data == "1" || data == "true") { // Example payload check
            if (_reboot_callback) {
                _reboot_callback(); // Call the registered reboot function
            } else {
                ESP_LOGW(TAG, "No reboot callback registered, performing default reboot.");
                // Default action: log and reboot
                // Add a small delay before rebooting to allow MQTT ACK or logging to complete
                vTaskDelay(pdMS_TO_TICKS(1000));
                esp_reboot();
            }
        } else {
            ESP_LOGI(TAG, "Reboot command topic received, but payload '%s' did not match expected reboot trigger.", data.c_str());
        }
    } else {
        ESP_LOGD(TAG, "Received MQTT message on unhandled topic: %s", topic.c_str());
    }
}


} // namespace Communication
