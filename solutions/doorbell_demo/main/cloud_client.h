#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cloud_command_handler_t)(const char *command);

void cloud_client_init(const char *device_id);
void cloud_client_post_event(const char *event_type, const char *room_name);
bool cloud_client_fetch_livekit_token(const char *room_name, const char *role);
bool cloud_client_upload_media_event(const char *event_type,
									 const char *room_name,
									 const char *media_type,
									 const char *content_type,
									 const uint8_t *data,
									 size_t data_len);
void cloud_client_set_command_handler(cloud_command_handler_t handler);
void cloud_client_start_mqtt(void);

#ifdef __cplusplus
}
#endif
