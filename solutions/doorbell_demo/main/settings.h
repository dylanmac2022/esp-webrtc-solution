/* General settings for Lab 7 — Cloud service deployment

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#pragma once

#include "sdkconfig.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Video resolution settings
 */
#if CONFIG_IDF_TARGET_ESP32P4
#define VIDEO_WIDTH  1280
#define VIDEO_HEIGHT 960
#define VIDEO_FPS    15
#else
#define VIDEO_WIDTH  320
#define VIDEO_HEIGHT 240
#define VIDEO_FPS    10
#endif

/**
 * @brief  Set for wifi ssid
 */
#define WIFI_SSID     "Galaxy24"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "Dylan1234"

/**
 * @brief  Whether enable data channel
 */
#define DATA_CHANNEL_ENABLED (false)

/* ──────────────────── Lab 7: Cloud config ──────────────────── */

/**
 * @brief  Device identity used across all cloud APIs
 */
#define DEVICE_ID       "esp32p4-birdfeeder"

/**
 * @brief  WHIP ingress URL from LiveKit (set after running scripts/create-ingress.js)
 */
#define WHIP_URL        "https://lab7cloudservice-f6kwjbnt.whip.livekit.cloud/w"

/**
 * @brief  WHIP stream key / bearer token from LiveKit ingress
 */
#define WHIP_STREAM_KEY "rfNUnQ79yNhL"

/**
 * @brief  API Gateway base URL (from CloudFormation output ApiBaseUrl)
 */
#define API_BASE_URL    "https://wy3mcplope.execute-api.us-east-2.amazonaws.com"

/**
 * @brief  Shared API key for authenticating requests to the API Gateway
 */
#define DEVICE_API_KEY  "doorbell-lab7-key-2026-04-02-9f7c"

/**
 * @brief  AWS IoT Core MQTT endpoint
 */
#define AWS_IOT_ENDPOINT "a1k8wv61s9pyz-ats.iot.us-east-2.amazonaws.com"

/**
 * @brief  MQTT topic prefix for device commands
 */
#define MQTT_TOPIC_PREFIX "doorbell"

/**
 * @brief  LiveKit WebSocket URL (for webpage viewer token requests)
 */
#define LIVEKIT_WS_URL  "wss://lab7cloudservice-f6kwjbnt.livekit.cloud"

#define WEBRTC_SUPPORT_OPUS

#ifdef __cplusplus
}
#endif
