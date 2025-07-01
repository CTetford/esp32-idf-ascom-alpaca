#pragma once
#ifndef ALPACA_DOME_IMPL_H
#define ALPACA_DOME_IMPL_H

#include "../alpaca_server/device.h" // For AlpacaServer::Dome base class
#include "../dome_control/DomeController.h"
#include "../communication/EspNowManager.h" // For shutter interaction
#include <string>
#include <vector>
#include "esp_err.h"

// May need cJSON for parsing shutter status if EspNowManager callback provides raw string
#include "cJSON.h"


namespace DeviceDrivers {

class AlpacaDomeImpl : public AlpacaServer::Dome {
public:
    AlpacaDomeImpl(const char* unique_id_base,
                     const char* device_name,
                     const char* driver_info,
                     const char* driver_version,
                     DomeControl::DomeController& dome_controller,
                     Communication::EspNowManager& esp_now_manager);
    ~AlpacaDomeImpl();

    // --- AlpacaServer::Device common methods ---
    AlpacaServer::DeviceType device_type() override;
    esp_err_t action(const char *action, const char *parameters, char *buf, size_t len) override;
    esp_err_t commandblind(const char *command, bool raw) override;
    esp_err_t commandbool(const char *command, bool raw, bool *resp) override;
    esp_err_t commandstring(const char *command, bool raw, char *buf, size_t len) override;

    esp_err_t get_connected(bool *connected) override;
    esp_err_t set_connected(bool connected) override; // Controls "online" status for Alpaca

    esp_err_t get_description(char *buf, size_t len) override;
    esp_err_t get_driverinfo(char *buf, size_t len) override;
    esp_err_t get_driverversion(char *buf, size_t len) override;
    esp_err_t get_interfaceversion(uint32_t *version) override; // Alpaca Dome interface version
    esp_err_t get_name(char *buf, size_t len) override;
    esp_err_t get_supportedactions(std::vector<std::string> &actions) override;

    // --- AlpacaServer::Dome specific methods ---
    esp_err_t get_altitude(double *altitude) override;
    esp_err_t get_athome(bool *athome) override;
    esp_err_t get_atpark(bool *atpark) override;
    esp_err_t get_azimuth(double *azimuth) override;
    esp_err_t get_canfindhome(bool *canfindhome) override;
    esp_err_t get_canpark(bool *canpark) override;
    esp_err_t get_cansetaltitude(bool *cansetaltitude) override; // Typically false for simple domes
    esp_err_t get_cansetazimuth(bool *cansetazimuth) override; // True if slewToAzimuth is supported
    esp_err_t get_cansetpark(bool *cansetpark) override;     // True if setParkPosition is supported
    esp_err_t get_cansetshutter(bool *cansetshutter) override; // True if shutter control is implemented
    esp_err_t get_canslave(bool *canslave) override;         // Typically false, unless implementing slaving logic
    esp_err_t get_cansyncazimuth(bool *cansyncazimuth) override; // True if syncToAzimuth is supported
    esp_err_t get_shutterstatus(ShutterState *shutterstatus) override;
    esp_err_t get_slaved(bool *slaved) override;
    esp_err_t put_slaved(bool slaved) override;
    esp_err_t get_slewing(bool *slewing) override;

    esp_err_t put_abortslew() override;
    esp_err_t put_closeshutter() override;
    esp_err_t put_findhome() override;
    esp_err_t put_openshutter() override;
    esp_err_t put_park() override;
    esp_err_t put_setpark() override;
    esp_err_t put_slewtoaltitude(double altitude) override; // Not implemented for typical dome
    esp_err_t put_slewtoazimuth(double azimuth) override;
    esp_err_t put_synctoazimuth(double azimuth) override;

    // Helper for EspNowManager callback
    void updateShutterStatus(const uint8_t *mac_addr, const char *data, int len);


private:
    DomeControl::DomeController& _dome_controller;
    Communication::EspNowManager& _esp_now_manager;

    std::string _device_name;
    std::string _driver_info;
    std::string _driver_version;
    // Unique ID is handled by the base AlpacaServer::Device class using _number and server_id.

    bool _connected_state; // Alpaca "connected" state (logical connection)

    // Store last known shutter status
    AlpacaServer::Dome::ShutterState _current_shutter_status;
    uint32_t _last_shutter_status_update_ms;

    // Helper to map our DomeController state to Alpaca ShutterState
    AlpacaServer::Dome::ShutterState _mapEspNowShutterStatus(const std::string& status_str);
};

} // namespace DeviceDrivers

#endif // ALPACA_DOME_IMPL_H
