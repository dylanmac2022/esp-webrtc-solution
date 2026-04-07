# Lab 7 Implementation Status

This repository now contains a full implementation scaffold for the Lab 7 requirements:

- Firmware cloud hooks in the ESP32-P4 app
- AWS backend (Lambda + API + DynamoDB + S3 + IoT command publish)
- Browser dashboard for live viewing, control, history, and playback

## What Was Implemented

## 1) Firmware (ESP32-P4)

Files:
- `main/cloud_client.c`
- `main/cloud_client.h`
- `main/webrtc.c`
- `main/main.c`
- `main/settings.h`

Implemented behavior:
- Device ID generated from MAC and passed to cloud client.
- LiveKit token fetch from `/token/livekit` at session start (publisher role).
- Event metadata posting to API `/events` for:
  - `ring`
  - `open_door`
  - `call_accepted`
  - `call_denied`
  - `call_start`
  - `call_end`
  - `record_start`
  - `record_stop`
  - `photo_capture`
- Device media upload flow integrated:
  - `photo_capture`: captures MJPEG frame from camera pipeline, requests pre-signed upload URL, uploads snapshot object, posts event with `s3Keys.snapshot`.
  - `record_stop`: requests pre-signed upload URL, uploads audio object, posts event with `s3Keys.audio`.
- MQTT subscription to AWS IoT topic:
  - `<topicPrefix>/<deviceId>/commands`
- MQTT command handling mapped to device command flow.

Configuration values to set in `main/settings.h`:
- `CLOUD_API_BASE_URL`
- `CLOUD_DEVICE_API_KEY`
- `AWS_IOT_ENDPOINT`
- `AWS_IOT_TOPIC_PREFIX`
- `AWS_IOT_CLIENT_CERT_PEM`
- `AWS_IOT_CLIENT_KEY_PEM`

## 2) AWS Cloud Backend

Folder:
- `cloud/`

Implemented handlers:
- `POST /token/livekit` -> `cloud/src/handlers/livekitToken.js`
- `POST /media/upload-url` -> `cloud/src/handlers/mediaUploadUrl.js`
- `POST /events` -> `cloud/src/handlers/createEvent.js`
- `GET /events` -> `cloud/src/handlers/listEvents.js`
- `GET /events/{eventId}/playback` -> `cloud/src/handlers/getPlayback.js`
- `POST /device/command` -> `cloud/src/handlers/publishCommand.js`

Infrastructure template:
- `cloud/template.yaml` (AWS SAM)

Support libraries:
- `cloud/src/lib/http.js`
- `cloud/src/lib/request.js`
- `cloud/src/lib/env.js`

## 3) Browser Dashboard

Folder:
- `dashboard/`

Files:
- `dashboard/index.html`
- `dashboard/main.js`

Implemented UI functions:
- Connect LiveKit viewer session via `/token/livekit`
- Live video rendering from remote track
- Publish MQTT commands via `/device/command`:
  - start recording
  - stop recording
  - capture photo
  - open door
- Load event history via `GET /events`
- Open playback link via `GET /events/{eventId}/playback`

## Validation Performed Locally

- ESP-IDF build executed successfully with cloud integration added.
- Output binary generated: `build/doorbell_demo.bin`.

## Current Limitation

- Firmware signaling path is currently AppRTC-based in this project source (`esp_signaling_get_apprtc_impl`), so media publishing is not yet switched to native LiveKit signaling.

## Remaining Deployment Actions (Credential-Dependent)

These steps require your cloud account credentials and cannot be completed automatically from this environment:

1. Deploy SAM stack in `cloud/`.
2. Fill `settings.h` cloud and IoT credentials.
3. Provision AWS IoT Thing certificate and key.
4. Flash firmware and run monitor.
5. Host dashboard static files and set API base URL + API key.
6. Execute demo tests from `LAB7_PLAN.md` section 14.

## End-to-End Run Order

1. Deploy backend (`cloud/`).
2. Configure firmware secrets in `main/settings.h`.
3. Build and flash board.
4. Open `dashboard/index.html` from a local static server.
5. Connect live view, trigger commands, and verify events/playback.
