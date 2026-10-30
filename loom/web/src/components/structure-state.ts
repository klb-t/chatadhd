// W11a-1: the state of the window „Struktura projektu” without a browser - the window parameters, the appearance mapping (data:
// shell/data/structure_mappings.json, or an object given as the parameter `mapping`) and the rows drawn from a graph.structure answer.
// Nothing here writes: the window is a view (the kernel's graph.structure is read only).
import MAPPINGS from "../shell/data/structure_mappings.json";
import type { GraphStructureRequest, GraphStructureResult, StructureAdvisory, StructureExpectation } from "../api/graph-projection";

export type Sign = Record<string, unknown>;
export interface MappingData { default: string; presets: Record<string, { label?: { pl?: string; en?: string }; states: Record<string, Sign> }>; signs: string[]; advisories: Record<string, { pl?: string; en?: string }> }
export const MAPPING_DATA = MAPPINGS as unknown as MappingData;

/** The parameters of a window of the kind project_structure (window_kinds.json params). Absent = the preset. */
export interface StructureParams { root: string; templates: string[]; show_expected: boolean; mapping: unknown; depth: number; budget: number | null }
export function paramsOf(get: (p: string) => unknown, initialRoot = ""): StructureParams {
  const root = get("root"), templates = get("templates"), show = get("show_expected"), depth = get("depth"), budget = get("budget");
  return {
    root: typeof root === "string" ? root : initialRoot,
    templates: Array.isArray(templates) ? templates.filter((x): x is string => typeof x === "string") : [],
    show_expected: typeof show === "boolean" ? show : true,
    mapping: get("mapping") ?? null,
    depth: typeof depth === "number" && Number.isFinite(depth) ? depth : 1,
    budget: typeof budget === "number" && Number.isFinite(budget) ? budget : null,
  };
}

/** The request graph.structure gets for these parameters. */
export function requestOf(p: StructureParams, lang: string, convId?: string | null): GraphStructureRequest {
  const r: GraphStructureRequest = { root: p.root, show_expected: p.show_expected, depth: p.depth, lang };
  if (p.templates.length) r.templates = p.templates;
  if (p.budget !== null) r.budget = p.budget;
  if (convId) r.conv_id = convId;
  return r;
}

export interface PageAdvisory { code: string; fingerprint: string; text: string }
const say = (w: { pl?: string; en?: string } | undefined, lang: string, vars: Record<string, string> = {}): string => {
  let s = (lang === "pl" ? w?.pl : w?.en) ?? w?.en ?? w?.pl ?? "";
  for (const [k, v] of Object.entries(vars)) s = s.split(`{{${k}}}`).join(v);
  return s;
};

export interface ResolvedMapping { id: string; states: Record<string, Sign>; advisories: PageAdvisory[] }
/** A preset id, an object {states} (its own mapping) or nothing (the default preset). `settings` gives the values {setting: key} read. */
export function resolveMapping(param: unknown, lang: string, settings: Record<string, unknown> = {}, data: MappingData = MAPPING_DATA): ResolvedMapping {
  const advisories: PageAdvisory[] = [];
  let id = data.default, raw: Record<string, Sign> = data.presets[data.default]?.states ?? {};
  if (typeof param === "string" && param) {
    if (data.presets[param]) { id = param; raw = data.presets[param].states; }
    else advisories.push({ code: "structure_mapping_unknown_preset", fingerprint: `unknown_preset|${param}`, text: say(data.advisories.unknown_preset, lang, { id: param, fallback: data.default }) });
  } else if (param && typeof param === "object" && !Array.isArray(param) && typeof (param as { states?: unknown }).states === "object") {
    id = "own"; raw = (param as { states: Record<string, Sign> }).states ?? {};
  }
  const states: Record<string, Sign> = {};
  for (const [state, sign] of Object.entries(raw ?? {})) {
    const out: Sign = {};
    for (const [k, v] of Object.entries(sign ?? {})) {
      if (!data.signs.includes(k)) advisories.push({ code: "structure_mapping_unknown_sign", fingerprint: `unknown_sign|${id}|${k}`, text: say(data.advisories.unknown_sign, lang, { sign: k, id }) });
      out[k] = v && typeof v === "object" && typeof (v as { setting?: unknown }).setting === "string" ? settings[(v as { setting: string }).setting] : v;
    }
    states[state] = out;
  }
  const missing = states.missing ?? {};
  const opacity = typeof missing.opacity === "number" ? missing.opacity : 1;
  if (missing.hidden === true || opacity <= 0)
    advisories.push({ code: "structure_mapping_nothing_visible", fingerprint: `nothing_visible|${id}`, text: say(data.advisories.nothing_visible, lang) });
  return { id, states, advisories };
}

/** The CSS of a sign (the open list of signs the page reads; others were said by resolveMapping). */
export function styleOf(sign: Sign | undefined): Record<string, string | number> {
  const s: Record<string, string | number> = {};
  if (!sign) return s;
  if (typeof sign.opacity === "number") s.opacity = Math.max(0, Math.min(1, sign.opacity));
  if (typeof sign.border === "string") { s.borderStyle = sign.border; s.borderWidth = 1; s.borderColor = "currentColor"; }
  if (typeof sign.color === "string") s.color = sign.color;
  if (typeof sign.size === "number") s.fontSize = `${sign.size}em`;
  if (typeof sign.weight === "string" || typeof sign.weight === "number") s.fontWeight = sign.weight as string;
  return s;
}

export interface Row { key: string; address: string; label: string; ghost: boolean; state?: string; stateLabel?: string; hidden: boolean; order: number; icon?: string; style: Record<string, string | number>; detail?: string }
export interface Group { expectation: StructureExpectation; rows: Row[] }
export interface Drawn { groups: Group[]; other: Row[]; hidden: number }

/** The rows of the window: one group per expectation (its children, or its ghost), then the children no template expects. */
export function rowsOf(r: GraphStructureResult, mapping: ResolvedMapping, t: (id: string, vars?: Record<string, string>) => string): Drawn {
  const kids = new Map((r.children ?? []).filter(c => (c.depth ?? 1) === 1).map(c => [c.address, c]));
  const ghosts = new Map((r.ghosts ?? []).map(g => [g.address, g]));
  let hidden = 0;
  const groups: Group[] = (r.expectations ?? []).map(e => {
    const sign = mapping.states[e.state];
    const rows: Row[] = [];
    for (const a of e.matched) {
      const c = kids.get(a);
      const evidenceState = c?.evidence?.class && mapping.states[c.evidence.class] ? c.evidence.class : e.state;
      const s = mapping.states[evidenceState];
      rows.push({ key: `${e.ref}|${a}`, address: a, label: c?.label ?? a, ghost: false, state: e.state, stateLabel: e.state_label, hidden: s?.hidden === true,
        order: typeof s?.order === "number" ? s.order : 0, icon: typeof s?.icon === "string" ? s.icon : undefined, style: styleOf(s) });
    }
    const g = e.ghost ? ghosts.get(e.ghost) : undefined;
    if (g) {
      const parts = [e.card.max === null ? t("ps_card_min", { min: String(e.card.min) }) : t("ps_card", { min: String(e.card.min), max: String(e.card.max) })];
      if (typeof e.confidence === "number") parts.push(t("ps_confidence", { value: String(Math.round(e.confidence * 100)) + "%" }));
      const sourceWord = t(`ps_source_${e.source}`);   // the words of a source of an expectation (open list); an unknown one is said as it is
      parts.push(t("ps_source", { source: sourceWord === `ps_source_${e.source}` ? e.source : sourceWord }));
      rows.push({ key: g.address, address: g.address, label: g.label, ghost: true, state: e.state, stateLabel: e.state_label, hidden: sign?.hidden === true,
        order: typeof sign?.order === "number" ? sign.order : 0, icon: typeof sign?.icon === "string" ? sign.icon : undefined, style: styleOf(sign), detail: parts.join(" · ") });
    }
    hidden += rows.filter(x => x.hidden).length;
    return { expectation: e, rows: rows.filter(x => !x.hidden).sort((a, b) => a.order - b.order) };
  });
  const groupsSorted = groups.map((g, i) => ({ g, i, o: Math.min(0, ...g.rows.map(r => r.order)) })).sort((a, b) => a.o - b.o || a.i - b.i).map(x => x.g);
  const other: Row[] = (r.children ?? []).filter(c => (c.depth ?? 1) === 1 && !(c.expected_by ?? []).length)
    .map(c => ({ key: c.address, address: c.address, label: c.label, ghost: false, hidden: false, order: 0, style: {} }));
  return { groups: groupsSorted, other, hidden };
}

/** The advisories of the kernel and of the page, without the ones the person answered "don't ask again". */
export function visibleAdvisories(kernel: StructureAdvisory[] | undefined, page: PageAdvisory[], remembered: ReadonlySet<string>, kept: ReadonlySet<string>): PageAdvisory[] {
  const all: PageAdvisory[] = [...(kernel ?? []).map(a => ({ code: `structure_${a.code}`, fingerprint: a.fingerprint, text: a.text })), ...page];
  return all.filter(a => !remembered.has(`${a.code}|${a.fingerprint}`) && !kept.has(`${a.code}|${a.fingerprint}`));
}
