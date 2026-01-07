#include "sensor_dht22.h"

// Open ESP-IDF Terminal:
// Run the command to add dht sensor library:
// "esp-idf-lib/dht^1.1.7"
// Also add dependency in idf_component.yml file.
// "dependencies:
//   esp-idf-lib/dht: ^1.1.7"
#include "dht.h"
#include "esp_log.h"
#include "sdkconfig.h"

static const char* TAG_DHT = "DHT22";
static gpio_num_t s_dht_gpio = (gpio_num_t)CONFIG_DHT22_GPIO;

esp_err_t sensor_dht22_read(float* temperature, float* humidity)
{
    if (!temperature || !humidity) {
        return ESP_ERR_INVALID_ARG;
    }
    // s_dht_gpio is configured via sdkconfig (CONFIG_DHT22_GPIO)

    esp_err_t res = dht_read_float_data(DHT_TYPE_AM2301, s_dht_gpio, humidity, temperature);
    if (res == ESP_OK) {
        ESP_LOGI(TAG_DHT, "Temperature: %.1f°C | Humidity: %.1f%%", *temperature, *humidity);
    } else {
        ESP_LOGE(TAG_DHT, "Read error: %s", esp_err_to_name(res));
    }
    return res;
}
