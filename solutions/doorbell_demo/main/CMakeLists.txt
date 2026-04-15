idf_component_register(SRCS "cloud_upload.c" "iot_mqtt.c" "webrtc.c"  "main.c" "board.c" "media_sys.c"
                       EMBED_TXTFILES "ring.aac" "open.aac" "join.aac"
                                      "certs/device.crt" "certs/device.key" "certs/root_ca.pem"
                       INCLUDE_DIRS ".")
