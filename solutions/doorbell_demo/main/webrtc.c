/* DoorBell WebRTC application code

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "driver/gpio.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"
#include "esp_peer_whip_signaling.h"

#define TAG "DOOR_BELL"
static const char *CLOUD_TAG = "CLOUD";

// Custom command strings exchanged over the AppRTC signaling data channel.
// These are plain-text commands sent peer-to-peer (not over RTP) to control
// the doorbell call flow before and after media transport is established.
#define DOOR_BELL_OPEN_DOOR_CMD     "OPEN_DOOR"    // Browser -> ESP32: user pressed "open door"
#define DOOR_BELL_DOOR_OPENED_CMD   "DOOR_OPENED"  // ESP32 -> Browser: door opener acknowledged
#define DOOR_BELL_RING_CMD          "RING"          // ESP32 -> Browser: doorbell button pressed
#define DOOR_BELL_CALL_ACCEPTED_CMD "ACCEPT_CALL"  // Browser -> ESP32: user accepted video call
#define DOOR_BELL_CALL_DENIED_CMD   "DENY_CALL"    // Browser -> ESP32: user rejected the call
#define DOOR_BELL_RECORD_START_CMD  "RECORD_START" // Browser/Cloud -> ESP32: start recording workflow
#define DOOR_BELL_RECORD_STOP_CMD   "RECORD_STOP"  // Browser/Cloud -> ESP32: stop recording workflow

#define SAME_STR(a, b) (strncmp(a, b, sizeof(b) - 1) == 0)
#define SEND_CMD(webrtc, cmd) \
    esp_webrtc_send_custom_data(webrtc, ESP_WEBRTC_CUSTOM_DATA_VIA_SIGNALING, (uint8_t *)cmd, strlen(cmd))

// door_bell_state_t: Tracks the current call lifecycle state.
// NONE       -> idle, waiting for a ring event
// RINGING    -> RING command sent, ring tone playing, waiting for browser response
// CONNECTING -> ACCEPT_CALL received, DTLS/ICE handshake in progress
// CONNECTED  -> Full WebRTC peer connection established, media flowing
typedef enum {
    DOOR_BELL_STATE_NONE,
    DOOR_BELL_STATE_RINGING,
    DOOR_BELL_STATE_CONNECTING,
    DOOR_BELL_STATE_CONNECTED,
} door_bell_state_t;

typedef enum {
    DOOR_BELL_TONE_RING,
    DOOR_BELL_TONE_OPEN_DOOR,
    DOOR_BELL_TONE_JOIN_SUCCESS,
} door_bell_tone_type_t;

typedef struct {
    const uint8_t *start;
    const uint8_t *end;
    int            duration;
} door_bell_tone_data_t;

static esp_webrtc_handle_t webrtc;
static door_bell_state_t   door_bell_state;
static bool                monitor_key;

extern const uint8_t ring_music_start[] asm("_binary_ring_aac_start");
extern const uint8_t ring_music_end[] asm("_binary_ring_aac_end");
extern const uint8_t open_music_start[] asm("_binary_open_aac_start");
extern const uint8_t open_music_end[] asm("_binary_open_aac_end");
extern const uint8_t join_music_start[] asm("_binary_join_aac_start");
extern const uint8_t join_music_end[] asm("_binary_join_aac_end");

static int play_tone(door_bell_tone_type_t type)
{
    door_bell_tone_data_t tone_data[] = {
        { ring_music_start, ring_music_end, 4000 },
        { open_music_start, open_music_end, 0 },
        { join_music_start, join_music_end, 0 },
    };
    if (type >= sizeof(tone_data) / sizeof(tone_data[0])) {
        return 0;
    }
    return play_music(tone_data[type].start, (int)(tone_data[type].end - tone_data[type].start), tone_data[type].duration);
}

int play_tone_int(int t)
{
    return play_tone((door_bell_tone_type_t)t);
}

static void door_bell_change_state(door_bell_state_t state)
{
    door_bell_state = state;
    // Stop any playing ring/open-door tone whenever we transition to
    // CONNECTING (call accepted, tone no longer needed) or NONE (call ended).
    if (state == DOOR_BELL_STATE_CONNECTING || state == DOOR_BELL_STATE_NONE) {
        stop_music();
    }
}

static int door_bell_on_cmd(esp_webrtc_custom_data_via_t via, uint8_t *data, int size, void *ctx)
{
    // This callback fires whenever a text command arrives from the browser peer
    // over the signaling data channel. Commands arrive as plain ASCII strings.
    if (size == 0 || webrtc == NULL) {
        return 0;
    }
    ESP_LOGI(TAG, "Receive command %.*s", size, (char *)data);
    const char *cmd = (const char *)data;
    if (SAME_STR(cmd, DOOR_BELL_OPEN_DOOR_CMD)) {
        // Browser pressed "Open Door" button; acknowledge and optionally play a tone.
        SEND_CMD(webrtc, DOOR_BELL_DOOR_OPENED_CMD);
        // Only play tome when connection not build up
        if (door_bell_state < DOOR_BELL_STATE_CONNECTING) {
            play_tone(DOOR_BELL_TONE_OPEN_DOOR);
        }
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_ACCEPTED_CMD)) {
        // Step 13: Browser accepted call; enable peer media transport.
        // This unblocks the ICE/DTLS handshake that was deliberately held back
        // (set to false in start_webrtc). Once enabled, the WebRTC engine completes
        // the handshake and begins sending H.264 video + OPUS audio to the browser.
        esp_webrtc_enable_peer_connection(webrtc, true);
    } else if (SAME_STR(cmd, DOOR_BELL_CALL_DENIED_CMD)) {
        // Step 13b: Browser denied call; stop peer media transport.
        // Disables the peer connection so no media is sent/received,
        // then resets state to NONE so the doorbell is ready for the next ring.
        esp_webrtc_enable_peer_connection(webrtc, false);
        door_bell_change_state(DOOR_BELL_STATE_NONE);
    } else if (SAME_STR(cmd, DOOR_BELL_RECORD_START_CMD) || SAME_STR(cmd, "record_start")) {
        ESP_LOGI(CLOUD_TAG, "Recording START command received");
    } else if (SAME_STR(cmd, DOOR_BELL_RECORD_STOP_CMD) || SAME_STR(cmd, "record_stop")) {
        ESP_LOGI(CLOUD_TAG, "Recording STOP command received");
    }
    return 0;
}

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    // Called by the WebRTC engine on connection lifecycle changes.
    // CONNECTING  : ICE/DTLS handshake started (peer enabled via ACCEPT_CALL)
    // CONNECTED   : DTLS done, SRTP keys exchanged, media flowing
    // CONNECT_FAILED / DISCONNECTED : peer dropped; reset to idle
    if (event->type == ESP_WEBRTC_EVENT_CONNECTING) {
        door_bell_change_state(DOOR_BELL_STATE_CONNECTING);
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        door_bell_change_state(DOOR_BELL_STATE_CONNECTED);
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED || event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        door_bell_change_state(DOOR_BELL_STATE_NONE);
    }
    return 0;
}

void send_cmd(char *cmd)
{
    // send_cmd() is called by the serial console ("cmd ring") or by the GPIO
    // key_monitor_thread when the physical doorbell button is pressed.
    // It sends the RING command to the browser peer over the signaling channel
    // and plays the ring tone locally on the speaker.
    if (SAME_STR(cmd, "ring")) {
        SEND_CMD(webrtc, DOOR_BELL_RING_CMD); // Notify browser: doorbell was pressed
        ESP_LOGI(TAG, "Ring button on state %d", door_bell_state);
        if (door_bell_state < DOOR_BELL_STATE_CONNECTING) {
            door_bell_state = DOOR_BELL_STATE_RINGING;
            play_tone(DOOR_BELL_TONE_RING); // Play 4-second AAC ring tone on local speaker
        }
    }
}

static void key_monitor_thread(void *arg)
{
    // Runs as a background FreeRTOS task to poll the physical doorbell button GPIO.
    // When the button level changes from its initial idle state (rising or falling edge)
    // it fires send_cmd("ring") - the same path as the serial console "cmd ring" command.
    gpio_config_t io_conf;
    memset(&io_conf, 0, sizeof(io_conf));
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = BIT64(DOOR_BELL_RING_BUTTON); // DOOR_BELL_RING_BUTTON defined in settings.h
    io_conf.pull_down_en = 1; // Internal pull-down: button reads 0 at rest, 1 when pressed
    esp_err_t ret = 0;
    ret |= gpio_config(&io_conf);

    media_lib_thread_sleep(50); // Allow GPIO to settle after configuration
    int last_level = gpio_get_level(DOOR_BELL_RING_BUTTON);
    int init_level = last_level; // Remember idle level to detect press direction

    while (monitor_key) {
        media_lib_thread_sleep(50); // Poll every 50 ms (20 Hz) - sufficient for debounce
        int level = gpio_get_level(DOOR_BELL_RING_BUTTON);
        if (level != last_level) {
            last_level = level;
            if (level != init_level) { // Detect edge away from idle = button pressed
                send_cmd("ring");
            }
        }
    }
    media_lib_thread_destroy(NULL);
}

int start_webrtc(char *url, char *auth_token, bool use_whip)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (url[0] == 0) {
        ESP_LOGE(TAG, "Room Url not set yet");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }
    monitor_key = true;
    media_lib_thread_handle_t key_thread;
    media_lib_thread_create_from_scheduler(&key_thread, "Key", key_monitor_thread, NULL);

    esp_peer_default_cfg_t peer_cfg = {
        .agent_recv_timeout = 500,
    };
    esp_peer_signaling_whip_cfg_t whip_cfg = {
        .auth_type = ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BEARER,
        .token = auth_token,
    };
    // Step 10: Define what media this device offers to browser peer.
    // The SDP offer sent during WebRTC negotiation is built from this config:
    //   Audio : OPUS codec, 16 kHz, stereo, SEND+RECV (full duplex talk-back)
    //   Video : H.264 codec, VIDEO_WIDTH x VIDEO_HEIGHT @ VIDEO_FPS, SEND-ONLY
    //           (ESP32-P4 streams camera to browser; browser does not send video back)
    // no_auto_reconnect=true means the ICE/DTLS handshake does NOT start automatically
    // when the signaling room connects - it waits for ACCEPT_CALL to gate it (Step 13).
    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS, // OPUS: high-quality, low-latency VoIP codec
                .sample_rate = 16000,               // 16 kHz narrowband - good for voice
                .channel = 2,                       // Stereo (interleaved L+R)
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A, // Fallback: G.711 (lower quality, less CPU)
#endif
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,  // H.264 hardware encoder on ESP32-P4
                .width = VIDEO_WIDTH,                // Defined in settings.h (e.g. 1280)
                .height = VIDEO_HEIGHT,              // Defined in settings.h (e.g. 720)
                .fps = VIDEO_FPS,                    // Defined in settings.h (e.g. 15)
            },
            .audio_dir = use_whip ? ESP_PEER_MEDIA_DIR_SEND_ONLY : ESP_PEER_MEDIA_DIR_SEND_RECV,
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY, // One-way video: camera -> browser only
            .on_custom_data = door_bell_on_cmd,         // Command handler for RING/ACCEPT/DENY
            .enable_data_channel = DATA_CHANNEL_ENABLED,
            .no_auto_reconnect = true, // No auto connect peer when signaling connected
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = url,
            .extra_cfg = use_whip ? &whip_cfg : NULL,
            .extra_size = use_whip ? sizeof(whip_cfg) : 0,
        },
        .peer_impl = esp_peer_get_default_impl(),           // Default ICE/DTLS peer implementation
        .signaling_impl = use_whip ? esp_signaling_get_whip_impl() : esp_signaling_get_apprtc_impl(),
    };
    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to open webrtc");
        return ret;
    }
    // Step 12: Bind media sources/sinks (from media_sys) into WebRTC engine.
    // media_sys_get_provider() fills in the capture handle (camera+mic -> browser)
    // and the player handle (browser audio -> speaker). After this call the WebRTC
    // engine knows where to pull encoded frames from and where to push decoded frames.
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

    // Set event handler
    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    if (use_whip) {
        // WHIP has no ACCEPT_CALL control channel in this flow, so connect immediately.
        esp_webrtc_enable_peer_connection(webrtc, true);
        ESP_LOGI(TAG, "Using WHIP signaling for LiveKit publish");
    } else {
        // Keep peer transport disabled until ACCEPT_CALL command is received.
        // This prevents ICE candidates and DTLS from being exchanged with the browser
        // until the user explicitly presses Accept on the browser UI.
        esp_webrtc_enable_peer_connection(webrtc, false);
        ESP_LOGI(TAG, "Using AppRTC signaling");
    }

    // Step 12b: Start signaling; SDP/ICE/DTLS will run after peer is enabled.
    // esp_webrtc_start() connects to the AppRTC room and waits for a browser peer.
    // Media does NOT flow yet - the peer connection is gated (false above).
    // Flow begins only when ACCEPT_CALL triggers enable_peer_connection(true) in Step 13.
    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Fail to start webrtc");
    } else {
        play_tone(DOOR_BELL_TONE_JOIN_SUCCESS);
    }
    return ret;
}

int set_webrtc_bitrate(bool audio, int bitrate)
{
    if (webrtc == NULL) {
        return -1;
    }
    if (audio) {
        return esp_webrtc_set_audio_bitrate(webrtc, bitrate);
    }
    return esp_webrtc_set_video_bitrate(webrtc, bitrate);
}

void query_webrtc(void)
{
    if (webrtc) {
        esp_webrtc_query(webrtc);
    }
}

int stop_webrtc(void)
{
    if (webrtc) {
        monitor_key = false;
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        ESP_LOGI(TAG, "Start to close webrtc %p", handle);
        esp_webrtc_close(handle);
    }
    return 0;
}
