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
 * @brief  Lab 7 cloud API base URL (example: https://xxxx.execute-api.us-east-1.amazonaws.com)
 */
#define CLOUD_API_BASE_URL "https://wy3mcplope.execute-api.us-east-2.amazonaws.com"

/**
 * @brief  API key used for authenticated device requests to Lab 7 cloud APIs
 */
#define CLOUD_DEVICE_API_KEY "doorbell-lab7-key-2026-04-02-9f7c"

/**
 * @brief  AWS IoT Core endpoint host (without protocol), e.g. a1b2c3d4e5f6-ats.iot.us-east-1.amazonaws.com
 */
#define AWS_IOT_ENDPOINT "a1k8wv61s9pyz-ats.iot.us-east-2.amazonaws.com"

/**
 * @brief  MQTT topic prefix used by cloud command publish API
 */
#define AWS_IOT_TOPIC_PREFIX "doorbell"

/**
 * @brief  PEM-formatted AWS IoT thing certificate
 */
#define AWS_IOT_CLIENT_CERT_PEM \
"-----BEGIN CERTIFICATE-----\n" \
"MIIDWTCCAkGgAwIBAgIUXF2/ofN/2WC5+sHkm6vj+JMI/9UwDQYJKoZIhvcNAQEL\n" \
"BQAwTTFLMEkGA1UECwxCQW1hem9uIFdlYiBTZXJ2aWNlcyBPPUFtYXpvbi5jb20g\n" \
"SW5jLiBMPVNlYXR0bGUgU1Q9V2FzaGluZ3RvbiBDPVVTMB4XDTI2MDQwMjIwMDE0\n" \
"N1oXDTQ5MTIzMTIzNTk1OVowHjEcMBoGA1UEAwwTQVdTIElvVCBDZXJ0aWZpY2F0\n" \
"ZTCCASIwDQYJKoZIhvcNAQEBBQADggEPADCCAQoCggEBAOtKLld7EMIW3bNOhzF2\n" \
"PuXJNqd/kY2AKDZ5pnGMnytOfcZYhemLalfVceJ2qRm/gUymUSE36SEH8nxG0LsF\n" \
"8cbfLN5xyj6WqDmxPOSoxsNx3qilrCsFx0tFxNfW6f8jESDWVILXhHsXr3e+z9mD\n" \
"XCTBPi72B87Zvtc8jB0pWUzRsxqi3BhUMR1LXRayINRfpVNZDW5aJgbcpl7NdTqP\n" \
"1bC5LOYq20b2Uk1X9ydS/0xQPjip5nRqLgjEQgGYC37HRlFy3GfOthDeO8KdDQXJ\n" \
"4YTIVowDBe7Ufv0b3ilK7+G6C+8jvymU32F0imKWeJ5wwOiVsA5YSWHZapmffCCf\n" \
"N8kCAwEAAaNgMF4wHwYDVR0jBBgwFoAU2vst6d9hsrjNCq5hVGxLQCoxb+QwHQYD\n" \
"VR0OBBYEFDgqRBJOjORH0OTRnQO7f0BnSK/ZMAwGA1UdEwEB/wQCMAAwDgYDVR0P\n" \
"AQH/BAQDAgeAMA0GCSqGSIb3DQEBCwUAA4IBAQA3jW3jefXPRyD48xMUttx90l1+\n" \
"YKuksEE4Tg5AoFNspjn5e9InhraDboILoQbn0UpBrYYuYF5rlIZ+Xn3CpYu5dw7M\n" \
"/ReAd0j+QbVi1qnoZ7mNaovUQH13LykAYqz6v6Go7yfJzWM6cHHgBW5q3qlubgHJ\n" \
"9YNA4TjBrPKv8NI8kVxescYtDX08pFtFsDhDF37Kqqgx4YT5jnYzxZNOLz5+OJ6L\n" \
"+lKtTATtPsbg18SD8uMz9bSpMZp2zAngMShWC24lnu6qixCIyO4seH+l0zVRyYPY\n" \
"2dNybsXDwSNrVcJy2VfUh9URNhZQ9xQJN7PysMN/FHQvDLhzU36rO7jxB59A\n" \
"-----END CERTIFICATE-----\n"

/**
 * @brief  PEM-formatted AWS IoT thing private key
 */
#define AWS_IOT_CLIENT_KEY_PEM  \
"-----BEGIN RSA PRIVATE KEY-----\n" \
"MIIEogIBAAKCAQEA60ouV3sQwhbds06HMXY+5ck2p3+RjYAoNnmmcYyfK059xliF\n" \
"6YtqV9Vx4napGb+BTKZRITfpIQfyfEbQuwXxxt8s3nHKPpaoObE85KjGw3HeqKWs\n" \
"KwXHS0XE19bp/yMRINZUgteEexevd77P2YNcJME+LvYHztm+1zyMHSlZTNGzGqLc\n" \
"GFQxHUtdFrIg1F+lU1kNblomBtymXs11Oo/VsLks5irbRvZSTVf3J1L/TFA+OKnm\n" \
"dGouCMRCAZgLfsdGUXLcZ862EN47wp0NBcnhhMhWjAMF7tR+/RveKUrv4boL7yO/\n" \
"KZTfYXSKYpZ4nnDA6JWwDlhJYdlqmZ98IJ83yQIDAQABAoIBABRbOKMs7Ig+PjQT\n" \
"KTMoTczHmcjoCom5esEryTCtv9+ZTNxqMDvCahLrTo0PQxYNMXyWLxK2qZ7H9zy1\n" \
"S73Ch+ZyzIj6Q0si8a78HI6T445pPaBNpRWbzGBAywT5fQkr2YGDyZAAYV/c7rtn\n" \
"cMgay1AKv/yEIKzOveoVPPIQ777rgx+/zhLNA8WfoHBeB5lFCLA1xQF/2UV+/A+s\n" \
"hIlLje2QGuVaEfDyXTqLKZZO7jpIlTvRQojzOXi/O50bfG3ZDuKA8EZjl1PFHycU\n" \
"DZpG7tLnSMSHtA9S2iywzZ4o5kmzZ7lJsaSUyhbO/y1YJ/BsE7HnOxSZKCt8WKzj\n" \
"9eXbvgECgYEA+ilguhTOZaWvZLyc5rC5duaFCslPxQvJmTiasxM2KGbnAAtydg2O\n" \
"Jnq+6JO0XWZKfOrdRyfwWMCvdXMRGCEqQtjydRo/mO2VSESZBk0fTXSZ8rjaDLRL\n" \
"TPT7e5zF37yNvrCggTM8176Ta7oWaz0Y09/7dlgBsMTYYT5DvXvEdiECgYEA8Mfz\n" \
"Cck66Q6HBIrp+ZtEkbLhUd7iV1knwADg2NPRoVF+t6tzKtg7A2rqgGDZCD5jsE84\n" \
"yRkbLgwuP0/9cefisSMV/sQg9fn/Z8oVg4A0ap97bje7mAUJiL1+pOA0UJeYxx4f\n" \
"DrkZ34+v0RNDw6tjep/nNO8gSpCzMNozb3WNvKkCgYB34kJ/ip8K6WbvgNA0YbbP\n" \
"u1NAww7eYHLBYfYJIYjPvdiwFcxJtN9No38/2CEUrYO+75MRmZs9/UFYqMclaCdn\n" \
"l65B3k1iDWGAG5e2BFme9eUdA+dDNVfszm6CY7QUL7lCDEUvBY3/2k2tz6UUyVfP\n" \
"mcRZh31v6DXGDF/MO7b/4QKBgHY9Sl0442QFTUpuyR1ZISAHXtysfiv0zS9dfw0b\n" \
"X6s/cOHTIPgePUSdYVDvvkRtFtlC3hjq6kz/kEppBoXEIK9qEmgMej8wqDqYo13z\n" \
"PEpLzPpABjBN8POkUqe2rhoRh+XoJco+HbWKQwWB11okNPLHyWtWLl5Plp9b17xZ\n" \
"VuQZAoGAZ3kAB7nG63/Ajh5Pgjw17S6yFqpJdk14RpUq2wB2wJzaJONv7f/CWqTT\n" \
"Q13pD0PcORctyytMyS8yNW894GhF3L8H9SbRTwce43TRJwGZgfrN+BXmQNEjrGCa\n" \
"Lj4bQ/x7EvGHx8hahOm57nPAiuulHibBm4oqLqUgcN1N7IlmaQE=\n" \
"-----END RSA PRIVATE KEY-----\n"

#ifdef __cplusplus
}
#endif
