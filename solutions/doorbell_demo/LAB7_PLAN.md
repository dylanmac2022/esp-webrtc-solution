# Lab 7 – Cloud Service Deployment for Media Data Management
### ESP32 Doorbell + LiveKit + AWS (End-to-End Implementation Plan)

---

## 1. Lab Objective

Build a cloud-backed media management pipeline for the ESP32 doorbell system that:

1. Uses LiveKit for real-time audio/video session orchestration.
2. Uses AWS for persistent media storage, metadata indexing, and secure API access.
3. Provides a browser dashboard for viewing live sessions and historical events.
4. Supports reliable upload, retrieval, and lifecycle management of recorded media.

---

## 2. Target Architecture (What You Are Building)

1. ESP32 doorbell captures audio/video and event signals.
2. Real-time stream is published through LiveKit room/session.
3. Cloud ingestion service receives event metadata and optional media chunks.
4. Media files are stored in S3.
5. Metadata is stored in DynamoDB.
6. API Gateway + Lambda expose secure endpoints for upload URL generation, metadata writes, and listing events.
7. Browser client uses:
   - LiveKit for live stream viewing.
   - AWS-backed APIs for historical event/media browsing.

---

## 3. Prerequisites Checklist

Complete all items before starting implementation.

1. AWS account with permission to create IAM, S3, DynamoDB, API Gateway, Lambda, CloudWatch.
2. LiveKit Cloud account (or self-hosted LiveKit Server).
3. Local tools installed:
   - AWS CLI v2
   - Node.js LTS (if using JS/TS Lambda packaging scripts)
   - Python 3.11+ (optional helper scripts)
   - VS Code with ESP-IDF extension
4. Existing Lab 6 doorbell build confirmed working on board.
5. Stable Wi-Fi/hotspot and browser (Chrome/Edge).

Verification:

1. `aws --version` succeeds.
2. You can sign in to AWS Console and LiveKit dashboard.
3. Board can still join and stream in current baseline flow.

---

## 4. Naming Convention (Set Once)

Define names early and keep consistent.

1. AWS region: choose one (example: `us-east-1`).
2. S3 bucket: `ece796-doorbell-media-<unique-suffix>`.
3. DynamoDB table: `doorbell_events`.
4. API name: `doorbell-media-api`.
5. Lambda prefix: `doorbellMedia*`.
6. LiveKit project: `ece796-doorbell-live`.
7. Device ID format: `esp-<mac>`.

Verification:

1. Record these values in a local lab notes file.
2. Use same values across all services.

---

## 5. LiveKit Setup (Detailed)

### Step 5.1 – Create LiveKit Project

1. Open LiveKit Cloud dashboard.
2. Create a new project for Lab 7.
3. Copy and save:
   - LiveKit URL
   - API Key
   - API Secret

Verification:

1. Project shows active status.
2. Credentials are visible and stored securely.

### Step 5.2 – Create Room Policy

1. Decide room naming strategy:
   - Per-device fixed room: `doorbell-esp-<mac>`
   - Or per-session room: `doorbell-esp-<mac>-<timestamp>`
2. Define participant roles:
   - Publisher: board/device
   - Subscriber/viewer: browser user
3. Define token TTL (example: 15 to 60 minutes).

Verification:

1. Policy documented.
2. You can explain who can publish vs subscribe.

### Step 5.3 – Create Token Service (Server-Side)

1. Implement a secure endpoint that returns LiveKit access tokens.
2. Input parameters:
   - `deviceId`
   - `role` (`publisher` or `viewer`)
   - `roomName`
3. Validate request and generate JWT with LiveKit API key/secret.
4. Never expose LiveKit secret to browser or device firmware.

Verification:

1. Token endpoint returns valid token JSON.
2. Invalid role or missing parameters return proper error.

### Step 5.4 – Test LiveKit Session Manually

1. Use LiveKit dashboard/test room page.
2. Join as publisher (simulated source or local camera).
3. Join second client as viewer.
4. Confirm audio/video subscription works.

Verification:

1. Two participants visible.
2. Viewer receives media without repeated disconnect loops.

---

## 6. AWS Foundation Setup

### Step 6.1 – Configure AWS CLI Profile

1. Run `aws configure`.
2. Enter access key, secret key, region, output format.
3. Verify identity:
   - `aws sts get-caller-identity`

Verification:

1. Command returns account and ARN.

### Step 6.2 – Create IAM Least-Privilege Roles

Create dedicated IAM roles/policies for:

1. Lambda execution role:
   - CloudWatch logs write
   - DynamoDB read/write on `doorbell_events`
   - S3 put/get/list on media bucket
2. Optional CI/deployment user role.
3. Optional pre-signed URL service role if separated.

Verification:

1. Policies attach successfully.
2. No wildcard admin policy for runtime services.

---

## 7. S3 Setup for Media Storage

### Step 7.1 – Create Media Bucket

1. Create bucket in selected region.
2. Keep public access blocked.
3. Enable versioning.
4. Enable default SSE encryption (SSE-S3 or SSE-KMS).

Verification:

1. Bucket exists with encryption and versioning enabled.

### Step 7.2 – Define Object Key Structure

Use deterministic path layout:

1. `deviceId/yyyy/mm/dd/sessionId/eventId/video.mp4`
2. `deviceId/yyyy/mm/dd/sessionId/eventId/audio.aac`
3. `deviceId/yyyy/mm/dd/sessionId/eventId/snapshot.jpg`
4. `deviceId/yyyy/mm/dd/sessionId/eventId/metadata.json`

Verification:

1. Path format documented and used by all upload code.

### Step 7.3 – Configure Lifecycle Policy

1. Transition old media to low-cost storage after N days.
2. Expire old test artifacts after M days.
3. Keep critical clips longer if tagged `important=true`.

Verification:

1. Lifecycle rules visible in bucket management page.

---

## 8. DynamoDB Setup for Metadata

### Step 8.1 – Create Table

1. Table name: `doorbell_events`.
2. Partition key: `deviceId` (string).
3. Sort key: `eventTs` (number or ISO string).
4. Billing mode: on-demand (recommended for lab).

Verification:

1. Table active.
2. You can insert and read test item.

### Step 8.2 – Add Access Patterns via GSI

Create GSI examples:

1. `sessionId-index` for session timeline queries.
2. `eventType-index` for filtering ring/open/motion events.
3. Optional `uploadStatus-index` for retry workflows.

Verification:

1. GSI status is active.
2. Query tests return expected results.

### Step 8.3 – Metadata Schema

Each event item should include:

1. `deviceId`
2. `eventTs`
3. `eventId`
4. `eventType` (`ring`, `open_door`, `motion`, `call_start`, `call_end`)
5. `sessionId`
6. `roomName`
7. `s3Keys` map (video/audio/snapshot)
8. `durationSec`
9. `uploadStatus`
10. `createdAt`

Verification:

1. Schema example stored in project notes.
2. API validates required fields.

---

## 9. Lambda + API Gateway Setup

### Step 9.1 – Lambda Functions to Implement

Create these functions:

1. `doorbellMediaGetUploadUrl`
   - Returns pre-signed PUT URL for S3 object.
2. `doorbellMediaCreateEvent`
   - Validates and writes event metadata to DynamoDB.
3. `doorbellMediaListEvents`
   - Lists events for a device/time range.
4. `doorbellMediaGetPlayback`
   - Returns pre-signed GET URLs for playback.
5. `doorbellLivekitToken`
   - Returns LiveKit token (if hosted in AWS).

Verification:

1. Each Lambda can be invoked in console test mode.
2. Logs appear in CloudWatch without unhandled exceptions.

### Step 9.2 – API Gateway Routes

Create HTTP API routes:

1. `POST /token/livekit`
2. `POST /media/upload-url`
3. `POST /events`
4. `GET /events`
5. `GET /events/{eventId}/playback`

Verification:

1. Deploy stage URL is active.
2. Route-level tests return JSON responses.

### Step 9.3 – Add Authentication

Choose one approach:

1. Cognito JWT authorizer for browser users.
2. API key + device secret for board requests (lab-simplified).
3. Signed request + short token for production-like flow.

Verification:

1. Unauthorized requests are rejected.
2. Authorized requests succeed.

### Step 9.4 – Add CORS and Validation

1. Enable CORS for browser dashboard origin.
2. Add request schema validation where possible.
3. Return consistent error model:
   - code
   - message
   - correlationId

Verification:

1. Browser calls succeed without CORS failures.
2. Invalid payloads produce clear 4xx responses.

---

## 10. Security Hardening (Required)

1. Store LiveKit and AWS secrets in AWS Secrets Manager or Parameter Store.
2. Never hardcode secrets in firmware or frontend code.
3. Encrypt all transport with HTTPS/WSS only.
4. Restrict IAM actions to specific bucket/table ARNs.
5. Enable CloudTrail for auditing.
6. Add S3 bucket policy to deny unencrypted transport.

Verification:

1. Secret values are not visible in source repository.
2. Security checks documented with screenshots or logs.

---

## 11. Device Integration Steps (ESP32 Doorbell)

### Step 11.1 – Add Cloud Configuration Block

Add runtime-configurable parameters:

1. API base URL
2. Device ID
3. LiveKit room naming mode
4. Upload retry limits/timeouts

Verification:

1. Firmware prints config summary at boot (without secrets).

### Step 11.2 – Integrate LiveKit Token Fetch

1. On call start/join, firmware requests token from `/token/livekit`.
2. Parse response and initialize session.
3. Add retry/backoff for transient network errors.

Verification:

1. Token fetch success visible in logs.
2. Invalid token path properly handled.

### Step 11.3 – Integrate Metadata Event Posting

At each key event, call `/events`:

1. Ring button press
2. Call accepted/rejected
3. Door opened
4. Call ended

Verification:

1. DynamoDB contains event records in chronological order.

### Step 11.4 – Integrate Media Upload Flow

1. Request pre-signed upload URL.
2. Upload media file/chunk to S3.
3. Post completion status to metadata endpoint.

Verification:

1. S3 object appears in expected key path.
2. Event item references uploaded object key.

---

## 12. Browser Dashboard Integration

### Step 12.1 – Live View

1. Join room as viewer using token from `/token/livekit`.
2. Render remote video/audio tracks.
3. Show participant/session state.

Verification:

1. Live video appears during active call.

### Step 12.2 – Event History View

1. Call `GET /events?deviceId=...&from=...&to=...`.
2. Render event list sorted by timestamp.
3. Add filters by type/status.

Verification:

1. New ring/open events appear within expected latency.

### Step 12.3 – Playback View

1. Select event.
2. Request playback links via `/events/{eventId}/playback`.
3. Stream or download media from pre-signed URLs.

Verification:

1. Playback works until URL expiry.
2. Expired URL refresh flow works.

---

## 13. Observability and Reliability

### Step 13.1 – Logging Standards

1. Add structured logs with fields:
   - `deviceId`
   - `eventId`
   - `sessionId`
   - `requestId`
2. Use CloudWatch log groups per Lambda.

Verification:

1. You can trace one event end-to-end across logs.

### Step 13.2 – Metrics and Alerts

1. CloudWatch metrics:
   - Lambda error rate
   - API 4xx/5xx
   - Upload failures
2. Alarm on elevated 5xx and repeated upload failures.

Verification:

1. Alarm test notification can be triggered.

### Step 13.3 – Retry/Idempotency

1. Add idempotency key (`eventId`) to create-event API.
2. Safe retry logic for transient upload/network failures.
3. Avoid duplicate DynamoDB records.

Verification:

1. Replayed requests do not create duplicate entries.

---

## 14. Validation Test Plan (Must Pass)

Run these tests and log outcomes.

1. Live stream functional test
   - Board starts call, browser receives A/V.
2. Door event metadata test
   - Trigger open door, event appears in DynamoDB.
3. Media upload test
   - Upload sample clip, verify S3 object and metadata link.
4. History retrieval test
   - Query events by time range returns expected records.
5. Playback URL test
   - Pre-signed GET URL plays media then expires correctly.
6. Auth rejection test
   - Missing/invalid token receives 401/403.
7. Resilience test
   - Temporary network drop, then recovery and resumed operation.

Exit criteria:

1. All seven tests pass.
2. No critical security finding (exposed secrets/public bucket).
3. Demo flow reproducible twice from clean boot.

---

## 15. Deliverables for Lab Submission

Prepare these artifacts.

1. Architecture diagram (LiveKit + AWS + ESP32 + Browser).
2. API specification (request/response examples).
3. IAM policy summary (least privilege evidence).
4. Test evidence:
   - screenshots
   - logs
   - timestamps
5. Short demo video:
   - live ring/call
   - metadata appearing
   - playback of stored media
6. Cost estimate for one week of usage.
7. Known limitations and future improvements.

---

## 16. Suggested Timeline (Execution Order)

Day 1:

1. LiveKit project + token service + basic room test.
2. AWS base setup (IAM, S3, DynamoDB).

Day 2:

1. Lambda/APIs for event + upload URL + list/playback.
2. Basic auth and CORS.

Day 3:

1. Firmware integration (event + upload + token).
2. Browser integration (live + history + playback).

Day 4:

1. Reliability hardening, logging, metrics, alarms.
2. Full validation pass and bug fixing.

Day 5:

1. Final demo capture.
2. Submission document packaging.

---

## 17. Common Failure Points and Mitigation

1. LiveKit token mismatch or expired token:
   - Recheck key/secret and TTL settings.
2. S3 upload denied:
   - Validate IAM role and bucket policy ARN scope.
3. API CORS failures:
   - Ensure exact frontend origin in API CORS config.
4. Duplicate event rows:
   - Enforce idempotency by `eventId` condition write.
5. Playback access denied:
   - Verify pre-signed URL generation region and key path.

---

## 18. Completion Checklist

Mark complete only when evidence exists.

1. LiveKit project configured and tested with two participants.
2. AWS S3 + DynamoDB + API + Lambda deployed.
3. Authentication enabled and validated.
4. ESP32 posts event metadata and uploads media.
5. Browser can view live stream and historical media.
6. Monitoring and alarms configured.
7. Final report and demo package ready.
