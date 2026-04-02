const { S3Client, PutObjectCommand } = require("@aws-sdk/client-s3");
const { getSignedUrl } = require("@aws-sdk/s3-request-presigner");
const { ok, badRequest, internalError, unauthorized } = require("../lib/http");
const { correlationId, parseJsonBody, requireApiKey } = require("../lib/request");
const { required } = require("../lib/env");

const s3 = new S3Client({});

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return unauthorized("Missing/invalid API key", reqId);
    }

    const body = parseJsonBody(event);
    const { deviceId, eventId, contentType, mediaType, sessionId } = body;

    if (!deviceId || !eventId || !mediaType) {
      return badRequest("deviceId, eventId, and mediaType are required", reqId);
    }

    const now = new Date();
    const yyyy = now.getUTCFullYear();
    const mm = String(now.getUTCMonth() + 1).padStart(2, "0");
    const dd = String(now.getUTCDate()).padStart(2, "0");
    const ext = mediaType === "snapshot" ? "jpg" : mediaType === "audio" ? "aac" : "mp4";

    const key = `${deviceId}/${yyyy}/${mm}/${dd}/${sessionId || "default"}/${eventId}/${mediaType}.${ext}`;

    const cmd = new PutObjectCommand({
      Bucket: required("MEDIA_BUCKET"),
      Key: key,
      ContentType: contentType || "application/octet-stream",
      Metadata: {
        deviceid: deviceId,
        eventid: eventId,
        mediatype: mediaType,
      },
    });

    const expiresInSec = Number(process.env.UPLOAD_URL_TTL_SEC || "300");
    const uploadUrl = await getSignedUrl(s3, cmd, { expiresIn: expiresInSec });

    return ok({
      uploadUrl,
      bucket: process.env.MEDIA_BUCKET,
      key,
      expiresInSec,
      correlationId: reqId,
    });
  } catch (err) {
    console.error("mediaUploadUrl failed", { reqId, err });
    return internalError("Failed to create upload URL", reqId);
  }
};
