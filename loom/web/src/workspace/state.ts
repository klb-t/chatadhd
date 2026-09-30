import type { KnowledgeCollection } from "../api/knowledge";

export type ViewKind = KnowledgeCollection | "graph" | "context" | "catalog" | "candidates";
export const VIEWS: Record<ViewKind, string> = {
  entities: "Entities", claims: "Claims", graph: "Knowledge graph", principles: "Principles",
  operators: "Operators", context: "Context trace", catalog: "Source catalog",
  instances: "Project instances", products: "Products", candidates: "Candidate proposals",
};
export interface Parameters {
  run: string; limit: number; filter: string; confidence: number; evidence: string;
  depth: number; fade: number; maxNodes: number; follow: boolean;
  selection: { kind: string; id: string; focus: string; run: string } | null;
  text: string; budget: number; candidateKind: string; pageSize: number; offset: number; selectedOnly: boolean;
}
export type Parameter = keyof Parameters;
export interface Pane { id: string; kind: ViewKind; parameters: Parameters; followRun: boolean }
export interface Binding { source: string; target: string; parameter: Parameter }
export interface Workspace {
  schema: "loom.workbench/2"; panes: Pane[]; bindings: Binding[];
  run: string; limit: number; profile: "adaptive" | "stacked" | "compact"; active: string;
}
export const STORAGE_KEY = "loom.knowledge.workspace.v2";
export const SAVED_KEY = "loom.knowledge.perspective.v2";
const defaults: Parameters = {
  run: "", limit: 1000, filter: "", confidence: 0, evidence: "", depth: 1, fade: 20,
  maxNodes: 60, follow: true, selection: null, text: "", budget: 4000,
  candidateKind: "", pageSize: 50, offset: 0, selectedOnly: false,
};
let sequence = 0;
export function newPane(kind: ViewKind, run = "", limit = 1000): Pane {
  return { id: `view-${Date.now()}-${++sequence}-${Math.random().toString(36).slice(2, 9)}`, kind,
    parameters: { ...defaults, run, limit }, followRun: true };
}
export function initialWorkspace(kinds: ViewKind[] = ["entities", "claims", "graph"]): Workspace {
  const panes = kinds.map((kind) => newPane(kind));
  const bindings = panes.slice(1).flatMap((pane): Binding[] => [
    { source: panes[0].id, target: pane.id, parameter: "selection" },
    { source: pane.id, target: panes[0].id, parameter: "selection" },
  ]);
  return { schema: "loom.workbench/2", panes, bindings, run: "", limit: 1000, profile: "adaptive", active: "" };
}

// Identity-only projection of T5's transactional event semantics. An endpoint is
// visited once, so convergent cycles terminate. No effect hooks emit more events.
// Bindings never reach execution config, the API, or canonical knowledge records.
export function changeParameter<K extends Parameter>(state: Workspace, source: string, parameter: K, value: Parameters[K]): Workspace {
  const visited = new Set<string>(), queue = [source];
  for (let i = 0; i < queue.length; i++) {
    const id = queue[i];
    if (visited.has(id)) continue;
    visited.add(id);
    state.bindings.filter((link) => link.source === id && link.parameter === parameter).forEach((link) => queue.push(link.target));
  }
  return { ...state, panes: state.panes.map((pane) => visited.has(pane.id) ? {
    ...pane, parameters: { ...pane.parameters, [parameter]: value },
  } : pane) };
}
export function unlink(state: Workspace, id: string): Workspace {
  return { ...state, bindings: state.bindings.filter((link) => link.source !== id && link.target !== id),
    panes: state.panes.map((pane) => pane.id === id ? { ...pane, followRun: false } : pane) };
}
export function connect(state: Workspace, source: string, target: string, parameter: Parameter): Workspace {
  if (source === target || !state.panes.some((p) => p.id === source) || !state.panes.some((p) => p.id === target)) return state;
  // One incoming driver per endpoint in this UI. Cycles and fan-out are supported.
  const next = { ...state, bindings: [...state.bindings.filter((b) => b.target !== target || b.parameter !== parameter), { source, target, parameter }] };
  const value = state.panes.find((p) => p.id === source)!.parameters[parameter];
  return changeParameter(next, source, parameter, value);
}
export function setWorkspaceRun(state: Workspace, run: string): Workspace {
  return { ...state, run, panes: state.panes.map((pane) => pane.followRun ? {
    ...pane, parameters: { ...pane.parameters, run, selection: pane.parameters.run === run ? pane.parameters.selection : null },
  } : pane) };
}
export function duplicatePane(state: Workspace, id: string): Workspace {
  const original = state.panes.find((p) => p.id === id);
  if (!original) return state;
  const copy = { ...newPane(original.kind), parameters: { ...original.parameters }, followRun: false };
  return { ...state, panes: [...state.panes, copy] };
}

// Saved views contain parameters/references only, never source rows or credentials.
// Reject unsupported/corrupt snapshots as a whole, without overwriting them.
export function parseWorkspace(raw: string): Workspace {
  const s = JSON.parse(raw) as Workspace;
  const object = (v: unknown) => !!v && typeof v === "object" && !Array.isArray(v);
  const fail = () => { throw new Error("Saved workspace is invalid or uses an unsupported version. Its stored copy was preserved."); };
  if (!object(s) || s.schema !== "loom.workbench/2" || !Array.isArray(s.panes) || !Array.isArray(s.bindings) ||
      typeof s.run !== "string" || typeof s.active !== "string" || !["adaptive", "stacked", "compact"].includes(s.profile) || !Number.isSafeInteger(s.limit) || s.limit < 1) return fail();
  const ids = new Set<string>();
  for (const pane of s.panes) {
    if (!object(pane) || typeof pane.id !== "string" || !pane.id || ids.has(pane.id) || !Object.prototype.hasOwnProperty.call(VIEWS, pane.kind) || typeof pane.followRun !== "boolean" || !object(pane.parameters)) return fail();
    ids.add(pane.id);
    for (const [key, value] of Object.entries(defaults)) {
      const p = pane.parameters[key as Parameter];
      if (key === "selection") {
        if (p !== null && (!object(p) || !["kind", "id", "focus", "run"].every((k) => typeof (p as Record<string, unknown>)[k] === "string"))) return fail();
      } else if (typeof p !== typeof value || (typeof value === "number" && (!Number.isSafeInteger(p) || Number(p) < 0))) return fail();
    }
    const p = pane.parameters;
    if (p.limit < 1 || p.maxNodes < 1 || p.pageSize < 1 || p.budget < 1 || p.confidence > 100 || p.fade > 100) return fail();
  }
  if (s.active && !ids.has(s.active)) return fail();
  for (const b of s.bindings) if (!object(b) || !ids.has(b.source) || !ids.has(b.target) || !Object.prototype.hasOwnProperty.call(defaults, b.parameter)) return fail();
  // Return a whitelist, not arbitrary properties supplied by browser storage.
  return { schema: s.schema, run: s.run, limit: s.limit, profile: s.profile, active: s.active,
    panes: s.panes.map((p) => ({ id: p.id, kind: p.kind, followRun: p.followRun,
      parameters: Object.fromEntries(Object.keys(defaults).map((k) => [k, p.parameters[k as Parameter]])) as unknown as Parameters })),
    bindings: s.bindings.map((b) => ({ source: b.source, target: b.target, parameter: b.parameter })) };
}
export function loadWorkspace(): { workspace: Workspace; error: string } {
  try {
    const raw = localStorage.getItem(STORAGE_KEY);
    if (raw !== null) return { workspace: parseWorkspace(raw), error: "" };
    const legacy: unknown = JSON.parse(localStorage.getItem("loom.knowledge.layout.v1") ?? "null");
    const kinds = Array.isArray(legacy) && legacy.every((kind) => typeof kind === "string" && Object.prototype.hasOwnProperty.call(VIEWS, kind)) ? legacy as ViewKind[] : undefined;
    return { workspace: initialWorkspace(kinds), error: "" };
  } catch (error) { return { workspace: initialWorkspace(), error: String(error) }; }
}
