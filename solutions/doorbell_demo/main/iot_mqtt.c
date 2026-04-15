/* MQTT Client for AWS IoT Core
 *
 * Connects via MQTTS (port 8883) using embedded X.509 certificates.
 * Subscribes to doorbell/{DEVICE_ID}/commands.
 * Parses JSON commands and dispatches to the registered handler.
 */

#include <string.h>
#include <stdbool.h>
#include "iot_mqtt.h"
#include "mqtt_client.h"
#include "esp_event.h"
#include "esp_log.h"
#include "cJSON.h"
#include "settings.h"

#define TAG "MQTT_IOT"

/* Embedded certificate files (linked via CMakeLists.txt EMBED_TXTFILES) */
extern const uint8_t device_crt_start[] asm("_binary_device_crt_start");
extern const uint8_t device_crt_end[]   asm("_binary_device_crt_end");
extern const uint8_t device_key_start[] asm("_binary_device_key_start");
extern const uint8_t device_key_end[]   asm("_binary_device_key_end");
extern const uint8_t root_ca_start[]    asm("_binary_root_ca_pem_start");
extern const uint8_t root_ca_end[]      asm("_binary_root_ca_pem_end");

static esp_mqtt_client_handle_t mqtt_handle = NULL;
static mqtt_cmd_handler_t       cmd_callback = NULL;
static bool                     connected = false;

/* Topic we subscribe to */
static char subscribe_topic[128];

static void parse_and_dispatch(const char *data, int len)
{
    if (!cmd_callback || !data || len <= 0) {
        return;
    }

    /* Parse the JSON command envelope:
     * { "command": "capture_snapshot", "payload": {...}, "ts": ..., "correlationId": "..." }
     */
    cJSON *root = cJSON_ParseWithLength(data, len);
    if (!root) {
        ESP_LOGW(TAG, "Failed to parse command JSON");
        return;
    }

    cJSON *cmd_obj = cJSON_GetObjectItem(root, "command");
    if (!cmd_obj || !cJSON_IsString(cmd_obj)) {
        ESP_LOGW(TAG, "Missing 'command' field");
        cJSON_Delete(root);
        return;
    }

    const char *command = cmd_obj->valuestring;
    cJSON *payload_obj = cJSON_GetObjectItem(root, "payload");
    char *payload_str = payload_obj ? cJSON_PrintUnformatted(payload_obj) : NULL;

    ESP_LOGI(TAG, "Received MQTT command: %s", command);
    cmd_callback(command, payload_str ? payload_str : "{}");

    if (payload_str) {
        cJSON_free(payload_str);
    }
    cJSON_Delete(root);
}

static void mqtt_event_handler(void *arg, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = (esp_mqtt_event_handle_t)event_data;

    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected to AWS IoT Core");
            connected = true;
            /* Subscribe to command topic */
            int msg_id = esp_mqtt_client_subscribe(mqtt_handle, subscribe_topic, 1);
            ESP_LOGI(TAG, "Subscribed to %s (msg_id=%d)", subscribe_topic, msg_id);
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            connected = false;
            break;

        case MQTT_EVENT_DATA:
            ESP_LOGD(TAG, "MQTT data on topic %.*s", event->topic_len, event->topic);
            parse_and_dispatch(event->data, event->data_len);
            break;

        case MQTT_EVENT_ERROR:
            ESP_LOGE(TAG, "MQTT error type=%d", event->error_handle->error_type);
            if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
                ESP_LOGE(TAG, "  TLS error=0x%04x, transport errno=%d",
                         (int)event->error_handle->esp_tls_last_esp_err,
                         event->error_handle->esp_transport_sock_errno);
            }
            break;

        case MQTT_EVENT_SUBSCRIBED:
            ESP_LOGI(TAG, "MQTT subscription confirmed (msg_id=%d)", event->msg_id);
            break;

        default:
            break;
    }
}

int mqtt_client_start(mqtt_cmd_handler_t handler)
{
    if (mqtt_handle) {
        ESP_LOGW(TAG, "MQTT client already started");
        return 0;
    }

    cmd_callback = handler;

    /* Build subscribe topic: doorbell/{DEVICE_ID}/commands */
    snprintf(subscribe_topic, sizeof(subscribe_topic),
             "%s/%s/commands", MQTT_TOPIC_PREFIX, DEVICE_ID);

    /* Build broker URI: mqtts://{endpoint}:8883 */
    char broker_uri[256];
    snprintf(broker_uri, sizeof(broker_uri), "mqtts://%s:8883", AWS_IOT_ENDPOINT);

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker = {
            .address = {
                .uri = broker_uri,
            },
            .verification = {
                .certificate = (const char *)root_ca_start,
            },
        },
        .credentials = {
            .authentication = {
                .certificate = (const char *)device_crt_start,
                .key = (const char *)device_key_start,
            },
            .client_id = DEVICE_ID,
        },
        .network = {
            .timeout_ms = 10000,
        },
        .session = {
            .keepalive = 60,
        },
    };

    mqtt_handle = esp_mqtt_client_init(&mqtt_cfg);
    if (!mqtt_handle) {
        ESP_LOGE(TAG, "Failed to init MQTT client");
        return -1;
    }

    esp_mqtt_client_register_event(mqtt_handle, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);

    esp_err_t err = esp_mqtt_client_start(mqtt_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start MQTT client: %s", esp_err_to_name(err));
        return -1;
    }

    ESP_LOGI(TAG, "MQTT client started, connecting to %s", broker_uri);
    return 0;
}

void mqtt_client_stop(void)
{
    if (mqtt_handle) {
        esp_mqtt_client_stop(mqtt_handle);
        esp_mqtt_client_destroy(mqtt_handle);
        mqtt_handle = NULL;
        connected = false;
        ESP_LOGI(TAG, "MQTT client stopped");
    }
}

bool mqtt_client_is_connected(void)
{
    return connected;
}
