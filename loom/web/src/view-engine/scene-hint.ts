// The always-visible hint of the 3D renderer (P28 step 3): the same bindings the DOM hint shows, as lines of text for the panel drawn inside the scene
// (head-locked, so it is visible in VR and AR too). Pure: the words come from the shell text catalog through `t`, the content from the resolved bindings.
import { LOCK_NAMES } from "./interaction";
import type { Hint } from "./interaction";
import type { XrHint } from "./xr-input";

export type Translate = (key: string, params?: Record<string, string>) => string;
export interface SceneHintInput {
  xr: boolean;
  flat: Hint | null;
  immersive: XrHint | null;
  heldNames: string[];
  /** The sector the user faces now (its label), when the view has sectors. */
  gazedSector: string | null;
  mode: string;
  conflicts: number;
  unsupported: number;
}
const BUTTON_NAMES: Record<string, string> = { left_trigger: "L-trigger", right_trigger: "R-trigger", left_squeeze: "L-grip", right_squeeze: "R-grip", primary: "A/X" };
const ARROW_GLYPH: Record<string, string> = { ArrowLeft: "←", ArrowRight: "→", ArrowUp: "↑", ArrowDown: "↓" };
const cap = (s: string) => s[0].toUpperCase() + s.slice(1);

export function sceneHintLines(i: SceneHintInput, t: Translate): string[] {
  const lines: string[] = [t("scene_hint_title", { mode: i.mode })];
  if (i.xr && i.immersive) {
    const h = i.immersive;
    if (h.select.length) lines.push(t("scene_hint_select", { what: h.select.join(" · ") }));
    if (h.held.length) lines.push(t("scene_hint_held", { what: h.held.map(x => `${BUTTON_NAMES[x.button] ?? x.button} = ${cap(x.mod)}${h.modifiers.find(m => m.mod === x.mod) ? ` (${h.modifiers.find(m => m.mod === x.mod)!.what})` : ""}`).join(" · ") }));
    lines.push(t("scene_hint_stick", { what: `${i.heldNames.length ? `${i.heldNames.join("+")}: ` : ""}${h.stick[0]?.label ?? t("interaction_arrows_plain")}` }));
    if (h.coarse.length) lines.push(t("scene_hint_coarse", { what: h.coarse.join(" · ") }));
    if (h.recorded > 0) lines.push(t("scene_hint_recorded", { count: String(h.recorded) }));
  } else if (i.flat) {
    const f = i.flat;
    lines.push(t("interaction_arrows_now", { what: `${i.heldNames.length ? `${i.heldNames.join("+")}: ` : ""}${f.now ?? t("interaction_arrows_plain")}` })
      + (f.arrows.length ? ` (${f.arrows.map(a => `${ARROW_GLYPH[a.key] ?? a.key} ${a.label}`).join(" · ")})` : ""));
    if (f.held.length) lines.push(t("interaction_held", { mods: f.held.map(x => `${cap(x.mod)} = ${x.what}`).join(" · ") }));
    const locks = f.locks.map(r => `${LOCK_NAMES[r.lock]} ${r.on ? t("interaction_lock_on") : t("interaction_lock_off")}${r.view ? ` → ${r.view}` : r.zoom !== undefined ? ` → ×${r.zoom}` : ""}`);
    lines.push(`${t("interaction_locks")}: ${locks.join(" · ")}`);
    lines.push(t("scene_hint_screen"));
  }
  if (i.gazedSector) lines.push(t("scene_hint_gaze", { sector: i.gazedSector }));
  if (i.conflicts > 0) lines.push(t("scene_hint_conflicts", { count: String(i.conflicts) }));
  if (i.unsupported > 0) lines.push(t("scene_hint_unsupported", { count: String(i.unsupported) }));
  return lines;
}
