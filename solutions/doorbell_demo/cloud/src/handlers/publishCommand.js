const { IoTDataPlaneClient, PublishCommand } = require("@aws-sdk/client-iot-data-plane");
const { ok, badRequest, internalError, unauthorized } = require("../lib/http");
const { correlationId, parseJsonBody, requireApiKey } = require("../lib/request");
const { required } = require("../lib/env");

const iot = new IoTDataPlaneClient({
  endpoint: required("IOT_DATA_ENDPOINT"),
});

exports.handler = async (event) => {
  const reqId = correlationId(event);
  try {
    if (!requireApiKey(event)) {
      return unauthorized("Missing/invalid API key", reqId);
    }

    const body = parseJsonBody(event);
    const { deviceId, command, payload } = body;

    if (!deviceId || !command) {
      return badRequest("deviceId and command are required", reqId);
    }

    const topic = `${required("IOT_TOPIC_PREFIX")}/${deviceId}/commands`;
    const message = {
      command,
      payload: payload || {},
      ts: Date.now(),
      correlationId: reqId,
    };

    await iot.send(
      new PublishCommand({
        topic,
        qos: 1,
        payload: Buffer.from(JSON.stringify(message), "utf-8"),
      })
    );

    return ok({ published: true, topic, correlationId: reqId });
  } catch (err) {
    console.error("publishCommand failed", { reqId, err });
    return internalError("Failed to publish MQTT command", reqId);
  }
};
