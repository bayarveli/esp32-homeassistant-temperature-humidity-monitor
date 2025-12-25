/*
 * DripCore - Smart Drip Controller
 * ESP32 WROOM 32D - C++ Version with Home Assistant MQTT Discovery
 */

#include <cstdio>
#include <cinttypes>
#include <iostream>
#include <string>
#include <cstring>
#include <cstdlib>
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "nvs_flash.h"
#include "mqtt_client.h"
// Open ESP-IDF Terminal:
// Run the command to add dht sensor library:
// "esp-idf-lib/dht^1.1.7"
// Also add dependency in idf_component.yml file.
// "dependencies:
//   esp-idf-lib/dht: ^1.1.7"
#include "dht.h"
#include "credentials.h"

static const char* TAG = "DRIPCORE";

// Home Assistant MQTT Discovery Configuration
#define HA_DISCOVERY_PREFIX "homeassistant"
#define DEVICE_ID "temp_and_humid_001"
#define DEVICE_NAME "Temperature and Humidity Sensor"

// DHT22 Sensor Configuration
#define DHT22_GPIO GPIO_NUM_10

// WiFi event group
static EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT      BIT1

// MQTT client handle
static esp_mqtt_client_handle_t mqtt_client = nullptr;
static bool mqtt_connected = false;

// WiFi event handler
static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                              int32_t event_id, void* event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        ESP_LOGI(TAG, "WiFi disconnected, retrying...");
        esp_wifi_connect();
        xEventGroupClearBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t* event = (ip_event_got_ip_t*) event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

// MQTT event handler
static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;  // Unused
    (void)event_data;    // Unused
    
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT Connected");
        mqtt_connected = true;
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT Disconnected");
        mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG, "MQTT Error");
        break;
    default:
        break;
    }
}

// Initialize WiFi
void wifi_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    
    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(WIFI_EVENT,
                                                        ESP_EVENT_ANY_ID,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(IP_EVENT,
                                                        IP_EVENT_STA_GOT_IP,
                                                        &wifi_event_handler,
                                                        NULL,
                                                        &instance_got_ip));
    
    wifi_config_t wifi_config = {};
    strcpy((char*)wifi_config.sta.ssid, WIFI_SSID);
    strcpy((char*)wifi_config.sta.password, WIFI_PASSWORD);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
    
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi init finished. Connecting to %s...", WIFI_SSID);
}

// Initialize MQTT
void mqtt_init(void)
{
    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    
    // Use authentication if credentials are provided
    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
    }
    if (strlen(MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }
    
    mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);
}

// DHT22 sensor okuma fonksiyonu
void read_dht22_sensor(float* temperature, float* humidity)
{
    esp_err_t res = dht_read_float_data(DHT_TYPE_AM2301, DHT22_GPIO, humidity, temperature);
    
    if (res == ESP_OK) {
        ESP_LOGI(TAG, "DHT22 - Temperature: %.1f°C | Humidity: %.1f%%", *temperature, *humidity);
    } else {
        ESP_LOGE(TAG, "DHT22 read error: %s", esp_err_to_name(res));
    }
}

// Send Home Assistant MQTT Discovery message
void send_ha_discovery(void)
{
    if (!mqtt_connected) {
        ESP_LOGW(TAG, "MQTT not connected, skipping HA discovery");
        return;
    }
    
    // Get MAC address for unique ID
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Allocate memory on heap instead of stack to prevent overflow
    char* device_info = (char*)malloc(512);
    char* sensor_config = (char*)malloc(1024);
    char* binary_sensor_config = (char*)malloc(1024);
    char discovery_topic[128];
    
    if (!device_info || !sensor_config || !binary_sensor_config) {
        ESP_LOGE(TAG, "Failed to allocate memory for HA discovery");
        free(device_info);
        free(sensor_config);
        free(binary_sensor_config);
        return;
    }
    
    // Device information JSON
    snprintf(device_info, 512,
        "\"device\":{"
        "\"identifiers\":[\"%s\"],"
        "\"name\":\"%s\","
        "\"model\":\"ESP32-C3 Super Mini and DHT22\","
        "\"manufacturer\":\"Pupa DIY\","
        "\"sw_version\":\"v0.0.1\","
        "\"hw_version\":\"0.1\""
        "}",
        mac_str, DEVICE_NAME);
    
    // Binary sensor discovery message (online status)
    snprintf(binary_sensor_config, 1024,
        "{"
        "\"name\":\"%s Status\","
        "\"unique_id\":\"%s_status\","
        "\"state_topic\":\"dripcore/%s/status\","
        "\"payload_on\":\"online\","
        "\"payload_off\":\"offline\","
        "\"device_class\":\"connectivity\","
        "%s"
        "}",
        DEVICE_NAME, mac_str, mac_str, device_info);
    
    // Send binary sensor discovery
    snprintf(discovery_topic, sizeof(discovery_topic), 
             "%s/binary_sensor/%s_status/config", HA_DISCOVERY_PREFIX, mac_str);
    
    esp_mqtt_client_publish(mqtt_client, discovery_topic, binary_sensor_config, 0, 1, 1);
    ESP_LOGI(TAG, "Sent HA discovery for status sensor");
    
    // Temperature sensor discovery
    snprintf(sensor_config, 1024,
        "{"
        "\"name\":\"%s Temperature\","
        "\"unique_id\":\"%s_temperature\","
        "\"state_topic\":\"climate/%s/temperature\","
        "\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\","
        "\"state_class\":\"measurement\","
        "%s"
        "}",
        DEVICE_NAME, mac_str, mac_str, device_info);
    
    snprintf(discovery_topic, sizeof(discovery_topic), 
             "%s/sensor/%s_temperature/config", HA_DISCOVERY_PREFIX, mac_str);
    
    esp_mqtt_client_publish(mqtt_client, discovery_topic, sensor_config, 0, 1, 1);
    ESP_LOGI(TAG, "Sent HA discovery for temperature sensor");
    
    // Humidity sensor discovery
    snprintf(sensor_config, 1024,
        "{"
        "\"name\":\"%s Humidity\","
        "\"unique_id\":\"%s_humidity\","
        "\"state_topic\":\"climate/%s/humidity\","
        "\"unit_of_measurement\":\"%%\","
        "\"device_class\":\"humidity\","
        "\"state_class\":\"measurement\","
        "%s"
        "}",
        DEVICE_NAME, mac_str, mac_str, device_info);
    
    snprintf(discovery_topic, sizeof(discovery_topic), 
             "%s/sensor/%s_humidity/config", HA_DISCOVERY_PREFIX, mac_str);
    
    esp_mqtt_client_publish(mqtt_client, discovery_topic, sensor_config, 0, 1, 1);
    ESP_LOGI(TAG, "Sent HA discovery for humidity sensor");
    
    // Free allocated memory
    free(device_info);
    free(sensor_config);
    free(binary_sensor_config);
}

// Publish sensor data to MQTT
void publish_sensor_data(float temperature, float humidity)
{
    if (!mqtt_connected) return;
    
    // Get MAC for topic
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    // Publish uptime
    char topic[64];
    char payload[32];
    
    // Publish status
    snprintf(topic, sizeof(topic), "dripcore/%s/status", mac_str);
    esp_mqtt_client_publish(mqtt_client, topic, "online", 0, 1, 0);
    
    // Publish temperature
    snprintf(topic, sizeof(topic), "climate/%s/temperature", mac_str);
    snprintf(payload, sizeof(payload), "%.1f", temperature);
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, 0);
    
    // Publish humidity
    snprintf(topic, sizeof(topic), "climate/%s/humidity", mac_str);
    snprintf(payload, sizeof(payload), "%.1f", humidity);
    esp_mqtt_client_publish(mqtt_client, topic, payload, 0, 0, 0);
}

// Device information structure
struct DeviceInfo {
    std::string device_name = "DripCore Smart Controller";
    std::string version = "v0.1.0";
    std::string hardware = "ESP32 WROOM 32D";
    std::string firmware = "ESP-IDF";
    std::string mac_address = "";
    uint32_t uptime_ms = 0;
};

// Function to send discovery message
void send_discovery_message() {
    DeviceInfo device;
    
    // Get MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    device.mac_address = mac_str;
    
    // Get uptime
    device.uptime_ms = esp_timer_get_time() / 1000; // Convert to milliseconds
    
    // Print discovery message
    ESP_LOGI(TAG, "=== DEVICE DISCOVERY ===");
    ESP_LOGI(TAG, "Device: %s", device.device_name.c_str());
    ESP_LOGI(TAG, "Version: %s", device.version.c_str());
    ESP_LOGI(TAG, "Hardware: %s", device.hardware.c_str());
    ESP_LOGI(TAG, "Firmware: %s", device.firmware.c_str());
    ESP_LOGI(TAG, "MAC Address: %s", device.mac_address.c_str());
    ESP_LOGI(TAG, "Uptime: %" PRIu32 " ms", device.uptime_ms);
    ESP_LOGI(TAG, "Free Heap: %" PRIu32 " bytes", esp_get_free_heap_size());
    ESP_LOGI(TAG, "Status: ONLINE");
    ESP_LOGI(TAG, "======================");
}

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "DripCore Smart Controller Starting...");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Send initial discovery message (serial)
    send_discovery_message();
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_init();
    
    // Wait for WiFi connection
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                          WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                          pdFALSE,
                                          pdFALSE,
                                          portMAX_DELAY);
    
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Connected to WiFi");
        
        // Initialize MQTT
        ESP_LOGI(TAG, "Initializing MQTT...");
        mqtt_init();
        
        // Wait a bit for MQTT connection
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Send Home Assistant discovery
        if (mqtt_connected) {
            ESP_LOGI(TAG, "Sending Home Assistant discovery...");
            send_ha_discovery();
        }
    } else {
        ESP_LOGE(TAG, "Failed to connect to WiFi");
    }
    
    // Print detailed chip information
    esp_chip_info_t chip_info;
    uint32_t flash_size;
    esp_chip_info(&chip_info);
    
    ESP_LOGI(TAG, "Chip Details: %d CPU core(s)", chip_info.cores);
    
    if (esp_flash_get_size(nullptr, &flash_size) == ESP_OK) {
        ESP_LOGI(TAG, "Flash: %" PRIu32 "MB", flash_size / (1024 * 1024));
    }
    
    // Main operation loop
    int count = 0;
    float temperature = 0, humidity = 0;
    
    while (true) {
        // Read DHT22 every 30 seconds (15 cycles)
        if (count % 15 == 0) {
            read_dht22_sensor(&temperature, &humidity);
            
            // Publish sensor data
            if (mqtt_connected) {
                publish_sensor_data(temperature, humidity);
            }
        }
        
        // Send discovery message every 60 seconds (30 cycles * 2s)
        if (count % 30 == 0) {
            send_discovery_message();
            if (mqtt_connected) {
                send_ha_discovery();
            }
        }
        
        count++;
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second delay
    }
}
