// G3c: the non-blocking advisories of the navigator map. The map's view model is built in the browser (navigator-map.ts viewModelFor), so the
// kernel's view engine cannot evaluate it; this module does only the generic degenerate checks (nothing visible, all marks identical) with the
// kernel's codes and the loom.advisory/1 shape, and the chip itself is the existing ViewBar answered through advisory.answer. Pure: no browser.
// The remembered answers are the kernel's (advisory.list); they are read only when there is something to advise.
import data from "./data/navigator_advisories.json";
import type { ViewAdvisory, ViewModel, ViewText } from "../api/view-model";
import { FALLBACK_CHOICES } from "../view-engine/view-bar";

export interface NavAdvisoryData {
  declaration: string;
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
export const navFingerprint = (channel: string, d: NavAdvisoryData = NAV_ADVISORY): string => `${d.declaration}|${channel}`;

/** The generic checks over the marks of the level (the children; the focus is the anchor, not a data mark). Advisories only; nothing is refused. */
export function navigatorAdvisories(model: Pick<ViewModel, "elements">, d: NavAdvisoryData = NAV_ADVISORY): ViewAdvisory[] {
  const marks = (model.elements ?? []).filter(e => !(e as { focus?: boolean }).focus);
  const out: ViewAdvisory[] = [];
  for (const [channel, def] of Object.entries(d.channels)) {
    const vals = marks.map(e => (e.channels as Record<string, unknown> | undefined)?.[channel]).filter(v => v !== undefined);
    if (vals.length === 0) continue;
    const base = { channel, total: vals.length, shows: def.shows };
    const make = (code: string, params: Record<string, unknown>): ViewAdvisory => ({
      schema: "loom.advisory/1", code, level: "advisory", text: fill(d.texts[code], params), params, evidence: params,
      subject: { kind: "channel", id: channel },
      ask: { fingerprint: navFingerprint(channel, d), question: d.texts.question, choices: FALLBACK_CHOICES, asking: true, state: "ask" },
    });
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
