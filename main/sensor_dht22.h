#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

// Read DHT22 sensor into temperature (°C) and humidity (%)
esp_err_t sensor_dht22_read(gpio_num_t gpio, float* temperature, float* humidity);
