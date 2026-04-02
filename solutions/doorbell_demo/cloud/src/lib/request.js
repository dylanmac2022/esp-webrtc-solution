const parseJsonBody = (event) => {
  if (!event || !event.body) {
    return {};
  }
  if (typeof event.body === "object") {
    return event.body;
  }
  return JSON.parse(event.body);
};

const correlationId = (event) => {
  const header = event?.headers?.["x-correlation-id"] || event?.headers?.["X-Correlation-Id"];
  if (header) {
    return header;
  }
  return event?.requestContext?.requestId || `local-${Date.now()}`;
};

const requireApiKey = (event) => {
  const expected = process.env.DEVICE_API_KEY;
  if (!expected) {
    return true;
  }
  const got = event?.headers?.["x-api-key"] || event?.headers?.["X-Api-Key"];
  return got && got === expected;
};

module.exports = {
  parseJsonBody,
  correlationId,
  requireApiKey,
};
