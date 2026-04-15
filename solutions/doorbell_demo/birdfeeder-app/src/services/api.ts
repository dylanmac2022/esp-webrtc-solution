/**
 * api.ts — Central API service for communicating with the Lab 7 cloud backend.
 *
 * All device control and media retrieval goes through the AWS API Gateway REST API.
 * The app never talks directly to the ESP32-P4 — it sends commands via the cloud,
 * which forwards them over MQTT to the device.
 */
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

/**
 * FEATURE: MQTT Control Commands
 * Sends a command (capture_snapshot, record_start, record_stop) to the ESP32-P4.
 * Flow: App → API Gateway → AWS IoT Core MQTT → ESP32-P4
 */
export async function sendCommand(command: string, payload: object = {}) {
  const cfg = await getConfig();
  return apiFetch('POST', '/device/command', {
    deviceId: cfg.deviceId,
    command,
    payload,
  });
}

/**
 * FEATURE: Live Video Streaming
 * Gets a JWT token to connect to the LiveKit room as a viewer.
 * The ESP32-P4 publishes H.264 video via WHIP; this token lets us subscribe to it.
 */
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

/**
 * FEATURE: Browse Photos & Videos
 * Queries DynamoDB via the API to list all snapshot/recording events.
 * Used by both the Photos tab and Videos tab.
 */
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

/**
 * FEATURE: Photo Preview & Video Playback
 * Gets time-limited pre-signed S3 URLs to download the actual media files.
 * Photos come as JPEG, videos come as MJPEG AVI.
 */
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
