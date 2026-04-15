/* Lab 7 — Cloud-enabled bird feeder demo
 *
 * On Wi-Fi connect: publishes video via WHIP to LiveKit, starts MQTT for commands,
 * and initializes cloud upload module for photo/video capture.
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
#include "iot_mqtt.h"
#include "cloud_upload.h"

static const char *TAG = "Lab7_Main";

#define RUN_ASYNC(name, body)           \
    void run_async##name(void *arg)     \
    {                                   \
        body;                           \
        media_lib_thread_destroy(NULL); \
    }                                   \
    media_lib_thread_create_from_scheduler(NULL, #name, run_async##name, NULL);

/* ──────────────────── MQTT command dispatcher ──────────────────── */

// Called when a command arrives over MQTT from the web UI (e.g. CAPTURE_SNAPSHOT,
// RECORD_START, RECORD_STOP). Forwards directly to cloud_upload module for handling.
static void on_mqtt_command(const char *command, const char *payload_json)
{
    /* Forward all commands to the cloud upload module */
    cloud_handle_command(command, payload_json);
}

/* ──────────────────── Console CLI ──────────────────── */

static int leave_room(int argc, char **argv)
{
    RUN_ASYNC(leave, { stop_webrtc(); });
    return 0;
}

static int sys_cli(int argc, char **argv)
{
    sys_state_show();
    return 0;
}

static int wifi_cli(int argc, char **argv)
{
    if (argc < 2) {
        return -1;
    }
    char *ssid = argv[1];
    char *password = argc > 2 ? argv[2] : NULL;
    return network_connect_wifi(ssid, password);
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

static int snapshot_cli(int argc, char **argv)
{
    ESP_LOGI(TAG, "Manual snapshot triggered via console");
    cloud_handle_command("capture_snapshot", "{}");
    return 0;
}

static int record_cli(int argc, char **argv)
{
    if (argc < 2) {
        ESP_LOGI(TAG, "Usage: rec start|stop");
        return -1;
    }
    if (strcmp(argv[1], "start") == 0) {
        cloud_handle_command("record_start", "{}");
    } else if (strcmp(argv[1], "stop") == 0) {
        cloud_handle_command("record_stop", "{}");
    } else {
        ESP_LOGW(TAG, "Unknown rec subcommand: %s", argv[1]);
    }
    return 0;
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

static int init_console(void)
{
    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "esp>";
    repl_config.task_stack_size = 10 * 1024;
    repl_config.task_priority = 22;
    repl_config.max_cmdline_length = 1024;

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

    esp_console_cmd_t cmds[] = {
        {
            .command = "leave",
            .help = "Stop WHIP publish\n",
            .func = leave_room,
        },
        {
            .command = "i",
            .help = "Show system status\r\n",
            .func = sys_cli,
        },
        {
            .command = "wifi",
            .help = "wifi ssid psw\r\n",
            .func = wifi_cli,
        },
        {
            .command = "bitrate",
            .help = "Set audio or video bitrate\r\n",
            .func = bitrate_cli,
        },
        {
            .command = "snap",
            .help = "Capture and upload a snapshot\n",
            .func = snapshot_cli,
        },
        {
            .command = "rec",
            .help = "rec start|stop — control recording\n",
            .func = record_cli,
        },
        {
            .command = "rec2play",
            .help = "Play capture content\n",
            .func = capture_to_player_cli,
        },
        {
            .command = "m",
            .help = "Measure system loading\r\n",
            .func = measure_cli,
        },
    };
    for (int i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        ESP_ERROR_CHECK(esp_console_cmd_register(&cmds[i]));
    }
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
    return 0;
}

/* ──────────────────── Thread schedulers ──────────────────── */

static void thread_scheduler(const char *thread_name, media_lib_thread_cfg_t *schedule_cfg)
{
    if (strcmp(thread_name, "venc_0") == 0) {
        schedule_cfg->priority = 10;
#if CONFIG_IDF_TARGET_ESP32S3
        schedule_cfg->stack_size = 20 * 1024;
#endif
    }
#ifdef WEBRTC_SUPPORT_OPUS
    else if (strcmp(thread_name, "aenc_0") == 0) {
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
    else if (strcmp(thread_name, "Adec") == 0) {
        schedule_cfg->stack_size = 40 * 1024;
        schedule_cfg->priority = 10;
        schedule_cfg->core_id = 1;
    }
#endif
    else if (strcmp(thread_name, "AUD_SRC") == 0) {
        schedule_cfg->priority = 15;
    } else if (strcmp(thread_name, "pc_task") == 0) {
        schedule_cfg->stack_size = 25 * 1024;
        schedule_cfg->priority = 18;
        schedule_cfg->core_id = 1;
    }
    if (strcmp(thread_name, "start") == 0) {
        schedule_cfg->stack_size = 6 * 1024;
    }
    /* Give cloud command tasks more stack */
    if (strcmp(thread_name, "cloud_cmd") == 0) {
        schedule_cfg->stack_size = 12 * 1024;
    }
    /* Recording capture task needs stack for JPEG encoding */
    if (strcmp(thread_name, "rec_cap") == 0) {
        schedule_cfg->stack_size = 12 * 1024;
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

/* ──────────────────── Network event handler ──────────────────── */

static int network_event_handler(bool connected)
{
    if (connected) {
        // Wi-Fi is up — launch all cloud services in a background thread.
        RUN_ASYNC(start, {
            // (a) Sync system clock via SNTP so TLS certificate dates are valid.
            static bool sntp_synced = false;
            if (!sntp_synced) {
                if (0 == webrtc_utils_time_sync_init()) {
                    sntp_synced = true;
                }
            }

            // (b) Start live-streaming: send H.264 video + OPUS audio to LiveKit
            //     via the WHIP protocol. Viewers connect through the LiveKit room.
            ESP_LOGI(TAG, "Starting WHIP publish to LiveKit...");
            int ret = start_webrtc(WHIP_URL, WHIP_STREAM_KEY);
            if (ret == 0) {
                ESP_LOGW(TAG, "WHIP publish started — stream visible in LiveKit room 'birdfeeder'");
            } else {
                ESP_LOGE(TAG, "Failed to start WHIP publish");
            }

            // (c) Initialize the cloud upload module (S3 upload, snapshot/recording logic).
            cloud_upload_init();

            // (d) Connect to AWS IoT Core over MQTT (TLS + mutual auth).
            //     Subscribes to doorbell/{device}/commands for remote control.
            ESP_LOGI(TAG, "Starting MQTT connection to AWS IoT Core...");
            mqtt_client_start(on_mqtt_command);
        });
    } else {
        // Wi-Fi lost — tear down streaming and MQTT gracefully.
        stop_webrtc();
        mqtt_client_stop();
    }
    return 0;
}

/* ──────────────────── app_main ──────────────────── */

void app_main(void)
{
    // Step 1: Set log level and register the media library OS adapter (memory, threads, etc.)
    esp_log_level_set("*", ESP_LOG_INFO);
    media_lib_add_default_adapter();

    // Step 2: Register custom thread schedulers so capture and media tasks
    //         get the right stack sizes, priorities, and core pinning.
    esp_capture_set_thread_scheduler(capture_scheduler);
    media_lib_thread_set_schedule_cb(thread_scheduler);

    // Step 3: Initialize the board hardware — I2C bus, audio codec (ES8311),
    //         camera power/clock pins, and I2S for mic + speaker.
    init_board();

    // Step 4: Build the media pipeline — registers codecs (H.264, OPUS),
    //         opens camera + mic as a synchronized capture source, and
    //         sets up the local playback path (speaker + optional LCD).
    media_sys_buildup();

    // Step 5: Start the serial console (USB/UART) with CLI commands like
    //         snap, rec start/stop, wifi, leave, etc. for manual testing.
    init_console();

    // Step 6: Connect to Wi-Fi. When connected, the callback (network_event_handler)
    //         syncs time via SNTP, starts WHIP live-streaming to LiveKit,
    //         initializes cloud upload, and starts MQTT for IoT commands.
    network_init(WIFI_SSID, WIFI_PASSWORD, network_event_handler);

    // Main loop: periodically logs WebRTC connection stats (bitrate, packet loss).
    while (1) {
        media_lib_thread_sleep(2000);
        query_webrtc();
    }
}
