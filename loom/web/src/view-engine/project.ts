// The 2D renderer of the visualization engine (P28 step 1): a pure projection of a view model onto a plane. The model is layout independent; this is
// where a coordinate system becomes x/y and channel values become colours. Coordinate systems: sector (direction -> perspective, angle blend),
// tree with branch angles (both polar: 0 degrees is up, "ahead"), cartesian; depth from distance is shown in 2D as size, fog and desaturation.
// Channels: position (the coordinate system), hue, saturation, lightness, alpha, size, blur, badge. Defaults are presets, not rules.
import type { ViewElement, ViewGroup, ViewModel } from "../api/view-model";

export interface ProjectOptions { width: number; height: number; margin?: number }
export interface Mark {
  id: string;
  label: string;
  x: number;
  y: number;
  r: number;
  fill: string;
  opacity: number;
  blur: number;
  badge?: string;
  focus: boolean;
  sector?: string;
  /** 0..1 how deep (far) the element is, when the model has a depth coordinate system. */
  depth?: number;
}
export interface Wedge { id: string; group: ViewGroup; path: string; labelX: number; labelY: number }
export interface Projection {
  system: string | null;
  marks: Mark[];
  edges: Array<{ x1: number; y1: number; x2: number; y2: number; source: string }>;
  wedges: Wedge[];
  /** Elements the chosen coordinate system has no position for - listed, never dropped. */
  unplaced: ViewElement[];
  notes: string[];
}

const SPATIAL = ["sector", "tree_branch_angles", "cartesian"];
const rad = (deg: number) => (deg * Math.PI) / 180;
const num = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) ? v : null);
const clamp01 = (v: number) => Math.max(0, Math.min(1, v));

export function hsla(hue: number, saturation: number, lightness: number, alpha: number): string {
  return `hsla(${Math.round(hue)}, ${Math.round(clamp01(saturation) * 100)}%, ${Math.round(clamp01(lightness) * 100)}%, ${Math.round(clamp01(alpha) * 100) / 100})`;
}

export function project(model: ViewModel, options: ProjectOptions): Projection {
  const margin = options.margin ?? 28;
  const notes: string[] = [];
  const cx = options.width / 2, cy = options.height / 2;
  const spatial = model.coordinate_systems.find(c => c.supported && SPATIAL.includes(c.system) && model.elements.some(e => e.coords[c.id] !== undefined));
  const depthSystem = model.coordinate_systems.find(c => c.supported && c.system === "depth_from_distance");
  const placed = new Map<string, { x: number; y: number }>();
  const polar = !!spatial && spatial.system !== "cartesian";

  if (spatial && polar) {
    const radiusKey = spatial.system === "sector" ? "radius" : "depth";
    let maxR = 0;
    for (const e of model.elements) maxR = Math.max(maxR, num(e.coords[spatial.id]?.[radiusKey]) ?? 0);
    const scale = maxR > 0 ? (Math.min(options.width, options.height) / 2 - margin) / maxR : 1;
    for (const e of model.elements) {
      const c = e.coords[spatial.id];
      const r = num(c?.[radiusKey]), a = num(c?.angle_deg);
      if (r === null || a === null) continue;
      placed.set(e.id, { x: cx + r * scale * Math.sin(rad(a)), y: cy - r * scale * Math.cos(rad(a)) });
    }
  } else if (spatial) {
    const xs = model.elements.map(e => num(e.coords[spatial.id]?.x)).filter((v): v is number => v !== null);
    const ys = model.elements.map(e => num(e.coords[spatial.id]?.y)).filter((v): v is number => v !== null);
    const [x0, x1] = [Math.min(...xs), Math.max(...xs)], [y0, y1] = [Math.min(...ys), Math.max(...ys)];
    const fx = (v: number) => (x1 > x0 ? margin + ((v - x0) / (x1 - x0)) * (options.width - 2 * margin) : cx);
    const fy = (v: number) => (y1 > y0 ? options.height - margin - ((v - y0) / (y1 - y0)) * (options.height - 2 * margin) : cy);
    for (const e of model.elements) {
      const x = num(e.coords[spatial.id]?.x), y = num(e.coords[spatial.id]?.y);
      if (x === null || y === null) continue;
      placed.set(e.id, { x: fx(x), y: fy(y) });
    }
  } else {
    notes.push("no_spatial_coordinate_system");
    const n = Math.max(1, model.elements.length);
    model.elements.forEach((e, i) => {
      const a = (i / n) * Math.PI * 2;
      placed.set(e.id, { x: cx + Math.cos(a) * (Math.min(cx, cy) - margin), y: cy + Math.sin(a) * (Math.min(cx, cy) - margin) });
    });
  }

  // depth from distance: in 2D it becomes size, fog (alpha) and desaturation; the strengths are the settings of the coordinates element
  let maxDepth = 0;
  if (depthSystem) for (const e of model.elements) maxDepth = Math.max(maxDepth, Math.abs(num(e.coords[depthSystem.id]?.depth) ?? 0));
  const setting = (key: string, fallback: number) => num(depthSystem?.settings?.[key]) ?? fallback;
  const shrink = setting("shrink", 0.5), fog = setting("fog", 0.6), desaturate = setting("desaturate", 0.7);

  const marks: Mark[] = [];
  const unplaced: ViewElement[] = [];
  for (const e of model.elements) {
    const p = placed.get(e.id);
    if (!p) { unplaced.push(e); continue; }
    const ch = e.channels;
    const depthValue = depthSystem ? Math.abs(num(e.coords[depthSystem.id]?.depth) ?? 0) : 0;
    const t = maxDepth > 0 ? depthValue / maxDepth : 0;
    const hue = num(ch.hue) ?? 210;
    const saturation = (num(ch.saturation) ?? 0.55) * (1 - desaturate * t);
    const lightness = num(ch.lightness) ?? 0.55;
    const alpha = (num(ch.alpha) ?? 1) * (1 - fog * t);
    const size = (num(ch.size) ?? 10) * (1 - shrink * t);
    marks.push({
      id: e.id, label: e.label, x: p.x, y: p.y, r: Math.max(1.5, size / 2), fill: hsla(hue, saturation, lightness, alpha), opacity: 1,
      blur: num(ch.blur) ?? 0, badge: typeof ch.badge === "string" ? ch.badge : undefined, focus: !!e.focus, sector: e.sector,
      ...(depthSystem ? { depth: t } : {}),
    });
  }

  const wedges: Wedge[] = [];
  if (polar) {
    const R = Math.min(options.width, options.height) / 2 - 6;
    for (const g of model.groups) {
      const c = num(g.center_deg), w = num(g.width_deg);
      if (g.kind !== "sector" || c === null || w === null) continue;
      const at = (deg: number, r: number) => `${(cx + r * Math.sin(rad(deg))).toFixed(1)} ${(cy - r * Math.cos(rad(deg))).toFixed(1)}`;
      wedges.push({
        id: g.id, group: g, labelX: cx + (R - 10) * Math.sin(rad(c)), labelY: cy - (R - 10) * Math.cos(rad(c)),
        path: `M ${cx} ${cy} L ${at(c - w / 2, R)} A ${R} ${R} 0 ${w > 180 ? 1 : 0} 1 ${at(c + w / 2, R)} Z`,
      });
    }
  }
  const byId = new Map(marks.map(m => [m.id, m]));
  const edges = model.edges.flatMap(e => {
    const a = byId.get(e.src), b = byId.get(e.dst);
    return a && b ? [{ x1: a.x, y1: a.y, x2: b.x, y2: b.y, source: e.source }] : [];
  });
  return { system: spatial ? spatial.system : null, marks, edges, wedges, unplaced, notes };
}
