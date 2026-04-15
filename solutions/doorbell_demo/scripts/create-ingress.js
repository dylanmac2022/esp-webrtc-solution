/**
 * One-time script to create a LiveKit WHIP ingress.
 * Run: node scripts/create-ingress.js
 *
 * Requires environment variables:
 *   LIVEKIT_URL        - e.g. wss://lab7cloudservice-f6kwjbnt.livekit.cloud
 *   LIVEKIT_API_KEY    - e.g. APIp8awWURu2riw
 *   LIVEKIT_API_SECRET - e.g. jeTLqelkuVJfafM1f6x4QV4LcLLe5o3o5FpPRRmKGlqG
 */

const { IngressClient, IngressInput } = require("livekit-server-sdk");

const LIVEKIT_URL = process.env.LIVEKIT_URL || "https://lab7cloudservice-f6kwjbnt.livekit.cloud";
const API_KEY = process.env.LIVEKIT_API_KEY || "APIp8awWURu2riw";
const API_SECRET = process.env.LIVEKIT_API_SECRET || "jeTLqelkuVJfafM1f6x4QV4LcLLe5o3o5FpPRRmKGlqG";

async function main() {
  const client = new IngressClient(LIVEKIT_URL, API_KEY, API_SECRET);

  // List existing ingresses first
  const existing = await client.listIngress({ roomName: "birdfeeder" });
  if (existing && existing.length > 0) {
    console.log("=== Existing ingresses for room 'birdfeeder' ===");
    for (const ing of existing) {
      console.log(JSON.stringify({
        ingressId: ing.ingressId,
        name: ing.name,
        url: ing.url,
        streamKey: ing.streamKey,
        state: ing.state,
      }, null, 2));
    }
    console.log("\nReuse above or delete first to create a new one.");
    return;
  }

  // Create a new WHIP ingress
  const ingress = await client.createIngress(IngressInput.WHIP_INPUT, {
    name: "esp32p4-birdfeeder-whip",
    roomName: "birdfeeder",
    participantIdentity: "esp32p4-cam",
    participantName: "Bird Feeder Camera",
  });

  console.log("=== WHIP Ingress Created ===");
  console.log("Ingress ID:", ingress.ingressId);
  console.log("WHIP URL:  ", ingress.url);
  console.log("Stream Key:", ingress.streamKey);
  console.log("\nAdd these to main/settings.h:");
  console.log(`#define WHIP_URL        "${ingress.url}"`);
  console.log(`#define WHIP_STREAM_KEY "${ingress.streamKey}"`);
}

main().catch(console.error);
