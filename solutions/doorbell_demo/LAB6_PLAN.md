# Lab 6 – AI-Assisted Video & Audio Data Collection and Transmission
### ESP32-P4 WebRTC Doorbell Demo — Step-by-Step Lab Plan

---

## Overview
You will configure and flash an ESP32-P4 board running a WebRTC doorbell application that streams live video and audio to a browser over Wi-Fi. Each step below has a clear verification checkpoint so you know when you can safely move on.

---

## Prerequisites (already done ✓)
- Hardware is set up (ESP32-P4 Function EV Board + OV5647 MIPI camera)
- VS Code is open with the `doorbell_demo` project
- ESP-IDF extension is installed and working

---

## Step 1 — Configure Wi-Fi Credentials

**File to edit:** `main/settings.h`

1. Open `main/settings.h`
2. Find these two lines:
   ```c
   #define WIFI_SSID     "XXXX"
   #define WIFI_PASSWORD "XXXX"
   ```
3. Replace them with your smartphone hotspot credentials:
   ```c
   #define WIFI_SSID     "YourHotspotName"
   #define WIFI_PASSWORD "YourHotspotPassword"
   ```
4. Save the file.

> **Verify:** The SSID and password exactly match what is shown in your phone's hotspot settings (case-sensitive).

---

## Step 2 — Configure Video Resolution for OV5647

**File to edit:** `main/settings.h`

1. Find this block in `settings.h`:
   ```c
   #if CONFIG_IDF_TARGET_ESP32P4
   #define VIDEO_WIDTH  1920
   #define VIDEO_HEIGHT 1080
   #define VIDEO_FPS    25
   ```
2. Change it to the OV5647-compatible resolution:
   ```c
   #if CONFIG_IDF_TARGET_ESP32P4
   #define VIDEO_WIDTH  1280
   #define VIDEO_HEIGHT 960
   #define VIDEO_FPS    15
   ```
3. Save the file.

> **Verify:** The three `#define` lines now read 1280, 960, and 15.

---

## Step 3 — Open Menuconfig and Enable OV5647 Camera

1. In VS Code, open the Command Palette (`Ctrl+Shift+P`) and run **ESP-IDF: SDK Configuration Editor (Menuconfig)**, OR click the gear icon in the ESP-IDF status bar at the bottom.
2. In the search bar at the top of menuconfig, type: `camera`
3. You will see a list of camera drivers. **Disable all cameras except OV5647:**
   - Uncheck/disable `SC2336` (currently enabled)
   - Check/enable `OV5647`
4. After enabling OV5647, find the OV5647 sub-settings and configure them as follows:

   | Setting | Value |
   |---|---|
   | Output format | RAW10 1280x960 Binning 45fps, MIPI 2-lane, 24M input |
   | Enable CSI line synchronization | ✅ Enabled |
   | Enable autofocus (AF) motor by OV5647's GPIO0 | ✅ Enabled |
   | IPA Configuration File | Use default configuration |

5. Click **Save** (or press `S` if using the terminal version).

> **Verify:** Search `camera` again in menuconfig — only `OV5647` should have a checkmark.

---

## Step 4 — Update sdkconfig.defaults for OV5647

**File to edit:** `sdkconfig.defaults.esp32p4`

The defaults file currently enables SC2336. You need to switch it to OV5647 so clean builds use the right camera.

1. Open `sdkconfig.defaults.esp32p4`
2. Find:
   ```
   # Use camera SC2336
   CONFIG_CAMERA_SC2336=y
   CONFIG_CAMERA_SC2336_MIPI_RAW10_1920x1080_25FPS_2_LANE=y
   ```
3. Replace with:
   ```
   # Use camera OV5647
   CONFIG_CAMERA_OV5647=y
   CONFIG_CAMERA_OV5647_MIPI_RAW10_1280x960_BINNING_45FPS_2_LANE=y
   ```
4. Save the file.

> **Verify:** The file no longer contains `SC2336` lines, and now has `OV5647` lines.

---

## Step 5 — Enable USB Serial/JTAG Console (if using USB-JTAG for flashing)

**File to edit:** `sdkconfig.defaults.esp32p4`

1. Find this commented-out line:
   ```
   # If you use serial JTAG turn on this option
   #CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
   ```
2. Uncomment it:
   ```
   # If you use serial JTAG turn on this option
   CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
   ```
3. Save the file.

> **Verify:** The line has no `#` at the start.  
> **Skip this step** if you are flashing over a standard UART serial port (not USB-JTAG).

---

## Step 6 — Build the Project

1. Open a terminal inside VS Code (`` Ctrl+` ``).
2. Make sure the ESP-IDF environment is activated (you should see `(ESP-IDF)` in the prompt, or use the ESP-IDF terminal shortcut).
3. Run the build:
   ```
   idf.py build
   ```
4. Wait for the build to complete. This may take several minutes the first time.

> **Verify:** Build ends with:
> ```
> Project build complete. To flash, run: idf.py flash
> ```
> And **no errors** (warnings are OK).

---

## Step 7 — Flash the Firmware to the Board

1. Connect your ESP32-P4 board via USB.
2. Identify the COM port (check Device Manager on Windows — look for a CP210x or similar port).
3. Flash and open the monitor in one command (replace `COMX` with your port, e.g. `COM5`):
   ```
   idf.py -p COMX flash monitor
   ```

> **Verify:** You see boot messages in the monitor, ending with something like:
> ```
> W (xxxx) Webrtc_Test: Please use browser to join in espXXXXXX on https://webrtc.espressif.com/doorbell
> ```

---

## Step 8 — Connect Phone Hotspot and Confirm Wi-Fi

1. Enable your smartphone hotspot (the one whose credentials you put in Step 1).
2. Watch the serial monitor. After boot, the board will try to connect to Wi-Fi automatically.

> **Verify:** You see a log line like:
> ```
> I (xxxx) wifi: connected with YourHotspotName
> ```
> and then the room URL message from Step 7.

---

## Step 9 — Join the Room from a Browser

1. Note the **room name** printed in the monitor — it looks like `espXXXXXX` (derived from the board's MAC address).
2. On a device connected to the **same hotspot**, open **Chrome or Edge** (not Firefox — WebRTC support is best in Chrome).
3. Navigate to:
   ```
   https://webrtc.espressif.com/doorbell
   ```
4. Enter the room name (`espXXXXXX`) and join.

> **Verify:** The browser shows the live video feed from the OV5647 camera.

---

## Step 10 — Test Doorbell Interactions

### Test A — Ring the Doorbell (audio call)
1. Press the **Boot button (GPIO35)** on the ESP32-P4 board.
2. The board plays ring music; the browser shows **Accept Call / Deny Call** popup.
3. Click **Accept Call** in the browser.

> **Verify:** Two-way audio is established (you can speak into your PC mic and hear it on the board, and vice versa). One-way video (board → browser) continues streaming.

### Test B — Open Door command
1. In the browser, click the **Door icon**.
2. The board plays a "Door is opened" tone.
3. The browser displays: `Receiving Door opened event`.

> **Verify:** You hear the tone from the board's speaker and see the message in the browser.

### Test C — End the Session
1. In the browser, click the **Exit icon** to leave the room.
2. In the serial monitor, type and send:
   ```
   leave
   ```

> **Verify:** The board logs that it left the room successfully.

---

## Troubleshooting Quick Reference

| Problem | Fix |
|---|---|
| Build fails with camera errors | Re-open menuconfig, confirm only OV5647 is selected, save, rebuild |
| Board won't connect to Wi-Fi | Double-check SSID/password in `settings.h` — case-sensitive |
| No video in browser | Confirm you're in the same room name, use Chrome/Edge |
| Room already occupied error | Wait ~2 minutes for server timeout, or `leave` then `join` a new room |
| Flash fails / port not found | Check Device Manager for correct COM port number |
| Board keeps leaving room | Use `server 1` command in monitor before `join` to switch to CN server |

---

## Summary of Files You Modified

| File | What Changed |
|---|---|
| `main/settings.h` | Wi-Fi credentials + video resolution (1280×960 @ 15fps) |
| `sdkconfig.defaults.esp32p4` | Camera changed from SC2336 → OV5647; USB-JTAG console enabled |
| Menuconfig (sdkconfig) | OV5647 enabled, SC2336 disabled |
