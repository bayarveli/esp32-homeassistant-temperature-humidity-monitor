#pragma once
#include "esp_err.h"
#include "driver/gpio.h"

esp_err_t sensor_dht22_read(float* temperature, float* humidity);
