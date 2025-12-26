idf_component_register(SRCS "sensor_dht22.cpp" "mqtt_manager.cpp" "wifi_manager.cpp" "main.cpp"
                       PRIV_REQUIRES spi_flash esp_wifi nvs_flash mqtt dht
                       INCLUDE_DIRS "")
