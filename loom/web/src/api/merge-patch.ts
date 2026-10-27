// M1-OKNA-W5b: the merge of one config value, the same on every host (the server does it under its config lock).
/** RFC 7386 merge patch (a stored JSON text of an object is merged as that object). */
export function mergePatch(target: unknown, patch: unknown): unknown {
  if (!patch || typeof patch !== "object" || Array.isArray(patch)) return patch;
  let base: unknown = target;
  if (typeof base === "string") { try { base = JSON.parse(base); } catch { /* not JSON: replaced */ } }
  const out: Record<string, unknown> = base && typeof base === "object" && !Array.isArray(base) ? { ...(base as Record<string, unknown>) } : {};
  for (const [k, v] of Object.entries(patch as Record<string, unknown>)) {
    if (v === null) delete out[k];
    else out[k] = mergePatch(out[k], v);
  }
  return out;
}
