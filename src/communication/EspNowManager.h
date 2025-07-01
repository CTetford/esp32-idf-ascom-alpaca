#pragma once
#ifndef ESP_NOW_MANAGER_H
#define ESP_NOW_MANAGER_H

#include "esp_now.h"
#include "esp_err.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional> // For std::function

// Forward declaration for cJSON or ArduinoJson
struct cJSON;
// Or if using ArduinoJson:
// #include <ArduinoJson.h> // If chosen over cJSON

namespace Communication {

// Define a type for the callback function that will process received shutter status
// It could take a parsed JSON object or a raw string, depending on preference.
// Using a function that takes a const char* for the raw JSON string for flexibility.
// If using cJSON: typedef std::function<void(const cJSON* json_status)> ShutterStatusCallback;
// If using ArduinoJson: typedef std::function<void(const JsonDocument& json_status)> ShutterStatusCallback;
typedef std::function<void(const uint8_t *mac_addr, const char *data, int len)> ShutterStatusCallback;


/*
Expected JSON structures for ESP-NOW communication with shutter:

Commands to Shutter:
{
  "command": "open" | "close" | "stop" | "status_request"
}

Status from Shutter:
{
  "status": "open" | "closed" | "opening" | "closing" | "error",
  "error_message": "optional string if status is error"
}
*/


class EspNowManager {
public:
    EspNowManager();
    ~EspNowManager();

    /**
     * @brief Initializes ESP-NOW communication.
     * @param channel WiFi channel for ESP-NOW (0-13). Should match the shutter controller's channel.
     * @return ESP_OK on success.
     */
    esp_err_t begin(uint8_t channel = 1); // Default channel 1

    /**
     * @brief Adds a peer (the shutter controller) to communicate with.
     * @param mac_address MAC address of the shutter ESP32.
     * @return ESP_OK if peer is added successfully.
     */
    esp_err_t addPeer(const uint8_t* mac_address);

    /**
     * @brief Sends a JSON command string to the registered shutter peer.
     * @param command_json The JSON command string.
     * @return ESP_OK if the message was queued for sending.
     */
    esp_err_t sendShutterCommand(const std::string& command_json);

    /**
     * @brief Sends a specific command (open, close, stop, status_request) to the shutter.
     * This is a helper that constructs the JSON and calls sendShutterCommand.
     * @param command "open", "close", "stop", "status_request".
     * @return ESP_OK if the message was queued for sending.
     */
    esp_err_t sendShutterCommand(const char* command);


    /**
     * @brief Registers a callback function to be invoked when a message (shutter status) is received.
     * @param callback The function to call.
     */
    void registerShutterStatusCallback(ShutterStatusCallback callback);

private:
    static void onDataSent(const uint8_t *mac_addr, esp_now_send_status_t status);
    static void onDataRecv(const uint8_t *mac_addr, const uint8_t *incoming_data, int len);

    // Store the MAC address of the shutter peer.
    // This simple version assumes one peer. For multiple, a list/vector would be needed.
    esp_now_peer_info_t _shutter_peer;
    bool _peer_added;

    static ShutterStatusCallback _status_callback; // Static to be accessible from static C-style callbacks
    bool _initialized;
    uint8_t _esp_now_channel;
};

} // namespace Communication

#endif // ESP_NOW_MANAGER_H
