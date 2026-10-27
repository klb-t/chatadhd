// The "Widok" bar (G3b): what the current view encodes, said in words, and its advisories as non-blocking chips. Pure: no browser, no kernel.
// Which advisories show is the kernel's answer (ask.asking, from the person's remembered answers and the policy) plus what this page was told in
// this session (a "keep" hides the chip until the next evaluation of another page; the kernel records it and asks again next time).
import type { ViewAdvisory, ViewAdvisoryChoice, ViewModel, ViewText } from "../api/view-model";

export interface BarEncoding { id: string; channel: string; measure: string; combine?: string; status: string }
export interface BarComposed { channel: string; encodings: string[]; combines: string[]; elements: number }
export interface BarChip { key: string; code: string; fingerprint: string; level: string; text: ViewText; question?: ViewText; choices: ViewAdvisoryChoice[]; advisory: ViewAdvisory }
export interface BarState { encodings: BarEncoding[]; composed: BarComposed[]; chips: BarChip[]; hidden: number }

/** The preset choices when an advisory carries none (the kernel always sends them; this keeps the bar answerable if a model omits them). */
export const FALLBACK_CHOICES: ViewAdvisoryChoice[] = [
  { id: "keep", label: { en: "Keep it", pl: "Zostaw" } },
  { id: "dont_ask", label: { en: "Keep it, don't ask again", pl: "Zostaw, nie pytaj więcej" } },
];

export const advisoryKey = (a: Pick<ViewAdvisory, "code" | "ask">): string => `${a.code}|${a.ask?.fingerprint ?? ""}`;

/** An advisory is shown while the kernel says it asks and this session did not answer it. Every advisory stays in the data (`hidden` counts the rest). */
export function barState(model: ViewModel | null, answered: ReadonlySet<string> = new Set()): BarState {
  if (!model) return { encodings: [], composed: [], chips: [], hidden: 0 };
  const encodings = (model.encodings ?? []).filter(e => e.status === "promoted").map(e => ({ id: e.id, channel: e.channel, measure: e.measure, status: e.status, ...(e.combine ? { combine: e.combine } : {}) }));
  const byChannel = new Map<string, BarComposed>();
  for (const el of model.elements ?? [])
    for (const [channel, parts] of Object.entries(el.channel_parts ?? {})) {
      const c = byChannel.get(channel) ?? { channel, encodings: [], combines: [], elements: 0 };
      c.elements++;
      for (const p of parts) {
        if (!c.encodings.includes(p.encoding)) c.encodings.push(p.encoding);
        const how = p.combine ?? "replace";
        if (!c.combines.includes(how)) c.combines.push(how);
      }
      byChannel.set(channel, c);
    }
  const chips: BarChip[] = [];
  let hidden = 0;
  for (const a of model.advisories ?? []) {
    const key = advisoryKey(a);
    if (a.ask?.asking === false || answered.has(key)) { hidden++; continue; }
    chips.push({ key, code: a.code, fingerprint: a.ask?.fingerprint ?? "", level: a.level, text: a.text, question: a.ask?.question, choices: a.ask?.choices?.length ? a.ask.choices : FALLBACK_CHOICES, advisory: a });
  }
  return { encodings, composed: [...byChannel.values()], chips, hidden };
}

/** The request of the catalogue action that answers a chip (POST /api/actions/run): the kernel remembers it under the code + fingerprint. */
export function answerRequest(chip: BarChip, choice: string, view: string, text: string): { action: string; element: { class: string }; args: Record<string, unknown> } {
  return { action: "advisory.answer", element: { class: "application" }, args: { code: chip.code, fingerprint: chip.fingerprint, answer: choice, context: { view, text } } };
}
