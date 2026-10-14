/** Bytes <-> base64 for the one transport that has no other way to move them: the native shell's string bridge. Always one bounded chunk. */

/** A binary string of the bytes in slices, so a long chunk does not overflow the argument limit of the call. */
const SLICE = 0x8000;

export function bytesToBase64(bytes: Uint8Array): string {
  let binary = "";
  for (let at = 0; bytes.length > at; at += SLICE) binary += String.fromCharCode(...bytes.subarray(at, at + SLICE));
  return btoa(binary);
}

export function base64ToBytes(encoded: string): Uint8Array {
  const binary = atob(encoded);
  const bytes = new Uint8Array(binary.length);
  for (let at = 0; binary.length > at; at += 1) bytes[at] = binary.charCodeAt(at);
  return bytes;
}
