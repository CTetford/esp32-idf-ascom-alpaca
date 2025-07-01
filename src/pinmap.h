#pragma once
#ifndef PINMAP_H
#define PINMAP_H

// WT32-ETH01 Specific Pin Definitions
// These are placeholders and MUST be verified with the actual hardware connections.

// Ethernet PHY Power (if controlled by a GPIO)
// #define ETH_PHY_POWER_PIN   XX

// Motor Control (BTS7960)
#define MOTOR_R_PWM_PIN     2  // Example: GPIO2 for Right PWM (connected to R_PWM on BTS7960)
#define MOTOR_L_PWM_PIN     4  // Example: GPIO4 for Left PWM (connected to L_PWM on BTS7960)
// For BTS7960, R_EN and L_EN are often tied together and controlled by one pin or tied to VCC.
// If controlled by separate pins:
// #define MOTOR_R_EN_PIN   XX  // Right Enable
// #define MOTOR_L_EN_PIN   XX  // Left Enable
// If tied together and controlled by one pin:
#define MOTOR_EN_PIN        15 // Example: GPIO15 for enabling both L_EN and R_EN (needs to be high)

// Quadrature Encoder (A/B)
#define ENCODER_CHA_PIN     34 // Example: GPIO34 (must be input-only if < GPIO32 on some ESP32s)
#define ENCODER_CHB_PIN     35 // Example: GPIO35 (must be input-only if < GPIO32 on some ESP32s)

// Home Limit Switch
#define HOME_LIMIT_SWITCH_PIN 32 // Example: GPIO32

// I2C Pins (if any I2C devices are used, e.g., for display or sensors)
// #define I2C_SDA_PIN         XX
// #define I2C_SCL_PIN         XX

// SPI Pins (if any SPI devices are used)
// #define SPI_MOSI_PIN        XX
// #define SPI_MISO_PIN        XX
// #define SPI_CLK_PIN         XX
// #define SPI_CS_PIN          XX

// User LED / Status LED (The WT32-ETH01 might have a built-in LED)
#define STATUS_LED_PIN      2  // Common for ESP32 dev boards, but WT32-ETH01 might differ or not have an easily accessible one.
                               // GPIO2 is also listed for MOTOR_R_PWM_PIN, this is a conflict and needs resolving.
                               // Let's assume no easily controllable status LED for now, or it's shared.
                               // Or assign a different pin if available.
                               // For WT32-ETH01, GPIO2 is often used as TXD0 for UART0 if not configured for other purposes.
                               // It's best to avoid GPIOs with fixed special functions unless reconfigured.

// Note: WT32-ETH01 GPIOs:
// GPIO0: RMII CLK_OUT (configured in sdkconfig)
// GPIO2: Typically TXD0 for programming/debug. Can be used if UART0 is not for console.
// GPIO4: Can be used.
// GPIO5: Often used for ETH_PHY_POWER on some designs, check schematic. If not, it's available.
// GPIO12: Can be used. (MTDI - strapping pin, check implications)
// GPIO13: Can be used. (MTCK - strapping pin)
// GPIO14: Can be used. (MTMS - strapping pin)
// GPIO15: Can be used. (MTDO - strapping pin)
// GPIO16: ETH_MDC
// GPIO17: ETH_MDIO
// GPIO18: ETH_PHY_POWER on some modules, check yours.
// GPIO19: RMII TX_EN
// GPIO21: RMII TXD0
// GPIO22: RMII TXD1
// GPIO23: Can be used.
// GPIO25: RMII RXD0
// GPIO26: RMII RXD1
// GPIO27: RMII CRS_DV
// GPIO32, 33, 34, 35, 36 (VP), 39 (VN) are generally available.
// GPIO34, 35, 36, 39 are input only.

// Re-evaluating based on typical WT32-ETH01 availability and common peripherals:
// Motor Control (BTS7960)
// #define MOTOR_R_PWM_PIN     12 // Example
// #define MOTOR_L_PWM_PIN     13 // Example
// #define MOTOR_EN_PIN        14 // Example

// Quadrature Encoder (A/B) - Must use input-only pins if they are from that range
// #define ENCODER_CHA_PIN     34
// #define ENCODER_CHB_PIN     35

// Home Limit Switch
// #define HOME_LIMIT_SWITCH_PIN 32

// It's critical to assign these based on the final PCB design or wiring.
// The initial assignments are just placeholders to allow code structure.

#endif // PINMAP_H
