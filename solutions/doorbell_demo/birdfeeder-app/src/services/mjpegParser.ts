/**
 * Parse an MJPEG AVI file by scanning for JPEG SOI (0xFFD8) and EOI (0xFFD9) markers.
 * Returns an array of base64-encoded JPEG frames.
 */
export function parseMjpegAvi(buffer: ArrayBuffer): string[] {
  const data = new Uint8Array(buffer);
  const frames: string[] = [];

  for (let i = 0; i < data.length - 1; i++) {
    if (data[i] === 0xff && data[i + 1] === 0xd8) {
      // Found JPEG SOI, search for EOI
      for (let j = i + 2; j < data.length - 1; j++) {
        if (data[j] === 0xff && data[j + 1] === 0xd9) {
          const frameBytes = data.slice(i, j + 2);
          frames.push(uint8ToBase64(frameBytes));
          i = j + 1;
          break;
        }
      }
    }
  }
  return frames;
}

function uint8ToBase64(bytes: Uint8Array): string {
  let binary = '';
  for (let i = 0; i < bytes.length; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}
