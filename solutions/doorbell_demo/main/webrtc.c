/* Lab 7 — WHIP WebRTC publisher for LiveKit cloud streaming
 *
 * Publishes H.264 video + OPUS audio from ESP32-P4 to LiveKit via WHIP ingress.
 * Replaces the Lab 6 AppRTC doorbell signaling with a simpler publish-only flow.
 */

#include "esp_webrtc.h"
#include "media_lib_os.h"
#include "common.h"
#include "esp_log.h"
#include "esp_webrtc_defaults.h"
#include "esp_peer_default.h"

#define TAG "WHIP_PUB"

static esp_webrtc_handle_t webrtc;

static int webrtc_event_handler(esp_webrtc_event_t *event, void *ctx)
{
    if (event->type == ESP_WEBRTC_EVENT_CONNECTING) {
        ESP_LOGI(TAG, "WHIP connecting (ICE/DTLS handshake)...");
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECTED) {
        ESP_LOGI(TAG, "WHIP connected — media streaming to LiveKit");
    } else if (event->type == ESP_WEBRTC_EVENT_CONNECT_FAILED) {
        ESP_LOGE(TAG, "WHIP connection failed");
    } else if (event->type == ESP_WEBRTC_EVENT_DISCONNECTED) {
        ESP_LOGW(TAG, "WHIP disconnected");
    }
    return 0;
}

int start_webrtc(char *url, char *token)
{
    if (network_is_connected() == false) {
        ESP_LOGE(TAG, "Wifi not connected yet");
        return -1;
    }
    if (!url || url[0] == 0) {
        ESP_LOGE(TAG, "WHIP URL not set");
        return -1;
    }
    if (webrtc) {
        esp_webrtc_close(webrtc);
        webrtc = NULL;
    }

    esp_peer_default_cfg_t peer_cfg = {
    };

    /* WHIP bearer token auth (stream key from LiveKit ingress) */
    esp_peer_signaling_whip_cfg_t whip_cfg = {
        .auth_type = ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BEARER,
        .token = token,
    };

    esp_webrtc_cfg_t cfg = {
        .peer_cfg = {
            .audio_info = {
#ifdef WEBRTC_SUPPORT_OPUS
                .codec = ESP_PEER_AUDIO_CODEC_OPUS,
                .sample_rate = 16000,
                .channel = 1,   /* Mono for WHIP ingress */
#else
                .codec = ESP_PEER_AUDIO_CODEC_G711A,
#endif
            },
            .video_info = {
                .codec = ESP_PEER_VIDEO_CODEC_H264,
                .width = VIDEO_WIDTH,
                .height = VIDEO_HEIGHT,
                .fps = VIDEO_FPS,
            },
            .audio_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY, /* Publish only — no talk-back */
            .video_dir = ESP_PEER_MEDIA_DIR_SEND_ONLY, /* Camera -> LiveKit only */
            .no_auto_reconnect = true,
            .extra_cfg = &peer_cfg,
            .extra_size = sizeof(peer_cfg),
        },
        .signaling_cfg = {
            .signal_url = url,
            .extra_cfg = token ? &whip_cfg : NULL,
            .extra_size = token ? sizeof(whip_cfg) : 0,
        },
        .peer_impl = esp_peer_get_default_impl(),
        .signaling_impl = esp_signaling_get_whip_impl(),  /* WHIP signaling */
    };

    int ret = esp_webrtc_open(&cfg, &webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to open webrtc");
        return ret;
    }

    /* Bind media sources (camera + mic) */
    esp_webrtc_media_provider_t media_provider = {};
    media_sys_get_provider(&media_provider);
    esp_webrtc_set_media_provider(webrtc, &media_provider);

    esp_webrtc_set_event_handler(webrtc, webrtc_event_handler, NULL);

    /* Enable peer connection immediately — WHIP auto-starts on SDP exchange */
    esp_webrtc_enable_peer_connection(webrtc, true);

    ret = esp_webrtc_start(webrtc);
    if (ret != 0) {
        ESP_LOGE(TAG, "Failed to start webrtc");
    } else {
        ESP_LOGI(TAG, "WHIP publish started to %s", url);
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
        esp_webrtc_handle_t handle = webrtc;
        webrtc = NULL;
        ESP_LOGI(TAG, "Closing WHIP session %p", handle);
        esp_webrtc_close(handle);
    }
    return 0;
}
