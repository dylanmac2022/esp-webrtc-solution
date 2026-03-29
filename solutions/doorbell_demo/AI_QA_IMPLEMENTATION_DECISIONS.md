# AI Questions and Answers Influencing Implementation Decisions

1. **Question:** How do I open WebRTC, and when should I open the browser after flashing?
   **Answer:** Open the doorbell page after board reboot and Wi-Fi connection, then join the same room as the board.
   **Decision Impact:** We validated boot-to-network timing and used runtime logs to confirm room readiness before browser tests.

2. **Question:** Why did signaling fail with `APPRTC_SIG: result FULL` and `Fail to get client id`?
   **Answer:** The room was full/stale.
   **Decision Impact:** We changed room naming to use unique per-boot room IDs and added retry behavior with fresh room IDs.

3. **Question:** Which room should I join, and where is it shown?
   **Answer:** Join the room in `Start to join in room ...` (or `Please use browser to join in ...`).
   **Decision Impact:** We stopped using fixed room names and switched to log-derived dynamic room names.

4. **Question:** Is `Got url: stun:...` a link/room to open?
   **Answer:** No. It is ICE/STUN configuration used internally by WebRTC.
   **Decision Impact:** We ignored STUN lines for user actions and focused on signaling/room logs.

5. **Question:** Why do I only see my computer camera instead of OV5647?
   **Answer:** Browser local preview can dominate the view; remote stream requires successful accept/call state and stable peer connection.
   **Decision Impact:** We traced `ACCEPT_CALL`, peer state transitions, and negotiation logs to validate remote stream setup.

6. **Question:** Audio works but video does not; what does that imply?
   **Answer:** Transport/DTLS were up, so the issue was likely in video path or browser-side rendering/permissions.
   **Decision Impact:** We verified H.264 negotiation, camera open, and video packet send counters before focusing on browser behavior.

7. **Question:** How can we prove MIPI CSI + ISP + H.264 were used for collection?
   **Answer:** Use log evidence: OV5647 detected/opened, negotiated video pipeline, H.264 in SDP, and increasing video send counters.
   **Decision Impact:** We treated capture/encode path as correct and narrowed remaining issues to UI/permission/call-flow behavior.

8. **Question:** What does browser `getUserMedia NotAllowedError` change?
   **Answer:** Denied camera/mic permissions can break call flow and cause early disconnect/deny behavior.
   **Decision Impact:** We added browser + OS permission checks as a required test condition for stable remote video behavior.
