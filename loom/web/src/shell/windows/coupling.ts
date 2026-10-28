// W6 of the window system (OKNA §5): coupling of parameters between windows. Pure: no React, no DOM, no kernel.
// - A binding per parameter lives in the window's params (`params.bind.<param>`, binding.ts), so it travels with the layout: the saved
//   last layout, the named layouts of W5 (written by the W5b merge patch) and the journal (bind/unbind are one undoable entry each).
// - Values are runtime: `Values.windows[id][param]` (own values) and `Values.channels[name]` (shared values; the layout's
//   `channels` give the starting value). A shared parameter reads and writes its channel; a linked one is computed from its source
//   (another window's parameter, or a channel) through a dependency (dependencies.ts) - any kind, any shape, also of little use.
// - Propagation runs to a fixed point. The written parameter is pinned for that run (the person's change is not undone by its own
//   loop); every other linked parameter is recomputed in a topological order, so an acyclic graph settles in one round. A loop that
//   does not settle stops after `max_iterations` (data, preset 8) with the last values: nothing oscillates, nothing throws.
// - A loop in the graph of bindings is allowed (R58: handled, not forbidden) and raises the non-blocking advisory `coupling_cycle`
//   (code + fingerprint answered through advisory.answer by the page, like the arrangement chips). A loop of equalities is consistent
//   and says so in its text. A binding to a window that is gone, an unknown dependency or a broken expression keeps the last value and
//   raises its own advisory with a fix "unbind".
import couplingFile from "../data/coupling.json";
import { DEPENDENCIES, applyDependency, compileExpression, expressionRefs, type DepEnv } from "./dependencies";
import { readBinding, INDEPENDENT, type Binding } from "./binding";
import type { WLayout, WState, Win } from "./model";
import type { WinAdvisory } from "./arrange";

type Words = { pl?: string; en?: string };
export interface CouplingData {
  max_iterations?: number; modes?: string[]; channels?: string[]; kind_params?: Record<string, string[]>;
  params?: Record<string, Words>; dependencies?: Record<string, Words>;
  menu_templates?: { mode: string; dependency?: { kind: string; [k: string]: unknown }; channel?: string }[];
  icons?: Record<string, string>; words?: Record<string, Words>;
  advisories?: Record<string, { text?: Words; fix?: { id: string; label: Words } }>;
}
export const COUPLING: CouplingData = couplingFile as CouplingData;
export interface Values { windows: Record<string, Record<string, unknown>>; channels: Record<string, unknown> }
/** W8 (W6 gap B): the settings field coupling_max_iterations (settings_fields.pack) overrides the preset of coupling.json; any integer
 *  ≥ 0 (a number or a numeric text); anything else keeps the preset (the field's own check warns about it). */
export function couplingDataOf(config: Record<string, unknown> | null | undefined, data: CouplingData = COUPLING): CouplingData {
  const raw = config?.coupling_max_iterations;
  const n = typeof raw === "number" ? raw : typeof raw === "string" && raw.trim() !== "" ? Number(raw) : NaN;
  return Number.isFinite(n) && n >= 0 ? { ...data, max_iterations: Math.floor(n) } : data;
}
export const emptyValues = (): Values => ({ windows: {}, channels: {} });
export const BIND_KEY = "bind";
const KNOWN_MODES = new Set(["shared", "independent", "linked"]);
const same = (a: unknown, b: unknown) => a === b || JSON.stringify(a) === JSON.stringify(b);
const own = <T,>(o: Record<string, T> | undefined, k: string): T | undefined => (o && Object.prototype.hasOwnProperty.call(o, k) ? o[k] : undefined);

// ---------- reading bindings ----------
/** The bindings of a window (`params.bind`), each read leniently (a malformed one is skipped, never thrown). */
export function bindingsOf(w: Win | undefined): Record<string, Binding> {
  const raw = w?.params?.[BIND_KEY];
  const out: Record<string, Binding> = {};
  if (raw && typeof raw === "object" && !Array.isArray(raw)) for (const [p, b] of Object.entries(raw as Record<string, unknown>)) { const r = readBinding(b); if (r) out[p] = r; }
  return out;
}
export const bindingOf = (w: Win | undefined, param: string): Binding => bindingsOf(w)[param] ?? INDEPENDENT;
const win = (l: WLayout, id: string | undefined) => (id ? l.windows.find((w) => w.id === id) : undefined);
const effMode = (b: Binding) => (KNOWN_MODES.has(b.binding) ? b.binding : "independent");

/** The node of the coupling graph a parameter's value lives in: a channel for a shared one, else the window's own slot. */
export function nodeOf(l: WLayout, id: string, param: string): string {
  const b = bindingOf(win(l, id), param);
  return effMode(b) === "shared" ? `ch:${b.channel || param}` : `w:${id}:${param}`;
}
function sourceNode(l: WLayout, b: Binding, param: string): string | null {
  if (b.from?.window) return win(l, b.from.window) ? nodeOf(l, b.from.window, b.from.param || param) : null;
  if (b.from?.channel) return `ch:${b.from.channel}`;
  return null;
}

export interface Edge { target: string; window: string; param: string; binding: Binding; sources: string[]; main: string | null; dangling: boolean }
/** The linked parameters as edges source(s) → target. An expression adds an edge for every `other.x`, `self.x` and channel it reads. */
export function edgesOf(l: WLayout): Edge[] {
  const out: Edge[] = [];
  for (const w of l.windows) for (const [param, b] of Object.entries(bindingsOf(w))) {
    if (effMode(b) !== "linked") continue;
    const main = sourceNode(l, b, param);
    const sources = main ? [main] : [];
    if (b.dependency?.kind === "expression" && typeof b.dependency.expr === "string") {
      const r = expressionRefs(b.dependency.expr);
      if (b.from?.window && win(l, b.from.window)) for (const p of r.other) sources.push(nodeOf(l, b.from.window, p));
      for (const p of r.self) if (p !== param) sources.push(nodeOf(l, w.id, p));
      for (const c of r.channels) sources.push(`ch:${c}`);
    }
    out.push({ target: `w:${w.id}:${param}`, window: w.id, param, binding: b, sources: [...new Set(sources)], main, dangling: !main && !!(b.from?.window) });
  }
  return out;
}

/** The strongly connected groups of the binding graph that are loops (more than one node, or a node bound to itself). Tarjan. */
export function cyclesOf(l: WLayout, edges: Edge[] = edgesOf(l)): string[][] {
  const next = new Map<string, Set<string>>();
  const add = (a: string, b: string) => { if (!next.has(a)) next.set(a, new Set()); next.get(a)!.add(b); if (!next.has(b)) next.set(b, new Set()); };
  for (const e of edges) for (const s of e.sources) add(s, e.target);
  let index = 0; const idx = new Map<string, number>(), low = new Map<string, number>(), on = new Set<string>(), stack: string[] = []; const out: string[][] = [];
  const strong = (v: string) => {
    idx.set(v, index); low.set(v, index); index++; stack.push(v); on.add(v);
    for (const w of next.get(v) ?? []) {
      if (!idx.has(w)) { strong(w); low.set(v, Math.min(low.get(v)!, low.get(w)!)); } else if (on.has(w)) low.set(v, Math.min(low.get(v)!, idx.get(w)!));
    }
    if (low.get(v) === idx.get(v)) {
      const comp: string[] = []; let w: string;
      do { w = stack.pop()!; on.delete(w); comp.push(w); } while (w !== v);
      if (comp.length > 1 || next.get(v)?.has(v)) out.push(comp.sort());
    }
  };
  for (const v of [...next.keys()].sort()) if (!idx.has(v)) strong(v);
  return out;
}

// ---------- values ----------
export function readNode(l: WLayout, values: Values, node: string): unknown {
  if (node.startsWith("ch:")) { const ch = node.slice(3); return own(values.channels, ch) !== undefined ? values.channels[ch] : own(l.channels as Record<string, unknown>, ch) ?? null; }
  const [, id, ...rest] = node.split(":"); const param = rest.join(":");
  return own(values.windows[id] ?? {}, param);
}
function writeNode(values: Values, node: string, v: unknown): Values {
  if (node.startsWith("ch:")) return { ...values, channels: { ...values.channels, [node.slice(3)]: v } };
  const [, id, ...rest] = node.split(":"); const param = rest.join(":");
  return { ...values, windows: { ...values.windows, [id]: { ...(values.windows[id] ?? {}), [param]: v } } };
}
/** The value a window sees for a parameter (shared → its channel, otherwise its own slot). */
export function resolveParam(l: WLayout, values: Values, id: string, param: string): unknown { return readNode(l, values, nodeOf(l, id, param)); }
function paramsOf(l: WLayout, values: Values, id: string): Record<string, unknown> {
  const out: Record<string, unknown> = { ...(values.windows[id] ?? {}) };
  for (const [p, b] of Object.entries(bindingsOf(win(l, id)))) if (effMode(b) === "shared") out[p] = readNode(l, values, nodeOf(l, id, p));
  return out;
}

export interface PropagateResult { values: Values; iterations: number; converged: boolean; changed: string[]; failures: { node: string; code: string; evidence: Record<string, unknown> }[] }
export interface PropagateOpts { maxIterations?: number; env?: Omit<DepEnv, "other" | "self" | "channels">; data?: CouplingData;
  /** Only these targets are recomputed (setParam: the nodes downstream of the changed one); absent = every linked parameter. */
  only?: ReadonlySet<string> }

/** Kahn order of the targets (acyclic part first; the members of loops after it, in a stable order). */
function order(edges: Edge[]): Edge[] {
  const targets = new Set(edges.map((e) => e.target));
  const indeg = new Map<string, number>(); for (const e of edges) indeg.set(e.target, e.sources.filter((s) => targets.has(s) && s !== e.target).length);
  const byTarget = new Map(edges.map((e) => [e.target, e]));
  const done: Edge[] = []; const ready = edges.filter((e) => indeg.get(e.target) === 0).map((e) => e.target);
  const seen = new Set<string>();
  while (ready.length) {
    const t = ready.shift()!; if (seen.has(t)) continue; seen.add(t); done.push(byTarget.get(t)!);
    for (const e of edges) if (!seen.has(e.target) && e.sources.includes(t)) { const n = (indeg.get(e.target) ?? 0) - 1; indeg.set(e.target, n); if (n === 0) ready.push(e.target); }
  }
  for (const e of edges) if (!seen.has(e.target)) done.push(e);
  return done;
}

/** Recomputes the linked parameters to a fixed point. `pinned` nodes (the one the person just changed) are not recomputed in this run. */
export function propagate(l: WLayout, values: Values, pinned: string[] = [], opts: PropagateOpts = {}): PropagateResult {
  const data = opts.data ?? COUPLING;
  const max = Math.max(0, Math.floor(opts.maxIterations ?? (typeof data.max_iterations === "number" ? data.max_iterations : 8)));
  const edges = order(edgesOf(l)).filter((e) => !pinned.includes(e.target) && (!opts.only || opts.only.has(e.target)));
  let v = values; const changed = new Set<string>(); const failures: PropagateResult["failures"] = [];
  let iterations = 0, converged = edges.length === 0 || max === 0 ? edges.length === 0 : false;
  for (; iterations < max && !converged; iterations++) {
    let moved = false;
    failures.length = 0;
    for (const e of edges) {
      if (!e.main && !(e.binding.dependency?.kind === "expression")) continue;   // a dangling binding keeps its value
      const src = e.main ? readNode(l, v, e.main) : undefined;
      const env: DepEnv = { ...(opts.env ?? {}), other: e.binding.from?.window ? paramsOf(l, v, e.binding.from.window) : {}, self: paramsOf(l, v, e.window), channels: { ...(l.channels as Record<string, unknown>), ...v.channels } };
      const r = applyDependency(src, e.binding.dependency, env);
      if (!r.ok) { failures.push({ node: e.target, code: r.code, evidence: r.evidence }); continue; }
      if (!same(readNode(l, v, e.target), r.value)) { v = writeNode(v, e.target, r.value); moved = true; changed.add(e.target); }
    }
    if (!moved) converged = true;
  }
  return { values: v, iterations, converged, changed: [...changed], failures };
}

/** The person (or a component) set a parameter of a window: written to its node, then propagated with that node pinned. */
/** The targets that depend on `node`, directly or through other linked parameters (the only ones a change of `node` may move). */
export function downstreamOf(l: WLayout, node: string, edges: Edge[] = edgesOf(l)): Set<string> {
  const reached = new Set<string>([node]); const out = new Set<string>();
  for (let grew = true; grew;) {
    grew = false;
    for (const e of edges) if (!out.has(e.target) && e.sources.some((x) => reached.has(x))) { out.add(e.target); reached.add(e.target); grew = true; }
  }
  return out;
}
/** The person (or a component) set a parameter of a window: written to its node, then propagated with that node pinned - and only to the
 *  parameters that depend on it (R101 D3 gate: a navigator reporting its focus re-ran an unrelated ×2 zoom loop, which does not settle). */
export function setParam(l: WLayout, values: Values, id: string, param: string, value: unknown, opts: PropagateOpts = {}): PropagateResult {
  const node = nodeOf(l, id, param);
  if (same(readNode(l, values, node), value)) return { values, iterations: 0, converged: true, changed: [], failures: [] };
  const only = downstreamOf(l, node);
  if (!only.size) return { values: writeNode(values, node, value), iterations: 0, converged: true, changed: [node], failures: [] };
  const r = propagate(l, writeNode(values, node, value), [node], { ...opts, only: opts.only ? new Set([...only].filter((t) => opts.only!.has(t))) : only });
  return { ...r, changed: [node, ...r.changed] };
}

// ---------- bind / unbind (one journal entry each) ----------
function step(s: WState, layout: WLayout, op: string): WState {
  const past = [...s.past, { op, layout: s.layout }];
  while (past.length > s.limit) past.shift();
  return { layout, past, future: [], limit: s.limit };
}
/** Sets the binding of a parameter of a window (a loop is allowed: it is advised about, not refused). Total: an unknown window = same state. */
export function bindParam(s: WState, id: string, param: string, binding: Binding): WState {
  const w = win(s.layout, id);
  if (!w || !param) return s;
  const bind = { ...(bindingsOf(w)), [param]: binding };
  if (same(bindingsOf(w)[param], binding)) return s;
  const layout = { ...s.layout, windows: s.layout.windows.map((x) => (x.id === id ? { ...x, params: { ...x.params, [BIND_KEY]: bind } } : x)) };
  return step(s, layout, "bind");
}
/** Removes the binding of a parameter: the window is independent again and keeps the value it shows now. */
export function unbindParam(s: WState, id: string, param: string): WState {
  const w = win(s.layout, id);
  if (!w || !(param in bindingsOf(w))) return s;
  const bind = { ...bindingsOf(w) }; delete bind[param];
  const params = { ...w.params }; if (Object.keys(bind).length) params[BIND_KEY] = bind; else delete params[BIND_KEY];
  return step(s, { ...s.layout, windows: s.layout.windows.map((x) => (x.id === id ? { ...x, params } : x)) }, "unbind");
}
/** After a bind: the values the new binding gives, the source of the new binding pinned (the reference the person looked at). */
export function afterBind(l: WLayout, values: Values, id: string, param: string, opts: PropagateOpts = {}): PropagateResult {
  const b = bindingOf(win(l, id), param);
  let v = values;
  // a window detached from a channel keeps what it showed; a window joining a channel brings its value when the channel has none yet
  if (effMode(b) === "shared") { const ch = `ch:${b.channel || param}`; if (readNode(l, v, ch) == null) { const mine = own(values.windows[id] ?? {}, param); if (mine !== undefined) v = writeNode(v, ch, mine); } }
  const src = effMode(b) === "linked" ? sourceNode(l, b, param) : null;
  return propagate(l, v, src ? [src] : [], opts);
}
/** What a window shows when it leaves a channel: its own slot takes the channel's value (nothing jumps on the screen). */
export function beforeUnbind(l: WLayout, values: Values, id: string, param: string): Values {
  const node = nodeOf(l, id, param);
  return node.startsWith("ch:") ? writeNode(values, `w:${id}:${param}`, readNode(l, values, node)) : values;
}

// ---------- words, menu, chain, advisories ----------
const say = (w: Words | undefined, lang: string) => (lang === "pl" ? w?.pl ?? w?.en : w?.en ?? w?.pl) ?? "";
const fill = (s: string, vars: Record<string, string>) => s.replace(/\{(\w+)\}/g, (_, k) => vars[k] ?? "");
const fillW = (t: Words | undefined, vars: Record<string, string>): Words => ({ pl: fill(t?.pl ?? "", vars), en: fill(t?.en ?? "", vars) });
export function paramWord(param: string, lang: string, data: CouplingData = COUPLING): string { return say(data.params?.[param], lang) || param; }
export function depWord(dep: Binding["dependency"], lang: string, data: CouplingData = COUPLING): string {
  const kind = dep?.kind ?? "same";
  const t = say(data.dependencies?.[kind], lang);
  return t ? fill(t, Object.fromEntries(Object.entries(dep ?? {}).map(([k, x]) => [k, typeof x === "string" ? x : JSON.stringify(x)]))) : kind;
}
/** W7: a word of a sentence („przybliżenie”, „zoom”, „przesunięcia”) to a parameter id: the id itself, else the longest common start of
 *  a folded word of the data (params pl/en) and the folded word, at least `min` letters (a Polish ending differs). Null = no parameter. */
export function paramOfWord(word: string, data: CouplingData = COUPLING, min = 5): string | null {
  const f = (t: string) => t.normalize("NFD").replace(/[\u0300-\u036f]/g, "").replace(/ł/g, "l").toLowerCase().trim();
  const w = f(word);
  if (!w) return null;
  if (data.params && Object.prototype.hasOwnProperty.call(data.params, w)) return w;
  let best: string | null = null, bestLen = 0;
  for (const [id, words] of Object.entries(data.params ?? {})) {
    for (const t of [id, ...Object.values(words ?? {})]) {
      const v = f(String(t ?? ""));
      let n = 0; while (n < v.length && n < w.length && v[n] === w[n]) n++;
      const enough = n === v.length && n === w.length ? true : n >= Math.min(min, v.length);
      if (enough && n > bestLen) { best = id; bestLen = n; }
    }
  }
  return best;
}
/** The parameters a window of a kind follows today (data; no fake controls: nothing else is offered). */
export const paramsOfKind = (kind: string, data: CouplingData = COUPLING): string[] => data.kind_params?.[kind] ?? [];
const kindOfWin = (w: Win) => String(w.params?.panel ?? w.kind);

export interface MenuEntry { id: string; label: string; icon?: string; action: { op: "bind"; param: string; binding: Binding } | { op: "unbind"; param: string } }
/** The coupling entries of the menu ⋯ of a window: per parameter it follows, a template per other window following it too, and unbind. */
export function couplingMenu(l: WLayout, id: string, lang: string, title: (w: Win) => string, data: CouplingData = COUPLING): MenuEntry[] {
  const w = win(l, id); if (!w) return [];
  const out: MenuEntry[] = [];
  const mine = bindingsOf(w);
  for (const param of paramsOfKind(kindOfWin(w), data)) {
    const pw = paramWord(param, lang, data);
    for (const t of data.menu_templates ?? []) {
      if (t.mode === "linked") {
        for (const o of l.windows) {
          if (o.id === id || !paramsOfKind(kindOfWin(o), data).includes(param)) continue;
          const binding: Binding = { binding: "linked", from: { window: o.id, param }, dependency: { ...(t.dependency ?? { kind: "same" }) } };
          if (same(mine[param], binding)) continue;
          const depKey = Object.entries(binding.dependency ?? {}).map(([, x]) => (typeof x === "string" ? x : JSON.stringify(x))).join(":");
          out.push({ id: `bind:${param}:${o.id}:${depKey}`, icon: data.icons?.linked, label: fill(say(data.words?.bind_linked, lang), { param: pw, dep: depWord(binding.dependency, lang, data), source: title(o) }), action: { op: "bind", param, binding } });
        }
      } else if (t.mode === "shared") {
        const channel = fill(t.channel ?? "{param}", { param });
        const binding: Binding = { binding: "shared", channel };
        if (same(mine[param], binding)) continue;
        out.push({ id: `bind:${param}:shared:${channel}`, icon: data.icons?.shared, label: fill(say(data.words?.bind_shared, lang), { param: pw, channel }), action: { op: "bind", param, binding } });
      }
    }
  }
  for (const param of Object.keys(mine)) out.push({ id: `unbind:${param}`, icon: data.icons?.independent, label: fill(say(data.words?.unbind, lang), { param: paramWord(param, lang, data) }), action: { op: "unbind", param } });
  return out;
}
/** The chain on the title bar: the bound parameters of a window said with words (null = nothing bound, no icon). */
export function chainOf(l: WLayout, id: string, lang: string, title: (w: Win) => string, data: CouplingData = COUPLING): { icon: string; lines: string[] } | null {
  const w = win(l, id); const b = bindingsOf(w);
  const keys = Object.keys(b); if (!w || !keys.length) return null;
  const lines = keys.map((param) => {
    const x = b[param]; const pw = paramWord(param, lang, data);
    if (x.binding === "shared") return fill(say(data.words?.chain_shared, lang), { param: pw, channel: x.channel || param });
    if (x.binding === "linked") { const src = win(l, x.from?.window); return fill(say(data.words?.chain_linked, lang), { param: pw, dep: depWord(x.dependency, lang, data), source: src ? title(src) : x.from?.channel ?? "?" }); }
    return `${pw}: ${x.binding}`;
  });
  const modes = new Set(keys.map((k) => b[k].binding));
  const icon = modes.has("linked") ? data.icons?.linked ?? "↔" : modes.has("shared") ? data.icons?.shared ?? "🔗" : data.icons?.independent ?? "⛓";
  return { icon, lines };
}

const nodeWords = (l: WLayout, node: string, lang: string, title: (w: Win) => string, data: CouplingData) => {
  if (node.startsWith("ch:")) return `«${node.slice(3)}»`;
  const [, id, ...rest] = node.split(":"); const w = win(l, id);
  return `${w ? title(w) : id}.${paramWord(rest.join(":"), lang, data)}`;
};
/** The advisories of the coupling of a layout (structural: they hold while the bindings hold). Fingerprints are the configuration
 *  (parameters, dependency kinds, window kinds), so "don't ask again" covers the same kind of loop, not one pair of window ids. */
export function couplingAdvisories(l: WLayout, title: (w: Win) => string = (w) => w.kind, lang = "pl", data: CouplingData = COUPLING): WinAdvisory[] {
  const out: WinAdvisory[] = [];
  const A = data.advisories ?? {};
  const q = { pl: "Czy to celowe?", en: "Is this intended?" };
  const max = typeof data.max_iterations === "number" ? data.max_iterations : 8;
  const edges = edgesOf(l);
  for (const cyc of cyclesOf(l, edges)) {
    const inLoop = edges.filter((e) => cyc.includes(e.target));
    const kinds = [...new Set(inLoop.map((e) => e.binding.dependency?.kind ?? "same"))].sort();
    const consistent = kinds.every((k) => k === "same");
    const params = [...new Set(cyc.map((n) => (n.startsWith("ch:") ? n.slice(3) : n.split(":").slice(2).join(":"))))].sort();
    const wk = [...new Set(inLoop.map((e) => { const w = win(l, e.window); return w ? kindOfWin(w) : "?"; }))].sort();
    const path = [...cyc, cyc[0]].map((n) => nodeWords(l, n, lang, title, data)).join(" → ");
    const t = A[consistent ? "coupling_cycle_consistent" : "coupling_cycle"]?.text;
    out.push({ code: "coupling_cycle", fingerprint: `${params.join("+")}|${kinds.join("+")}|${wk.join("+")}`, evidence: { nodes: cyc, dependencies: kinds, consistent, max_iterations: max },
      text: fillW(t ?? { pl: "coupling_cycle", en: "coupling_cycle" }, { path, max: String(max) }), question: q });
  }
  for (const w of l.windows) for (const [param, b] of Object.entries(bindingsOf(w))) {
    const vars = { title: title(w), param: paramWord(param, lang, data) };
    const make = (code: string, evidence: Record<string, unknown>, more: Record<string, string> = {}) => out.push({ code, fingerprint: `${kindOfWin(w)}|${param}|${JSON.stringify(evidence.kind ?? evidence.mode ?? "")}`, window: w.id, evidence: { ...evidence, param },
      text: fillW(A[code]?.text ?? { pl: code, en: code }, { ...vars, ...more }), question: q, ...(A[code]?.fix ? { fix: A[code].fix } : {}) });
    if (!KNOWN_MODES.has(b.binding)) { make("unknown_binding", { mode: b.binding }, { mode: b.binding }); continue; }
    if (b.binding !== "linked") continue;
    if (b.from?.window && !win(l, b.from.window)) make("coupling_dangling", { source: b.from.window });
    const kind = b.dependency?.kind ?? "same";
    if (!Object.prototype.hasOwnProperty.call(DEPENDENCIES, kind)) make("unknown_dependency", { kind }, { kind });
    else if (kind === "expression") { const c = compileExpression(String(b.dependency?.expr ?? "")); if (!c.ok) make("expression_invalid", { kind, error: c.error }, { error: c.error }); }
  }
  return out;
}

/** A fix of a coupling advisory (unbind the window's parameter), or the same state. */
export function applyCouplingFix(s: WState, a: WinAdvisory): WState {
  if (a.fix?.id !== "unbind" || !a.window || typeof a.evidence.param !== "string") return s;
  return unbindParam(s, a.window, a.evidence.param);
}
export { say as sayWords };
