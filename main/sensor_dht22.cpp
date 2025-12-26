#include "sensor_dht22.h"
#include "dht.h"
#include "esp_log.h"

static const char* TAG_DHT = "DHT22";

esp_err_t sensor_dht22_read(gpio_num_t gpio, float* temperature, float* humidity)
{
    if (!temperature || !humidity) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t res = dht_read_float_data(DHT_TYPE_AM2301, gpio, humidity, temperature);
    if (res == ESP_OK) {
        ESP_LOGI(TAG_DHT, "Temperature: %.1f°C | Humidity: %.1f%%", *temperature, *humidity);
    } else {
        ESP_LOGE(TAG_DHT, "Read error: %s", esp_err_to_name(res));
    }
    return res;
}
