#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_spi_flash.h"
#include "esp_log.h"
#include "nvs_flash.h"

// Network
#include "esp_netif.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_wifi.h" // For ESP-NOW prerequisites

// Project includes
#include "config.h"
#include "pinmap.h"

#include "hal/Motor.h"
#include "hal/Encoder.h"
#include "hal/LimitSwitch.h"
#include "dome_control/DomeController.h"
#include "communication/EspNowManager.h"
#include "communication/MqttClientWrapper.h"
#include "device_drivers/AlpacaDomeImpl.h"

// Alpaca Server includes
#include "alpaca_server/api.h"      // For AlpacaServer::Api
#include "alpaca_server/discovery.h" // For Alpaca discovery functions
#include "alpaca_server/udp_server.h" // For discovery server
#include <esp_http_server.h>


static const char *TAG = "MainApp";

// --- Global Object Declarations ---
HAL::Motor g_motor;
HAL::Encoder g_encoder;
HAL::LimitSwitch g_home_limit_switch;

DomeControl::DomeController g_dome_controller(g_motor, g_encoder, g_home_limit_switch);

Communication::EspNowManager g_esp_now_manager;
Communication::MqttClientWrapper g_mqtt_client;

// Alpaca related objects
DeviceDrivers::AlpacaDomeImpl* g_alpaca_dome_device = nullptr; // Pointer, will be new'd
AlpacaServer::Api* g_alpaca_api = nullptr;                   // Pointer, will be new'd
httpd_handle_t g_alpaca_http_server = nullptr;
static udp_server_t g_discovery_server; // From discovery.c example


#include "freertos/event_groups.h"

// --- Function Prototypes ---
static void initialize_nvs();
static void initialize_network_stack(); // Combined init for netif and event loop
static void initialize_ethernet();
static void initialize_wifi_for_espnow();
static void network_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data);
static void alpaca_services_task(void *pvParameters); // Renamed and will wait for network
static void main_control_loop_task(void *pvParameters);

// Network Ready Event Group
static EventGroupHandle_t network_event_group;
const int NETWORK_ETH_CONNECTED_BIT = BIT0;
const int NETWORK_ETH_GOT_IP_BIT = BIT1;


// Reboot callback for MQTT
void handle_reboot_command() {
    ESP_LOGW(TAG, "Reboot command received via MQTT. Rebooting in 3 seconds...");
    vTaskDelay(pdMS_TO_TICKS(3000));
    esp_reboot();
}


extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Starting Dome Controller Application...");

    initialize_nvs();
    initialize_network_stack(); // Init netif, create default event loop

    network_event_group = xEventGroupCreate();
    TEST_ASSERT_NOT_NULL_MESSAGE(network_event_group, "Failed to create network event group");


    // Register custom event handlers for Ethernet and IP events
    ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID, &network_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP, &network_event_handler, NULL));

    initialize_ethernet();
    initialize_wifi_for_espnow();

    ESP_LOGI(TAG, "Initializing Hardware Abstraction Layer (HAL)...");
    // Motor: Pins from pinmap.h, MCPWM unit/timer defaults
    // Ensure pins in pinmap.h are correctly defined for WT32-ETH01 and BTS7960
    esp_err_t motor_init_res = g_motor.begin(
        (gpio_num_t)MOTOR_R_PWM_PIN,
        (gpio_num_t)MOTOR_L_PWM_PIN,
        (gpio_num_t)MOTOR_EN_PIN
    );
    if (motor_init_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize motor: %s", esp_err_to_name(motor_init_res));
        // Handle error: perhaps enter a safe mode or halt.
    }

    // Encoder: Pins from pinmap.h, PCNT unit default
    esp_err_t encoder_init_res = g_encoder.begin(
        (gpio_num_t)ENCODER_CHA_PIN,
        (gpio_num_t)ENCODER_CHB_PIN
        // Add other params like filter, max/min val if needed from config.h
    );
    if (encoder_init_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize encoder: %s", esp_err_to_name(encoder_init_res));
    }

    // Limit Switch: Pin from pinmap.h, active low from config.h
    esp_err_t limit_sw_init_res = g_home_limit_switch.begin(
        (gpio_num_t)HOME_LIMIT_SWITCH_PIN,
        HOME_LIMIT_SWITCH_ACTIVE_LOW
    );
    if (limit_sw_init_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize limit switch: %s", esp_err_to_name(limit_sw_init_res));
    }


    ESP_LOGI(TAG, "Initializing Dome Controller Logic...");
    esp_err_t dome_ctrl_init_res = g_dome_controller.begin(
        DOME_HOME_AZIMUTH,
        DOME_PARK_AZIMUTH,
        (ENCODER_TOTAL_PULSES_FOR_360_DOME_DEGREES) / 360.0f,
        PID_KP, PID_KI, PID_KD,
        HOMING_SPEED, HOMING_DIRECTION,
        HOME_LIMIT_SWITCH_ACTIVE_LOW
    );
    if (dome_ctrl_init_res != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize DomeController: %s", esp_err_to_name(dome_ctrl_init_res));
    }

    ESP_LOGI(TAG, "Initializing Communication Modules...");
    // ESP-NOW
    // Channel from config.h
    if (g_esp_now_manager.begin(ESP_NOW_CHANNEL) == ESP_OK) {
        uint8_t peer_mac[] = ESP_NOW_SHUTTER_PEER_MAC;
        g_esp_now_manager.addPeer(peer_mac);
    } else {
        ESP_LOGE(TAG, "Failed to initialize ESP-NOW Manager.");
    }

    // MQTT
    // Broker URI, client ID, topics from config.h
    // User/Pass can be empty strings if not used.
    g_mqtt_client.begin(
        MQTT_HOST, // Should be full URI like "mqtt://host" or "mqtts://host"
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_BASE_TOPIC,
        &g_dome_controller
    );
    g_mqtt_client.registerRebootCommandCallback(handle_reboot_command);


    ESP_LOGI(TAG, "Initializing ASCOM Alpaca Device Driver...");
    // Alpaca Device instance
    g_alpaca_dome_device = new DeviceDrivers::AlpacaDomeImpl(
        "esp32-dome-0", // Unique ID base (server will append device number)
        ALPACA_SERVER_NAME, // Device Name from config.h
        ALPACA_MANUFACTURER, // Driver Info
        ALPACA_MANUFACTURER_VERSION, // Driver Version
        g_dome_controller,
        g_esp_now_manager
    );

    // Start task that waits for network and then starts Alpaca & MQTT
    xTaskCreate(alpaca_services_task, "alpaca_mqtt_srv", 8192 + 4096, NULL, 5, NULL); // Increased stack for both

    // Start main control loop task (can start independently of full network IP, but MQTT pub will wait)
    xTaskCreate(main_control_loop_task, "main_loop", 4096, NULL, 5, NULL);


    ESP_LOGI(TAG, "Core initialization complete. Network services will start once IP is acquired.");
    // app_main can exit, tasks will continue running.
}

static void initialize_nvs() {
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was erased or new version found. Erasing and re-initializing...");
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    ESP_LOGI(TAG, "NVS initialized.");
}

// ESP-IDF Ethernet Init for WT32-ETH01 (PHY IP101)
// This should align with settings in sdkconfig.esp32eth
static void initialize_ethernet(void) {
    ESP_LOGI(TAG, "Initializing Ethernet...");

    // 1. Initialize Ethernet MAC and PHY via esp_eth driver
    //    Configuration for WT32-ETH01 (RMII, GPIO0 for CLK_OUT, IP101 PHY)
    //    Most of this is set by sdkconfig.esp32eth.
    //    This function ensures it's started.

    esp_netif_config_t netif_cfg = ESP_NETIF_DEFAULT_ETH();
    esp_netif_t *eth_netif = esp_netif_new(&netif_cfg);

    // Init MAC
    eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
    esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&mac_config);

    // Init PHY
    eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();
    // phy_config.phy_addr = CONFIG_ETH_PHY_ADDR; // From sdkconfig
    // phy_config.reset_gpio_num = CONFIG_ETH_PHY_RST_GPIO; // From sdkconfig
    esp_eth_phy_t *phy = esp_eth_phy_new_ip101(&phy_config); // For IP101 PHY

    esp_eth_config_t eth_config = ETH_DEFAULT_CONFIG(mac, phy);
    esp_eth_handle_t eth_handle = NULL;
    ESP_ERROR_CHECK(esp_eth_driver_install(&eth_config, &eth_handle));

    // Attach MAC to Ethernet netif
    ESP_ERROR_CHECK(esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

    ESP_ERROR_CHECK(esp_eth_start(eth_handle));
    ESP_LOGI(TAG, "Ethernet driver started.");
}

static void initialize_wifi_for_espnow() {
    // EspNowManager::begin() handles the necessary WiFi initialization (init, mode, start, channel).
    // This function is called before EspNowManager::begin() in app_main to ensure
    // the event loop and netif are ready if EspNowManager needed them earlier,
    // but EspNowManager does its own WiFi init.
    // Could be refactored to a central WiFi init if WiFi used for more than ESP-NOW.
    ESP_LOGI(TAG, "WiFi prerequisites for ESP-NOW will be handled by EspNowManager::begin().");
}

static void initialize_network_stack() {
    ESP_LOGI(TAG, "Initializing TCP/IP adapter and event loop...");
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
}

static void network_event_handler(void* arg, esp_event_base_t event_base,
                                int32_t event_id, void* event_data) {
    if (event_base == ETH_EVENT) {
        switch (event_id) {
            case ETHERNET_EVENT_CONNECTED:
                ESP_LOGI(TAG, "Ethernet Link Up");
                xEventGroupSetBits(network_event_group, NETWORK_ETH_CONNECTED_BIT);
                // esp_netif_dhcpc_start(esp_netif_get_handle_from_ifkey("ETH_DEF")); // Start DHCP client
                break;
            case ETHERNET_EVENT_DISCONNECTED:
                ESP_LOGW(TAG, "Ethernet Link Down");
                xEventGroupClearBits(network_event_group, NETWORK_ETH_CONNECTED_BIT | NETWORK_ETH_GOT_IP_BIT);
                break;
            case ETHERNET_EVENT_START:
                ESP_LOGI(TAG, "Ethernet Started");
                break;
            case ETHERNET_EVENT_STOP:
                ESP_LOGW(TAG, "Ethernet Stopped");
                xEventGroupClearBits(network_event_group, NETWORK_ETH_CONNECTED_BIT | NETWORK_ETH_GOT_IP_BIT);
                break;
            default:
                break;
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_ETH_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Ethernet Got IP Address: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(network_event_group, NETWORK_ETH_GOT_IP_BIT);
    }
}


static void alpaca_services_task(void *pvParameters) {
    ESP_LOGI(TAG, "Alpaca Services Task started, waiting for network IP...");

    EventBits_t bits = xEventGroupWaitBits(network_event_group,
                                           NETWORK_ETH_GOT_IP_BIT,
                                           pdFALSE, // Don't clear bits on exit
                                           pdTRUE,  // Wait for all bits
                                           portMAX_DELAY); // Wait indefinitely

    if ((bits & NETWORK_ETH_GOT_IP_BIT) != NETWORK_ETH_GOT_IP_BIT) {
        ESP_LOGE(TAG, "Failed to get network IP. Alpaca/MQTT services will not start.");
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "Network IP acquired. Starting Alpaca server and MQTT client...");

    // Start MQTT Client (now that network is up)
    g_mqtt_client.begin(
        std::string("mqtt://") + MQTT_HOST, // Construct full URI
        MQTT_CLIENT_ID,
        MQTT_USERNAME,
        MQTT_PASSWORD,
        MQTT_BASE_TOPIC,
        &g_dome_controller
    );
    g_mqtt_client.registerRebootCommandCallback(handle_reboot_command);


    // Start Alpaca Server (now that network is up)
    std::vector<AlpacaServer::Device *> devices;
    if (g_alpaca_dome_device) {
        devices.push_back(g_alpaca_dome_device);
    } else {
        ESP_LOGE(TAG, "Alpaca Dome Device is null, cannot start Alpaca server.");
        // MQTT might still run, or we could exit task if Alpaca is critical
    }

    if (!devices.empty()) {
        g_alpaca_api = new AlpacaServer::Api(devices,
                                             "ESP32AlpacaServer",
                                             ALPACA_SERVER_NAME,
                                             ALPACA_MANUFACTURER,
                                             ALPACA_MANUFACTURER_VERSION,
                                             ALPACA_LOCATION);

        httpd_config_t config = HTTPD_DEFAULT_CONFIG();
        config.server_port = ALPACA_SERVER_PORT;
        config.ctrl_port = config.server_port + 10;
        config.max_open_sockets = 7;
        config.lru_purge_enable = true;
        config.stack_size = 8192;

        ESP_LOGI(TAG, "Starting Alpaca HTTP server on port: %d", config.server_port);
        esp_err_t ret = httpd_start(&g_alpaca_http_server, &config);
        if (ret == ESP_OK) {
            g_alpaca_api->register_routes(g_alpaca_http_server);
            ESP_LOGI(TAG, "Alpaca HTTP server started and routes registered.");

            ret = discovery_server_init(&g_discovery_server);
            if (ret == ESP_OK) {
                // Ensure discovery_server_start is non-blocking or runs its own task.
                // If it's blocking, this task will hang here.
                ret = discovery_server_start(&g_discovery_server, ALPACA_SERVER_PORT);
                if (ret == ESP_OK) {
                    ESP_LOGI(TAG, "Alpaca UDP Discovery server started.");
                } else {
                    ESP_LOGE(TAG, "Failed to start Alpaca UDP Discovery server: %s", esp_err_to_name(ret));
                }
            } else {
                ESP_LOGE(TAG, "Failed to initialize Alpaca UDP Discovery server: %s", esp_err_to_name(ret));
            }
        } else {
            ESP_LOGE(TAG, "Error starting Alpaca HTTP server: %s", esp_err_to_name(ret));
            delete g_alpaca_api;
            g_alpaca_api = nullptr;
        }
    } else {
         ESP_LOGE(TAG, "No Alpaca devices registered, Alpaca server not started.");
    }

    // This task will now just keep alive.
    // If discovery_server_start is blocking and doesn't self-task, it needs a separate task.
    while(true) {
        vTaskDelay(pdMS_TO_TICKS(10000));
        if (!g_mqtt_client.isConnected()) {
            ESP_LOGW(TAG, "MQTT client is disconnected in alpaca_services_task. Attempting reconnect is handled by client's event handler.");
            // Could add explicit reconnect call here if needed, but ESP-IDF client should auto-reconnect.
        }
    }
    // ESP_LOGW(TAG, "Alpaca services task exiting (should not happen if while(true)).");
    // vTaskDelete(NULL); // Should not be reached
}


static void main_control_loop_task(void *pvParameters) {
    ESP_LOGI(TAG, "Main control loop task started.");
    uint32_t last_mqtt_publish_time_ms = 0;
    const uint32_t mqtt_publish_interval_ms = 5000; // Publish every 5s

    while (1) {
        g_dome_controller.update();

        uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
        if (g_mqtt_client.isConnected() && (now_ms - last_mqtt_publish_time_ms >= mqtt_publish_interval_ms)) {
            esp_err_t pub_ret = g_mqtt_client.publishStatus();
            if (pub_ret == ESP_OK) {
                ESP_LOGD(TAG, "MQTT status published successfully.");
            } else {
                ESP_LOGW(TAG, "MQTT status publish failed.");
            }
            last_mqtt_publish_time_ms = now_ms;
        }

        vTaskDelay(pdMS_TO_TICKS(PID_SAMPLE_TIME_MS));
    }
}
