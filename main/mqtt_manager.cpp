#include "mqtt_manager.h"
#include "credentials.h"
#include "mqtt_client.h"
#include "esp_log.h"
#include "esp_mac.h"
#include <cstring>

static const char* TAG_MQTT = "MqttManager";

#define TOPIC_PREFIX "env"
#define HA_DISCOVERY_PREFIX "homeassistant"
#define DEVICE_NAME "TH Sensor"

static esp_mqtt_client_handle_t s_mqtt_client = nullptr;
static bool s_mqtt_connected = false;
static char device_id[32];

static void build_device_id() {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    snprintf(device_id, sizeof(device_id), "sensor-%02x%02x%02x%02x%02x%02x", 
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    (void)handler_args;
    (void)base;
    (void)event_data;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT Connected");
        s_mqtt_connected = true;
        char topic[64];
        snprintf(topic, sizeof(topic), "%s/%s/status", TOPIC_PREFIX, device_id);
        esp_mqtt_client_publish(s_mqtt_client, topic, "online", 0, 1, 1);
        mqtt_send_ha_discovery();
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG_MQTT, "MQTT Disconnected");
        s_mqtt_connected = false;
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGI(TAG_MQTT, "MQTT Error");
        break;
    default:
        break;
    }
}

void mqtt_init(void)
{
    build_device_id();

    char lwt_topic[64];
    snprintf(lwt_topic, sizeof(lwt_topic), "%s/%s/status", TOPIC_PREFIX, device_id);

    esp_mqtt_client_config_t mqtt_cfg = {};
    mqtt_cfg.broker.address.uri = MQTT_BROKER_URI;
    mqtt_cfg.session.last_will.topic = lwt_topic;
    mqtt_cfg.session.last_will.msg = "offline";
    mqtt_cfg.session.last_will.qos = 1;
    mqtt_cfg.session.last_will.retain = true;

    if (strlen(MQTT_USERNAME) > 0) {
        mqtt_cfg.credentials.username = MQTT_USERNAME;
    }
    if (strlen(MQTT_PASSWORD) > 0) {
        mqtt_cfg.credentials.authentication.password = MQTT_PASSWORD;
    }
    s_mqtt_client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(s_mqtt_client, (esp_mqtt_event_id_t)ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt_client);
}

bool mqtt_is_connected(void)
{
    return s_mqtt_connected;
}

void mqtt_publish_sensor(float temperature, float humidity)
{
    if (!s_mqtt_connected) return;
    char topic[64];
    char payload[32];

    snprintf(topic, sizeof(topic), "%s/%s/temperature", TOPIC_PREFIX, device_id);
    snprintf(payload, sizeof(payload), "%.1f", temperature);
    esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 0, 0);

    snprintf(topic, sizeof(topic), "%s/%s/humidity", TOPIC_PREFIX, device_id);
    snprintf(payload, sizeof(payload), "%.1f", humidity);
    esp_mqtt_client_publish(s_mqtt_client, topic, payload, 0, 0, 0);
}

void mqtt_send_ha_discovery(void)
{
    if (!s_mqtt_connected) {
        ESP_LOGW(TAG_MQTT, "MQTT not connected, skipping HA discovery");
        return;
    }

    char* device_info = (char*)malloc(512);
    char* sensor_config = (char*)malloc(1024);
    char* binary_sensor_config = (char*)malloc(1024);
    char discovery_topic[128];

    if (!device_info || !sensor_config || !binary_sensor_config) {
        ESP_LOGE(TAG_MQTT, "Failed to allocate memory for HA discovery");
        free(device_info);
        free(sensor_config);
        free(binary_sensor_config);
        return;
    }

    snprintf(device_info, 512,
        "\"device\":{"
        "\"identifiers\":[\"%s\"],"
        "\"name\":\"%s\","
        "\"model\":\"ESP32-C3 Super Mini with DHT22\","
        "\"manufacturer\":\"Pupa DIY\","
        "\"sw_version\":\"v0.0.1\","
        "\"hw_version\":\"0.1\""
        "}",
        device_id, DEVICE_NAME);

    snprintf(binary_sensor_config, 1024,
        "{"
        "\"name\":\"%s Status\","
        "\"unique_id\":\"%s_status\","
        "\"state_topic\":\"%s/%s/status\","
        "\"payload_on\":\"online\","
        "\"payload_off\":\"offline\","
        "\"device_class\":\"connectivity\","
        "%s"
        "}",
        DEVICE_NAME, device_id, TOPIC_PREFIX, device_id, device_info);

    snprintf(discovery_topic, sizeof(discovery_topic), "%s/binary_sensor/%s_status/config", HA_DISCOVERY_PREFIX, device_id);
    esp_mqtt_client_publish(s_mqtt_client, discovery_topic, binary_sensor_config, 0, 1, 1);
    ESP_LOGI(TAG_MQTT, "Sent HA discovery for status sensor");

    snprintf(sensor_config, 1024,
        "{"
        "\"name\":\"%s Temperature\","
        "\"unique_id\":\"%s_temperature\","
        "\"state_topic\":\"%s/%s/temperature\","
        "\"availability_topic\":\"%s/%s/status\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"unit_of_measurement\":\"°C\","
        "\"device_class\":\"temperature\","
        "\"state_class\":\"measurement\","
        "%s"
        "}",
        DEVICE_NAME, device_id, TOPIC_PREFIX, device_id, TOPIC_PREFIX, device_id, device_info);

    snprintf(discovery_topic, sizeof(discovery_topic), "%s/sensor/%s_temperature/config", HA_DISCOVERY_PREFIX, device_id);
    esp_mqtt_client_publish(s_mqtt_client, discovery_topic, sensor_config, 0, 1, 1);
    ESP_LOGI(TAG_MQTT, "Sent HA discovery for temperature sensor");

    snprintf(sensor_config, 1024,
        "{"
        "\"name\":\"%s Humidity\","
        "\"unique_id\":\"%s_humidity\","
        "\"state_topic\":\"%s/%s/humidity\","
        "\"availability_topic\":\"%s/%s/status\","
        "\"payload_available\":\"online\","
        "\"payload_not_available\":\"offline\","
        "\"unit_of_measurement\":\"%%\","
        "\"device_class\":\"humidity\","
        "\"state_class\":\"measurement\","
        "%s"
        "}",
        DEVICE_NAME, device_id, TOPIC_PREFIX, device_id, TOPIC_PREFIX, device_id, device_info);

    snprintf(discovery_topic, sizeof(discovery_topic), "%s/sensor/%s_humidity/config", HA_DISCOVERY_PREFIX, device_id);
    esp_mqtt_client_publish(s_mqtt_client, discovery_topic, sensor_config, 0, 1, 1);
    ESP_LOGI(TAG_MQTT, "Sent HA discovery for humidity sensor");

    free(device_info);
    free(sensor_config);
    free(binary_sensor_config);
}
