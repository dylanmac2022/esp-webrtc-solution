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
#include <nvs_flash.h>
#include <sys/param.h>
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
#include "cloud_client.h"

static const char *TAG = "Webrtc_Test";

static struct {
    struct arg_str *room_id;
    struct arg_end *end;
} room_args;

static char room_url[128];

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

static const char *gen_device_id_use_mac(void)
{
    static char device_id[16];
    uint8_t mac[6];
    network_get_mac(mac);
    snprintf(device_id, sizeof(device_id), "esp-%02x%02x%02x", mac[3], mac[4], mac[5]);
    return device_id;
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
            for (int attempt = 0; attempt < 3; attempt++) {
                room = gen_room_id_use_mac(); // New random nonce each attempt
                snprintf(room_url, sizeof(room_url), "%s/join/%s", server_url, room);
                ESP_LOGI(TAG, "Start to join in room %s", room);
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
    media_sys_buildup();       // Steps 0-8: codecs registered, camera + mic streaming
    init_console();            // Serial REPL ready ("esp>" prompt)
    cloud_client_init(gen_device_id_use_mac());
    /* network_init() has internal retry logic for esp_wifi_init to handle transient Hosted startup delays */
    int net_ret = network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler); // Connect Wi-Fi -> Step A
    if (net_ret != 0) {
        ESP_LOGE(TAG, "network_init failed: 0x%x, running without cloud link", net_ret);
    }
    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc(); // Print RTP send/recv counters every 2 s (V:XXXX A:XXXX in log)
    }
}
