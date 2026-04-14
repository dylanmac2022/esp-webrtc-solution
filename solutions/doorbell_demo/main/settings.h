/* General settings

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

#if CONFIG_IDF_TARGET_ESP32P4
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32P4-Fuction-Ev-Board, GPIO35(boot button) is connected RMII_TXD1
 *        When enable `NETWORK_USE_ETHERNET` will cause socket error
 *        User must replace it to a unused GPIO instead (like GPIO27)
 */
#define DOOR_BELL_RING_BUTTON  35
#else
/**
 * @brief  GPIO for ring button
 *
 * @note  When use ESP32S3-KORVO-V3 Use ADC button as ring button
 */
#define DOOR_BELL_RING_BUTTON  5

#endif

#define WEBRTC_SUPPORT_OPUS

/**
 * @brief  Cloud API base URL (not a secret)
 */
#define CLOUD_API_BASE_URL  "https://ofwruqgx9c.execute-api.us-east-2.amazonaws.com"

/**
 * @brief  Enable cloud integration features
 */
#define CLOUD_ENABLED       (true)

/**
 * @brief  Device API key placeholder (provision from NVS; do not commit real key)
 */
#define CLOUD_DEVICE_API_KEY "doorbell-lab7-key-2026-04-02-9f7c"

#ifdef __cplusplus
}
#endif
