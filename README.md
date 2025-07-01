# ESP32 ASCOM Alpaca Dome Controller (WT32-ETH01)

## Overview

This project implements an ASCOM Alpaca dome controller designed to run on an ESP32, specifically targeting the WT32-ETH01 board for Ethernet connectivity. It controls a dome's rotation using a BTS7960 motor driver, tracks position with a quadrature A/B encoder, and uses a limit switch for homing.

The system is fully ASCOM Alpaca compliant, providing a standard interface for astronomy software. It also features:
-   PID control for precise dome positioning.
-   ESP-NOW communication for controlling a separate shutter mechanism (companion project).
-   MQTT capabilities for publishing dome status and receiving a reboot command.
-   Configuration via a central `src/config.h` and pin mapping via `src/pinmap.h`.

## Features

-   **ASCOM Alpaca Dome Interface:** Implements version 3 of the ASCOM Dome standard.
-   **Ethernet Connectivity:** Utilizes the WT32-ETH01's built-in Ethernet for robust network communication.
-   **Motor Control:** Drives a DC motor via a BTS7960 H-Bridge driver with PWM for speed control.
-   **Position Tracking:** Uses an A/B quadrature encoder with 32-bit pulse accumulation for accurate position feedback.
-   **PID Control:** Employs a PID controller for smooth and accurate slewing to target azimuths.
-   **Homing:** Supports homing to a limit switch with a multi-stage sequence for precision.
-   **ESP-NOW Shutter Control:** Communicates with a companion ESP32 shutter controller using ESP-NOW, exchanging JSON-based commands.
-   **MQTT Interface:**
    -   Publishes dome status (azimuth, at home, slewing, parked, current state) to configurable topics.
    -   Subscribes to a topic to receive a reboot command.
-   **Configurable:** System parameters, network settings, MQTT details, PID constants, and pin mappings are user-configurable.
-   **Modular Design:** Built with C++ classes for HAL, dome logic, communication, and Alpaca device implementation.
-   **PlatformIO Project:** Designed for use with PlatformIO and VS Code/Cursor.

## Hardware Requirements

1.  **ESP32 Board:** WT32-ETH01 (or compatible ESP32 with Ethernet PHY and sufficient GPIOs).
2.  **Motor Driver:** BTS7960 H-Bridge motor driver (or similar).
3.  **DC Motor:** Suitable for rotating the dome.
4.  **Quadrature Encoder:** A/B phase encoder connected to the motor or dome axle.
5.  **Limit Switch:** A mechanical or optical limit switch for the home position.
6.  **Power Supply:** Adequate power supply for the ESP32, motor driver, and motor.
7.  **(Optional) Shutter Controller:** A separate ESP32-based shutter controller compatible with the defined ESP-NOW JSON protocol.

## Software Setup

### Prerequisites

-   PlatformIO CLI or PlatformIO IDE for VS Code/Cursor.
-   ESP-IDF framework (managed by PlatformIO, ensure your PlatformIO installation has support for ESP-IDF projects).

### Configuration

Before building and uploading, you **must** configure the following files:

1.  **`src/pinmap.h`**:
    *   This file defines the GPIO pin assignments for the motor controller, encoder, and limit switch.
    *   **You must edit this file to match your specific hardware wiring to the WT32-ETH01.**
    *   Example (verify these for your setup!):
        ```c++
        // Motor Control (BTS7960)
        #define MOTOR_R_PWM_PIN     12 // Example for Right PWM
        #define MOTOR_L_PWM_PIN     13 // Example for Left PWM
        #define MOTOR_EN_PIN        14 // Example for Enable (R_EN and L_EN tied)

        // Quadrature Encoder (A/B)
        #define ENCODER_CHA_PIN     34 // Example (input-only pin)
        #define ENCODER_CHB_PIN     35 // Example (input-only pin)

        // Home Limit Switch
        #define HOME_LIMIT_SWITCH_PIN 32 // Example
        ```

2.  **`src/config.h`**:
    *   This file contains critical settings for the dome controller's operation.
    *   **Key parameters to configure:**
        *   **Network:**
            *   `ETH_USE_STATIC_IP`, `ETH_STATIC_IP_ADDRESS`, etc. (if not using DHCP for Ethernet).
        *   **MQTT:**
            *   `MQTT_HOST`: MQTT broker address (e.g., "192.168.1.100" or "mqtt.example.com").
            *   `MQTT_PORT`: Typically 1883.
            *   `MQTT_USERNAME`, `MQTT_PASSWORD`: If your broker requires authentication.
            *   `MQTT_CLIENT_ID`, `MQTT_BASE_TOPIC`.
        *   **Dome Configuration:**
            *   `DOME_HOME_AZIMUTH`: Azimuth value (degrees) for the home position (e.g., 0.0 for North).
            *   `DOME_PARK_AZIMUTH`: Default park position azimuth.
        *   **Encoder Configuration:**
            *   `ENCODER_PULSES_PER_REVOLUTION`: Pulses from your encoder for one full revolution *of the encoder itself*.
            *   `DOME_DEGREES_PER_ENCODER_REVOLUTION`: How many degrees the *dome* rotates for one full revolution *of the encoder*. This depends on gearing. Alternatively, calculate `ENCODER_TOTAL_PULSES_FOR_360_DOME_DEGREES`.
        *   **PID Controller Constants:**
            *   `PID_KP`, `PID_KI`, `PID_KD`: These will require tuning for your specific dome mechanics.
        *   **Motor Configuration:**
            *   `HOMING_SPEED`, `HOMING_DIRECTION`.
        *   **Limit Switch:**
            *   `HOME_LIMIT_SWITCH_ACTIVE_LOW`: `true` if switch signal is LOW when active, `false` otherwise.
        *   **ESP-NOW Shutter Companion:**
            *   `ESP_NOW_SHUTTER_PEER_MAC`: The MAC address of your shutter controller ESP32.
            *   `ESP_NOW_CHANNEL`: WiFi channel for ESP-NOW (must match shutter controller).
        *   **Alpaca Server Configuration:**
            *   `ALPACA_SERVER_PORT`: Default is 80.
            *   `ALPACA_SERVER_NAME`, `ALPACA_MANUFACTURER`, etc.: Information exposed via Alpaca.

### Building and Uploading

1.  Open the project in VS Code with the PlatformIO extension, or use the PlatformIO CLI.
2.  Build the project:
    ```bash
    pio run -e wt32-eth01
    ```
3.  Upload to the WT32-ETH01:
    ```bash
    pio run -e wt32-eth01 -t upload
    ```
4.  Monitor serial output:
    ```bash
    pio device monitor -e wt32-eth01
    ```

## ASCOM Alpaca Interface

-   **Discovery:** The dome controller announces itself via Alpaca discovery (UDP multicast on port 32227).
-   **Alpaca Server Port:** Configurable in `src/config.h` (default is 80).
-   **Supported Actions:** Standard ASCOM Dome interface version 3 methods.
    -   Connect/Disconnect
    *   Slew to Azimuth
    *   Sync to Azimuth
    *   Find Home
    *   Park / Set Park Position
    *   Abort Slew
    *   Open/Close Shutter (via ESP-NOW)
    *   Get various status properties (Azimuth, AtHome, AtPark, Slewing, ShutterStatus, etc.)

## MQTT Interface

-   **Base Topic:** Configurable in `src/config.h` (default: `/OCS/dome_rotator`).
-   **Status Topics (Published by device):**
    -   `<base_topic>/azimuth`: Current dome azimuth (float, degrees).
    -   `<base_topic>/at_home`: `true` or `false`.
    -   `<base_topic>/slewing`: `true` or `false`.
    -   `<base_topic>/parked`: `true` or `false`.
    -   `<base_topic>/dome_state`: Current internal state string (e.g., "IDLE", "HOMING_MOVING_TO_SWITCH", "SLEWING_TO_AZIMUTH").
    -   `<base_topic>/shutter_status`: Shutter status string ("open", "closed", "opening", "closing", "error") - *Note: This is planned, confirm implementation detail for topic if different from general status_json.*
    -   `<base_topic>/status_json`: A single JSON payload containing multiple status values (implementation detail, confirm actual topic if used).
-   **Command Topics (Subscribed by device):**
    -   `<base_topic>/command/reboot`: Publishing "reboot", "1", or "true" to this topic will cause the ESP32 to reboot.

## ESP-NOW Shutter Interface

-   The dome controller acts as an ESP-NOW initiator.
-   It sends JSON commands to the shutter controller's MAC address (configured in `src/config.h`).
-   **Commands to Shutter (JSON):**
    ```json
    {"command": "open"}
    {"command": "close"}
    {"command": "stop"}
    {"command": "status_request"}
    ```
-   **Status from Shutter (JSON, expected format for parsing):**
    ```json
    {
      "status": "open" | "closed" | "opening" | "closing" | "error",
      "error_message": "optional string if status is error"
    }
    ```

## Code Structure

-   `src/main.cpp`: Main application entry point, initialization, task creation.
-   `src/config.h`: User-configurable parameters.
-   `src/pinmap.h`: GPIO pin definitions.
-   `src/hal/`: Hardware Abstraction Layer (Motor, Encoder, LimitSwitch).
-   `src/dome_control/`: Core dome logic (`DomeController` class, PID, state machine).
-   `src/communication/`: Communication modules (`EspNowManager`, `MqttClientWrapper`).
-   `src/device_drivers/`: ASCOM Alpaca device implementation (`AlpacaDomeImpl`).
-   `src/alpaca_server/`: Existing Alpaca server framework files.
-   `test/`: Unit tests for various modules (designed for PlatformIO test runner).
-   `platformio.ini`: PlatformIO project configuration.
-   `sdkconfig.esp32eth`: ESP-IDF specific configurations for Ethernet and other features.

## TODO / Future Enhancements

-   Implement stall detection in `DomeController`.
-   Update deprecated ESP-IDF functions (e.g., in `Encoder.cpp` for PCNT filters/events).
-   More sophisticated PID anti-windup and possibly output ramping.
-   NVS storage for park position and home calibration persistence.
-   More comprehensive error reporting via Alpaca and MQTT.
-   Web interface for basic control and configuration (if resources allow).
-   Refactor Alpaca discovery server (`discovery.c`, `udp_server.c`) if it's blocking to ensure it runs in its own non-interfering task.
```
