// WebXR feature detection (P28 step 3), without loading the 3D library: is navigator.xr there, and which sessions does the device support?
// The answer is shown to the user as a note; nothing here falls back silently.
export interface XrSupport {
  /** navigator.xr exists. */
  available: boolean;
  /** Session mode -> supported (isSessionSupported); modes that could not be checked are false. */
  sessions: Record<string, boolean>;
  /** Why the check failed, when it did. */
  error?: string;
}
export async function detectXr(modes: string[], nav: { xr?: { isSessionSupported(mode: string): Promise<boolean> } } | undefined = typeof navigator === "undefined" ? undefined : (navigator as never)): Promise<XrSupport> {
  const xr = nav?.xr;
  const sessions: Record<string, boolean> = {};
  for (const m of modes) sessions[m] = false;
  if (!xr || typeof xr.isSessionSupported !== "function") return { available: false, sessions };
  const errors: string[] = [];
  for (const m of modes) {
    try { sessions[m] = (await xr.isSessionSupported(m)) === true; } catch (cause) { errors.push(`${m}: ${cause instanceof Error ? cause.message : String(cause)}`); }
  }
  return { available: true, sessions, ...(errors.length ? { error: errors.join("; ") } : {}) };
}
