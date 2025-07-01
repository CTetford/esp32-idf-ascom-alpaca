#pragma once
#ifndef MQTT_CLIENT_WRAPPER_H
#define MQTT_CLIENT_WRAPPER_H

#include "esp_err.h"
#include <string>
#include <functional> // For std::function
#include "mqtt_client.h" // ESP-IDF MQTT client

// Forward declare DomeController to avoid circular dependencies
// Or include a minimal interface header if specific dome data is needed directly by MQTT
namespace DomeControl {
    class DomeController; // Forward declaration
    enum class DomeState; // Forward declaration for DomeState
}


namespace Communication {

// Define a type for the reboot command callback
typedef std::function<void()> RebootCommandCallback;

class MqttClientWrapper {
public:
    MqttClientWrapper();
    ~MqttClientWrapper();

    /**
     * @brief Initializes and starts the MQTT client.
     * @param broker_uri Full URI of the MQTT broker (e.g., "mqtt://mqtt.example.com").
     * @param client_id Client ID for MQTT connection.
     * @param username MQTT username (optional, can be empty).
     * @param password MQTT password (optional, can be empty).
     * @param status_topic Topic to publish dome status to.
     * @param reboot_cmd_topic Topic to subscribe to for reboot commands.
     * @param dome_controller Pointer to the DomeController instance for fetching status.
     * @return ESP_OK on success.
     */
    esp_err_t begin(const std::string& broker_uri,
                      const std::string& client_id,
                      const std::string& username,
                      const std::string& password,
                      const std::string& base_topic, // e.g. /OCS/dome_rotator
                      DomeControl::DomeController* dome_controller // Pointer to access dome status
                      );

    /**
     * @brief Stops the MQTT client.
     */
    void stop();

    /**
     * @brief Publishes the current dome status to the configured MQTT topics.
     * Requires _dome_controller to be set.
     * @return ESP_OK if publish was successful (or queued). ESP_FAIL otherwise.
     */
    esp_err_t publishStatus();

    /**
     * @brief Registers a callback function to be invoked when a reboot command is received.
     * @param callback The function to call.
     */
    void registerRebootCommandCallback(RebootCommandCallback callback);


    bool isConnected() const;

private:
    static esp_err_t mqttEventHandler(esp_mqtt_event_handle_t event);
    static void handleConnected(esp_mqtt_client_handle_t client);
    static void handleData(esp_mqtt_event_handle_t event);

    esp_mqtt_client_handle_t _client;
    bool _initialized;
    bool _connected;

    std::string _broker_uri;
    std::string _client_id;
    std::string _username;
    std::string _password;

    // Specific topics derived from base_topic
    std::string _status_topic_str; // General status
    std::string _azimuth_topic_str;
    std::string _athome_topic_str;
    std::string _slewing_topic_str;
    std::string _parked_topic_str;
    std::string _dome_state_topic_str;
    std::string _shutter_status_topic_str; // For reporting shutter status via MQTT as well
    std::string _reboot_cmd_topic_str;


    DomeControl::DomeController* _dome_controller_ptr; // Pointer to the dome controller for status

    static RebootCommandCallback _reboot_callback; // Static for C-style event handler
    static MqttClientWrapper* _instance; // Static pointer to the instance for C-style event handler
};

} // namespace Communication

#endif // MQTT_CLIENT_WRAPPER_H
