/* Cloud upload module — S3 upload, snapshot capture, recording, API calls */

#pragma once

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize the cloud upload module (call once after network is ready)
 */
void cloud_upload_init(void);

/**
 * @brief  Handle a cloud command (called from MQTT dispatcher)
 *
 * Supported commands: "capture_snapshot", "record_start", "record_stop"
 * Runs heavy work in a background task.
 */
void cloud_handle_command(const char *command, const char *payload_json);

/**
 * @brief  Check if recording is active
 */
bool cloud_is_recording(void);

#ifdef __cplusplus
}
#endif
