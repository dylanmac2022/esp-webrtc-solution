const DEFAULT_HEADERS = {
  "content-type": "application/json",
};

const corsHeaders = () => ({
  "access-control-allow-origin": process.env.CORS_ALLOW_ORIGIN || "*",
  "access-control-allow-methods": "GET,POST,OPTIONS",
  "access-control-allow-headers": "authorization,content-type,x-api-key",
});

const response = (statusCode, body) => ({
  statusCode,
  headers: {
    ...DEFAULT_HEADERS,
    ...corsHeaders(),
  },
  body: JSON.stringify(body),
});

const ok = (body) => response(200, body);
const badRequest = (message, correlationId) =>
  response(400, { code: "BAD_REQUEST", message, correlationId });
const unauthorized = (message, correlationId) =>
  response(401, { code: "UNAUTHORIZED", message, correlationId });
const forbidden = (message, correlationId) =>
  response(403, { code: "FORBIDDEN", message, correlationId });
const internalError = (message, correlationId) =>
  response(500, { code: "INTERNAL_ERROR", message, correlationId });

module.exports = {
  ok,
  response,
  badRequest,
  unauthorized,
  forbidden,
  internalError,
};
