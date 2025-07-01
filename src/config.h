#pragma once
#ifndef CONFIG_H
#define CONFIG_H

// Network Configuration
#define WIFI_SSID "YOUR_WIFI_SSID" // Only needed if using WiFi for fallback or specific services
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

// Ethernet Configuration
// Define these if using static IP for Ethernet. Otherwise, DHCP will be used.
// #define ETH_USE_STATIC_IP
// #define ETH_STATIC_IP_ADDRESS "192.168.1.100"
// #define ETH_STATIC_IP_GATEWAY "192.168.1.1"
// #define ETH_STATIC_IP_SUBNET "255.255.255.0"
// #define ETH_STATIC_IP_DNS1 "192.168.1.1"
// #define ETH_STATIC_IP_DNS2 "8.8.8.8" // Optional

// MQTT Configuration
#define MQTT_HOST "your_mqtt_broker_host" // e.g., "mqtt.example.com" or IP address
#define MQTT_PORT 1883
#define MQTT_USERNAME "your_mqtt_username" // Leave empty if no authentication
#define MQTT_PASSWORD "your_mqtt_password" // Leave empty if no authentication
#define MQTT_CLIENT_ID "OCS_Dome_Rotator_ESP32"
#define MQTT_BASE_TOPIC "/OCS/dome_rotator" // Base topic for publishing status
#define MQTT_STATUS_TOPIC MQTT_BASE_TOPIC "/status"
#define MQTT_AZIMUTH_TOPIC MQTT_BASE_TOPIC "/azimuth"
#define MQTT_ATHOME_TOPIC MQTT_BASE_TOPIC "/at_home"
#define MQTT_SLEWING_TOPIC MQTT_BASE_TOPIC "/slewing"
#define MQTT_PARKED_TOPIC MQTT_BASE_TOPIC "/parked"
#define MQTT_SHUTTER_STATUS_TOPIC MQTT_BASE_TOPIC "/shutter_status" // For reporting shutter status received via ESP-NOW
#define MQTT_REBOOT_COMMAND_TOPIC MQTT_BASE_TOPIC "/reboot" // Topic to listen for reboot command

// Dome Configuration
#define DOME_HOME_AZIMUTH 0.0      // Azimuth value (degrees) when the dome is at the home position
#define DOME_PARK_AZIMUTH 180.0    // Azimuth value (degrees) for the park position
#define DOME_MAX_AZIMUTH 359.9     // Maximum azimuth the dome can physically reach
#define DOME_MIN_AZIMUTH 0.0       // Minimum azimuth

// Encoder Configuration
#define ENCODER_PULSES_PER_REVOLUTION 4096 // Pulses per full motor/encoder revolution
// If there's gearing between encoder and dome:
#define DOME_DEGREES_PER_ENCODER_REVOLUTION 10.0 // How many degrees the dome rotates for one full encoder revolution
                                                 // This needs to be calculated based on gear ratios.
                                                 // Or, define total pulses for 360 degrees of dome rotation:
#define ENCODER_TOTAL_PULSES_FOR_360_DOME_DEGREES (ENCODER_PULSES_PER_REVOLUTION * (360.0 / DOME_DEGREES_PER_ENCODER_REVOLUTION))


// PID Controller Constants for Dome Rotation
#define PID_KP 1.5  // Proportional gain
#define PID_KI 0.1  // Integral gain
#define PID_KD 0.05 // Derivative gain
#define PID_OUTPUT_MIN -100 // Minimum motor speed (-100%)
#define PID_OUTPUT_MAX 100  // Maximum motor speed (100%)
#define PID_SAMPLE_TIME_MS 100 // PID calculation interval in milliseconds

// Motor Configuration
#define MOTOR_MAX_SPEED 100 // Max speed in percent (0-100)
#define MOTOR_MIN_SPEED 10  // Minimum speed in percent to overcome static friction (if applicable)
#define MOTOR_ACCELERATION_RATE 10 // Speed change per PID sample time (e.g., 10% speed increase per 100ms) - for ramping

// Homing Configuration
#define HOMING_SPEED 25 // Speed (percent) for homing sequence
#define HOMING_DIRECTION 1 // 1 for clockwise, -1 for counter-clockwise, towards the home switch

// Limit Switch Configuration
#define HOME_LIMIT_SWITCH_ACTIVE_LOW true // Set to true if the switch pulls low when active, false if high

// ESP-NOW Shutter Companion Configuration
#define ESP_NOW_SHUTTER_PEER_MAC {0x01, 0x02, 0x03, 0x04, 0x05, 0x06} // Replace with actual MAC of shutter ESP32
#define ESP_NOW_CHANNEL 1 // WiFi channel for ESP-NOW (0-13)

// Alpaca Server Configuration
#define ALPACA_SERVER_PORT 80
#define ALPACA_DEVICE_NUMBER 0 // For a single dome device
#define ALPACA_SERVER_NAME "ESP32 Dome Controller"
#define ALPACA_MANUFACTURER "DIY Astronomy"
#define ALPACA_MANUFACTURER_VERSION "0.1.0"
#define ALPACA_LOCATION "My Observatory"

// Logging
#define LOG_LEVEL_VERBOSE // Comment out for less logging, or define specific levels like LOG_LEVEL_INFO

#endif // CONFIG_H
