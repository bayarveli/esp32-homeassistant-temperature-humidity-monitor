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
#include "credentials.h"
#include "wifi_manager.h"
#include "sensor_dht22.h"
#include "mqtt_manager.h"

static const char* TAG = "TEMP_HUMID_MONITOR";
struct DeviceInfo {
    std::string device_name = "Temperature and Humidity Monitor";
    std::string version = "v0.0.1";
    std::string hardware = "ESP32-C3 Super Mini";
    std::string firmware = "ESP-IDF";
    std::string mac_address = "";
    uint32_t uptime_ms = 0;
};

void log_device_info() {
    DeviceInfo device;
    
    // Get MAC address
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    device.mac_address = mac_str;
    
    // Get uptime
    device.uptime_ms = esp_timer_get_time() / 1000;
    
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
    ESP_LOGI(TAG, "Application starting...");
    
    // Initialize NVS (required for WiFi)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Send initial discovery message (serial)
    log_device_info();
    
    // Initialize WiFi
    ESP_LOGI(TAG, "Initializing WiFi...");
    wifi_init();
    
    // Wait for WiFi connection (timeout 15s)
    TickType_t start = xTaskGetTickCount();
    const TickType_t timeout = pdMS_TO_TICKS(15000);
    while (!wifi_is_connected() && (xTaskGetTickCount() - start) < timeout) {
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    
    if (wifi_is_connected()) {
        ESP_LOGI(TAG, "Connected to WiFi");
        
        // Initialize MQTT
        ESP_LOGI(TAG, "Initializing MQTT...");
        mqtt_init();
        
        // Wait a bit for MQTT connection
        vTaskDelay(pdMS_TO_TICKS(3000));
        
        // Send Home Assistant discovery
        if (mqtt_is_connected()) {
            ESP_LOGI(TAG, "Sending Home Assistant discovery...");
            mqtt_send_ha_discovery();
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
            sensor_dht22_read(&temperature, &humidity);
            
            // Publish sensor data
            if (mqtt_is_connected()) {
                mqtt_publish_sensor(temperature, humidity);
            }
        }
        
        // Send discovery message every 60 seconds (30 cycles * 2s)
        if (count % 30 == 0) {
            log_device_info();
            if (mqtt_is_connected()) {
                mqtt_send_ha_discovery();
            }
        }
        
        count++;
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second delay
    }
}
