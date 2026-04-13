/* Door Bell Demo

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <esp_wifi.h>
#include <esp_event.h>
#include <esp_log.h>
#include <esp_random.h>
#include <esp_system.h>
#include <esp_mac.h>
#include <esp_http_client.h>
#include <esp_crt_bundle.h>
#include <nvs_flash.h>
#include <sys/param.h>
#include <time.h>
#include <stdlib.h>
#include <cJSON.h>
#include "argtable3/argtable3.h"
#include "esp_console.h"
#include "esp_webrtc.h"
#include "media_lib_adapter.h"
#include "media_lib_os.h"
#include "esp_timer.h"
#include "webrtc_utils_time.h"
#include "esp_cpu.h"
#include "settings.h"
#include "common.h"
#include "esp_capture.h"

static const char *TAG = "Webrtc_Test";
static const char *CLOUD_TAG = "CLOUD";

static struct {
    struct arg_str *room_id;
    struct arg_end *end;
} room_args;

static char room_url[192];
static char cloud_token[1800];
static char cloud_room_name[64];
static char cloud_ws_url[128];
static int cloud_expires_in_sec = 0;

static void cloud_get_device_id(char *out, size_t out_size);

typedef struct {
    char *buf;
    int cap;
    int len;
} cloud_http_resp_t;

static esp_err_t cloud_http_event_handler(esp_http_client_event_t *evt)
{
    if (!evt || !evt->user_data) {
        return ESP_OK;
    }
    cloud_http_resp_t *resp = (cloud_http_resp_t *)evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data && evt->data_len > 0) {
        int copy = evt->data_len;
        if (resp->len + copy > resp->cap - 1) {
            copy = (resp->cap - 1) - resp->len;
        }
        if (copy > 0) {
            memcpy(resp->buf + resp->len, evt->data, copy);
            resp->len += copy;
            resp->buf[resp->len] = '\0';
        }
    }
    return ESP_OK;
}

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

char server_url[64] = "https://webrtc.espressif.com";

static int join_room(int argc, char **argv)
{
    int nerrors = arg_parse(argc, argv, (void **)&room_args);
    if (nerrors != 0) {
        arg_print_errors(stderr, room_args.end, argv[0]);
        return 1;
    }
    // Sync system time via SNTP once after boot.
    // WebRTC DTLS certificate validation requires an accurate wall-clock time;
    // without SNTP the TLS handshake will fail due to certificate date checks.
    static bool sntp_synced = false;
    if (sntp_synced == false) {
        if (0 == webrtc_utils_time_sync_init()) {
            sntp_synced = true;
        }
    }
    // Build the full AppRTC room URL and start the WebRTC session.
    // Format: https://webrtc.espressif.com/join/<room_id>
    const char *room_id = room_args.room_id->sval[0];
    snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, room_id);
    ESP_LOGI(TAG, "Start to join in room %s", room_id);
    start_webrtc(room_url);
    return 0;
}

static int leave_room(int argc, char **argv)
{
    RUN_ASYNC(leave, { stop_webrtc(); });
    return 0;
}

static int cmd_cli(int argc, char **argv)
{
    send_cmd(argc > 1 ? argv[1] : "ring");
    return 0;
}

static bool cloud_request_snapshot_upload_url(const char *device_id, const char *event_id,
                                              char *upload_url, size_t upload_url_size,
                                              char *key, size_t key_size)
{
    char url[196];
    snprintf(url, sizeof(url), "%s/media/upload-url", CLOUD_API_BASE_URL);

    char session_id[40];
    snprintf(session_id, sizeof(session_id), "sess-%lld", (long long)(esp_timer_get_time() / 1000));

    char body[320];
    snprintf(body, sizeof(body),
             "{\"deviceId\":\"%s\",\"eventId\":\"%s\",\"mediaType\":\"snapshot\",\"contentType\":\"image/jpeg\",\"sessionId\":\"%s\"}",
             device_id, event_id, session_id);

    size_t resp_cap = 8192;
    char *resp = (char *)calloc(resp_cap, 1);
    if (!resp) {
        ESP_LOGE(CLOUD_TAG, "Out of memory allocating upload-url response buffer");
        return false;
    }
    cloud_http_resp_t resp_ctx = {
        .buf = resp,
        .cap = (int)resp_cap,
        .len = 0,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = cloud_http_event_handler,
        .user_data = &resp_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp);
        return false;
    }

    esp_http_client_set_header(client, "content-type", "application/json");
    esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int read_len = resp_ctx.len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(CLOUD_TAG, "Upload-url request failed err=%s http=%d", esp_err_to_name(err), status);
        free(resp);
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(resp, read_len);
    if (!root) {
        ESP_LOGE(CLOUD_TAG, "Upload-url JSON parse failed read=%d", read_len);
        free(resp);
        return false;
    }
    cJSON *upload_url_json = cJSON_GetObjectItem(root, "uploadUrl");
    cJSON *key_json = cJSON_GetObjectItem(root, "key");
    if (!cJSON_IsString(upload_url_json) || !cJSON_IsString(key_json)) {
        cJSON_Delete(root);
        free(resp);
        ESP_LOGE(CLOUD_TAG, "Upload-url response missing uploadUrl/key");
        return false;
    }

    int url_need = snprintf(upload_url, upload_url_size, "%s", upload_url_json->valuestring);
    int key_need = snprintf(key, key_size, "%s", key_json->valuestring);
    if (url_need < 0 || (size_t)url_need >= upload_url_size) {
        cJSON_Delete(root);
        free(resp);
        ESP_LOGE(CLOUD_TAG, "Upload URL truncated need=%d cap=%u", url_need, (unsigned)upload_url_size);
        return false;
    }
    if (key_need < 0 || (size_t)key_need >= key_size) {
        cJSON_Delete(root);
        free(resp);
        ESP_LOGE(CLOUD_TAG, "S3 key truncated need=%d cap=%u", key_need, (unsigned)key_size);
        return false;
    }
    cJSON_Delete(root);
    free(resp);
    return true;
}

static bool cloud_upload_snapshot_bytes(const char *upload_url, const uint8_t *data, int size)
{
    char resp_buf[512] = {0};
    cloud_http_resp_t resp = {
        .buf = resp_buf,
        .cap = sizeof(resp_buf),
        .len = 0,
    };

    esp_http_client_config_t cfg = {
        .url = upload_url,
        .method = HTTP_METHOD_PUT,
        .timeout_ms = 20000,
        .buffer_size = 8192,
        .buffer_size_tx = 8192,
        .event_handler = cloud_http_event_handler,
        .user_data = &resp,
        .crt_bundle_attach = esp_crt_bundle_attach,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }

    esp_http_client_set_header(client, "content-type", "image/jpeg");

    // Keep upload path in perform() mode so HTTP client sets request framing reliably.
    esp_http_client_set_post_field(client, (const char *)data, size);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(CLOUD_TAG, "Snapshot upload failed: %s", esp_err_to_name(err));
        if (resp.len > 0) {
            ESP_LOGE(CLOUD_TAG, "Upload error body: %s", resp.buf);
        }
        return false;
    }
    if (!(status == 200 || status == 204)) {
        ESP_LOGE(CLOUD_TAG, "Snapshot upload HTTP %d", status);
        if (resp.len > 0) {
            ESP_LOGE(CLOUD_TAG, "Upload error body: %s", resp.buf);
        }
        return false;
    }
    ESP_LOGI(CLOUD_TAG, "Snapshot upload success http=%d", status);
    return true;
}

static bool cloud_create_snapshot_event(const char *device_id, const char *event_id,
                                        int64_t event_ts_ms, const char *s3_key)
{
    char url[196];
    snprintf(url, sizeof(url), "%s/events", CLOUD_API_BASE_URL);

    char body[640];
    snprintf(body, sizeof(body),
             "{\"deviceId\":\"%s\",\"eventTs\":%lld,\"eventId\":\"%s\",\"eventType\":\"doorbell\",\"s3Keys\":{\"snapshot\":\"%s\"},\"uploadStatus\":\"uploaded\",\"durationSec\":0}",
             device_id, (long long)event_ts_ms, event_id, s3_key);

    size_t resp_cap = 2048;
    char *resp = (char *)calloc(resp_cap, 1);
    if (!resp) {
        ESP_LOGE(CLOUD_TAG, "Out of memory allocating events response buffer");
        return false;
    }
    cloud_http_resp_t resp_ctx = {
        .buf = resp,
        .cap = (int)resp_cap,
        .len = 0,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = cloud_http_event_handler,
        .user_data = &resp_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp);
        return false;
    }

    esp_http_client_set_header(client, "content-type", "application/json");
    esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int read_len = resp_ctx.len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(CLOUD_TAG, "Create event request failed err=%s http=%d", esp_err_to_name(err), status);
        if (read_len > 0) {
            ESP_LOGE(CLOUD_TAG, "Create event error body: %s", resp);
        }
        free(resp);
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(resp, read_len);
    if (!root) {
        ESP_LOGE(CLOUD_TAG, "Create event JSON parse failed read=%d", read_len);
        free(resp);
        return false;
    }

    cJSON *created_json = cJSON_GetObjectItem(root, "created");
    cJSON *duplicate_json = cJSON_GetObjectItem(root, "duplicate");
    bool created = cJSON_IsBool(created_json) ? cJSON_IsTrue(created_json) : false;
    bool duplicate = cJSON_IsBool(duplicate_json) ? cJSON_IsTrue(duplicate_json) : false;

    if (created) {
        ESP_LOGI(CLOUD_TAG, "Event create result created=true");
    } else if (duplicate) {
        ESP_LOGI(CLOUD_TAG, "Event create result duplicate=true");
    } else {
        ESP_LOGW(CLOUD_TAG, "Event create response missing created/duplicate: %s", resp);
    }

    cJSON_Delete(root);
    free(resp);
    return created || duplicate;
}

static bool cloud_get_snapshot_playback_link(const char *device_id, const char *event_id,
                                             char *snapshot_link, size_t snapshot_link_size)
{
    char url[320];
    snprintf(url, sizeof(url), "%s/events/%s/playback?deviceId=%s", CLOUD_API_BASE_URL, event_id, device_id);

    size_t resp_cap = 8192;
    char *resp = (char *)calloc(resp_cap, 1);
    if (!resp) {
        ESP_LOGE(CLOUD_TAG, "Out of memory allocating playback response buffer");
        return false;
    }
    cloud_http_resp_t resp_ctx = {
        .buf = resp,
        .cap = (int)resp_cap,
        .len = 0,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_GET,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = cloud_http_event_handler,
        .user_data = &resp_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp);
        return false;
    }

    esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int read_len = resp_ctx.len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK || status != 200) {
        ESP_LOGE(CLOUD_TAG, "Playback request failed err=%s http=%d", esp_err_to_name(err), status);
        if (read_len > 0) {
            ESP_LOGE(CLOUD_TAG, "Playback error body: %s", resp);
        }
        free(resp);
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(resp, read_len);
    if (!root) {
        ESP_LOGE(CLOUD_TAG, "Playback JSON parse failed read=%d", read_len);
        free(resp);
        return false;
    }

    cJSON *links_json = cJSON_GetObjectItem(root, "links");
    cJSON *snapshot_json = links_json ? cJSON_GetObjectItem(links_json, "snapshot") : NULL;
    if (!cJSON_IsString(snapshot_json) || snapshot_json->valuestring == NULL || snapshot_json->valuestring[0] == '\0') {
        ESP_LOGE(CLOUD_TAG, "Playback response missing links.snapshot");
        cJSON_Delete(root);
        free(resp);
        return false;
    }

    int need = snprintf(snapshot_link, snapshot_link_size, "%s", snapshot_json->valuestring);
    cJSON_Delete(root);
    free(resp);
    if (need < 0 || (size_t)need >= snapshot_link_size) {
        ESP_LOGE(CLOUD_TAG, "Playback snapshot link truncated need=%d cap=%u", need, (unsigned)snapshot_link_size);
        return false;
    }
    return true;
}

static int snapshot_cli(int argc, char **argv)
{
    RUN_ASYNC(snapshot, {
        uint8_t *jpg = NULL;
        do {
            if (!CLOUD_ENABLED) {
                ESP_LOGW(CLOUD_TAG, "Cloud disabled, snapshot upload skipped");
                break;
            }
            if (CLOUD_DEVICE_API_KEY[0] == '\0') {
                ESP_LOGW(CLOUD_TAG, "Device API key not provisioned, snapshot upload skipped");
                break;
            }

            char device_id[24];
            cloud_get_device_id(device_id, sizeof(device_id));
            char event_id[40];
            snprintf(event_id, sizeof(event_id), "evt-%lld", (long long)(esp_timer_get_time() / 1000));
            int64_t event_ts_ms = (int64_t)time(NULL) * 1000;

            int jpg_size = 0;
            if (media_sys_capture_snapshot(&jpg, &jpg_size) != 0 || jpg == NULL || jpg_size <= 0) {
                ESP_LOGE(CLOUD_TAG, "Snapshot capture failed");
                break;
            }

            ESP_LOGI(CLOUD_TAG, "Requesting upload URL for eventId=%s", event_id);
            char upload_url[4096];
            char s3_key[256];
            if (!cloud_request_snapshot_upload_url(device_id, event_id, upload_url, sizeof(upload_url), s3_key, sizeof(s3_key))) {
                break;
            }
            ESP_LOGI(CLOUD_TAG, "Upload URL received key=%s", s3_key);

            if (!cloud_upload_snapshot_bytes(upload_url, jpg, jpg_size)) {
                break;
            }

            ESP_LOGI(CLOUD_TAG, "Creating event metadata eventId=%s", event_id);
            if (!cloud_create_snapshot_event(device_id, event_id, event_ts_ms, s3_key)) {
                break;
            }

            char snapshot_link[4096];
            bool playback_ok = false;
            for (int i = 0; i < 3; i++) {
                if (cloud_get_snapshot_playback_link(device_id, event_id, snapshot_link, sizeof(snapshot_link))) {
                    playback_ok = true;
                    break;
                }
                media_lib_thread_sleep(1200);
            }
            if (playback_ok) {
                ESP_LOGI(CLOUD_TAG, "Playback links ready eventId=%s", event_id);
                ESP_LOGI(CLOUD_TAG, "snapshot=%s", snapshot_link);
            } else {
                ESP_LOGE(CLOUD_TAG, "Playback lookup failed eventId=%s", event_id);
            }
        } while (0);

        if (jpg) {
            free(jpg);
        }
    });
    return 0;
}

static int assert_cli(int argc, char **argv)
{
    *(int *)0 = 0;
    return 0;
}

static int sys_cli(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 1) {
        return -1;
    }
    char *ssid = argv[1];
    char *password = argc > 2 ? argv[2] : NULL;
    return network_connect_wifi(ssid, password);
}

static int server_cli(int argc, char **argv)
{
    int server_sel = argc > 1 ? atoi(argv[1]) : 0;
    if (server_sel == 0) {
        strcpy(server_url, "https://webrtc.espressif.com");
    } else {
        strcpy(server_url, "https://webrtc.espressif.cn");
    }
    ESP_LOGI(TAG, "Select server %s", server_url);
    return 0;
}

static int bitrate_cli(int argc, char **argv)
{
    bool is_audio = false;
    int bitrate = 0;
    if (argc < 2) {
        return -1;
    } else if (argc == 2) {
        is_audio = true;
        bitrate = atoi(argv[1]);
    } else {
        if (strcmp(argv[1], "audio") == 0 || argv[1][0] == '1') {
            is_audio = true;
        }
        bitrate = atoi(argv[2]);
    }
    return set_webrtc_bitrate(is_audio, bitrate);
}

static int capture_to_player_cli(int argc, char **argv)
{
    return test_capture_to_player();
}

static int measure_cli(int argc, char **argv)
{
    void measure_enable(bool enable);
    void show_measure(void);
    measure_enable(true);
    media_lib_thread_sleep(1500);
    measure_enable(false);
    return 0;
}

static int init_console()
{
    // init_console() sets up the UART/USB serial REPL so commands can be typed
    // over the serial monitor to control the doorbell during the demo.
    // Available commands (registered below):
    //   join <room>  - manually join a specific AppRTC room
    //   leave        - leave the current WebRTC room
    //   cmd ring     - simulate pressing the doorbell button (sends RING to browser)
    //   i            - show CPU/memory/thread status
    //   wifi ssid pw - connect to a different Wi-Fi network at runtime
    //   bitrate ...  - adjust audio or video bitrate
    //   server 0|1   - switch between .com and .cn signaling servers
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;
    // install console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    room_args.room_id = arg_str1(NULL, NULL, "<w123456>", "room name");
    room_args.end = arg_end(2);
    esp_console_cmd_t cmds[] = {
        {
            .command = "join",
            .help = "Please enter a room name.\r\n",
            .func = join_room,
            .argtable = &room_args,
        },
        {
            .command = "leave",
            .help = "Leave from room\n",
            .func = leave_room,
        },
        {
            .command = "cmd",
            .help = "Send command (ring etc)\n",
            .func = cmd_cli,
        },
        {
            .command = "snapshot",
            .help = "Capture and upload one snapshot\n",
            .func = snapshot_cli,
        },
        {
            .command = "i",
            .help = "Show system status\r\n",
            .func = sys_cli,
        },
        {
            .command = "assert",
            .help = "Assert system\r\n",
            .func = assert_cli,
        },
        {
            .command = "rec2play",
            .help = "Play capture content\n",
            .func = capture_to_player_cli,
        },
        {
            .command = "wifi",
            .help = "wifi ssid psw\r\n",
            .func = wifi_cli,
        },
        {
            .command = "m",
            .help = "measure system loading\r\n",
            .func = measure_cli,
        },
        {
            .command = "server",
            .help = "Select server\r\n",
            .func = server_cli,
        },
         {
            .command = "bitrate",
            .help = "Set audio or video bitrate\r\n",
            .func = bitrate_cli,
        },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    // thread_scheduler() is called by the media library before creating each internal
    // task, allowing us to override stack size, priority, and CPU core affinity.
    // This is required because the default sizes are too small for OPUS and H.264.
    if (strcmp(thread_name, "venc_0") == 0) {
        // H.264 video encoder task - runs on whichever core the scheduler assigns.
        // Hardware encoder on P4 is fast; stack can stay small.
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024; // S3 uses software H.264 - needs more stack
#endif
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        // OPUS encoder task - software codec needs large stack (FFT tables, look-ahead buffer).
        // Pinned to core 1 to avoid competing with network/signaling on core 0.
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
    else if (strcmp(thread_name, "Adec") == 0) {
        // OPUS decoder task - same large stack requirement as encoder.
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
#endif
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        // Audio source thread - elevated priority so mic samples are never dropped.
        // Must run at higher priority than encoder to keep the pipeline filled.
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "pc_task") == 0) {
        // WebRTC peer connection task - ICE/DTLS/SRTP state machine.
        // Pinned to core 1, high priority to meet real-time RTP deadlines.
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
        schedule_cfg->core_id = 1;
    } else if (strcmp(thread_name, "snapshot") == 0) {
        schedule_cfg->stack_size = 20 * 1024;
        schedule_cfg->priority = 12;
    }
    if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024; // Signaling startup task - small stack is fine
    }
}

static void capture_scheduler(const char *name, esp_capture_thread_schedule_cfg_t *schedule_cfg)
{
    media_lib_thread_cfg_t cfg = {
        .stack_size = schedule_cfg->stack_size,
        .priority = schedule_cfg->priority,
        .core_id = schedule_cfg->core_id,
    };
    schedule_cfg->stack_in_ext = true;
    thread_scheduler(name, &cfg);
    schedule_cfg->stack_size = cfg.stack_size;
    schedule_cfg->priority = cfg.priority;
    schedule_cfg->core_id = cfg.core_id;
}

static void cloud_config_init(void)
{
    if (!CLOUD_ENABLED) {
        ESP_LOGW(CLOUD_TAG, "Cloud features disabled");
        return;
    }
    uint8_t mac[6];
    // Use base eFuse MAC here to avoid target-specific STA MAC-type errors.
    esp_efuse_mac_get_default(mac);
    char device_id[24];
    snprintf(device_id, sizeof(device_id), "esp32p4-%02x%02x%02x", mac[3], mac[4], mac[5]);
    ESP_LOGI(CLOUD_TAG, "Cloud config loaded");
    ESP_LOGI(CLOUD_TAG, "DeviceId=%s", device_id);
    ESP_LOGI(CLOUD_TAG, "ApiBaseUrl=%s", CLOUD_API_BASE_URL);
    ESP_LOGI(CLOUD_TAG, "ApiKey=configured-via-nvs (not stored in firmware)");
}

static void cloud_get_device_id(char *out, size_t out_size)
{
    uint8_t mac[6];
    if (network_get_mac(mac) != 0) {
        esp_efuse_mac_get_default(mac);
    }
    snprintf(out, out_size, "esp32p4-%02x%02x%02x", mac[3], mac[4], mac[5]);
}

static bool cloud_fetch_livekit_token(const char *device_id, const char *room_name)
{
    if (!CLOUD_ENABLED) {
        return false;
    }
    if (CLOUD_DEVICE_API_KEY[0] == '\0') {
        ESP_LOGW(CLOUD_TAG, "Device API key not provisioned yet; skipping cloud token fetch");
        ESP_LOGW(CLOUD_TAG, "ApiKeyPresent=false (set CLOUD_DEVICE_API_KEY or NVS source)");
        return false;
    }
    ESP_LOGI(CLOUD_TAG, "ApiKeyPresent=true");

    char url[160];
    snprintf(url, sizeof(url), "%s/token/livekit", CLOUD_API_BASE_URL);
    char body[256];
    snprintf(body, sizeof(body), "{\"deviceId\":\"%s\",\"roomName\":\"%s\",\"role\":\"publisher\"}",
             device_id, room_name);

    size_t resp_cap = 8192;
    char *resp = (char *)calloc(resp_cap, 1);
    if (!resp) {
        ESP_LOGE(CLOUD_TAG, "Out of memory allocating token response buffer");
        return false;
    }
    cloud_http_resp_t resp_ctx = {
        .buf = resp,
        .cap = (int)resp_cap,
        .len = 0,
    };

    esp_http_client_config_t cfg = {
        .url = url,
        .method = HTTP_METHOD_POST,
        .timeout_ms = 10000,
        .crt_bundle_attach = esp_crt_bundle_attach,
        .event_handler = cloud_http_event_handler,
        .user_data = &resp_ctx,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        free(resp);
        ESP_LOGE(CLOUD_TAG, "Failed to create HTTP client");
        return false;
    }

    esp_http_client_set_header(client, "content-type", "application/json");
    esp_http_client_set_header(client, "x-api-key", CLOUD_DEVICE_API_KEY);
    esp_http_client_set_post_field(client, body, strlen(body));

    esp_err_t err = esp_http_client_perform(client);
    int status = esp_http_client_get_status_code(client);
    int content_len = esp_http_client_get_content_length(client);
    int read_len = resp_ctx.len;
    esp_http_client_cleanup(client);

    if (err != ESP_OK) {
        ESP_LOGE(CLOUD_TAG, "Token request failed: %s", esp_err_to_name(err));
        free(resp);
        return false;
    }
    if (status != 200) {
        ESP_LOGE(CLOUD_TAG, "Token request HTTP %d: %s", status, resp);
        free(resp);
        return false;
    }

    cJSON *root = cJSON_ParseWithLength(resp, read_len);
    if (!root) {
        ESP_LOGE(CLOUD_TAG, "Token response JSON parse failed (status=%d, read=%d, content_len=%d)",
                 status, read_len, content_len);
        if (read_len >= (int)(resp_cap - 1)) {
            ESP_LOGE(CLOUD_TAG, "Token response may be truncated; increase response buffer size");
        }
        free(resp);
        return false;
    }
    cJSON *token = cJSON_GetObjectItem(root, "token");
    cJSON *json_room = cJSON_GetObjectItem(root, "roomName");
    cJSON *ws_url = cJSON_GetObjectItem(root, "wsUrl");
    cJSON *expires = cJSON_GetObjectItem(root, "expiresInSec");
    if (!cJSON_IsString(token) || !cJSON_IsString(json_room) || !cJSON_IsString(ws_url)) {
        cJSON_Delete(root);
        ESP_LOGE(CLOUD_TAG, "Token response missing required fields");
        return false;
    }

    snprintf(cloud_token, sizeof(cloud_token), "%s", token->valuestring);
    snprintf(cloud_room_name, sizeof(cloud_room_name), "%s", json_room->valuestring);
    snprintf(cloud_ws_url, sizeof(cloud_ws_url), "%s", ws_url->valuestring);
    cloud_expires_in_sec = cJSON_IsNumber(expires) ? expires->valueint : 0;
    cJSON_Delete(root);
    free(resp);

    ESP_LOGI(CLOUD_TAG, "Token received, expiresInSec=%d", cloud_expires_in_sec);
    ESP_LOGI(CLOUD_TAG, "wsUrl=%s", cloud_ws_url);
    return true;
}

static char* gen_room_id_use_mac(void)
{
    // Generate a unique room ID using the last 3 bytes of the Wi-Fi MAC address
    // plus a 16-bit random nonce. This ensures:
    //   1. No two ESP32 boards with the same MAC clash on the signaling server.
    //   2. A new nonce per boot prevents leftover browser sessions from the last run
    //      from accidentally joining the new session (fixes "FULL" signaling errors).
    // Example output: "esp_a1b2c3_7f4e"
    static char room_mac[24];
    uint8_t mac[6];
    uint16_t nonce = (uint16_t)(esp_random() & 0xFFFF); // Hardware RNG - unpredictable per boot
    network_get_mac(mac);
    // Use only mac[3..5] (last 3 octets) to keep the room name short and readable.
    snprintf(room_mac, sizeof(room_mac), "esp_%02x%02x%02x_%04x", mac[3], mac[4], mac[5], nonce);
    return room_mac;
}

static int network_event_handler(bool connected)
{
    if (connected) {
        // Step A: After Wi-Fi is up, create room URL and start signaling/call pipeline.
        // RUN_ASYNC spawns a one-shot FreeRTOS task so the Wi-Fi event loop is not blocked.
        // The retry loop tries up to 3 room IDs (each with a fresh random nonce) in case
        // the AppRTC signaling server returns FULL on the first attempt.
        RUN_ASYNC(start, {
            int ret = -1;
            char *room = NULL;
            char device_id[24];
            cloud_get_device_id(device_id, sizeof(device_id));
            for (int attempt = 0; attempt < 3; attempt++) {
                room = gen_room_id_use_mac(); // New random nonce each attempt
                ESP_LOGI(CLOUD_TAG, "Requesting LiveKit token for room %s", room);
                bool token_ok = cloud_fetch_livekit_token(device_id, room);
                if (token_ok) {
                    snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, cloud_room_name);
                    ESP_LOGW(CLOUD_TAG, "LiveKit token fetched; AppRTC signaling remains active for this step");
                } else {
                    snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, room);
                }
                ESP_LOGI(TAG, "Start to join in room %s", token_ok ? cloud_room_name : room);
                ret = start_webrtc(room_url);
                if (ret == 0) {
                    break; // Room joined successfully
                }
            }
            if (ret == 0) {
                // Print the room name and browser URL for the user.
                // Open https://webrtc.espressif.com/doorbell in Chrome and enter this room name.
                ESP_LOGW(TAG, "Please use browser to join in %s on %s/doorbell", room, server_url);
            } else {
                ESP_LOGE(TAG, "Failed to start webrtc after retries, check network/signaling");
            }
        });
    } else {
        stop_webrtc(); // Wi-Fi lost - tear down signaling and peer connection
    }
    return 0;
}

void app_main(void)
{
    // Step A0: Initialize board, media pipeline, console, then Wi-Fi -> WebRTC flow starts.
    // Execution order matters - each stage depends on the previous:
    //   1. esp_log_level_set   - enable INFO logs for all tags (visible in serial monitor)
    //   2. media_lib_add_default_adapter - hooks FreeRTOS primitives into the media library
    //   3. esp_capture_set_thread_scheduler / media_lib_thread_set_schedule_cb
    //                          - register our custom stack/priority/core overrides
    //   4. init_board()        - initializes I2C, I2S, LCD, codec (ES8311), MIPI CSI
    //   5. media_sys_buildup() - registers codecs, opens camera+mic, starts capture (Steps 0-8)
    //   6. init_console()      - starts the serial REPL ("esp>" prompt) for demo commands
    //   7. network_init()      - connects to Wi-Fi; on success fires network_event_handler
    //                           which calls start_webrtc() -> Steps 9-12b
    //   8. Main loop: query_webrtc() every 2 s prints TX/RX packet counters to the log
    esp_log_level_set("*", ESP_LOG_INFO);
    media_lib_add_default_adapter();
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);
    init_board();              // Hardware init: codec, camera, I2S, LCD
    cloud_config_init();       // Log cloud config at boot (Step 5)
    media_sys_buildup();       // Steps 0-8: codecs registered, camera + mic streaming
    init_console();            // Serial REPL ready ("esp>" prompt)
    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler); // Connect Wi-Fi -> Step A
    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc(); // Print RTP send/recv counters every 2 s (V:XXXX A:XXXX in log)
    }
}
