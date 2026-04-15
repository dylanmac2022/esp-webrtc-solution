import { getConfig } from './config';

async function apiHeaders(): Promise<Record<string, string>> {
  const cfg = await getConfig();
  return {
    'Content-Type': 'application/json',
    'x-api-key': cfg.apiKey,
  };
}

async function apiFetch(method: string, path: string, body?: object) {
  const cfg = await getConfig();
  const headers = await apiHeaders();
  const opts: RequestInit = { method, headers };
  if (body) opts.body = JSON.stringify(body);

  const resp = await fetch(`${cfg.apiUrl}${path}`, opts);
  if (!resp.ok) {
    const errText = await resp.text();
    throw new Error(`API ${resp.status}: ${errText}`);
  }
  return resp.json();
}

/** Send an MQTT command to the ESP32-P4 via the cloud API */
export async function sendCommand(command: string, payload: object = {}) {
  const cfg = await getConfig();
  return apiFetch('POST', '/device/command', {
    deviceId: cfg.deviceId,
    command,
    payload,
  });
}

/** Get a LiveKit viewer JWT token */
export async function getViewerToken(): Promise<{
  token: string;
  wsUrl: string;
  roomName: string;
}> {
  const cfg = await getConfig();
  return apiFetch('POST', '/token/livekit', {
    deviceId: cfg.deviceId,
    roomName: 'birdfeeder',
    role: 'viewer',
  });
}

export interface EventItem {
  eventId: string;
  eventType: 'snapshot' | 'recording';
  eventTs: number;
  deviceId: string;
  s3Keys?: {
    snapshot?: string;
    video?: string;
  };
}

/** List events (photos + videos) from the cloud */
export async function listEvents(
  fromMs: number,
  toMs: number,
  limit: number = 50,
): Promise<EventItem[]> {
  const cfg = await getConfig();
  const data = await apiFetch(
    'GET',
    `/events?deviceId=${encodeURIComponent(cfg.deviceId)}&from=${fromMs}&to=${toMs}&limit=${limit}`,
  );
  return data.events || [];
}

/** Get pre-signed S3 playback URLs for an event */
export async function getPlaybackUrls(
  eventId: string,
): Promise<{ snapshot?: string; video?: string }> {
  const cfg = await getConfig();
  const data = await apiFetch(
    'GET',
    `/events/${encodeURIComponent(eventId)}/playback?deviceId=${encodeURIComponent(cfg.deviceId)}`,
  );
  return data.links || {};
}
