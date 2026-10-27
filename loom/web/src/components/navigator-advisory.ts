// G3c: the non-blocking advisories of the navigator map. The map's view model is built in the browser (navigator-map.ts viewModelFor), so the
// kernel's view engine cannot evaluate it; this module does only the generic degenerate checks (nothing visible, all marks identical) with the
// kernel's codes and the loom.advisory/1 shape, and the chip itself is the existing ViewBar answered through advisory.answer. Pure: no browser.
// The remembered answers are the kernel's (advisory.list); they are read only when there is something to advise.
import data from "./data/navigator_advisories.json";
import type { ViewAdvisory, ViewModel, ViewText } from "../api/view-model";
import { FALLBACK_CHOICES } from "../view-engine/view-bar";
import { NAV_MAPPING, measureWords, parseMapping, type ChannelReport } from "./navigator-mapping";

export interface NavAdvisoryData {
  declaration: string;
  advise_on_defaults?: boolean;
  default_mapping?: Record<string, string>;
  min_elements: number;
  channels: Record<string, { invisible_at_or_below?: number; shows?: ViewText }>;
  texts: Record<string, ViewText>;
}
export const NAV_ADVISORY: NavAdvisoryData = data as unknown as NavAdvisoryData;

const fill = (t: ViewText | undefined, params: Record<string, unknown>): ViewText => {
  const out: Record<string, string> = {};
  for (const lang of ["pl", "en"]) {
    const src = (t as Record<string, string> | undefined)?.[lang] ?? "";
    out[lang] = src.replace(/\{(\w+)\}/g, (_, k) => {
      const v = params[k];
      return v && typeof v === "object" && ("pl" in v || "en" in v) ? String((v as Record<string, string>)[lang] ?? "") : String(v ?? "");
    });
  }
  return out as unknown as ViewText;
};

/** The configuration fingerprint: the declaration + the channel (the mapping, not the data), so "don't ask" holds on any level. */
export const navFingerprint = (channel: string, d: NavAdvisoryData = NAV_ADVISORY, measure = ""): string => `${d.declaration}|${channel}${measure ? `|${measure}` : ""}`;

/** The mapping the person set (setting navigator_mapping: an object or its JSON text; channel -> measure, or channel -> {measure, ...}) as channel -> measure;
 *  anything unreadable is "no mapping". */
export function readMapping(raw: unknown): Record<string, string> {
  return Object.fromEntries(Object.entries(parseMapping(raw)).map(([c, x]) => [c, x.measure]));
}

const shownAs = (measure: string): ViewText => ({ pl: measureWords(measure, "pl"), en: measureWords(measure, "en") } as unknown as ViewText);

/** The generic checks over the marks of the level (the children; the focus is the anchor, not a data mark). Advisories only; nothing is refused.
 *  G3d: with the mapping report of the level (navigator-map.ts viewModelFor) also channel_unknown (a channel the map does not draw) and
 *  measure_missing (a measure no mark has: the channel draws its preset for every mark). */
export function navigatorAdvisories(model: Pick<ViewModel, "elements"> & { mapping_report?: ChannelReport[] }, d: NavAdvisoryData = NAV_ADVISORY, mapping: Record<string, string> = {}): ViewAdvisory[] {
  const marks = (model.elements ?? []).filter(e => !(e as { focus?: boolean }).focus);
  const defaults = d.default_mapping ?? NAV_MAPPING.default;
  const out: ViewAdvisory[] = [];
  const report = new Map((model.mapping_report ?? []).map(r => [r.channel, r]));
  const channels = [...new Set([...Object.keys(d.channels), ...[...report.values()].filter(r => !r.drawn).map(r => r.channel)])];   // a channel deleted from the data is not checked; one the map cannot draw is always said
  for (const channel of channels) {
    const def = d.channels[channel] ?? {};
    const shipped = defaults[channel];
    const measure = mapping[channel] ?? shipped ?? "";
    // the app's own default is not a question to the person: with advise_on_defaults false only a changed mapping is checked
    if (!d.advise_on_defaults && measure === (shipped ?? "")) continue;
    const changedMeasure = mapping[channel] !== undefined && mapping[channel] !== shipped ? mapping[channel] : "";
    const base = { channel, total: marks.length, shows: changedMeasure ? shownAs(changedMeasure) : def.shows ?? (measure ? shownAs(measure) : "") };
    const make = (code: string, params: Record<string, unknown>): ViewAdvisory => ({
      schema: "loom.advisory/1", code, level: "advisory", text: fill(d.texts[code], params), params, evidence: params,
      subject: { kind: "channel", id: channel },
      ask: { fingerprint: navFingerprint(channel, d, changedMeasure), question: d.texts.question, choices: FALLBACK_CHOICES, asking: true, state: "ask" },
    });
    const r = report.get(channel);
    if (r && !r.drawn) { out.push(make("channel_unknown", { ...base, measure: shownAs(measure), name: measure })); continue; }
    if (r && r.total >= Math.max(1, d.min_elements) && r.present === 0 && !NAV_MAPPING.measures[r.measure]?.constant) {
      out.push(make("measure_missing", { ...base, measure: shownAs(measure), name: measure, total: r.total, missing: d.channels[channel]?.invisible_at_or_below ?? "" }));
      continue;   // every mark draws the same preset: one chip says why, the generic "all identical" would only repeat it
    }
    const vals = marks.map(e => (e.channels as Record<string, unknown> | undefined)?.[channel]).filter(v => v !== undefined);
    if (vals.length === 0) continue;
    base.total = vals.length;
    const inv = def.invisible_at_or_below;
    if (typeof inv === "number") {
      const n = vals.filter(v => typeof v === "number" && v <= inv).length;
      if (n > 0 && n >= vals.length) out.push(make("nothing_visible", { ...base, invisible: n, threshold: inv }));
    }
    if (vals.length >= Math.max(1, d.min_elements) && vals.every(v => JSON.stringify(v) === JSON.stringify(vals[0])))
      out.push(make("marks_identical", { ...base, value: typeof vals[0] === "number" ? Math.round((vals[0] as number) * 100) / 100 : vals[0] }));
  }
  return out;
}

/** The advisories the person still has to see: a remembered "don't ask" (code + fingerprint, or the code alone) or a policy ask=false marks it not asking.
 *  The chip component hides what is not asking; the advisory itself stays in the data. */
export function resolveAsk(list: ViewAdvisory[], remembered: ReadonlySet<string>, ask = true): ViewAdvisory[] {
  return list.map(a => {
    const fp = a.ask?.fingerprint ?? "";
    const quiet = !ask || remembered.has(`${a.code}|${fp}`) || remembered.has(`${a.code}|`);
    return quiet ? { ...a, ask: { ...a.ask, asking: false, state: ask ? "acknowledged" : "muted" } } : a;
  });
}
