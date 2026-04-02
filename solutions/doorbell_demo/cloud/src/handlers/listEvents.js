const { DynamoDBClient } = require("@aws-sdk/client-dynamodb");
const { DynamoDBDocumentClient, QueryCommand } = require("@aws-sdk/lib-dynamodb");
const { ok, badRequest, internalError, unauthorized } = require("../lib/http");
const { correlationId, requireApiKey } = require("../lib/request");
const { required } = require("../lib/env");

const db = DynamoDBDocumentClient.from(new DynamoDBClient({}));

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return unauthorized("Missing/invalid API key", reqId);
    }

    const query = event?.queryStringParameters || {};
    const deviceId = query.deviceId;
    const from = Number(query.from || "0");
    const to = Number(query.to || `${Date.now()}`);

    if (!deviceId) {
      return badRequest("deviceId query parameter is required", reqId);
    }

    const result = await db.send(
      new QueryCommand({
        TableName: required("EVENTS_TABLE"),
        KeyConditionExpression: "deviceId = :d AND eventTs BETWEEN :f AND :t",
        ExpressionAttributeValues: {
          ":d": deviceId,
          ":f": from,
          ":t": to,
        },
        ScanIndexForward: false,
        Limit: Number(query.limit || "100"),
      })
    );

    return ok({ events: result.Items || [], correlationId: reqId });
  } catch (err) {
    console.error("listEvents failed", { reqId, err });
    return internalError("Failed to list events", reqId);
  }
};
