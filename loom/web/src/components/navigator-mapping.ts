// G3d: the channel mapping of the navigator map as a real setting. navigator_mapping (channel -> measure) decides what the map draws: every channel
// of a mark takes its value from one measure of the row (kind, count, age, order, evidence, any dotted path into the row ...), with the presets and
// the words in data/navigator_mapping.json. The shipped default reproduces the M1-W0 map exactly (colour from the kind, size from the count). Pure:
// no browser. Nothing is refused: an unknown channel or a measure no row has is drawn with the preset and reported (the advisories say it).
import data from "./data/navigator_mapping.json";
import type { GraphNodeRow } from "../api/graph-projection";
import type { ViewText } from "../api/view-model";
import { colorFor } from "./kind-colors";

export interface ChannelDef {
  words: ViewText;
  /** A colour channel: a category takes this component of the colour of the category (hue | saturation | lightness). */
  colour?: "hue" | "saturation" | "lightness";
  range: [number, number];
  missing: number;
  curve: "linear" | "log1p" | string;
  base?: number;
  gain?: number;
}
export interface MeasureDef { words: ViewText; path?: string; age_of?: string; constant?: boolean }
export interface MappingData {
  default: Record<string, string>;
  focus_size: number;
  channels: Record<string, ChannelDef>;
  measures: Record<string, MeasureDef>;
  words: Record<string, ViewText>;
}
export const NAV_MAPPING: MappingData = data as unknown as MappingData;

/** One channel's choice: a measure, optionally with its own range / missing / curve (the finer form of the setting). */
export interface ChannelChoice { measure: string; range?: [number, number]; missing?: number; curve?: string; base?: number; gain?: number }
export type Mapping = Record<string, ChannelChoice>;

/** The setting navigator_mapping as given (an object, its JSON text, or nothing): channel -> measure name or {measure, ...}; anything unreadable is "no choice". */
export function parseMapping(raw: unknown): Mapping {
  let v = raw;
  if (typeof v === "string") { try { v = JSON.parse(v); } catch { return {}; } }
  if (!v || typeof v !== "object" || Array.isArray(v)) return {};
  const out: Mapping = {};
  for (const [channel, x] of Object.entries(v as Record<string, unknown>)) {
    if (typeof x === "string" && x) out[channel] = { measure: x };
    else if (x && typeof x === "object" && !Array.isArray(x) && typeof (x as { measure?: unknown }).measure === "string") out[channel] = { ...(x as ChannelChoice) };
  }
  return out;
}

/** The mapping in effect: the shipped default with the person's choices over it (a choice for a channel the map does not draw is kept: it is reported). */
export function effectiveMapping(raw: unknown, d: MappingData = NAV_MAPPING): Mapping {
  const out: Mapping = {};
  for (const [channel, measure] of Object.entries(d.default)) out[channel] = { measure };
  return { ...out, ...parseMapping(raw) };
}

/** The setting as the person's choices only (what differs from the default), the form the control writes; {} = the default. */
export function choicesOnly(mapping: Mapping, d: MappingData = NAV_MAPPING): Record<string, string | ChannelChoice> {
  const out: Record<string, string | ChannelChoice> = {};
  for (const [channel, c] of Object.entries(mapping)) {
    const plain = Object.keys(c).length === 1;
    if (plain && d.default[channel] === c.measure) continue;
    out[channel] = plain ? c.measure : c;
  }
  return out;
}

const at = (obj: unknown, path: string): unknown => {
  let v: unknown = obj;
  for (const k of path.split(".")) { if (!v || typeof v !== "object") return undefined; v = (v as Record<string, unknown>)[k]; }
  return v;
};

/** The value of a measure on a row: a number, a text (a category), or undefined (the row has no such value). */
export function measureOf(row: Partial<GraphNodeRow>, measure: string, now: number, d: MappingData = NAV_MAPPING): number | string | undefined {
  const def = d.measures[measure];
  if (def?.constant) return 1;
  if (def?.age_of) {
    const t = at(row, def.age_of);
    const ms = typeof t === "string" || typeof t === "number" ? new Date(t).getTime() : NaN;
    return Number.isFinite(ms) ? (now - ms) / 86_400_000 : undefined;
  }
  const v = at(row, def?.path ?? measure);
  if (typeof v === "boolean") return v ? 1 : 0;
  if (typeof v === "number") return Number.isFinite(v) ? v : undefined;
  if (typeof v === "string") return v;   // "" is a category too (the M1-W0 map coloured a kind "" with the fallback colour)
  return undefined;
}

const lerp = (r: [number, number], t: number) => r[0] + (r[1] - r[0]) * t;
const clean = (x: number) => Math.round(x * 1000) / 1000;

/** The HSL of a #rrggbb colour (hue in degrees, saturation and lightness 0..1), the channels project() understands. */
export function hslOfHex(hex: string): { hue: number; saturation: number; lightness: number } {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return { hue: 210, saturation: 0.55, lightness: 0.55 };
  const n = parseInt(m[1], 16), r = ((n >> 16) & 255) / 255, g = ((n >> 8) & 255) / 255, b = (n & 255) / 255;
  const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2, dd = max - min;
  if (dd === 0) return { hue: 0, saturation: 0, lightness: Math.round(l * 1000) / 1000 };
  const s = dd / (1 - Math.abs(2 * l - 1));
  const h = max === r ? ((g - b) / dd + (g < b ? 6 : 0)) : max === g ? (b - r) / dd + 2 : (r - g) / dd + 4;
  return { hue: Math.round(h * 60) % 360, saturation: Math.round(s * 1000) / 1000, lightness: Math.round(l * 1000) / 1000 };
}

/** What happened to each channel on a level: its measure, whether the map draws the channel, how many marks had a value. The advisories read it. */
export interface ChannelReport { channel: string; measure: string; drawn: boolean; known_measure: boolean; present: number; total: number; changed: boolean }

export interface LevelChannels { channels: Map<Partial<GraphNodeRow>, Record<string, number>>; report: ChannelReport[] }

/** The channels of every row of a level (the focus included: the same mapping, but size is the anchor's preset). The level's own spread (smallest to
 *  largest, the distinct categories) is computed over the children, the marks of the level; the focus is placed on the same scale. */
export function levelChannels(focus: Partial<GraphNodeRow>, children: readonly Partial<GraphNodeRow>[], raw: unknown, now = Date.now(), d: MappingData = NAV_MAPPING): LevelChannels {
  const mapping = effectiveMapping(raw, d);
  const out = new Map<Partial<GraphNodeRow>, Record<string, number>>();
  for (const r of [focus, ...children]) out.set(r, {});
  const report: ChannelReport[] = [];
  for (const [channel, choice] of Object.entries(mapping)) {
    const base = d.channels[channel];
    const changed = d.default[channel] !== choice.measure || Object.keys(choice).length > 1;
    const values = children.map(r => measureOf(r, choice.measure, now, d));
    report.push({ channel, measure: choice.measure, drawn: !!base, known_measure: !!d.measures[choice.measure], present: values.filter(v => v !== undefined).length, total: children.length, changed });
    if (!base) continue;   // a channel the map does not draw: reported, never refused
    const def: ChannelDef = { ...base, ...(choice.range ? { range: choice.range } : {}), ...(typeof choice.missing === "number" ? { missing: choice.missing } : {}),
      ...(choice.curve ? { curve: choice.curve } : {}), ...(typeof choice.base === "number" ? { base: choice.base } : {}), ...(typeof choice.gain === "number" ? { gain: choice.gain } : {}) };
    const nums = values.filter((v): v is number => typeof v === "number");
    const lo = nums.length ? Math.min(...nums) : 0, hi = nums.length ? Math.max(...nums) : 0;
    const cats = [...new Set(values.filter((v): v is string => typeof v === "string"))].sort();
    const valueOf = (v: number | string | undefined): number => {
      if (v === undefined) return def.missing;
      if (d.measures[choice.measure]?.constant) return clean(lerp(def.range, 0.5));
      if (typeof v === "string") {
        if (def.colour) return hslOfHex(colorFor(v))[def.colour];
        const i = cats.indexOf(v);
        return clean(lerp(def.range, cats.length > 1 && i >= 0 ? i / (cats.length - 1) : 0.5));
      }
      if (def.curve === "log1p") return (def.base ?? def.range[0]) + (v > 0 ? (def.gain ?? 1) * Math.log10(1 + v) : v < 0 ? -(def.gain ?? 1) * Math.log10(1 - v) : 0);
      return clean(lerp(def.range, hi > lo ? (Math.min(hi, Math.max(lo, v)) - lo) / (hi - lo) : 0.5));
    };
    if (channel !== "size") out.get(focus)![channel] = valueOf(measureOf(focus, choice.measure, now, d));
    children.forEach((r, i) => { out.get(r)![channel] = valueOf(values[i]); });
  }
  out.get(focus)!.size = d.focus_size;
  return { channels: out, report };
}

/** Words of a channel / a measure in a language (a measure without words is shown as "other: <name>": the person typed it, it is their text). */
export const wordOf = (t: ViewText | undefined, language: string): string => {
  const w = t as Record<string, string> | undefined;
  return w?.[language] ?? w?.en ?? w?.pl ?? "";
};
export const channelWords = (channel: string, language: string, d: MappingData = NAV_MAPPING): string => wordOf(d.channels[channel]?.words, language) || channel;
export const measureWords = (measure: string, language: string, d: MappingData = NAV_MAPPING): string =>
  d.measures[measure] ? wordOf(d.measures[measure].words, language) : wordOf(d.words.other, language).replace("{measure}", measure);
export const mappingText = (key: string, language: string, params: Record<string, string> = {}, d: MappingData = NAV_MAPPING): string =>
  wordOf(d.words[key], language).replace(/\{(\w+)\}/g, (_, k) => params[k] ?? "");
