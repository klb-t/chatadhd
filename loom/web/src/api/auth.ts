// Account endpoints of loom-server (/api/auth/*). The session lives in an HttpOnly cookie the browser
// sends by itself; nothing here stores a credential.
import { solveChallenge, type PowChallenge } from "./pow";

export interface AuthResult { ok: true; body: unknown }
export interface AuthFailure { ok: false; status: number; reason: string }

async function call(method: string, path: string, body?: unknown): Promise<AuthResult | AuthFailure> {
  let response: Response;
  try {
    response = await fetch(path, {
      method, credentials: "same-origin",
      headers: body === undefined ? undefined : { "Content-Type": "application/json" },
      body: body === undefined ? undefined : JSON.stringify(body),
    });
  } catch (cause) {
    return { ok: false, status: 0, reason: cause instanceof Error ? cause.message : String(cause) };
  }
  const text = await response.text();
  let parsed: unknown = {};
  try { parsed = text ? JSON.parse(text) : {}; } catch { /* non-JSON body */ }
  if (response.ok) return { ok: true, body: parsed };
  announceLimit(response.status, parsed);
  const error = (parsed as { error?: { message?: string } } | null)?.error;
  return { ok: false, status: response.status, reason: error?.message ?? `HTTP ${response.status}` };
}

export const authApi = {
  me: () => call("GET", "/api/auth/me"),
  login: (login: string, password: string) => call("POST", "/api/auth/login", { login, password }),
  register: (login: string, password: string, invite?: string) =>
    call("POST", "/api/auth/register", invite ? { login, password, invite } : { login, password }),
  logout: () => call("POST", "/api/auth/logout", {}),
  /** R103: a session without an account (only when the server offers guests). R103 B2: it starts with the proof of
   *  work the server asks for, solved in a Web Worker ({bits: 0} = none). */
  guest: async (invite?: string) => {
    let proof: { challenge?: string; nonce?: string } = {};
    const c = await call("GET", "/api/auth/guest/challenge");
    if (c.ok) {
      const challenge = c.body as PowChallenge;
      if (challenge.bits > 0 && challenge.challenge) proof = { challenge: challenge.challenge, nonce: await solveChallenge(challenge) };
    }
    return call("POST", "/api/auth/guest", invite ? { ...proof, invite } : proof);
  },
};

/** R103 B2: fired when the guest protection refused a request (429/403/413 with `limit`), with the wait and the words. */
export const GUEST_LIMIT_EVENT = "loom:guest-limit";
export interface GuestLimitDetail { mechanism: string; retryAfter: number | null; text: string; alternatives: string; status: number }

/** Reads a guest-protection refusal from an answer body and announces it; true when it was one. */
export function announceLimit(status: number, body: unknown): boolean {
  const b = body as { limit?: { mechanism?: unknown }; retry_after?: unknown; words?: { text?: unknown; alternatives?: unknown } } | null;
  if (!b || typeof b !== "object" || !b.limit || typeof b.limit.mechanism !== "string") return false;
  const detail: GuestLimitDetail = {
    mechanism: b.limit.mechanism, status,
    retryAfter: typeof b.retry_after === "number" ? b.retry_after : null,
    text: typeof b.words?.text === "string" ? b.words.text : "",
    alternatives: typeof b.words?.alternatives === "string" ? b.words.alternatives : "",
  };
  if (typeof window !== "undefined") window.dispatchEvent(new CustomEvent<GuestLimitDetail>(GUEST_LIMIT_EVENT, { detail }));
  return true;
}

/** Fired by the HTTP transport when an API call is answered 401. */
export const AUTH_REQUIRED_EVENT = "loom:auth-required";
