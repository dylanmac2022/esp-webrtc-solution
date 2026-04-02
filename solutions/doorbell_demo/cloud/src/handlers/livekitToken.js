const { AccessToken } = require("livekit-server-sdk");
const { required } = require("../lib/env");
const { ok, badRequest, internalError } = require("../lib/http");
const { correlationId, parseJsonBody, requireApiKey } = require("../lib/request");

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return badRequest("API key required", reqId);
    }

    const body = parseJsonBody(event);
    const deviceId = body.deviceId;
    const role = body.role || "viewer";
    const roomName = body.roomName;

    if (!deviceId || !roomName) {
      return badRequest("deviceId and roomName are required", reqId);
    }
    if (!["publisher", "viewer"].includes(role)) {
      return badRequest("role must be publisher or viewer", reqId);
    }

    const apiKey = required("LIVEKIT_API_KEY");
    const apiSecret = required("LIVEKIT_API_SECRET");
    const ttlSec = Number(process.env.LIVEKIT_TOKEN_TTL_SEC || "1800");

    const token = new AccessToken(apiKey, apiSecret, {
      identity: `${role}-${deviceId}`,
      ttl: `${ttlSec}s`,
      metadata: JSON.stringify({ deviceId, role }),
    });

    token.addGrant({
      room: roomName,
      roomJoin: true,
      canPublish: role === "publisher",
      canSubscribe: true,
      canPublishData: true,
    });

    const jwt = await token.toJwt();

    return ok({
      token: jwt,
      roomName,
      wsUrl: required("LIVEKIT_WS_URL"),
      correlationId: reqId,
      expiresInSec: ttlSec,
    });
  } catch (err) {
    console.error("livekitToken failed", { reqId, err });
    return internalError("Failed to create LiveKit token", reqId);
  }
};
