/* Lab 7 — Cloud-enabled bird feeder demo */

#pragma once

#include "settings.h"
#include "media_sys.h"
#include "network.h"
#include "sys_state.h"
#include "esp_capture.h"
#include "esp_capture_sink.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief  Initialize for board
 */
void init_board(void);

/**
 * @brief  Start WebRTC via WHIP
 *
 * @param[in]  url    WHIP ingress URL
 * @param[in]  token  Bearer token (stream key), or NULL
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int start_webrtc(char *url, char *token);

/**
 * @brief  Set stream bitrate for WebRTC
 *
 * @param[in]  audio    Whether set audio stream bitrate, false set to video
 * @param[in]  bitrate  Bitrate to set
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to start
 */
int set_webrtc_bitrate(bool audio, int bitrate);

/**
 * @brief  Query WebRTC status
 */
void query_webrtc(void);

/**
 * @brief  Stop WebRTC
 *
 * @return
 *      - 0       On success
 *      - Others  Fail to stop
 */
int stop_webrtc(void);

/**
 * @brief  Get the capture handle
 */
esp_capture_handle_t media_sys_get_capture_handle(void);

/**
 * @brief  Get pre-created snapshot sink (MJPEG, index 1)
 */
esp_capture_sink_handle_t media_sys_get_snapshot_sink(void);

#ifdef __cplusplus
}
#endif
