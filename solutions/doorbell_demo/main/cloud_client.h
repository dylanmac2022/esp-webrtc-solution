#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cloud_command_handler_t)(const char *command);

void cloud_client_init(const char *device_id);
void cloud_client_post_event(const char *event_type, const char *room_name);
void cloud_client_set_command_handler(cloud_command_handler_t handler);
void cloud_client_start_mqtt(void);

#ifdef __cplusplus
}
#endif
