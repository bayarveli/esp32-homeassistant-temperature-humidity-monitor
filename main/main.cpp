#include <cstdio>
#include <cinttypes>

#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_timer.h"
#include "esp_event.h"
#include "freertos/task.h"
#include "freertos/FreeRTOS.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#include "mqtt_manager.h"
#include "sensor_dht22.h"
#include "wifi_manager.h"

static const char* TAG = "TEMP_HUMID_MONITOR";

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
        
        // Wait for MQTT connection (timeout 10s)
        TickType_t mqtt_start = xTaskGetTickCount();
        const TickType_t mqtt_timeout = pdMS_TO_TICKS(10000);
        while (!mqtt_is_connected() && (xTaskGetTickCount() - mqtt_start) < mqtt_timeout) {
            vTaskDelay(pdMS_TO_TICKS(500));
        }

        if (mqtt_is_connected()) {
            ESP_LOGI(TAG, "MQTT connection established");
        } else {
            ESP_LOGW(TAG, "MQTT connection timeout");
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
        
        count++;
        vTaskDelay(pdMS_TO_TICKS(2000));  // 2 second delay
    }
}
