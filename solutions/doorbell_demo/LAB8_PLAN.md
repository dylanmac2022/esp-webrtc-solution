# Lab 8 — Smartphone App for Media Data Control and Visualization

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                        React Native (Expo) App                         │
│                                                                         │
│  ┌──────────┐  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐  │
│  │  MQTT     │  │  Live Video  │  │  Photo       │  │  Video        │  │
│  │  Commands │  │  Viewer      │  │  Preview     │  │  Playback     │  │
│  └─────┬────┘  └──────┬───────┘  └──────┬───────┘  └──────┬────────┘  │
└────────┼───────────────┼────────────────┼──────────────────┼───────────┘
         │               │                │                  │
    REST API        LiveKit SDK      S3 pre-signed      S3 pre-signed
    (POST)          (WebSocket)      GET URLs            GET URLs
         │               │                │                  │
    ┌────▼───────┐  ┌────▼────────┐  ┌────▼──────────────────▼──────┐
    │ API Gateway │  │ LiveKit     │  │        Amazon S3             │
    │ /device/cmd │  │ Cloud       │  │  (photos + recordings)      │
    └────┬───────┘  └─────────────┘  └─────────────────────────────┘
         │
    ┌────▼───────┐
    │ AWS IoT    │
    │ Core MQTT  │
    └────┬───────┘
         │
    ┌────▼───────┐
    │ ESP32-P4   │
    │ Bird Feeder│
    └────────────┘
```

### Existing Cloud API Endpoints (from Lab 7)

| Method | Endpoint | Purpose |
|--------|----------|---------|
| `POST` | `/device/command` | Send MQTT command to ESP32-P4 |
| `POST` | `/token/livekit` | Get LiveKit viewer JWT token |
| `GET`  | `/events?deviceId=...&from=...&to=...&limit=...` | List photo/video events |
| `GET`  | `/events/{eventId}/playback?deviceId=...` | Get pre-signed S3 download URLs |

### MQTT Commands Supported by ESP32-P4

| Command | Description |
|---------|-------------|
| `capture_snapshot` | Capture a JPEG photo and upload to S3 |
| `record_start` | Begin MJPEG video recording |
| `record_stop` | Stop recording, build AVI, upload to S3 |

---

## Step-by-Step Implementation Plan

---

### Step 1: Initialize React Native (Expo) Project

**Goal:** Create the mobile app project skeleton with all required dependencies.

**Actions:**
1. Install Node.js 18+ and Expo CLI if not already available
2. Create a new Expo project inside the workspace:
   ```bash
   cd c:\Lab6_ECE796\esp-webrtc-solution\solutions\doorbell_demo
   npx create-expo-app@latest birdfeeder-app --template blank-typescript
   cd birdfeeder-app
   ```
3. Install required dependencies:
   ```bash
   npx expo install @livekit/react-native @livekit/react-native-webrtc
   npx expo install react-native-url-polyfill
   npm install @react-navigation/native @react-navigation/bottom-tabs
   npx expo install react-native-screens react-native-safe-area-context
   ```

**Verification:**
- [ ] `birdfeeder-app/` directory exists with `package.json`, `app.json`, `App.tsx`
- [ ] Run `npx expo start` — Metro bundler starts without errors
- [ ] Scan QR code with Expo Go on smartphone — blank app loads

---

### Step 2: Create API Service Layer

**Goal:** Build a reusable module that calls the Lab 7 REST API endpoints using `fetch()`.

**Actions:**
1. Create `birdfeeder-app/src/services/api.ts` with:
   - Configuration constants:
     ```
     API_BASE_URL = "https://nex0zvlly4.execute-api.us-east-2.amazonaws.com"
     DEVICE_API_KEY = "doorbell-lab7-key-2026-04-02-9f7c"
     DEVICE_ID = "esp32p4-birdfeeder"
     ```
   - `sendCommand(command: string, payload?: object)` → calls `POST /device/command`
   - `getViewerToken()` → calls `POST /token/livekit` with `{deviceId, roomName: "birdfeeder", role: "viewer"}`
   - `listEvents(fromMs: number, toMs: number, limit?: number)` → calls `GET /events`
   - `getPlaybackUrls(eventId: string)` → calls `GET /events/{eventId}/playback`

**Verification:**
- [ ] File compiles with no TypeScript errors
- [ ] Add a temporary test button in `App.tsx` that calls `listEvents()` and logs the response to the console
- [ ] Confirm the console shows the events JSON array (or empty array if no events exist yet)
- [ ] Remove temporary test button after verification

---

### Step 3: Implement Tab Navigation Structure

**Goal:** Set up bottom tab navigation with four screens matching the app's core features.

**Actions:**
1. Create the following screen files under `birdfeeder-app/src/screens/`:
   - `LiveViewScreen.tsx` — live video stream + snapshot/record controls
   - `PhotosScreen.tsx` — gallery of captured photos
   - `VideosScreen.tsx` — list and playback of recorded videos
   - `SettingsScreen.tsx` — configuration (API URL, API key, device ID)
2. Create `birdfeeder-app/src/navigation/AppNavigator.tsx`:
   - Bottom tab navigator with 4 tabs: Live, Photos, Videos, Settings
   - Use icons (Ionicons from `@expo/vector-icons`)
3. Update `App.tsx` to render `<NavigationContainer><AppNavigator /></NavigationContainer>`

**Verification:**
- [ ] App displays 4 tabs at the bottom of the screen: Live, Photos, Videos, Settings
- [ ] Tapping each tab switches to the correct screen (placeholder text is fine at this stage)
- [ ] No navigation errors in Metro bundler console

---

### Step 4: Implement the Settings Screen

**Goal:** Allow users to configure the API URL, API key, and Device ID (with defaults pre-filled).

**Actions:**
1. In `SettingsScreen.tsx`:
   - Three text input fields: API Base URL, Device API Key, Device ID
   - Pre-fill with the Lab 7 defaults from `api.ts`
   - Store values using React state + `AsyncStorage` for persistence
   - "Save" button that writes config to `AsyncStorage`
   - "Test Connection" button that calls `listEvents()` and shows success/failure alert
2. Create `birdfeeder-app/src/services/config.ts`:
   - `getConfig()` → reads from `AsyncStorage`, returns `{apiUrl, apiKey, deviceId}`
   - `saveConfig(config)` → writes to `AsyncStorage`
   - Update `api.ts` to read from config instead of hardcoded constants

**Verification:**
- [ ] Settings screen shows three input fields pre-filled with defaults
- [ ] "Test Connection" button returns a success alert when the ESP32-P4 is connected and cloud is running
- [ ] Change the API key to an invalid value → "Test Connection" returns an error alert
- [ ] Revert to the correct API key, save, force-close app, reopen → saved values persist

---

### Step 5: Implement MQTT Command Controls (Live View Screen — Controls)

**Goal:** Add buttons that send MQTT commands to the ESP32-P4 via the REST API.

**Actions:**
1. In `LiveViewScreen.tsx`, add a control panel below the video area:
   - **📷 Capture Photo** button → calls `sendCommand("capture_snapshot")`
   - **⏺ Start Recording** / **⏹ Stop Recording** toggle button → calls `sendCommand("record_start")` or `sendCommand("record_stop")`
   - Status indicators: show "Command sent" confirmation or error message
2. Add loading spinners on buttons while API calls are in progress
3. Add a toast/alert for command success or failure

**Verification:**
- [ ] Tap "Capture Photo" → button shows loading → "Command sent" message appears
- [ ] Check ESP32-P4 serial monitor → logs: `Received MQTT command: capture_snapshot`
- [ ] Tap "Start Recording" → button text changes to "Stop Recording"
- [ ] Check ESP32-P4 serial monitor → logs: `Received MQTT command: record_start`
- [ ] Tap "Stop Recording" → button text reverts to "Start Recording"
- [ ] Check ESP32-P4 serial monitor → logs: `Received MQTT command: record_stop`
- [ ] Disconnect ESP32-P4 from Wi-Fi → send command → error message shown (API still accepts it, but device won't receive it — verify no crash)

---

### Step 6: Implement Live Video Viewing (LiveKit Integration)

**Goal:** Show the real-time video stream from the ESP32-P4 via LiveKit.

**Actions:**
1. Install the LiveKit React Native SDK (already done in Step 1)
2. In `LiveViewScreen.tsx`:
   - Add a "Connect" button that:
     1. Calls `getViewerToken()` to get a LiveKit JWT
     2. Connects to the LiveKit room using the returned `token` and `wsUrl`
   - Display the remote video track in a `<VideoView>` component from `@livekit/react-native`
   - Show connection status indicator (disconnected / connecting / live)
   - Add a "Disconnect" button to leave the room
3. Handle edge cases:
   - If the ESP32-P4 is not streaming, show "Waiting for camera..." message
   - Auto-reconnect on network drop (LiveKit SDK handles this natively)

**Verification:**
- [ ] ESP32-P4 is powered on and streaming via WHIP (serial log confirms "WHIP connected")
- [ ] Tap "Connect" on the app → status changes from "Disconnected" to "Connecting" to "Live"
- [ ] Live video from the bird feeder camera appears on the smartphone screen
- [ ] Video is smooth with no major artifacts (H.264 stream at 15 FPS)
- [ ] Tap "Disconnect" → video stops, status returns to "Disconnected"
- [ ] Tap "Connect" again → video resumes

---

### Step 7: Implement Photo Preview (Photos Screen)

**Goal:** List captured snapshots from the cloud and display them in a gallery.

**Actions:**
1. In `PhotosScreen.tsx`:
   - On screen focus, call `listEvents()` with `limit=50` and last-24-hours time range
   - Filter events where `eventType === "snapshot"`
   - Display as a grid of thumbnails (2 or 3 columns)
   - Each thumbnail shows the event timestamp
2. Add pull-to-refresh using `<FlatList refreshing={...} onRefresh={...}>`
3. On thumbnail tap:
   - Call `getPlaybackUrls(eventId)` to get the pre-signed S3 URL
   - Open a full-screen modal showing the photo using `<Image source={{uri: url}} />`
   - Add a close button to dismiss the modal

**Verification:**
- [ ] Trigger a snapshot first: go to Live tab → tap "Capture Photo" → wait 5 seconds
- [ ] Switch to Photos tab → pull to refresh → new snapshot appears in the grid with correct timestamp
- [ ] Tap the thumbnail → full-screen photo modal opens showing the actual camera image
- [ ] Tap close → modal dismisses
- [ ] If no snapshots exist, a "No photos yet" empty-state message is shown
- [ ] Trigger 3+ snapshots → all appear in chronological order in the grid

---

### Step 8: Implement Video Playback (Videos Screen)

**Goal:** List recorded videos from the cloud and play them back on the smartphone.

**Actions:**
1. In `VideosScreen.tsx`:
   - On screen focus, call `listEvents()` and filter for `eventType === "recording"`
   - Display as a list with: event timestamp, recording icon
   - Each item has a "Play" button
2. On "Play" tap:
   - Call `getPlaybackUrls(eventId)` to get the pre-signed S3 URL for the video
   - The recording is an MJPEG AVI file — implement a simple MJPEG player:
     - Fetch the AVI file binary data
     - Parse JPEG frames by scanning for SOI (0xFFD8) and EOI (0xFFD9) markers (same logic as `webpage/index.html` `parseMjpegAvi()`)
     - Display frames sequentially in an `<Image>` component at 2 FPS using `setInterval`
   - Show playback controls: play/pause, frame counter ("Frame 5/30")
3. Add a full-screen playback modal with close button

**Verification:**
- [ ] Record a video first: Live tab → "Start Recording" → wait 10 seconds → "Stop Recording" → wait 10 seconds for upload
- [ ] Switch to Videos tab → pull to refresh → new recording appears in the list
- [ ] Tap "Play" → loading indicator → MJPEG frames play sequentially in a modal
- [ ] Frame counter shows progress (e.g., "Frame 1/20")
- [ ] Tap close → playback stops, modal dismisses
- [ ] If no recordings exist, a "No videos yet" empty-state message is shown

---

### Step 9: End-to-End Integration Testing

**Goal:** Verify all features work together in a complete workflow.

**Test Sequence (perform in order):**

1. **App Launch & Configuration**
   - [ ] Open the app → Settings tab shows correct defaults
   - [ ] "Test Connection" → success

2. **Live Streaming**
   - [ ] Live tab → "Connect" → live video appears from the bird feeder
   - [ ] Video streams continuously without freezing

3. **Photo Capture & Preview**
   - [ ] While live view is active, tap "Capture Photo"
   - [ ] ESP32-P4 serial log shows snapshot capture + S3 upload success
   - [ ] Switch to Photos tab → pull to refresh → photo appears
   - [ ] Tap photo → full-screen preview loads the actual image

4. **Video Recording & Playback**
   - [ ] Live tab → "Start Recording" → button changes to "Stop Recording"
   - [ ] Wait ~10 seconds, then tap "Stop Recording"
   - [ ] ESP32-P4 serial log shows recording frames captured + AVI upload success
   - [ ] Wait ~10 seconds, switch to Videos tab → pull to refresh → recording appears
   - [ ] Tap "Play" → MJPEG frames play back in the modal

5. **Multiple Operations**
   - [ ] Capture 2 more photos → both appear in Photos tab
   - [ ] Record another video → appears in Videos tab
   - [ ] All previously captured media still accessible

6. **Error Handling**
   - [ ] Disconnect ESP32-P4 from power → send command → app shows error or queues gracefully (no crash)
   - [ ] Reconnect ESP32-P4 → live view reconnects → commands work again

---

### Step 10: Polish and Final Demo Preparation

**Goal:** Ensure the app is demo-ready for the TA/Instructor.

**Actions:**
1. Add an app icon and splash screen (optional but recommended):
   - Place a bird/camera icon in `assets/`
   - Update `app.json` with icon and splash config
2. Verify the following demo flow works smoothly end-to-end:
   1. Open app → connect to live view → show live video
   2. Tap "Capture Photo" → switch to Photos → show preview
   3. Tap "Start Recording" → wait → "Stop Recording" → switch to Videos → play back
   4. Show Settings screen with cloud configuration
3. Ensure no console errors or crashes during the demo flow

**Verification:**
- [ ] Complete the full demo flow 3 times consecutively without any errors or crashes
- [ ] App responds to button taps within 1-2 seconds
- [ ] All media loads correctly (photos display, video frames play)

---

## Technology Stack Summary

| Component | Technology | Purpose |
|-----------|-----------|---------|
| Mobile Framework | React Native (Expo) | Cross-platform smartphone app |
| Live Video | `@livekit/react-native` | LiveKit WebRTC viewer |
| REST API Calls | `fetch()` | Send commands, get tokens, list events |
| Navigation | `@react-navigation/bottom-tabs` | Tab-based UI |
| State Persistence | `AsyncStorage` | Save app settings |
| Video Playback | Custom MJPEG parser | Parse AVI → display JPEG frames |
| Language | TypeScript | Type-safe development |

## File Structure

```
birdfeeder-app/
├── App.tsx                          # Root component with NavigationContainer
├── app.json                         # Expo configuration
├── package.json                     # Dependencies
├── src/
│   ├── services/
│   │   ├── api.ts                   # REST API helper functions
│   │   ├── config.ts                # AsyncStorage config management
│   │   └── mjpegParser.ts           # AVI/MJPEG frame extraction utility
│   ├── navigation/
│   │   └── AppNavigator.tsx         # Bottom tab navigator
│   └── screens/
│       ├── LiveViewScreen.tsx       # Live video + command controls
│       ├── PhotosScreen.tsx         # Photo gallery + full-screen preview
│       ├── VideosScreen.tsx         # Video list + MJPEG playback
│       └── SettingsScreen.tsx       # API configuration
└── assets/                          # Icons, splash screen
```

## Key Configuration Values (from Lab 7)

```
API_BASE_URL:    https://nex0zvlly4.execute-api.us-east-2.amazonaws.com
DEVICE_API_KEY:  doorbell-lab7-key-2026-04-02-9f7c
DEVICE_ID:       esp32p4-birdfeeder
LIVEKIT_WS_URL:  wss://lab7cloudservice-f6kwjbnt.livekit.cloud
MQTT_TOPIC:      doorbell/esp32p4-birdfeeder/commands
```

These are already deployed and functional from Lab 7. The smartphone app connects to the same cloud backend — **no firmware or cloud changes are needed**.
