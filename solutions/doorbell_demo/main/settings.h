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
#define WIFI_SSID     "YOUR_WIFI_SSID"

/**
 * @brief  Set for wifi password
 */
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"

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
 * @brief  Lab 7 cloud API base URL (example: https://xxxx.execute-api.us-east-1.amazonaws.com)
 */
#define CLOUD_API_BASE_URL ""

/**
 * @brief  API key used for authenticated device requests to Lab 7 cloud APIs
 */
#define CLOUD_DEVICE_API_KEY ""

/**
 * @brief  AWS IoT Core endpoint host (without protocol), e.g. a1b2c3d4e5f6-ats.iot.us-east-1.amazonaws.com
 */
#define AWS_IOT_ENDPOINT ""

/**
 * @brief  MQTT topic prefix used by cloud command publish API
 */
#define AWS_IOT_TOPIC_PREFIX "doorbell"

/**
 * @brief  PEM-formatted AWS IoT thing certificate
 */
#define AWS_IOT_CLIENT_CERT_PEM ""

/**
 * @brief  PEM-formatted AWS IoT thing private key
 */
#define AWS_IOT_CLIENT_KEY_PEM ""

#ifdef __cplusplus
}
#endif
