import data from "./data/navigator_visualization.json";
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ViewModel } from "../api/view-model";
import { elementIdOf, type NavViewModel } from "./navigator-map";
import { measureOf } from "./navigator-mapping";

export const NAV_VIS = data;
// Validate the shipped presentation data at its boundary; labels and presets stay data, mechanisms stay here.
const axesKnown = (name: string) => Object.prototype.hasOwnProperty.call(data.axes, name);
if (!data.profiles.length || data.profiles.some(p => !["sector", "cartesian"].includes(p.system) || ![p.x, p.y, p.z].every(axesKnown) || !data.z_axes.includes(p.z))
  || data.z_axes.some(a => !axesKnown(a))
  || Object.values(data.ranges).some(r => r.length !== 3 || !r.every(Number.isFinite) || r[0] >= r[1] || r[2] <= 0)
  || data.ranges.fov[0] <= 0 || data.ranges.fov[1] >= 180
  || data.ranges.floor[0] < 0 || data.ranges.floor[1] > 1 || data.ranges.softness[0] <= 0
  || Object.values(data.motion).some(n => !Number.isFinite(n) || n <= 0)
  || data.camera_buttons.some(b => !["orbit", "pan", "dolly"].includes(b.kind) || !(b.word in data.words))) {
  throw new Error("invalid_navigator_visualization_data");
}
export const visText = (key: keyof typeof data.words, params: Record<string, string | number> = {}) =>
  data.words[key].replace(/\{(\w+)\}/g, (_, k: string) => String(params[k] ?? ""));
export type Axis = keyof typeof data.axes;
export interface LocalView { system: string; x: Axis; y: Axis; z: Axis; reach: number | null; softness: number; floor: number }
export const localHome = (): LocalView => {
  const p = data.profiles.find(p => p.id === data.defaults.profile) ?? data.profiles[0];
  return { system: p.system, x: p.x as Axis, y: p.y as Axis, z: p.z as Axis, reach: null, softness: data.defaults.softness, floor: data.defaults.floor };
};
export const numericMeasure = (row: Partial<GraphNodeRow>, measure: string, now: number): number | undefined => {
  const value = measureOf(row, measure, now);
  return typeof value === "number" && Number.isFinite(value) ? value : undefined;
};

/** This adapter is ONLY for graph.navigate addresses and its own view model. No ObjectRef or kernel-node ID conversion. */
export function navigatorIdentity(result: Pick<GraphNavigateResult, "focus" | "children">) {
  const byId = new Map<string, Partial<GraphNodeRow> & { address: string; label: string }>();
  byId.set(elementIdOf(result.focus.address), { ...result.focus, why: result.focus.why ?? undefined, value_human: result.focus.value_human ?? undefined });
  for (const row of result.children) {
    const id = elementIdOf(row.address);
    // Root's presentation ID must not silently alias a different address.
    if (byId.has(id) && byId.get(id)!.address !== row.address) throw new Error("navigator_address_alias");
    byId.set(id, row);
  }
  return { byId, idOf: (address: string) => {
    const id = elementIdOf(address);
    return byId.get(id)?.address === address ? id : null;
  }, rowOf: (id: string) => byId.get(id) };
}

export function localOpacity(depth: number | undefined, reach: number, softness: number, floor: number): number {
  if (depth === undefined) return 1; // unknown distance is not a fabricated 0
  const t = Math.max(0, Math.min(1, (depth - reach) / Math.max(Number.EPSILON, softness)));
  const smooth = t * t * (3 - 2 * t);
  return 1 - (1 - Math.max(0, Math.min(1, floor))) * smooth;
}

/** Presentation only: keep rows, IDs, edges and coordinate domains, even when alpha reaches zero. Never expands the graph. */
export function localModel(base: ViewModel, result: GraphNavigateResult, view: LocalView, selectedAddress: string | null, now: number): NavViewModel & {
  local: { hidden: number; depthMissing: number; maxDepth: number | null; axes: Record<string, number[]> };
} {
  const identity = navigatorIdentity(result);
  const depths = result.children.map(r => numericMeasure(r, "depth", now));
  const knownDepths = depths.filter((v): v is number => v !== undefined);
  const maxDepth = knownDepths.length ? Math.max(...knownDepths) : null;
  const selectedId = selectedAddress === null ? null : identity.idOf(selectedAddress);
  let hidden = 0;
  const axes: Record<string, number[]> = { x: [], y: [], z: [] };
  const elements = base.elements.map(e => {
    const row = identity.rowOf(e.id);
    const depth = row ? numericMeasure(row, "depth", now) : undefined;
    const multiplier = e.focus || e.id === selectedId ? 1 : localOpacity(depth, view.reach ?? maxDepth ?? 0, view.softness, view.floor);
    if (multiplier === 0) hidden++;
    const coords: Record<string, number> = {};
    if (view.system === "sector") {
      // The focus is an anchor; unknown CHILD depth stays unplaced rather than falling back to radius 1.
      if (e.focus) Object.assign(coords, { radius: 0, angle_deg: 0 });
      else if (depth !== undefined) Object.assign(coords, { radius: depth, angle_deg: e.coords.sector?.angle_deg });
    } else if (row) {
      for (const axis of ["x", "y", "z"] as const) {
        const value = numericMeasure(row, view[axis], now);
        if (value !== undefined) { coords[axis] = value; axes[axis].push(value); }
      }
    }
    return { ...e, coords: { local: coords }, channels: { ...e.channels, alpha: (typeof e.channels.alpha === "number" ? e.channels.alpha : 1) * multiplier } };
  });
  const bind: Record<string, string> = {};
  if (view.system === "sector") Object.assign(bind, { radius: "depth", angle_deg: "order" });
  else Object.assign(bind, { x: view.x, y: view.y, z: view.z });
  const measure_stats = { ...base.measure_stats };
  if (view.system === "cartesian") for (const axis of ["x", "y", "z"] as const) {
    const values = axes[axis];
    measure_stats[view[axis]] = { kind: "number", data_type: "quantitative", unit: data.axes[view[axis]].unit, count: values.length,
      ...(values.length ? { min: Math.min(...values), max: Math.max(...values) } : {}) };
  }
  // In relation perspectives, draw only edges actually returned. A listing fallback is never a history or purpose relation.
  const edgeKey = (src: string, dst: string, kind: string) => JSON.stringify([src, dst, kind]);
  const returnedEdges = new Set(result.edges.map(e => edgeKey(elementIdOf(e.src), elementIdOf(e.dst), e.kind)));
  const edges = result.perspectives?.length ? base.edges.filter(e => returnedEdges.has(edgeKey(e.src, e.dst, e.type))) : base.edges;
  return { ...base, elements, edges, measure_stats, coordinate_systems: [{ id: "local", system: view.system, supported: true,
    axes: view.system === "sector" ? ["radius", "angle_deg"] : ["x", "y", "z"],
    bind, settings: { missing: "unplaced" } }],
    local: { hidden, depthMissing: depths.filter(v => v === undefined).length, maxDepth, axes } };
}
