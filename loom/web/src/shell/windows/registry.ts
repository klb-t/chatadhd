// The registry of window kinds (data: shell/data/window_kinds.json). Open list: an unknown kind is an advisory at open time, not an error.
import registry from "../data/window_kinds.json";
import type { Ctx } from "./model";

export interface Kind { id: string; label: { pl: string }; component: string | null; params: string[]; status: string; reason?: { pl: string }; note?: string; preset_params?: Record<string, unknown> }
export const KINDS: readonly Kind[] = registry.kinds as Kind[];
export const NOT_WINDOW: readonly { component: string; reason: { pl: string } }[] = registry.not_window;

/** The `kinds` field of the model context (status + reason) built from the registry. */
export function kindsCtx(kinds: readonly Kind[] = KINDS): NonNullable<Ctx["kinds"]> {
  return Object.fromEntries(kinds.map((k) => [k.id, { status: k.status, reason: k.reason?.pl }]));
}
