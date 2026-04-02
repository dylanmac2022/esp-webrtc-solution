const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, PutCommand } = require("@aws-sdk/lib-dynamodb");
const { ok, badRequest, internalError, unauthorized } = require("../lib/http");
const { correlationId, parseJsonBody, requireApiKey } = require("../lib/request");
const { required } = require("../lib/env");

const db = DynamoDBDocumentClient.from(new DynamoDBClient({}), {
  marshallOptions: { removeUndefinedValues: true },
});

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return unauthorized("Missing/invalid API key", reqId);
    }

    const body = parseJsonBody(event);
    const {
      deviceId,
      eventTs,
      eventId,
      eventType,
      sessionId,
      roomName,
      s3Keys,
      durationSec,
      uploadStatus,
    } = body;

    if (!deviceId || !eventTs || !eventId || !eventType) {
      return badRequest("deviceId, eventTs, eventId, and eventType are required", reqId);
    }

    const item = {
      deviceId,
      eventTs,
      eventId,
      eventType,
      sessionId,
      roomName,
      s3Keys,
      durationSec,
      uploadStatus: uploadStatus || "pending",
      createdAt: new Date().toISOString(),
      gsi1pk: sessionId || "none",
      gsi1sk: eventTs,
      gsi2pk: eventType,
      gsi2sk: eventTs,
    };

    await db.send(
      new PutCommand({
        TableName: required("EVENTS_TABLE"),
        Item: item,
        ConditionExpression: "attribute_not_exists(eventId)",
      })
    );

    return ok({ eventId, created: true, correlationId: reqId });
  } catch (err) {
    if (err.name === "ConditionalCheckFailedException") {
      return ok({ created: false, duplicate: true, correlationId: reqId });
    }
    console.error("createEvent failed", { reqId, err });
    return internalError("Failed to write event", reqId);
  }
};
