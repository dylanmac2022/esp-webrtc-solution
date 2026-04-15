# Lab 7 — Cloud Service Deployment for Media Data Management

## Architecture

```
ESP32-P4 ──WHIP──► LiveKit Cloud ◄──LiveKit JS SDK── Webpage (Viewer)
ESP32-P4 ◄──MQTT── AWS IoT Core ◄──Lambda── API Gateway ◄── Webpage (Commands)
ESP32-P4 ──HTTP PUT──► S3 (pre-signed URL from API) ──► Webpage (Playback/Preview)
```

---

## Phase 1: Cloud Backend Deployment & LiveKit Ingress Setup

### Step 1.1 — Deploy the AWS SAM Stack

The `cloud/.aws-sam/build/` directory contains the pre-built SAM template and 6 Lambda functions:

| Function | Endpoint | Purpose |
|----------|----------|---------|
| LivekitTokenFunction | `POST /token/livekit` | Generate LiveKit JWT tokens |
| MediaUploadUrlFunction | `POST /media/upload-url` | Generate pre-signed S3 PUT URLs |
| CreateEventFunction | `POST /events` | Log event metadata to DynamoDB |
| ListEventsFunction | `GET /events` | Query events by device + time range |
| GetPlaybackFunction | `GET /events/{eventId}/playback` | Generate signed S3 GET URLs |
| PublishCommandFunction | `POST /device/command` | Publish MQTT command via AWS IoT Core |

**Actions:**
1. Update `samconfig.toml` parameter_overrides with:
   - `LivekitApiSecret=jeTLqelkuVJfafM1f6x4QV4LcLLe5o3o5FpPRRmKGlqG`
   - `DeviceApiKey=<choose a shared secret UUID for API auth>`
   - `IotDataEndpoint=<real endpoint from aws iot describe-endpoint --endpoint-type iot:Data-ATS>`
2. Deploy: `sam deploy --config-file samconfig.toml`
3. Note the `ApiBaseUrl` from CloudFormation outputs

**Verification:**
```bash
curl -X POST {ApiBaseUrl}/token/livekit \
  -H "x-api-key: {DeviceApiKey}" \
  -H "Content-Type: application/json" \
  -d '{"deviceId":"esp32p4","roomName":"test","role":"publisher"}'
# Expected: JSON with {token, roomName, wsUrl, expiresInSec}
```

---

### Step 1.2 — Create LiveKit WHIP Ingress

Write a one-time Node.js script using `livekit-server-sdk` (already in `cloud/node_modules/`) to call `CreateIngress` API.

**Actions:**
1. Create `scripts/create-ingress.js`:
   - `input_type: WHIP`
   - Target room: `birdfeeder`
   - Participant identity: `esp32p4-cam`
2. Run the script
3. Save the returned **WHIP URL** and **stream key** — these go into ESP32-P4 firmware `settings.h`

**Verification:**
- Script outputs a WHIP URL (format: `https://ingress-*.livekit.cloud/w/...`) and a stream key
- Confirm ingress appears in LiveKit Cloud dashboard under Ingress tab

---

### Step 1.3 — Configure AWS IoT Core Thing

**Actions:**
1. In AWS IoT Console → Create Thing: `esp32p4-birdfeeder`
2. Create and download X.509 device certificate + private key + Amazon Root CA
3. Attach an IoT policy allowing:
   - `iot:Connect` on client ID `esp32p4-birdfeeder`
   - `iot:Subscribe` on topic filter `doorbell/esp32p4-birdfeeder/commands`
   - `iot:Receive` on topic `doorbell/esp32p4-birdfeeder/commands`
4. Place certs in `main/certs/`:
   - `device.crt` — device certificate
   - `device.key` — private key
   - `root_ca.pem` — Amazon Root CA
5. Update SAM stack `IotDataEndpoint` parameter with the real endpoint

**Verification:**
```bash
# Terminal 1: subscribe to commands
node scripts/mqtt-subscribe.js

# Terminal 2: publish a test command
node scripts/verify-device-command.js record_start
# Expected: Terminal 1 shows the received command JSON
```

---

## Phase 2: ESP32-P4 Firmware — LiveKit Streaming via WHIP

### Step 2.1 — Update `settings.h` with Cloud Configuration

**File:** `main/settings.h`

**Actions:** Add the following defines:
```c
#define WHIP_URL        "https://ingress-XXXX.livekit.cloud/w/YYYY"  // From Step 1.2
#define WHIP_STREAM_KEY "ZZZZ"                                       // From Step 1.2
#define DEVICE_ID       "esp32p4-birdfeeder"
#define API_BASE_URL    "https://XXXX.execute-api.us-east-2.amazonaws.com" // From Step 1.1
#define DEVICE_API_KEY  "your-shared-api-key"                        // From Step 1.1
#define AWS_IOT_ENDPOINT "XXXX-ats.iot.us-east-2.amazonaws.com"      // From Step 1.3
```

**Verification:** File compiles with no errors (syntax check only at this stage).

---

### Step 2.2 — Switch `webrtc.c` from AppRTC to WHIP Signaling

**File:** `main/webrtc.c`

**Actions:**
1. Include `esp_peer_whip_signaling.h`
2. Change `start_webrtc()` signature to accept WHIP URL + token
3. Replace `esp_signaling_get_apprtc_impl()` with `esp_signaling_get_whip_impl()`
4. Configure `esp_peer_signaling_whip_cfg_t` with `auth_type = ESP_PEER_SIGNALING_WHIP_AUTH_TYPE_BEARER`
5. Change `audio_dir` from `ESP_PEER_MEDIA_DIR_SEND_RECV` to `ESP_PEER_MEDIA_DIR_SEND_ONLY`
6. Remove doorbell call-flow state machine (RING/ACCEPT_CALL/DENY_CALL/OPEN_DOOR commands)
7. Call `esp_webrtc_enable_peer_connection(webrtc, true)` immediately (no gating)
8. Remove `key_monitor_thread` (doorbell button not needed)

**Reference:** `solutions/whip_demo/main/webrtc.c` — use as template

**Verification:**
- `idf.py build` succeeds
- No undefined symbol errors for removed functions

---

### Step 2.3 — Update `main.c` for Auto-Start WHIP

**File:** `main/main.c`

**Actions:**
1. Replace `join_room()` / AppRTC room URL generation with WHIP auto-connect
2. In `network_event_handler()`: call `start_webrtc(WHIP_URL, WHIP_STREAM_KEY)` on Wi-Fi connect
3. Remove AppRTC server selection CLI (`server` command)
4. Remove `cmd ring` CLI command
5. Keep: `wifi`, `i`, `leave`, `bitrate` console commands

**Verification:**
- `idf.py build` succeeds
- Flash → ESP32-P4 serial log shows: "Start to publish via WHIP" then "WHIP connected"

---

### Step 2.4 — Build, Flash, and Test LiveKit Streaming

**Actions:**
1. `idf.py build`
2. `idf.py flash monitor`

**Verification (all must pass):**
- [ ] ESP32-P4 connects to Wi-Fi (serial log: "Got IP address")
- [ ] SNTP time sync succeeds (serial log: "Time synced")
- [ ] WHIP signaling connects (serial log: "Get remote SDP")
- [ ] LiveKit Cloud dashboard → Rooms → `birdfeeder` room shows `esp32p4-cam` participant
- [ ] Open LiveKit Meet (`https://meet.livekit.io`) with a viewer token → see live H.264 video from camera

---

## Phase 3: ESP32-P4 Firmware — MQTT Command Reception

### Step 3.1 — Create MQTT Client Module

**Files:** New `main/mqtt_client.c` + `main/mqtt_client.h`

**Actions:**
1. Use ESP-IDF `esp_mqtt_client` API (library already available: `libmqtt.a`)
2. Connect to AWS IoT Core via MQTTS (port 8883) using embedded X.509 certificates
3. Subscribe to topic: `doorbell/{DEVICE_ID}/commands`
4. Parse incoming JSON with `cJSON`:
   - `capture_snapshot` → calls snapshot handler
   - `record_start` → calls recording start handler
   - `record_stop` → calls recording stop handler
5. Dispatch commands to handlers in a separate FreeRTOS task (not in MQTT callback)

**Verification:** Code compiles. MQTT function prototypes resolve.

---

### Step 3.2 — Update CMakeLists.txt

**File:** `main/CMakeLists.txt`

**Actions:**
```cmake
idf_component_register(
    SRCS "webrtc.c" "main.c" "board.c" "media_sys.c" "mqtt_client.c" "cloud_upload.c"
    EMBED_TXTFILES "ring.aac" "open.aac" "join.aac"
                   "certs/device.crt" "certs/device.key" "certs/root_ca.pem"
    INCLUDE_DIRS ".")
```

**Verification:** `idf.py build` succeeds with new source files and embedded certs.

---

### Step 3.3 — Initialize MQTT in `main.c`

**File:** `main/main.c`

**Actions:**
1. Include `mqtt_client.h`
2. In `network_event_handler()`, after SNTP sync: call `mqtt_client_start()`
3. Add `mqtt` console command for status check (optional)

**Verification:**
- Flash → serial log shows: "MQTT connected to {AWS_IOT_ENDPOINT}"
- Serial log shows: "Subscribed to doorbell/esp32p4-birdfeeder/commands"

---

### Step 3.4 — Test MQTT Command Reception

**Actions:**
1. Flash firmware, open serial monitor
2. From PC terminal, run: `node scripts/verify-device-command.js capture_snapshot`

**Verification:**
- [ ] ESP32-P4 serial log: `"Received MQTT command: capture_snapshot"`
- [ ] Repeat with `record_start` and `record_stop` — both logged correctly

---

## Phase 4: ESP32-P4 Firmware — Photo Capture & S3 Upload

### Step 4.1 — Create Cloud Upload Module

**Files:** New `main/cloud_upload.c` + `main/cloud_upload.h`

**Actions — implement 3 HTTP helpers using `esp_http_client` + `cJSON`:**

| Function | HTTP | Endpoint | Purpose |
|----------|------|----------|---------|
| `cloud_get_upload_url()` | POST | `/media/upload-url` | Get pre-signed S3 PUT URL |
| `cloud_upload_data()` | PUT | `{pre-signed URL}` | Upload binary data to S3 |
| `cloud_create_event()` | POST | `/events` | Create event metadata in DynamoDB |

All requests include `x-api-key: {DEVICE_API_KEY}` header.

**Verification:** Code compiles. Function signatures match expected API contracts.

---

### Step 4.2 — Implement Snapshot Capture

**File:** `main/cloud_upload.c` (or separate `main/snapshot.c`)

**Actions:**
1. Grab the current raw camera frame from the capture pipeline
2. JPEG-encode using `espressif__esp_new_jpeg` managed component (already in dependencies)
3. Store JPEG in PSRAM buffer
4. Return buffer pointer + size for upload

**Verification:** Manually trigger snapshot via console command → serial log: "Snapshot captured: {size} bytes"

---

### Step 4.3 — Wire Snapshot to MQTT Command Handler

**Actions — on `capture_snapshot` command:**
1. Generate UUID-style `eventId` and `sessionId`
2. Capture JPEG snapshot (Step 4.2)
3. Call `cloud_get_upload_url(DEVICE_ID, eventId, "snapshot", "image/jpeg", sessionId)`
4. Call `cloud_upload_data(uploadUrl, jpegBuffer, jpegSize, "image/jpeg")`
5. Call `cloud_create_event(DEVICE_ID, timestamp, eventId, "snapshot", sessionId, s3Keys)`
6. Free JPEG buffer

Run all of the above in a dedicated FreeRTOS task (not in MQTT callback, to avoid blocking).

**Verification:**
- [ ] Send `capture_snapshot` via `verify-device-command.js`
- [ ] ESP32 log: "Snapshot captured, uploading..." → "Upload complete"
- [ ] `GET /events?deviceId=esp32p4-birdfeeder&from=...&to=...` returns event with `eventType: "snapshot"`
- [ ] `GET /events/{eventId}/playback?deviceId=esp32p4-birdfeeder` returns signed URL
- [ ] Open signed URL in browser → photo displays correctly

---

## Phase 5: ESP32-P4 Firmware — Video Recording & S3 Upload

### Step 5.1 — Implement Recording Buffer

**File:** `main/cloud_upload.c` (or separate `main/recorder.c`)

**Actions:**
1. On `record_start`:
   - Set `recording_active = true` flag
   - Allocate PSRAM ring buffer for H.264 NAL units + AAC audio frames
   - Tap into the capture pipeline to copy encoded frames with timestamps
2. On `record_stop`:
   - Set `recording_active = false`
   - Stop accumulating frames

**Note:** Recording duration limited by PSRAM (~8MB). Expect ~10–30 seconds of H.264 at moderate bitrate. This is acceptable for demo.

**Verification:** Serial log: "Recording started" → (10s) → "Recording stopped, {N} frames buffered"

---

### Step 5.2 — Mux and Upload Recording

**Actions:**
1. Use `espressif__esp_muxer` managed component to mux buffered H.264 + AAC into MP4 in PSRAM
2. Call `cloud_get_upload_url(DEVICE_ID, eventId, "video", "video/mp4", sessionId)`
3. Call `cloud_upload_data(uploadUrl, mp4Buffer, mp4Size, "video/mp4")`
4. Call `cloud_create_event(...)` with `durationSec` and `s3Keys: {video: <key>}`
5. Free buffers

**Verification:**
- [ ] Send `record_start`, wait 10s, send `record_stop`
- [ ] ESP32 log: "Muxing MP4..." → "Upload complete, duration: Xs"
- [ ] Playback API returns signed URL → video plays in browser

---

## Phase 6: Demo Webpage

### Step 6.1 — Create `webpage/index.html`

**File:** New `webpage/index.html`

Single-page HTML application (opened locally in browser). Include LiveKit JS SDK from CDN (`unpkg.com/livekit-client`).

**Configuration** (constants at top of JS):
```javascript
const API_BASE_URL = "https://XXXX.execute-api.us-east-2.amazonaws.com";
const DEVICE_API_KEY = "your-shared-api-key";
const LIVEKIT_WS_URL = "wss://lab7cloudservice-f6kwjbnt.livekit.cloud";
const DEVICE_ID = "esp32p4-birdfeeder";
const ROOM_NAME = "birdfeeder";
```

**Sections:**

| Section | UI Elements | API Calls |
|---------|-------------|-----------|
| Live View | `<video>` element, Connect/Disconnect buttons, status indicator | `POST /token/livekit` (role=viewer) |
| Controls | Start Recording, Stop Recording, Capture Photo buttons | `POST /device/command` |
| Events Gallery | Scrollable grid of event cards with timestamp, type, preview/playback | `GET /events`, `GET /events/{id}/playback` |

**Verification:** File opens in browser without console errors. Layout renders correctly.

---

### Step 6.2 — Implement LiveKit Viewer

**Actions:**
1. On "Connect" click: fetch viewer token from `POST /token/livekit`
2. Create `Room` instance, connect to LiveKit using token + wsUrl
3. Listen for `TrackSubscribed` event → attach remote video/audio to `<video>` element
4. Show connection status (Disconnected → Connecting → Connected)

**Verification:**
- [ ] Click Connect → status shows "Connected"
- [ ] Live video from ESP32-P4 camera renders in the `<video>` element
- [ ] Audio from ESP32-P4 microphone plays through browser speakers

---

### Step 6.3 — Implement Recording Controls

**Actions:**
1. "Start Recording" button → `POST /device/command` with `{"deviceId":"...","command":"record_start","payload":{}}`
2. "Stop Recording" button → `POST /device/command` with `{"deviceId":"...","command":"record_stop","payload":{}}`
3. Toggle button states (disable Start while recording, etc.)
4. Show recording duration timer

**Verification:**
- [ ] Click Start Recording → ESP32 serial log: "Received MQTT command: record_start"
- [ ] Click Stop Recording → ESP32 serial log: "Received MQTT command: record_stop"
- [ ] Button states toggle correctly

---

### Step 6.4 — Implement Photo Capture

**Actions:**
1. "Capture Photo" button → `POST /device/command` with `{"deviceId":"...","command":"capture_snapshot","payload":{}}`
2. Show "Capturing..." spinner
3. After 5 second delay, auto-refresh event list

**Verification:**
- [ ] Click Capture Photo → ESP32 captures + uploads → photo appears in gallery

---

### Step 6.5 — Implement Events Gallery with Preview/Playback

**Actions:**
1. "Refresh Events" button → `GET /events?deviceId={DEVICE_ID}&from={24h_ago}&to={now}&limit=20`
2. For each event, render a card:
   - Timestamp + event type badge
   - If `snapshot`: load image via `GET /events/{id}/playback` → `<img src="{signedUrl}">`
   - If `video`: `<video controls src="{signedUrl}">` with play button
3. Click card to expand full-size preview

**Verification:**
- [ ] Gallery lists previously captured photos and recorded videos
- [ ] Photo thumbnails load from S3 signed URLs
- [ ] Video playback works inline (click play → video streams from S3)

---

## Files Summary

| Action | File | Description |
|--------|------|-------------|
| Modify | `main/settings.h` | Add WHIP URL, stream key, API config, IoT endpoint |
| Modify | `main/webrtc.c` | Switch AppRTC → WHIP signaling, remove doorbell state machine |
| Modify | `main/main.c` | Auto WHIP connect, MQTT init, updated CLI |
| Modify | `main/common.h` | Updated function declarations |
| Modify | `main/CMakeLists.txt` | Add new sources + cert embeds |
| Create | `main/mqtt_client.c` | AWS IoT MQTT subscriber + command dispatcher |
| Create | `main/mqtt_client.h` | MQTT module public API |
| Create | `main/cloud_upload.c` | S3 upload, snapshot capture, recording, API calls |
| Create | `main/cloud_upload.h` | Cloud upload module public API |
| Create | `main/certs/device.crt` | AWS IoT X.509 device certificate |
| Create | `main/certs/device.key` | AWS IoT private key |
| Create | `main/certs/root_ca.pem` | Amazon Root CA |
| Create | `webpage/index.html` | Demo webpage (local HTML) |
| Create | `scripts/create-ingress.js` | One-time LiveKit WHIP ingress creation |

---

## End-to-End Verification Checklist

| # | Test | Expected Result |
|---|------|-----------------|
| 1 | ESP32-P4 boot | Connects Wi-Fi, syncs time, publishes via WHIP, connects MQTT |
| 2 | LiveKit dashboard | `birdfeeder` room shows `esp32p4-cam` participant with video track |
| 3 | Webpage Live View | Click Connect → live H.264 video + audio from ESP32-P4 |
| 4 | Webpage Photo Capture | Click Capture Photo → photo appears in gallery within ~5s |
| 5 | Webpage Start/Stop Recording | Click Start → wait 10s → Stop → video appears in gallery |
| 6 | Photo Preview | Click photo in gallery → full-size image loads from S3 |
| 7 | Video Playback | Click video in gallery → plays inline from S3 |
| 8 | MQTT flow | All commands visible in ESP32 serial log with correct payloads |

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| **WHIP Ingress** (not native LiveKit protocol) | Reuses existing `esp_signaling_get_whip_impl()` — no new signaling code needed |
| **Audio send-only** in WHIP mode | WHIP ingress is publish-only; talk-back not required for Lab 7 |
| **AWS IoT Core X.509** for MQTT | Standard ESP-IDF pattern; one-time manual cert provisioning |
| **Local HTML file** for webpage | No hosting infrastructure needed; opens directly in browser |
| **PSRAM-limited recording** (~10–30s) | ~8MB PSRAM constrains buffer size; sufficient for demo |
| **Credentials not in git** | Certs in `main/certs/` added to `.gitignore` |
