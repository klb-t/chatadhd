// W6 (OKNA §5.1): the binding of ONE parameter of a window, as data, and how its value is read. No imports on purpose: the profile view
// state (src/profiles/view-state.ts, loaded alone as a data: module by several e2e scripts) reads its conversation through it.
// Modes (the vocabulary of §5.1): shared (the value lives in a channel of the layout, every window bound to it reads and writes it),
// independent (the window's own value), linked (computed from another window or channel by a dependency - shell/windows/coupling.ts).
// An unknown mode is read as independent (nothing dropped); coupling.ts says so with an advisory.

export interface Source { window?: string; channel?: string; param?: string }
export interface Binding { binding: string; channel?: string; value?: unknown; from?: Source; dependency?: { kind: string; [arg: string]: unknown } }

/** A raw value from data/storage as a binding, or null when it is not one (never throws). */
export function readBinding(raw: unknown): Binding | null {
  if (!raw || typeof raw !== "object" || Array.isArray(raw)) return null;
  const r = raw as Record<string, unknown>;
  if (typeof r.binding !== "string" || !r.binding) return null;
  const out: Binding = { binding: r.binding };
  if (typeof r.channel === "string") out.channel = r.channel;
  if ("value" in r) out.value = r.value;
  if (r.from && typeof r.from === "object" && !Array.isArray(r.from)) {
    const f = r.from as Record<string, unknown>;
    out.from = { ...(typeof f.window === "string" ? { window: f.window } : {}), ...(typeof f.channel === "string" ? { channel: f.channel } : {}), ...(typeof f.param === "string" ? { param: f.param } : {}) };
  }
  if (r.dependency && typeof r.dependency === "object" && !Array.isArray(r.dependency) && typeof (r.dependency as { kind?: unknown }).kind === "string") out.dependency = { ...(r.dependency as { kind: string }) };
  return out;
}

/** The value a binding gives to its window: shared reads the channel, everything else the window's own value. */
export function bindingValue(b: Binding | null | undefined, own: unknown, channels: Record<string, unknown>, param = ""): unknown {
  if (b?.binding === "shared") { const ch = b.channel || param; return Object.prototype.hasOwnProperty.call(channels, ch) ? channels[ch] : null; }
  return own;
}

/** The shipped preset of a parameter nobody bound: independent (its own value). */
export const INDEPENDENT: Binding = { binding: "independent" };
