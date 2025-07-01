#include "AlpacaDomeImpl.h"
#include "../alpaca_server/api.h" // For ALPACA_ERR_NOT_IMPLEMENTED etc.
#include "esp_log.h"
#include <cstring> // For strncpy
#include "esp_timer.h" // For esp_timer_get_time()

static const char *TAG = "AlpacaDomeImpl";

namespace DeviceDrivers {

AlpacaDomeImpl::AlpacaDomeImpl(const char* unique_id_base, // Not directly used here, base class handles it
                                 const char* device_name,
                                 const char* driver_info,
                                 const char* driver_version,
                                 DomeControl::DomeController& dome_controller,
                                 Communication::EspNowManager& esp_now_manager)
    : _dome_controller(dome_controller),
      _esp_now_manager(esp_now_manager),
      _device_name(device_name ? device_name : "ESP32 Alpaca Dome"),
      _driver_info(driver_info ? driver_info : "ESP-IDF Alpaca Dome Driver"),
      _driver_version(driver_version ? driver_version : "0.1.0"),
      _connected_state(true), // Default to connected (online)
      _current_shutter_status(AlpacaServer::Dome::ShutterState::Error) // Initial state unknown
{
    ESP_LOGI(TAG, "AlpacaDomeImpl instance created: %s", _device_name.c_str());
    _last_shutter_status_update_ms = 0;

    // Register callback with EspNowManager to receive shutter status updates
    // Use a lambda to capture `this` and call the member function
    _esp_now_manager.registerShutterStatusCallback(
        [this](const uint8_t *mac_addr, const char *data, int len) {
            this->updateShutterStatus(mac_addr, data, len);
        }
    );
    // Request initial shutter status
    _esp_now_manager.sendShutterCommand("status_request");
}

AlpacaDomeImpl::~AlpacaDomeImpl() {
    ESP_LOGI(TAG, "AlpacaDomeImpl instance destroyed: %s", _device_name.c_str());
}

// --- AlpacaServer::Device common methods ---

AlpacaServer::DeviceType AlpacaDomeImpl::device_type() {
    return AlpacaServer::DeviceType::Dome;
}

esp_err_t AlpacaDomeImpl::action(const char *action, const char *parameters, char *buf, size_t len) {
    ESP_LOGD(TAG, "Action called: %s with params: %s", action, parameters);
    // No custom actions implemented for now
    strncpy(buf, AlpacaServer::ALPACA_ERR_MESSAGE_ACTION_NOT_IMPLEMENTED, len -1);
    buf[len-1] = '\0';
    return AlpacaServer::ALPACA_ERR_ACTION_NOT_IMPLEMENTED;
}

esp_err_t AlpacaDomeImpl::commandblind(const char *command, bool raw) {
    ESP_LOGD(TAG, "CommandBlind called: %s, raw: %s", command, raw ? "true" : "false");
    // No commandblind implemented
    return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED;
}

esp_err_t AlpacaDomeImpl::commandbool(const char *command, bool raw, bool *resp) {
    ESP_LOGD(TAG, "CommandBool called: %s, raw: %s", command, raw ? "true" : "false");
    // No commandbool implemented
    return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED;
}

esp_err_t AlpacaDomeImpl::commandstring(const char *command, bool raw, char *buf, size_t len) {
    ESP_LOGD(TAG, "CommandString called: %s, raw: %s", command, raw ? "true" : "false");
    // No commandstring implemented
    strncpy(buf, AlpacaServer::ALPACA_ERR_MESSAGE_NOT_IMPLEMENTED, len-1);
    buf[len-1] = '\0';
    return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED;
}

esp_err_t AlpacaDomeImpl::get_connected(bool *connected) {
    if (!connected) return ESP_ERR_INVALID_ARG;
    *connected = _connected_state;
    ESP_LOGD(TAG, "get_connected: %s", *connected ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::set_connected(bool connected_val) {
    ESP_LOGI(TAG, "set_connected: %s", connected_val ? "true" : "false");
    _connected_state = connected_val;
    // This is a logical connection. If false, many methods should return error.
    // Actual hardware (motor, encoder) is managed by DomeController.
    // If set to false, we might want to stop any motion.
    if (!_connected_state) {
        _dome_controller.abortSlew(); // Example: stop dome if disconnected
    }
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_description(char *buf, size_t len) {
    if (!buf) return ESP_ERR_INVALID_ARG;
    // This should be a more detailed description if available from config
    strncpy(buf, "ESP32 Based ASCOM Alpaca Dome Controller", len-1);
    buf[len-1] = '\0';
    ESP_LOGD(TAG, "get_description: %s", buf);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_driverinfo(char *buf, size_t len) {
    if (!buf) return ESP_ERR_INVALID_ARG;
    strncpy(buf, _driver_info.c_str(), len-1);
    buf[len-1] = '\0';
    ESP_LOGD(TAG, "get_driverinfo: %s", buf);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_driverversion(char *buf, size_t len) {
    if (!buf) return ESP_ERR_INVALID_ARG;
    strncpy(buf, _driver_version.c_str(), len-1);
    buf[len-1] = '\0';
    ESP_LOGD(TAG, "get_driverversion: %s", buf);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_interfaceversion(uint32_t *version) {
    if (!version) return ESP_ERR_INVALID_ARG;
    *version = 3; // ASCOM Dome Interface Version 3
    ESP_LOGD(TAG, "get_interfaceversion: %u", *version);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_name(char *buf, size_t len) {
    if (!buf) return ESP_ERR_INVALID_ARG;
    strncpy(buf, _device_name.c_str(), len-1);
    buf[len-1] = '\0';
    ESP_LOGD(TAG, "get_name: %s", buf);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_supportedactions(std::vector<std::string> &actions) {
    actions.clear(); // No custom actions supported yet
    ESP_LOGD(TAG, "get_supportedactions: 0 actions");
    return ESP_OK;
}

// --- AlpacaServer::Dome specific methods ---

esp_err_t AlpacaDomeImpl::get_altitude(double *altitude) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!altitude) return ESP_ERR_INVALID_ARG;
    // This dome controller does not support altitude control.
    *altitude = 0.0; // Or a fixed value if it makes sense, e.g., 90.0 for zenith-only slit
    ESP_LOGD(TAG, "get_altitude: %.2f (Not Supported)", *altitude);
    return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED; // More appropriate than returning a fixed val
}

esp_err_t AlpacaDomeImpl::get_athome(bool *athome) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!athome) return ESP_ERR_INVALID_ARG;
    *athome = _dome_controller.isAtHome();
    ESP_LOGD(TAG, "get_athome: %s", *athome ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_atpark(bool *atpark) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!atpark) return ESP_ERR_INVALID_ARG;
    *atpark = _dome_controller.isParked();
    ESP_LOGD(TAG, "get_atpark: %s", *atpark ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_azimuth(double *azimuth) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!azimuth) return ESP_ERR_INVALID_ARG;
    if (!_dome_controller.isHomed()) { // If not homed, azimuth is unknown
        // Alpaca spec says to raise an error if value is unknown and cannot be obtained
        ESP_LOGW(TAG, "get_azimuth: Dome not homed, azimuth unknown.");
        return AlpacaServer::ALPACA_ERR_VALUE_NOT_SET; // Or ALPACA_ERR_NOT_CONNECTED if "homed" is part of being "connected"
    }
    *azimuth = (double)_dome_controller.getCurrentAzimuth();
    ESP_LOGD(TAG, "get_azimuth: %.2f", *azimuth);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_canfindhome(bool *canfindhome) {
    if (!canfindhome) return ESP_ERR_INVALID_ARG;
    *canfindhome = true; // We have a home switch and findHome()
    ESP_LOGD(TAG, "get_canfindhome: %s", *canfindhome ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_canpark(bool *canpark) {
    if (!canpark) return ESP_ERR_INVALID_ARG;
    *canpark = true; // We have park() and setPark()
    ESP_LOGD(TAG, "get_canpark: %s", *canpark ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_cansetaltitude(bool *cansetaltitude) {
    if (!cansetaltitude) return ESP_ERR_INVALID_ARG;
    *cansetaltitude = false; // Altitude not supported
    ESP_LOGD(TAG, "get_cansetaltitude: %s", *cansetaltitude ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_cansetazimuth(bool *cansetazimuth) {
    if (!cansetazimuth) return ESP_ERR_INVALID_ARG;
    *cansetazimuth = true; // slewToAzimuth is supported
    ESP_LOGD(TAG, "get_cansetazimuth: %s", *cansetazimuth ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_cansetpark(bool *cansetpark) {
    if (!cansetpark) return ESP_ERR_INVALID_ARG;
    *cansetpark = true; // setParkPosition is supported
    ESP_LOGD(TAG, "get_cansetpark: %s", *cansetpark ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_cansetshutter(bool *cansetshutter) {
    if (!cansetshutter) return ESP_ERR_INVALID_ARG;
    *cansetshutter = true; // Shutter control via ESP-NOW
    ESP_LOGD(TAG, "get_cansetshutter: %s", *cansetshutter ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_canslave(bool *canslave) {
    if (!canslave) return ESP_ERR_INVALID_ARG;
    *canslave = false; // Slaving not supported
    ESP_LOGD(TAG, "get_canslave: %s", *canslave ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_cansyncazimuth(bool *cansyncazimuth) {
    if (!cansyncazimuth) return ESP_ERR_INVALID_ARG;
    *cansyncazimuth = true; // syncToAzimuth is supported
    ESP_LOGD(TAG, "get_cansyncazimuth: %s", *cansyncazimuth ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_shutterstatus(ShutterState *shutterstatus_out) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!shutterstatus_out) return ESP_ERR_INVALID_ARG;

    // Request status from shutter if last update is too old or initial
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    if (_current_shutter_status == AlpacaServer::Dome::ShutterState::Error ||
        (now_ms - _last_shutter_status_update_ms > 5000)) { // Update every 5s or if error
        ESP_LOGD(TAG, "Requesting shutter status update from ESP-NOW peer.");
        _esp_now_manager.sendShutterCommand("status_request");
        // The status will be updated asynchronously by the callback.
        // For an immediate GET, this might return stale data.
        // Consider if this GET should block or if clients should poll.
        // For now, return last known status.
    }

    *shutterstatus_out = _current_shutter_status;
    ESP_LOGD(TAG, "get_shutterstatus: %d", static_cast<int>(*shutterstatus_out));
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::get_slaved(bool *slaved) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!slaved) return ESP_ERR_INVALID_ARG;
    *slaved = false; // Slaving not supported
    ESP_LOGD(TAG, "get_slaved: %s", *slaved ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_slaved(bool slaved_val) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGD(TAG, "put_slaved: %s", slaved_val ? "true" : "false");
    if (slaved_val) { // Cannot enable slaving if not supported
        return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED;
    }
    return ESP_OK; // Setting slaved to false is OK
}

esp_err_t AlpacaDomeImpl::get_slewing(bool *slewing) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!slewing) return ESP_ERR_INVALID_ARG;
    *slewing = _dome_controller.isSlewing();
    ESP_LOGD(TAG, "get_slewing: %s", *slewing ? "true" : "false");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_abortslew() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGI(TAG, "put_abortslew called.");
    _dome_controller.abortSlew();
    // Also abort shutter movement if applicable
    _esp_now_manager.sendShutterCommand("stop");
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_closeshutter() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGI(TAG, "put_closeshutter called.");
    // Update our internal state optimistically, will be confirmed by ESP-NOW callback
    // _current_shutter_status = AlpacaServer::Dome::ShutterState::Closing;
    return _esp_now_manager.sendShutterCommand("close");
}

esp_err_t AlpacaDomeImpl::put_findhome() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (_dome_controller.isSlewing()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION; // Cannot home if already moving
    ESP_LOGI(TAG, "put_findhome called.");
    _dome_controller.findHome();
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_openshutter() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGI(TAG, "put_openshutter called.");
    // _current_shutter_status = AlpacaServer::Dome::ShutterState::Opening;
    return _esp_now_manager.sendShutterCommand("open");
}

esp_err_t AlpacaDomeImpl::put_park() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!_dome_controller.isHomed()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION; // Must be homed to park
    if (_dome_controller.isSlewing()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION;
    ESP_LOGI(TAG, "put_park called.");
    _dome_controller.park();
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_setpark() {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!_dome_controller.isHomed()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION;
    ESP_LOGI(TAG, "put_setpark called.");
    _dome_controller.setParkPosition();
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_slewtoaltitude(double altitude) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGD(TAG, "put_slewtoaltitude: %.2f (Not Supported)", altitude);
    return AlpacaServer::ALPACA_ERR_NOT_IMPLEMENTED;
}

esp_err_t AlpacaDomeImpl::put_slewtoazimuth(double azimuth) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    if (!_dome_controller.isHomed()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION; // Must be homed
    if (_dome_controller.isSlewing()) return AlpacaServer::ALPACA_ERR_INVALID_OPERATION; // Dome is busy
    ESP_LOGI(TAG, "put_slewtoazimuth: %.2f", azimuth);
    _dome_controller.slewToAzimuth((float)azimuth);
    return ESP_OK;
}

esp_err_t AlpacaDomeImpl::put_synctoazimuth(double azimuth) {
    if (!_connected_state) return AlpacaServer::ALPACA_ERR_NOT_CONNECTED;
    ESP_LOGI(TAG, "put_synctoazimuth: %.2f", azimuth);
    _dome_controller.syncToAzimuth((float)azimuth);
    return ESP_OK;
}

// Helper to map string status from ESP-NOW to Alpaca ShutterState enum
AlpacaServer::Dome::ShutterState AlpacaDomeImpl::_mapEspNowShutterStatus(const std::string& status_str) {
    if (status_str == "open") return AlpacaServer::Dome::ShutterState::Open;
    if (status_str == "closed") return AlpacaServer::Dome::ShutterState::Closed;
    if (status_str == "opening") return AlpacaServer::Dome::ShutterState::Opening;
    if (status_str == "closing") return AlpacaServer::Dome::ShutterState::Closing;
    if (status_str == "error") return AlpacaServer::Dome::ShutterState::Error;
    ESP_LOGW(TAG, "Unknown shutter status string: %s", status_str.c_str());
    return AlpacaServer::Dome::ShutterState::Error; // Default to error for unknown status
}


// Callback for EspNowManager
void AlpacaDomeImpl::updateShutterStatus(const uint8_t *mac_addr, const char *data, int len) {
    ESP_LOGI(TAG, "Shutter status update received via ESP-NOW: %.*s", len, data);
    // Parse JSON (assuming data is JSON string like {"status": "open"})
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (root == NULL) {
        const char *error_ptr = cJSON_GetErrorPtr();
        if (error_ptr != NULL) {
            ESP_LOGE(TAG, "Failed to parse shutter status JSON: %s", error_ptr);
        } else {
            ESP_LOGE(TAG, "Failed to parse shutter status JSON (unknown error).");
        }
        _current_shutter_status = AlpacaServer::Dome::ShutterState::Error;
        return;
    }

    cJSON *status_item = cJSON_GetObjectItemCaseSensitive(root, "status");
    if (cJSON_IsString(status_item) && (status_item->valuestring != NULL)) {
        std::string status_str = status_item->valuestring;
        _current_shutter_status = _mapEspNowShutterStatus(status_str);
        _last_shutter_status_update_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        ESP_LOGI(TAG, "Parsed shutter status: %s -> Alpaca State: %d", status_str.c_str(), static_cast<int>(_current_shutter_status));

        // Optionally, get error message if status is "error"
        if (_current_shutter_status == AlpacaServer::Dome::ShutterState::Error) {
            cJSON *error_msg_item = cJSON_GetObjectItemCaseSensitive(root, "error_message");
            if (cJSON_IsString(error_msg_item) && (error_msg_item->valuestring != NULL)) {
                ESP_LOGE(TAG, "Shutter reported error: %s", error_msg_item->valuestring);
            }
        }
    } else {
        ESP_LOGE(TAG, "Shutter status JSON does not contain valid 'status' string field.");
        _current_shutter_status = AlpacaServer::Dome::ShutterState::Error;
    }

    cJSON_Delete(root);
}


} // namespace DeviceDrivers
