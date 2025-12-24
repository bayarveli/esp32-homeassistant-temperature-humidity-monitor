idf_component_register(SRCS "hello_world_main.cpp"
                       PRIV_REQUIRES spi_flash esp_wifi nvs_flash mqtt dht
                       INCLUDE_DIRS "")
