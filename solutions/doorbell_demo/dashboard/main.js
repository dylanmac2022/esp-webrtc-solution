import { Room } from "https://cdn.skypack.dev/livekit-client";

const state = {
  room: null,
};

const $ = (id) => document.getElementById(id);
const logEl = $("log");
const historyEl = $("history");
const liveViewEl = $("liveView");

const log = (msg, obj) => {
  const line = `[${new Date().toISOString()}] ${msg}${obj ? ` ${JSON.stringify(obj)}` : ""}`;
  logEl.textContent = `${line}\n${logEl.textContent}`;
};

const apiHeaders = () => ({
  "content-type": "application/json",
  "x-api-key": $("apiKey").value.trim(),
});

const apiBase = () => $("apiBase").value.trim().replace(/\/$/, "");
const deviceId = () => $("deviceId").value.trim();
const roomName = () => $("roomName").value.trim();

async function callApi(path, method = "GET", body) {
  const res = await fetch(`${apiBase()}${path}`, {
    method,
    headers: apiHeaders(),
    body: body ? JSON.stringify(body) : undefined,
  });
  const data = await res.json();
  if (!res.ok) {
    throw new Error(data.message || `HTTP ${res.status}`);
  }
  return data;
}

async function connectLive() {
  const tokenData = await callApi("/token/livekit", "POST", {
    deviceId: deviceId(),
    roomName: roomName(),
    role: "viewer",
  });

  if (state.room) {
    state.room.disconnect();
  }

  const room = new Room();
  state.room = room;

  room.on("trackSubscribed", (track) => {
    if (track.kind === "video") {
      const element = track.attach();
      liveViewEl.textContent = "";
      liveViewEl.appendChild(element);
      log("Subscribed to remote video track");
    }
  });

  await room.connect(tokenData.wsUrl, tokenData.token);
  log("Connected to LiveKit room", { room: roomName() });
}

async function publishCommand(command, payload = {}) {
  const data = await callApi("/device/command", "POST", {
    deviceId: deviceId(),
    command,
    payload,
  });
  log("Command published", data);
}

function renderEvents(events) {
  historyEl.innerHTML = "";
  for (const event of events) {
    const row = document.createElement("div");
    row.className = "event";
    row.textContent = `${new Date(event.eventTs).toLocaleString()} | ${event.eventType} | ${event.eventId}`;

    const btn = document.createElement("button");
    btn.textContent = "Playback";
    btn.style.marginTop = "8px";
    btn.onclick = async () => {
      try {
        const playback = await callApi(`/events/${event.eventId}/playback?deviceId=${encodeURIComponent(deviceId())}`);
        const first = playback.links.video || playback.links.snapshot || playback.links.audio;
        if (!first) {
          log("No media links found for event", event);
          return;
        }
        window.open(first, "_blank");
      } catch (err) {
        log("Playback fetch failed", { error: err.message });
      }
    };

    row.appendChild(document.createElement("br"));
    row.appendChild(btn);
    historyEl.appendChild(row);
  }
}

async function refreshEvents() {
  const now = Date.now();
  const from = now - 7 * 24 * 60 * 60 * 1000;
  const data = await callApi(`/events?deviceId=${encodeURIComponent(deviceId())}&from=${from}&to=${now}&limit=100`);
  renderEvents(data.events || []);
  log("Event history refreshed", { count: (data.events || []).length });
}

$("btnLive").onclick = async () => {
  try {
    await connectLive();
  } catch (err) {
    log("Live connect failed", { error: err.message });
  }
};

$("btnEvents").onclick = async () => {
  try {
    await refreshEvents();
  } catch (err) {
    log("Event refresh failed", { error: err.message });
  }
};

$("btnRecordStart").onclick = () => publishCommand("record_start");
$("btnRecordStop").onclick = () => publishCommand("record_stop");
$("btnPhoto").onclick = () => publishCommand("photo_capture");
$("btnDoor").onclick = () => publishCommand("open_door");

log("Dashboard ready");
