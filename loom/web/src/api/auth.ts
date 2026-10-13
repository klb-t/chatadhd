// Account endpoints of loom-server (/api/auth/*). The session lives in an HttpOnly cookie the browser
// sends by itself; nothing here stores a credential.
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
  const error = (parsed as { error?: { message?: string } } | null)?.error;
  return { ok: false, status: response.status, reason: error?.message ?? `HTTP ${response.status}` };
}

export const authApi = {
  me: () => call("GET", "/api/auth/me"),
  login: (login: string, password: string) => call("POST", "/api/auth/login", { login, password }),
  register: (login: string, password: string, invite?: string) =>
    call("POST", "/api/auth/register", invite ? { login, password, invite } : { login, password }),
  logout: () => call("POST", "/api/auth/logout", {}),
};

/** Fired by the HTTP transport when an API call is answered 401. */
export const AUTH_REQUIRED_EVENT = "loom:auth-required";
