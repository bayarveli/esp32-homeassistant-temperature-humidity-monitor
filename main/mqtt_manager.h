#pragma once

// Initialize MQTT client and start connection
void mqtt_init(void);

// Query MQTT connection state
bool mqtt_is_connected(void);

// Publish sensor readings to MQTT topics
void mqtt_publish_sensor(float temperature, float humidity);

// Send Home Assistant MQTT discovery payloads
void mqtt_send_ha_discovery(void);
