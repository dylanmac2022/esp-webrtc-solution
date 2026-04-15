/* MQTT Client for AWS IoT Core — receives commands from cloud
 *
 * Subscribes to doorbell/{DEVICE_ID}/commands and dispatches
 * capture_snapshot, record_start, record_stop to handlers.
 */

#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Command callback type
 */
typedef void (*mqtt_cmd_handler_t)(const char *command, const char *payload_json);

/**
 * @brief  Initialize and start MQTT connection to AWS IoT Core
 *
 * @param[in]  cmd_handler  Callback invoked on each received command
 * @return  0 on success
 */
int mqtt_client_start(mqtt_cmd_handler_t cmd_handler);

/**
 * @brief  Stop MQTT client
 */
void mqtt_client_stop(void);

/**
 * @brief  Check if MQTT is connected
 */
bool mqtt_client_is_connected(void);

#ifdef __cplusplus
}
#endif
