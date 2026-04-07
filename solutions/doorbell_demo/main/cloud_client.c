#include <string.h>
#include <stdio.h>
#include <inttypes.h>
#include <stdbool.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_random.h"
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

static int64_t make_event_ts_ms(void)
{
    return esp_timer_get_time() / 1000;
}

static void make_event_id(char *out, size_t out_size, int64_t event_ts)
{
    uint32_t nonce = esp_random() & 0xFFFF;
    snprintf(out, out_size, "%s-%" PRId64 "-%04" PRIx32, s_device_id, event_ts, nonce);
}

static bool http_post_json(const char *url, const char *body, char *resp_buf, size_t resp_buf_size, int *http_status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", "application/json");
    if (CLOUD_DEVICE_API_KEY[0] != '\0') {
        esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
    }
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (http_status) {
        *http_status = status;
    }
    if (resp_buf && resp_buf_size > 0) {
        int n = esp_http_client_read_response(client, resp_buf, (int)resp_buf_size - 1);
        if (n < 0) {
            n = 0;
        }
        resp_buf[n] = '\0';
    }
    esp_http_client_cleanup(client);
    return (ret == ESP_OK) && (status >= 200 && status < 300);
}

static bool http_put_binary(const char *url, const char *content_type, const uint8_t *data, size_t data_len, int *http_status)
{
    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 15000,
    };

    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_http_client_set_header(client, "Content-Type", content_type ? content_type : "application/octet-stream");
    esp_http_client_set_post_field(client, (const char *)data, data_len);

    esp_err_t ret = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    if (http_status) {
        *http_status = status;
    }
    esp_http_client_cleanup(client);
    return (ret == ESP_OK) && (status >= 200 && status < 300);
}

static bool post_event_internal(const char *event_type,
                                const char *room_name,
                                int64_t event_ts,
                                const char *event_id,
                                const char *upload_status,
                                const char *media_type,
                                const char *media_key)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/events", CLOUD_API_BASE_URL);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", s_device_id);
    cJSON_AddNumberToObject(root, "eventTs", (double)event_ts);
    cJSON_AddStringToObject(root, "eventId", event_id);
    cJSON_AddStringToObject(root, "eventType", event_type ? event_type : "unknown");
    cJSON_AddStringToObject(root, "sessionId", "default");
    cJSON_AddStringToObject(root, "roomName", room_name ? room_name : "");
    cJSON_AddStringToObject(root, "uploadStatus", upload_status ? upload_status : "pending");
    if (media_type && media_key) {
        cJSON *s3_keys = cJSON_CreateObject();
        if (s3_keys) {
            cJSON_AddStringToObject(s3_keys, media_type, media_key);
            cJSON_AddItemToObject(root, "s3Keys", s3_keys);
        }
    }

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }

    int status = 0;
    bool ok = http_post_json(url, body, NULL, 0, &status);
    free(body);
    if (!ok) {
        ESP_LOGW(TAG, "Event post failed %s (%d)", event_type ? event_type : "unknown", status);
    }
    return ok;
}

static bool request_upload_url(const char *event_id,
                               const char *media_type,
                               const char *content_type,
                               char *upload_url,
                               size_t upload_url_size,
                               char *media_key,
                               size_t media_key_size)
{
    char url[256];
    snprintf(url, sizeof(url), "%s/media/upload-url", CLOUD_API_BASE_URL);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", s_device_id);
    cJSON_AddStringToObject(root, "eventId", event_id);
    cJSON_AddStringToObject(root, "mediaType", media_type);
    cJSON_AddStringToObject(root, "contentType", content_type ? content_type : "application/octet-stream");
    cJSON_AddStringToObject(root, "sessionId", "default");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }

    char response[1024];
    int status = 0;
    bool ok = http_post_json(url, body, response, sizeof(response), &status);
    free(body);
    if (!ok) {
        ESP_LOGW(TAG, "Upload URL request failed (%d)", status);
        return false;
    }

    cJSON *resp = cJSON_Parse(response);
    if (!resp) {
        return false;
    }
    cJSON *upload_url_json = cJSON_GetObjectItemCaseSensitive(resp, "uploadUrl");
    cJSON *key_json = cJSON_GetObjectItemCaseSensitive(resp, "key");
    bool parsed = cJSON_IsString(upload_url_json) && upload_url_json->valuestring &&
                  cJSON_IsString(key_json) && key_json->valuestring;
    if (parsed) {
        strlcpy(upload_url, upload_url_json->valuestring, upload_url_size);
        strlcpy(media_key, key_json->valuestring, media_key_size);
    }
    cJSON_Delete(resp);
    return parsed;
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

    int64_t event_ts = make_event_ts_ms();
    char event_id[80];
    make_event_id(event_id, sizeof(event_id), event_ts);
    post_event_internal(event_type, room_name, event_ts, event_id, "pending", NULL, NULL);
}

bool cloud_client_fetch_livekit_token(const char *room_name, const char *role)
{
    if (!cloud_enabled() || !room_name || room_name[0] == '\0') {
        return false;
    }

    char url[256];
    snprintf(url, sizeof(url), "%s/token/livekit", CLOUD_API_BASE_URL);

    cJSON *root = cJSON_CreateObject();
    if (!root) {
        return false;
    }
    cJSON_AddStringToObject(root, "deviceId", s_device_id);
    cJSON_AddStringToObject(root, "roomName", room_name);
    cJSON_AddStringToObject(root, "role", role ? role : "publisher");

    char *body = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    if (!body) {
        return false;
    }

    char response[1024];
    int status = 0;
    bool ok = http_post_json(url, body, response, sizeof(response), &status);
    free(body);
    if (!ok) {
        ESP_LOGW(TAG, "LiveKit token request failed (%d)", status);
        return false;
    }

    cJSON *resp = cJSON_Parse(response);
    if (!resp) {
        return false;
    }
    cJSON *token = cJSON_GetObjectItemCaseSensitive(resp, "token");
    cJSON *ws_url = cJSON_GetObjectItemCaseSensitive(resp, "wsUrl");
    bool valid = cJSON_IsString(token) && token->valuestring &&
                 cJSON_IsString(ws_url) && ws_url->valuestring;
    if (valid) {
        ESP_LOGI(TAG, "LiveKit token acquired for room %s", room_name);
    }
    cJSON_Delete(resp);
    return valid;
}

bool cloud_client_upload_media_event(const char *event_type,
                                     const char *room_name,
                                     const char *media_type,
                                     const char *content_type,
                                     const uint8_t *data,
                                     size_t data_len)
{
    if (!cloud_enabled() || !media_type || !data || data_len == 0) {
        return false;
    }

    int64_t event_ts = make_event_ts_ms();
    char event_id[80];
    make_event_id(event_id, sizeof(event_id), event_ts);

    char upload_url[768] = {0};
    char media_key[256] = {0};
    if (!request_upload_url(event_id, media_type, content_type, upload_url, sizeof(upload_url), media_key, sizeof(media_key))) {
        post_event_internal(event_type, room_name, event_ts, event_id, "upload_url_failed", NULL, NULL);
        return false;
    }

    int upload_status = 0;
    if (!http_put_binary(upload_url, content_type, data, data_len, &upload_status)) {
        ESP_LOGW(TAG, "Media upload failed for %s (%d)", media_type, upload_status);
        post_event_internal(event_type, room_name, event_ts, event_id, "upload_failed", NULL, NULL);
        return false;
    }

    return post_event_internal(event_type, room_name, event_ts, event_id, "uploaded", media_type, media_key);
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
