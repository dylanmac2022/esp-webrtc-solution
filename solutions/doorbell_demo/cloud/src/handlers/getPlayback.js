const { S3Client, GetObjectCommand } = require("@aws-sdk/client-s3");
const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, QueryCommand } = require("@aws-sdk/lib-dynamodb");
const { getSignedUrl } = require("@aws-sdk/s3-request-presigner");
const { ok, badRequest, internalError, unauthorized } = require("../lib/http");
const { correlationId, requireApiKey } = require("../lib/request");
const { required } = require("../lib/env");

const s3 = new S3Client({});
const db = DynamoDBDocumentClient.from(new DynamoDBClient({}));

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return unauthorized("Missing/invalid API key", reqId);
    }

    const eventId = event?.pathParameters?.eventId;
    const deviceId = event?.queryStringParameters?.deviceId;

    if (!eventId || !deviceId) {
      return badRequest("eventId path parameter and deviceId query parameter are required", reqId);
    }

    const query = await db.send(
      new QueryCommand({
        TableName: required("EVENTS_TABLE"),
        KeyConditionExpression: "deviceId = :d",
        FilterExpression: "eventId = :e",
        ExpressionAttributeValues: {
          ":d": deviceId,
          ":e": eventId,
        },
        Limit: 1,
      })
    );

    const item = query.Items?.[0];
    if (!item?.s3Keys) {
      return badRequest("No playback media for event", reqId);
    }

    const expiresInSec = Number(process.env.PLAYBACK_URL_TTL_SEC || "300");
    const links = {};

    for (const [mediaType, key] of Object.entries(item.s3Keys)) {
      if (!key) {
        continue;
      }
      const cmd = new GetObjectCommand({
        Bucket: required("MEDIA_BUCKET"),
        Key: key,
      });
      links[mediaType] = await getSignedUrl(s3, cmd, { expiresIn: expiresInSec });
    }

    return ok({
      eventId,
      links,
      expiresInSec,
      correlationId: reqId,
    });
  } catch (err) {
    console.error("getPlayback failed", { reqId, err });
    return internalError("Failed to create playback URLs", reqId);
  }
};
