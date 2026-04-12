# Lab 7 Methodical Execution Plan (Code + Monitor Verifiable)

## Objective

Build Lab 7 in small, testable increments from your known-good Lab 6 baseline. Every step must have:

1. A code-level verification check.
2. A runtime verification check (ESP monitor output and/or API script output).
3. A commit point.

## Rules For This Plan

1. Do not start a new step until the previous step passes.
2. Each step ends with one commit.
3. Keep a short evidence log per step: command run, expected result, actual result, pass/fail.
4. Never commit secrets.

## Mandatory Verification Loop (Every Step)

Use this loop for every step so results are visible and repeatable:

1. Run firmware gate command: `ESP-IDF: Build, Flash and Monitor`.
2. Capture monitor evidence lines (at least 3 lines that prove correct behavior).
3. Run step-specific validation (API/script/UI check).
4. If code changed in the step, run `Build, Flash and Monitor` again after the change.
5. Commit only after both checks pass.

For cloud-only steps (Step 1 to Step 4), still run monitor and capture baseline device health lines:

1. Wi-Fi connected log line.
2. Room join prompt line.
3. No new runtime error lines during the check window.

## Baseline Freeze (Step 0)

Goal: Protect your working Lab 6 state before cloud work.

Code verification:
1. Build firmware successfully.
2. Confirm current room-join flow still works.

Monitor verification:
1. See Wi-Fi connection logs.
2. See room prompt like `Please use browser to join in ...`.

Commit:
1. `lab7:step0 freeze lab6 baseline`

---

## Step 1: Confirm Cloud Stack Health

Goal: Ensure the AWS SAM stack is deployed and healthy before firmware integration.

Code verification:
1. Confirm the stack outputs include `ApiBaseUrl`.
2. Confirm backend routes exist:
3. `POST /token/livekit`
4. `POST /media/upload-url`
5. `POST /events`
6. `GET /events`
7. `GET /events/{eventId}/playback`
8. `POST /device/command`

Runtime verification:
1. `aws cloudformation describe-stacks --stack-name doorbell-lab7 --region us-east-2 --query "Stacks[0].StackStatus" --output text`
2. Expected: `CREATE_COMPLETE` or `UPDATE_COMPLETE`.

Commit:
1. `lab7:step1 verify cloud stack and routes`

---

## Step 2: Verify LiveKit Token Service End-To-End

Goal: Prove token minting is correct and protected by API key.

Code verification:
1. Confirm token handler requires `deviceId` and `roomName`.
2. Confirm JWT grant includes room join and subscribe permissions.

Runtime verification:
1. Negative test without API key: expect unauthorized.
2. Positive test with API key: expect `token`, `roomName`, `wsUrl`, `expiresInSec`.
3. Run script:

```powershell
$env:API_BASE_URL="https://<api-id>.execute-api.<region>.amazonaws.com"
$env:DEVICE_API_KEY="<device-api-key>"
$env:LIVEKIT_WS_URL="wss://<project>.livekit.cloud"
$env:LIVEKIT_API_KEY="<livekit-api-key>"
$env:LIVEKIT_API_SECRET="<livekit-api-secret>"
npm.cmd run verify:livekit
```

Expected script output:
1. `PASS: LiveKit is configured and reachable.`

Commit:
1. `lab7:step2 verify livekit token path`

---

## Step 3: Verify Media Storage APIs (Upload, Events, Playback)

Goal: Prove full cloud media metadata pipeline works before touching firmware.

Code verification:
1. Confirm upload URL generation writes expected S3 key format.
2. Confirm `POST /events` idempotency on `eventId`.
3. Confirm playback API signs GET URLs from saved S3 keys.

Runtime verification:

```powershell
$env:API_BASE_URL="https://<api-id>.execute-api.<region>.amazonaws.com"
$env:DEVICE_API_KEY="<device-api-key>"
$env:VERIFY_DEVICE_ID="esp32p4-verify"
npm.cmd run verify:aws
```

Expected script output:
1. `PASS: AWS backend path verified.`
2. Printed `eventId` and `s3Key`.

Commit:
1. `lab7:step3 verify upload events playback apis`

---

## Step 4: Verify Device Command Publish Path

Goal: Prove cloud-to-device command publish works at MQTT topic level.

Code verification:
1. Confirm publish handler builds topic format `<prefix>/<deviceId>/commands`.
2. Confirm payload includes `command`, `payload`, `ts`, `correlationId`.

Runtime verification:
1. Subscribe in AWS IoT MQTT test client to `doorbell/+/commands`.
2. Call `POST /device/command` with valid `x-api-key`.
3. Expected: MQTT message appears with matching `deviceId` and `command`.

Commit:
1. `lab7:step4 verify cloud device command publish`

---

## Step 5: Add Firmware Cloud Config Layer

Goal: Add cloud config constants and boot-time validation to firmware.

Code changes:
1. Add cloud config fields in firmware settings (API base URL, device ID, API key source, feature flags).
2. Add a startup check function that validates required cloud config.
3. Add explicit logs with a dedicated tag (example: `CLOUD`).

Code verification:
1. Build succeeds.
2. No hardcoded secrets are committed.

Monitor verification:
1. On boot, expected logs like:
2. `I (...) CLOUD: Cloud config loaded`
3. `I (...) CLOUD: DeviceId=<...>`
4. If missing config: clear error log and feature disabled message.

Commit:
1. `lab7:step5 add firmware cloud config and boot validation`

---

## Step 6: Add Firmware LiveKit Token Fetch

Goal: Move room token acquisition to cloud API path and log result.

Code changes:
1. Add HTTP client call from firmware to `POST /token/livekit`.
2. Parse response JSON for `token`, `roomName`, `wsUrl`.
3. Integrate token into WebRTC join flow.

Code verification:
1. Build succeeds.
2. Failure paths are handled (timeout, 401, invalid JSON).

Monitor verification:
1. Expected sequence:
2. `I (...) CLOUD: Requesting LiveKit token`
3. `I (...) CLOUD: Token received, expiresInSec=<...>`
4. `I (...) Webrtc_Test: Start to join in room <...>`

Commit:
1. `lab7:step6 add token fetch and join integration`

---

## Step 7: Add Snapshot Upload Flow

Goal: Capture a photo, upload to S3 via pre-signed URL, and print event key.

Code changes:
1. Add a firmware trigger command for snapshot capture.
2. Request `POST /media/upload-url`.
3. Upload bytes to returned `uploadUrl`.
4. Log key and upload status.

Code verification:
1. Build succeeds.
2. Snapshot path returns non-zero payload.

Monitor verification:
1. Expected logs:
2. `I (...) CLOUD: Requesting upload URL for eventId=<...>`
3. `I (...) CLOUD: Upload URL received key=<...>`
4. `I (...) CLOUD: Snapshot upload success http=200`

Commit:
1. `lab7:step7 add cloud snapshot upload path`

---

## Step 8: Add Event Metadata Write From Firmware

Goal: Firmware writes event metadata after successful upload.

Code changes:
1. Call `POST /events` with `deviceId`, `eventTs`, `eventId`, `eventType`, `s3Keys`.
2. Log `created` vs `duplicate` behavior.

Code verification:
1. Build succeeds.
2. Event payload schema matches backend requirements.

Monitor verification:
1. Expected logs:
2. `I (...) CLOUD: Creating event metadata eventId=<...>`
3. `I (...) CLOUD: Event create result created=true` or `duplicate=true`

Commit:
1. `lab7:step8 add firmware event metadata write`

---

## Step 9: Add Cloud Playback Query Hook

Goal: Prove playback links can be requested for a known event and shown in UI/debug output.

Code changes:
1. Add request to `GET /events/{eventId}/playback?deviceId=<...>`.
2. Parse and log returned media links.

Code verification:
1. Build succeeds.
2. URL parsing handles missing media types safely.

Monitor verification:
1. Expected logs:
2. `I (...) CLOUD: Playback links ready eventId=<...>`
3. `I (...) CLOUD: snapshot=<https://...>`

Commit:
1. `lab7:step9 add playback lookup path`

---

## Step 10: Add Cloud Recording Control Command Path

Goal: Implement start/stop recording control path via backend command API.

Code changes:
1. Add command mapping in web/app control layer: `record_start`, `record_stop`.
2. Send command through `POST /device/command`.
3. Add firmware-side command acknowledgement log path.

Code verification:
1. Build succeeds.
2. Command payload schema is stable and versioned.

Monitor verification:
1. Cloud side: MQTT subscriber sees start/stop messages.
2. ESP monitor expected logs after command handling:
3. `I (...) CLOUD: Recording START command received`
4. `I (...) CLOUD: Recording STOP command received`

Commit:
1. `lab7:step10 add recording start stop command path`

---

## Step 11: Web Demo Integration Checkpoint

Goal: Validate the required demo actions in one page flow.

Required demo actions:
1. Live viewing.
2. Recording start/stop.
3. Photo capture.
4. Recorded video playback.
5. Photo preview.

Code verification:
1. Confirm UI actions map to actual API requests.
2. Confirm event IDs are surfaced so playback lookup is deterministic.

Runtime verification:
1. Browser network tab shows successful API calls.
2. ESP monitor shows matching action logs for each click.

Commit:
1. `lab7:step11 wire web demo controls to cloud workflow`

---

## Step 12: Full End-To-End Acceptance Run

Goal: One clean run proving complete Lab 7 behavior.

Run order:
1. Boot ESP and join room.
2. Start live view from web page.
3. Trigger snapshot upload.
4. Start recording.
5. Stop recording.
6. Create/list event.
7. Fetch playback URL.
8. Play recorded video and preview snapshot.

Pass criteria:
1. No failed API calls in cloud verification output.
2. Expected cloud logs appear in ESP monitor for each stage.
3. Media objects are present in S3 and retrievable via signed URLs.

Commit:
1. `lab7:step12 complete end-to-end validated`

---

## Suggested Verification Log Template

1. Step:
2. Commit hash:
3. Code diff summary:
4. Command(s) run:
5. Expected result:
6. Actual result:
7. Monitor evidence line(s):
8. Pass/fail:

## Security Checklist (Run Before Final Submission)

1. No API keys, LiveKit secret, or private keys in git history.
2. Device API key is injected via secure config, not hardcoded.
3. Temporary cloud resources are documented for cleanup.
4. After grading, disable or delete cloud resources to avoid charges.

## Execution Status (Live)

1. Step 0: Passed
2. Evidence: Build and flash completed; monitor shows Wi-Fi got IP, room join start, signaling success, and room prompt.
3. Note: `LCD_RENDER: Invalid argument to open` and `MEDIA_SYS: Fail to create video render` are present, but WebRTC room join and signaling still work.

4. Step 1: Passed
5. Evidence: CloudFormation stack `doorbell-lab7` is `UPDATE_COMPLETE`; all six API routes respond and are API-key protected.

6. Step 2: Passed
7. Evidence: `npm run verify:livekit` returns `PASS: LiveKit is configured and reachable.`

8. Step 3: Passed
9. Evidence: `npm run verify:aws` → `PASS: AWS backend path verified.` eventId=evt-1776027869854, s3Key confirmed.
10. Fixes applied: (1) created DynamoDB GSI `eventId-index`; (2) redeployed `GetPlaybackFunction` with correct GSI query code; (3) updated `doorbell-lab7-common-policy` to add `table/doorbell_events/index/*` to Query resource.

11. Step 4: Passed
12. Evidence: `POST /device/command` → `{"published":true,"topic":"doorbell/esp32p4-d5c7e8/commands","correlationId":"buaD8hmdCYcEMYQ="}`. CloudWatch shows clean invocation with no errors. Firmware flashed to COM9, MAC 80:f1:b2:d1:ec:63.
13. Fix applied: `PublishCommandFunction` had missing `https://` scheme on IoT endpoint — fixed in source and redeployed.

14. Step 5: Passed
15. Evidence: Firmware cloud config layer added and verified after build, flash, and monitor. Boot logs show `CLOUD` initialization lines including `Cloud config loaded`, derived `DeviceId=esp32p4-d5c7e8`, and configured API base URL.
16. Notes: `cloud_config_init()` now runs at startup, uses `esp_read_mac(..., ESP_MAC_WIFI_STA)` to derive device ID before network init, and does not embed secrets (API key remains NVS-injected).
