// R103 B2: the proof of work a session without an account starts with (instead of a captcha: nothing from third
// parties). The server gives a challenge; the browser finds a nonce such that SHA-256("challenge:nonce") starts with
// `bits` zero bits. The search runs in a Web Worker so the page stays responsive; without workers it runs here in
// slices. One SHA-256 block (the challenge is short), so a plain JavaScript implementation is fast enough.

const K = new Uint32Array([
  0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01,
  0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
  0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da, 0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
  0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
  0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070, 0x19a4c116, 0x1e376c08,
  0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
  0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2,
]);
const W = new Uint32Array(64);

/** SHA-256 of a short ASCII text (at most 55 bytes: one block) as eight 32-bit words. */
export function sha256Short(text: string): Uint32Array {
  const n = text.length;
  if (n > 55) throw new Error("pow_input_too_long");
  W.fill(0, 0, 16);
  for (let i = 0; i < n; i++) W[i >> 2] |= (text.charCodeAt(i) & 0xff) << (24 - (i & 3) * 8);
  W[n >> 2] |= 0x80 << (24 - (n & 3) * 8);
  W[15] = n * 8;
  for (let i = 16; i < 64; i++) {
    const a = W[i - 15], b = W[i - 2];
    const s0 = ((a >>> 7) | (a << 25)) ^ ((a >>> 18) | (a << 14)) ^ (a >>> 3);
    const s1 = ((b >>> 17) | (b << 15)) ^ ((b >>> 19) | (b << 13)) ^ (b >>> 10);
    W[i] = (W[i - 16] + s0 + W[i - 7] + s1) | 0;
  }
  let a = 0x6a09e667, b = 0xbb67ae85, c = 0x3c6ef372, d = 0xa54ff53a, e = 0x510e527f, f = 0x9b05688c, g = 0x1f83d9ab, h = 0x5be0cd19;
  for (let i = 0; i < 64; i++) {
    const S1 = ((e >>> 6) | (e << 26)) ^ ((e >>> 11) | (e << 21)) ^ ((e >>> 25) | (e << 7));
    const t1 = (h + S1 + ((e & f) ^ (~e & g)) + K[i] + W[i]) | 0;
    const S0 = ((a >>> 2) | (a << 30)) ^ ((a >>> 13) | (a << 19)) ^ ((a >>> 22) | (a << 10));
    const t2 = (S0 + ((a & b) ^ (a & c) ^ (b & c))) | 0;
    h = g; g = f; f = e; e = (d + t1) | 0; d = c; c = b; b = a; a = (t1 + t2) | 0;
  }
  return new Uint32Array([0x6a09e667 + a, 0xbb67ae85 + b, 0x3c6ef372 + c, 0xa54ff53a + d, 0x510e527f + e, 0x9b05688c + f, 0x1f83d9ab + g, 0x5be0cd19 + h]);
}

export function leadingZeroBits(words: Uint32Array, bits: number): boolean {
  for (let i = 0; bits > 0; i++, bits -= 32) {
    const need = Math.min(bits, 32);
    if (need === 32 ? words[i] !== 0 : words[i] >>> (32 - need) !== 0) return false;
  }
  return true;
}

/** Searches nonces [from, from + count); the found nonce as text, or null. */
export function findNonce(challenge: string, bits: number, from: number, count: number): string | null {
  for (let n = from; n < from + count; n++) if (leadingZeroBits(sha256Short(`${challenge}:${n}`), bits)) return String(n);
  return null;
}

export interface PowChallenge { challenge?: string; bits: number }

/** The nonce for a challenge, found in a Web Worker when the browser has them. */
export async function solveChallenge(c: PowChallenge): Promise<string> {
  if (!c.bits || !c.challenge) return "";
  const challenge = c.challenge;
  if (typeof Worker !== "undefined") {
    try {
      return await new Promise<string>((resolve, reject) => {
        const worker = new Worker(new URL("./pow-worker.ts", import.meta.url), { type: "module" });
        worker.onmessage = (event: MessageEvent<{ nonce?: string }>) => { worker.terminate(); event.data.nonce ? resolve(event.data.nonce) : reject(new Error("pow_not_found")); };
        worker.onerror = (event) => { worker.terminate(); reject(new Error(event.message || "pow_worker_failed")); };
        worker.postMessage({ challenge, bits: c.bits });
      });
    } catch { /* no worker (blocked, old browser): the same search here, in slices */ }
  }
  for (let from = 0; ; from += 20000) {
    const found = findNonce(challenge, c.bits, from, 20000);
    if (found !== null) return found;
    await new Promise((r) => setTimeout(r, 0));
  }
}
