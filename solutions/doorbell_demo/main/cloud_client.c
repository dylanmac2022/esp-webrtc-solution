#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "settings.h"
#include "cloud_client.h"

static const char *TAG = "cloud_client";

static char s_device_id[32];
static cloud_command_handler_t s_command_handler;
static esp_mqtt_client_handle_t s_mqtt;

static bool cloud_enabled(void)
{
    return CLOUD_API_BASE_URL[0] != '\0';
}

void cloud_client_init(const char *device_id)
{
    if (device_id) {
        strlcpy(s_device_id, device_id, sizeof(s_device_id));
    }
    ESP_LOGI(TAG, "Cloud config: api=%s mqtt=%s topicPrefix=%s device=%s",
        CLOUD_API_BASE_URL,
        AWS_IOT_ENDPOINT,
        AWS_IOT_TOPIC_PREFIX,
        s_device_id);
}

void cloud_client_set_command_handler(cloud_command_handler_t handler)
{
    s_command_handler = handler;
}

void cloud_client_post_event(const char *event_type, const char *room_name)
{
    if (!cloud_enabled()) {
        return;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/events", CLOUD_API_BASE_URL);

    int64_t event_ts = esp_timer_get_time() / 1000;
    char event_id[64];
    snprintf(event_id, sizeof(event_id), "%s-%" PRId64 "", s_device_id, event_ts);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return;
    }
    cJSON_AddStringToObject(root, "deviceId", s_device_id);
    cJSON_AddNumberToObject(root, "eventTs", (double)event_ts);
    cJSON_AddStringToObject(root, "eventId", event_id);
    cJSON_AddStringToObject(root, "eventType", event_type ? event_type : "unknown");
    cJSON_AddStringToObject(root, "sessionId", "default");
    cJSON_AddStringToObject(root, "roomName", room_name ? room_name : "");
    cJSON_AddStringToObject(root, "uploadStatus", "pending");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return;
    }

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 5000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (client) {
        esp_http_client_set_header(client, "Content-Type", "application/json");
        if (CLOUD_DEVICE_API_KEY[0] != '\0') {
            esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
        }
        esp_http_client_set_post_field(client, body, strlen(body));
        esp_err_t ret = esp_http_client_perform(client);
        if (ret == ESP_OK) {
            ESP_LOGI(TAG, "Posted event %s (%d)", event_type, esp_http_client_get_status_code(client));
        } else {
            ESP_LOGW(TAG, "Event post failed: %s", esp_err_to_name(ret));
        }
        esp_http_client_cleanup(client);
    }
    free(body);
}

static void handle_command_json(const char *json, int len)
{
    cJSON *root = cJSON_ParseWithLength(json, len);
    if (!root) {
        return;
    }
    cJSON *cmd = cJSON_GetObjectItemCaseSensitive(root, "command");
    if (cJSON_IsString(cmd) && cmd->valuestring && s_command_handler) {
        s_command_handler(cmd->valuestring);
    }
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED: {
        char topic[128];
        snprintf(topic, sizeof(topic), "%s/%s/commands", AWS_IOT_TOPIC_PREFIX, s_device_id);
        esp_mqtt_client_subscribe(event->client, topic, 1);
        ESP_LOGI(TAG, "MQTT connected, subscribed: %s", topic);
        break;
    }
    case MQTT_EVENT_DATA:
        handle_command_json(event->data, event->data_len);
        break;
    default:
        break;
    }
}

void cloud_client_start_mqtt(void)
{
    if (AWS_IOT_ENDPOINT[0] == '\0' || AWS_IOT_CLIENT_CERT_PEM[0] == '\0' || AWS_IOT_CLIENT_KEY_PEM[0] == '\0') {
        ESP_LOGW(TAG, "MQTT disabled: configure AWS IoT endpoint/cert/key in settings.h");
        return;
    }

    char uri[160];
    snprintf(uri, sizeof(uri), "mqtts://%s:8883", AWS_IOT_ENDPOINT);

    esp_mqtt_client_config_t cfg = {
        .broker.address.uri = uri,
        .credentials.client_id = s_device_id,
        .credentials.authentication.certificate = AWS_IOT_CLIENT_CERT_PEM,
        .credentials.authentication.key = AWS_IOT_CLIENT_KEY_PEM,
    };

    s_mqtt = esp_mqtt_client_init(&cfg);
    if (!s_mqtt) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return;
    }

    esp_mqtt_client_register_event(s_mqtt, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(s_mqtt);
}
