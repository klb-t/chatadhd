// R56: animation as data. Styles, the element table, units and reduced motion come from runtime/ui_animation.pack (and the user's overlay settings).
import pack from "./generated/ui-animation.json";

export interface Duration { value: number; unit: "ms" | "s" | "frames" }
export interface AnimStyle { kind: string; direction?: string; duration: Duration; easing: string }
export interface AnimData {
  refresh_hz: number; reduced_motion_style: string;
  easings: Record<string, string>; styles: Record<string, AnimStyle>; elements: Record<string, string>;
}
export const SHIPPED: AnimData = pack as unknown as AnimData;
export interface AnimSettings { animation_styles?: unknown; animations?: unknown; animation_reduced_motion?: unknown; animation_refresh_hz?: unknown }
export interface Resolved { style: string; kind: string; direction: string; ms: number; easing: string }

const rec = (v: unknown): Record<string, unknown> => v && typeof v === "object" && !Array.isArray(v) ? v as Record<string, unknown> : {};

/** A duration in any unit to milliseconds; frames use the refresh rate. An invalid one is 0 (no animation). */
export function toMs(d: unknown, hz: number): number {
  const o = rec(d); const v = o.value;
  if (typeof v !== "number" || !Number.isFinite(v) || v < 0) return 0;
  if (o.unit === "s") return v * 1000;
  if (o.unit === "frames") return hz > 0 ? v * 1000 / hz : 0;
  return v;
}

export function mergeData(settings: AnimSettings = {}, base: AnimData = SHIPPED): AnimData {
  const styles: Record<string, AnimStyle> = { ...base.styles };
  for (const [name, s] of Object.entries(rec(settings.animation_styles))) if (s && typeof s === "object") styles[name] = s as AnimStyle;
  const elements = { ...base.elements };
  for (const [el, name] of Object.entries(rec(settings.animations))) if (typeof name === "string") elements[el] = name;
  const hz = typeof settings.animation_refresh_hz === "number" && settings.animation_refresh_hz > 0 ? settings.animation_refresh_hz : base.refresh_hz;
  const reduced = typeof settings.animation_reduced_motion === "string" && settings.animation_reduced_motion ? settings.animation_reduced_motion : base.reduced_motion_style;
  return { ...base, styles, elements, refresh_hz: hz, reduced_motion_style: reduced };
}

/** The style of an element; reduced motion replaces it by the reduced-motion style. Unknown names mean no animation. */
export function resolveElement(data: AnimData, element: string, reducedMotion: boolean): Resolved {
  const name = reducedMotion ? data.reduced_motion_style : data.elements[element] ?? "none";
  const s = data.styles[name] ?? data.styles.none ?? { kind: "none", duration: { value: 0, unit: "ms" }, easing: "linear" };
  const easing = data.easings[s.easing] ?? (/^cubic-bezier\(/.test(s.easing) ? s.easing : "linear");
  const ms = s.kind === "none" ? 0 : Math.round(toMs(s.duration, data.refresh_hz));
  return { style: name, kind: s.kind, direction: s.direction ?? "left", ms, easing };
}

/** CSS variables of every element: --anim-<element>-ms, -easing, -kind. */
export function cssVariables(data: AnimData, reducedMotion: boolean): Record<string, string> {
  const out: Record<string, string> = {};
  for (const el of Object.keys(data.elements)) {
    const r = resolveElement(data, el, reducedMotion);
    out[`--anim-${el}-ms`] = `${r.ms}ms`; out[`--anim-${el}-easing`] = r.easing; out[`--anim-${el}-kind`] = r.kind;
    out[`--anim-${el}-fade`] = r.kind.includes("fade") ? "0" : "1"; // start opacity of a fading element
  }
  return out;
}
