/*
 * Credentials Template
 * 
 * Copy this file to credentials.h and update with your actual credentials.
 * credentials.h is gitignored and will not be committed to the repository.
 */

#pragma once

// WiFi Configuration - Update with your credentials
#define WIFI_SSID      "YOUR_WIFI_SSID"
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"

// MQTT Configuration - Update with your MQTT broker details
#define MQTT_BROKER_URI "mqtt://192.168.1.100"  // Your MQTT broker IP address
#define MQTT_USERNAME   "YOUR_MQTT_USERNAME"     // Leave empty "" for no authentication
#define MQTT_PASSWORD   "YOUR_MQTT_PASSWORD"     // Leave empty "" for no authentication

// Home Assistant MQTT Discovery Configuration
#define HA_DISCOVERY_PREFIX "homeassistant"
#define DEVICE_ID "temp_and_humid_001"
#define DEVICE_NAME "Temperature and Humidity Sensor"
