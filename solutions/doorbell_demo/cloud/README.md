# Lab 7 Cloud Backend

This folder contains a deployable AWS SAM backend for the Lab 7 doorbell cloud flow.

## Implemented APIs

- `POST /token/livekit` - Generate a LiveKit token for `publisher` or `viewer`
- `POST /media/upload-url` - Generate pre-signed S3 PUT URL
- `POST /events` - Write event metadata to DynamoDB with idempotency (`eventId`)
- `GET /events` - List events by `deviceId` and time range
- `GET /events/{eventId}/playback` - Generate pre-signed S3 GET URLs
- `POST /device/command` - Publish command to AWS IoT Core MQTT topic

## Quick Deploy

1. Install dependencies:
   - `npm install`
2. Build and deploy with SAM:
   - `sam build`
   - `sam deploy --guided`
3. Provide parameters when prompted:
   - `DeviceApiKey`
   - `LivekitWsUrl`
   - `LivekitApiKey`
   - `LivekitApiSecret`
   - `IotDataEndpoint`

## Runtime Data Model

DynamoDB item keys:
- Partition key: `deviceId` (string)
- Sort key: `eventTs` (number, epoch ms)

Important fields:
- `eventId`
- `eventType`
- `sessionId`
- `roomName`
- `s3Keys` (map of media type to object key)
- `uploadStatus`

## Notes

- CORS origin is controlled with `CorsAllowOrigin` parameter.
- API key enforcement is enabled via `DEVICE_API_KEY` environment variable.
- LiveKit secret is never returned to clients.
