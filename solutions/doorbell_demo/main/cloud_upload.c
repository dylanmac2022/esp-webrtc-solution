/* Cloud upload module
 *
 * Handles: snapshot capture + JPEG encode + S3 upload,
 *          video recording buffer + mux + S3 upload,
 *          event metadata creation via REST API.
 *
 * Uses esp_http_client for HTTP requests, cJSON for JSON, PSRAM for buffers.
 */

#include <string.h>
#include <strings.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include "cloud_upload.h"
#include "esp_log.h"
#include "esp_http_client.h"
#include "esp_random.h"
#include "cJSON.h"
#include "media_lib_os.h"
#include "settings.h"
#include "esp_crt_bundle.h"
#include "esp_capture_sink.h"
#include "esp_jpeg_enc.h"
#include "common.h"

#define TAG "CLOUD_UPLOAD"

/* Maximum HTTP response body we'll buffer */
#define HTTP_RESP_MAX   (4 * 1024)

/* ──────────────────── helpers ──────────────────── */

/* Generate a simple event ID: hex string from random bytes */
static void gen_event_id(char *buf, size_t len)
{
    uint32_t r1 = esp_random();
    uint32_t r2 = esp_random();
    snprintf(buf, len, "%08lx%08lx", (unsigned long)r1, (unsigned long)r2);
}

/* Get ISO-8601 timestamp string */
static void get_iso_timestamp(char *buf, size_t len)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm timeinfo;
    gmtime_r(&tv.tv_sec, &timeinfo);
    strftime(buf, len, "%Y-%m-%dT%H:%M:%SZ", &timeinfo);
}

/* Get epoch millis */
static int64_t get_epoch_ms(void)
{
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (int64_t)tv.tv_sec * 1000 + tv.tv_usec / 1000;
}

/* ──────────────────── HTTP helpers ──────────────────── */

typedef struct {
    char  *data;
    int    len;
    int    capacity;
} http_buffer_t;

static esp_err_t http_event_handler(esp_http_client_event_t *evt)
{
    http_buffer_t *buf = (http_buffer_t *)evt->user_data;
    if (!buf) return ESP_OK;

    switch (evt->event_id) {
        case HTTP_EVENT_ON_DATA:
            if (buf->len + evt->data_len < buf->capacity) {
                memcpy(buf->data + buf->len, evt->data, evt->data_len);
                buf->len += evt->data_len;
                buf->data[buf->len] = '\0';
            }
            break;
        default:
            break;
    }
    return ESP_OK;
}

/**
 * POST JSON to API endpoint, return parsed cJSON response (caller must cJSON_Delete).
 * Returns NULL on error.
 */
static cJSON *api_post_json(const char *path, cJSON *body)
{
    char url[256];
    snprintf(url, sizeof(url), "%s%s", API_BASE_URL, path);

    char *body_str = cJSON_PrintUnformatted(body);
    if (!body_str) return NULL;

    http_buffer_t resp_buf = {
        .data = calloc(1, HTTP_RESP_MAX),
        .len = 0,
        .capacity = HTTP_RESP_MAX,
    };
    if (!resp_buf.data) {
        cJSON_free(body_str);
        return NULL;
    }

    esp_http_client_config_t config = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .event_handler = http_event_handler,
        .user_data = &resp_buf,
        .timeout_ms = 15000,
        .buffer_size = 2048,
        .buffer_size_tx = 2048,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };

    esp_http_client_handle_t client = esp_http_client_init(&config);
    esp_http_client_set_header(client, "Content-Type", "application/json");
    esp_http_client_set_header(client, "x-api-key", DEVICE_API_KEY);
    esp_http_client_set_post_field(client, body_str, strlen(body_str));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);
    cJSON_free(body_str);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "POST %s failed: %s", path, esp_err_to_name(err));
        free(resp_buf.data);
        return NULL;
    }
    if (status < 200 || status >= 300) {
        ESP_LOGE(TAG, "POST %s returned %d: %s", path, status, resp_buf.data);
        free(resp_buf.data);
        return NULL;
    }

    cJSON *resp = cJSON_Parse(resp_buf.data);
    free(resp_buf.data);
    return resp;
}

/**
 * Upload binary data to a pre-signed S3 URL via HTTP PUT.
 * Returns 0 on success.
 */
static int upload_to_s3(const char *upload_url, const uint8_t *data, int size, const char *content_type)
{
    ESP_LOGI(TAG, "Uploading %d bytes to S3...", size);

    for (int attempt = 0; attempt < 3; attempt++) {
        if (attempt > 0) {
            ESP_LOGW(TAG, "S3 upload retry %d/3...", attempt + 1);
            media_lib_thread_sleep(2000);
        }

        esp_http_client_config_t config = {
            .url = upload_url,
            .method = HTTP_METHOD_PUT,
            .timeout_ms = 30000,
            .buffer_size = 4096,
            .buffer_size_tx = 4096,
            .crt_bundle_attach = esp_crt_bundle_attach,
        };

        esp_http_client_handle_t client = esp_http_client_init(&config);
        esp_http_client_set_header(client, "Content-Type", content_type);
        esp_http_client_set_post_field(client, (const char *)data, size);

        esp_err_t err = esp_http_client_perform(client);
        int status = esp_http_client_get_status_code(client);
        esp_http_client_cleanup(client);

        if (err == ESP_OK && status >= 200 && status < 300) {
            ESP_LOGI(TAG, "S3 upload complete (status=%d)", status);
            return 0;
        }
        ESP_LOGE(TAG, "S3 upload attempt %d failed: err=%s status=%d", attempt + 1, esp_err_to_name(err), status);
    }

    ESP_LOGE(TAG, "S3 upload failed after 3 attempts");
    return -1;
}

/* ──────────────────── Capture helpers ──────────────────── */

/* Recording state */
static bool    recording_active = false;
static int64_t record_start_time = 0;

/**
 * Grab a single RGB565 frame from the pre-created snapshot sink,
 * encode it to JPEG with the hardware JPEG encoder, and return
 * the JPEG buffer (allocated in PSRAM — caller must free).
 */
static int capture_jpeg_frame(uint8_t **out_data, int *out_size)
{
    esp_capture_sink_handle_t sink = media_sys_get_snapshot_sink();
    if (!sink) {
        ESP_LOGE(TAG, "Snapshot sink not available");
        return -1;
    }

    /* ONESHOT: capture exactly one frame then auto-disable */
    esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_ONESHOT);

    esp_capture_stream_frame_t frame = {
        .stream_type = ESP_CAPTURE_STREAM_TYPE_VIDEO,
    };

    int ret = -1;
    for (int tries = 0; tries < 30; tries++) {
        if (esp_capture_sink_acquire_frame(sink, &frame, true) == ESP_CAPTURE_ERR_OK) {
            if (frame.size > 0 && frame.data) {
                /* Allocate JPEG output buffer in PSRAM */
                int jpeg_buf_size = 300 * 1024;
                uint8_t *jpeg_buf = heap_caps_malloc(jpeg_buf_size, MALLOC_CAP_SPIRAM);
                if (!jpeg_buf) {
                    ESP_LOGE(TAG, "Failed to alloc JPEG output buffer");
                    esp_capture_sink_release_frame(sink, &frame);
                    break;
                }

                /* Encode the RGB565 frame to JPEG */
                jpeg_enc_config_t enc_cfg = DEFAULT_JPEG_ENC_CONFIG();
                enc_cfg.width       = VIDEO_WIDTH;
                enc_cfg.height      = VIDEO_HEIGHT;
                enc_cfg.src_type    = JPEG_PIXEL_FORMAT_RGB565_LE;
                enc_cfg.subsampling = JPEG_SUBSAMPLE_420;
                enc_cfg.quality     = 50;
                enc_cfg.task_enable = false;

                jpeg_enc_handle_t enc = NULL;
                if (jpeg_enc_open(&enc_cfg, &enc) == JPEG_ERR_OK && enc) {
                    int jpeg_size = 0;
                    if (jpeg_enc_process(enc, frame.data, frame.size,
                                         jpeg_buf, jpeg_buf_size, &jpeg_size) == JPEG_ERR_OK) {
                        *out_data = jpeg_buf;
                        *out_size = jpeg_size;
                        ret = 0;
                        ESP_LOGI(TAG, "JPEG encoded: %d bytes from %d byte RGB565 frame",
                                 jpeg_size, frame.size);
                    } else {
                        ESP_LOGE(TAG, "JPEG encode failed");
                        heap_caps_free(jpeg_buf);
                    }
                    jpeg_enc_close(enc);
                } else {
                    ESP_LOGE(TAG, "Failed to open JPEG encoder");
                    heap_caps_free(jpeg_buf);
                }

                esp_capture_sink_release_frame(sink, &frame);
                break;
            }
            esp_capture_sink_release_frame(sink, &frame);
        }
        media_lib_thread_sleep(50);
    }

    /* Ensure sink is disabled (ONESHOT auto-disables, but be safe) */
    esp_capture_sink_enable(sink, ESP_CAPTURE_RUN_MODE_DISABLE);
    return ret;
}

/* ──────────────────── Snapshot command ──────────────────── */

static void do_capture_snapshot(void)
{
    char event_id[20];
    char session_id[20];
    char timestamp[32];

    gen_event_id(event_id, sizeof(event_id));
    gen_event_id(session_id, sizeof(session_id));
    get_iso_timestamp(timestamp, sizeof(timestamp));

    ESP_LOGI(TAG, "Snapshot: eventId=%s", event_id);

    /* Step 1: Request pre-signed upload URL from API */
    cJSON *req = cJSON_CreateObject();
    cJSON_AddStringToObject(req, "deviceId", DEVICE_ID);
    cJSON_AddStringToObject(req, "eventId", event_id);
    cJSON_AddStringToObject(req, "mediaType", "snapshot");
    cJSON_AddStringToObject(req, "contentType", "image/jpeg");
    cJSON_AddStringToObject(req, "sessionId", session_id);

    cJSON *resp = api_post_json("/media/upload-url", req);
    cJSON_Delete(req);

    if (!resp) {
        ESP_LOGE(TAG, "Failed to get upload URL");
        return;
    }

    cJSON *url_obj = cJSON_GetObjectItem(resp, "uploadUrl");
    cJSON *key_obj = cJSON_GetObjectItem(resp, "key");
    if (!url_obj || !cJSON_IsString(url_obj)) {
        ESP_LOGE(TAG, "No uploadUrl in response");
        cJSON_Delete(resp);
        return;
    }

    const char *upload_url = url_obj->valuestring;
    const char *s3_key = key_obj ? key_obj->valuestring : "unknown";

    /* Step 2: Capture a real MJPEG frame from the camera */
    uint8_t *jpeg_data = NULL;
    int jpeg_size = 0;
    int cap_ret = capture_jpeg_frame(&jpeg_data, &jpeg_size);
    if (cap_ret != 0 || !jpeg_data) {
        ESP_LOGE(TAG, "Failed to capture JPEG frame");
        cJSON_Delete(resp);
        return;
    }

    ESP_LOGI(TAG, "Captured JPEG snapshot: %d bytes, uploading to S3 (key=%s)...", jpeg_size, s3_key);
    int ret = upload_to_s3(upload_url, jpeg_data, jpeg_size, "image/jpeg");
    heap_caps_free(jpeg_data);
    cJSON_Delete(resp);

    if (ret != 0) {
        ESP_LOGE(TAG, "Snapshot upload failed");
        return;
    }

    /* Step 3: Create event metadata in DynamoDB */
    cJSON *event_req = cJSON_CreateObject();
    cJSON_AddStringToObject(event_req, "deviceId", DEVICE_ID);
    cJSON_AddNumberToObject(event_req, "eventTs", (double)get_epoch_ms());
    cJSON_AddStringToObject(event_req, "eventId", event_id);
    cJSON_AddStringToObject(event_req, "eventType", "snapshot");
    cJSON_AddStringToObject(event_req, "sessionId", session_id);

    cJSON *s3_keys = cJSON_CreateObject();
    cJSON_AddStringToObject(s3_keys, "snapshot", s3_key);
    cJSON_AddItemToObject(event_req, "s3Keys", s3_keys);
    cJSON_AddStringToObject(event_req, "uploadStatus", "complete");

    cJSON *event_resp = api_post_json("/events", event_req);
    cJSON_Delete(event_req);

    if (event_resp) {
        ESP_LOGI(TAG, "Snapshot event created successfully");
        cJSON_Delete(event_resp);
    } else {
        ESP_LOGE(TAG, "Failed to create snapshot event");
    }
}

/* ──────────────────── Recording commands ──────────────────── */

static void do_record_start(void)
{
    if (recording_active) {
        ESP_LOGW(TAG, "Recording already active");
        return;
    }

    record_start_time = get_epoch_ms();
    recording_active = true;
    ESP_LOGI(TAG, "Recording started (live stream is being recorded via LiveKit)");
}

static void do_record_stop(void)
{
    if (!recording_active) {
        ESP_LOGW(TAG, "Recording not active");
        return;
    }

    recording_active = false;
    int64_t duration_ms = get_epoch_ms() - record_start_time;
    int duration_sec = (int)(duration_ms / 1000);
    ESP_LOGI(TAG, "Recording stopped. Duration=%ds", duration_sec);

    /* Capture a snapshot as the recording thumbnail */
    uint8_t *thumb_data = NULL;
    int thumb_size = 0;
    capture_jpeg_frame(&thumb_data, &thumb_size);

    char event_id[20];
    char session_id[20];
    char timestamp[32];

    gen_event_id(event_id, sizeof(event_id));
    gen_event_id(session_id, sizeof(session_id));
    get_iso_timestamp(timestamp, sizeof(timestamp));

    const char *s3_key_thumb_copy = NULL;

    /* Upload thumbnail if captured */
    if (thumb_data && thumb_size > 0) {
        cJSON *url_req = cJSON_CreateObject();
        cJSON_AddStringToObject(url_req, "deviceId", DEVICE_ID);
        cJSON_AddStringToObject(url_req, "eventId", event_id);
        cJSON_AddStringToObject(url_req, "mediaType", "snapshot");
        cJSON_AddStringToObject(url_req, "contentType", "image/jpeg");
        cJSON_AddStringToObject(url_req, "sessionId", session_id);

        cJSON *url_resp = api_post_json("/media/upload-url", url_req);
        cJSON_Delete(url_req);

        if (url_resp) {
            cJSON *uo = cJSON_GetObjectItem(url_resp, "uploadUrl");
            cJSON *ko = cJSON_GetObjectItem(url_resp, "key");
            if (uo && cJSON_IsString(uo)) {
                upload_to_s3(uo->valuestring, thumb_data, thumb_size, "image/jpeg");
                if (ko && cJSON_IsString(ko)) {
                    s3_key_thumb_copy = strdup(ko->valuestring);
                }
            }
            cJSON_Delete(url_resp);
        }
        heap_caps_free(thumb_data);
    }

    /* Create event */
    cJSON *event_req = cJSON_CreateObject();
    cJSON_AddStringToObject(event_req, "deviceId", DEVICE_ID);
    cJSON_AddNumberToObject(event_req, "eventTs", (double)get_epoch_ms());
    cJSON_AddStringToObject(event_req, "eventId", event_id);
    cJSON_AddStringToObject(event_req, "eventType", "recording");
    cJSON_AddStringToObject(event_req, "sessionId", session_id);
    cJSON_AddNumberToObject(event_req, "durationSec", duration_sec);

    cJSON *s3_keys = cJSON_CreateObject();
    if (s3_key_thumb_copy) {
        cJSON_AddStringToObject(s3_keys, "snapshot", s3_key_thumb_copy);
        free((void *)s3_key_thumb_copy);
    }
    cJSON_AddItemToObject(event_req, "s3Keys", s3_keys);
    cJSON_AddStringToObject(event_req, "uploadStatus", "complete");

    cJSON *event_resp = api_post_json("/events", event_req);
    cJSON_Delete(event_req);

    if (event_resp) {
        ESP_LOGI(TAG, "Recording event created (duration=%ds)", duration_sec);
        cJSON_Delete(event_resp);
    } else {
        ESP_LOGE(TAG, "Failed to create recording event");
    }
}

/* ──────────────────── Task wrapper ──────────────────── */

typedef struct {
    char command[32];
    char payload[256];
} cmd_task_arg_t;

static void cloud_cmd_task(void *arg)
{
    cmd_task_arg_t *cmd = (cmd_task_arg_t *)arg;

    if (strcasecmp(cmd->command, "capture_snapshot") == 0) {
        do_capture_snapshot();
    } else if (strcasecmp(cmd->command, "record_start") == 0) {
        do_record_start();
    } else if (strcasecmp(cmd->command, "record_stop") == 0) {
        do_record_stop();
    } else {
        ESP_LOGW(TAG, "Unknown command: %s", cmd->command);
    }

    free(cmd);
    media_lib_thread_destroy(NULL);
}

/* ──────────────────── Public API ──────────────────── */

void cloud_upload_init(void)
{
    ESP_LOGI(TAG, "Cloud upload module initialized (API=%s, device=%s)", API_BASE_URL, DEVICE_ID);
}

void cloud_handle_command(const char *command, const char *payload_json)
{
    /* Spawn a background task so we don't block the MQTT callback */
    cmd_task_arg_t *arg = calloc(1, sizeof(cmd_task_arg_t));
    if (!arg) {
        ESP_LOGE(TAG, "No memory for command task");
        return;
    }
    strncpy(arg->command, command, sizeof(arg->command) - 1);
    if (payload_json) {
        strncpy(arg->payload, payload_json, sizeof(arg->payload) - 1);
    }

    media_lib_thread_handle_t thread;
    int ret = media_lib_thread_create_from_scheduler(&thread, "cloud_cmd", cloud_cmd_task, arg);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to create command task");
        free(arg);
    }
}

bool cloud_is_recording(void)
{
    return recording_active;
}
