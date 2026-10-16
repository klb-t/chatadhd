// The spatial projection of the visualization engine (P28 step 3): a pure mapping of a view model onto 3D, with no 3D library. The model is layout
// independent; here a coordinate system becomes a direction and a distance around the user, and channel values become colours, alpha, size and fog.
// The spatial preset (spec section 3): sector angles are REAL directions around the user (0 degrees = ahead, 90 = to the right, 180 = behind), depth is
// distance, the world stands still and the user turns; the sector the user faces is emphasised by a blend curve over the gaze angle. Every number here is
// a setting of the `view_renderers` entry (data, effective view P24); the defaults below are only the presets of that entry, used when it is missing.
import type { ViewElement, ViewGroup, ViewModel } from "../api/view-model";
import { hsla } from "./project";

export type Curve = Array<[number, number]>;
export interface SpatialConfig {
  directionOffsetDeg: number;
  mirror: boolean;
  nearM: number;
  farM: number;
  distanceCurve: Curve;
  flatM: number;
  /** Metres a node rises per metre of distance, so a line of nodes on one ray does not hide itself from the centre. */
  riseMPerM: number;
  mPerPx: number;
  minRadiusM: number;
  maxRadiusM: number;
  /** A node never looks smaller than this angle from the user, so the far ones stay visible (0 = off). */
  minAngleDeg: number;
  /** Strength of the depth cues taken from the `depth_from_distance` element of the declaration (fog and desaturation; size is perspective in 3D). */
  useDepthSystem: boolean;
  groundShow: boolean;
  groundYM: number;
  groundOpacity: number;
  edgeOpacity: number;
  blendCurve: Curve;
  blendFloorAlpha: number;
  blendFloorScale: number;
  labelPolicy: string;
  labelGazedWeight: number;
  labelNearM: number;
  labelMaxChars: number;
  labelWorldHeightM: number;
  badges: boolean;
  pickToleranceDeg: number;
  /** Notes about values in the entry that were not usable (the preset was used instead) - shown, never silent. */
  notes: string[];
}

export const DEFAULT_RENDERER: Record<string, unknown> = {
  spatial: { direction_offset_deg: 0, mirror: false, distance: { near_m: 1, far_m: 7, curve: [[0, 0], [1, 1]], flat_m: 3, rise_m_per_m: 0.12 }, node: { m_per_px: 0.012, min_radius_m: 0.03, max_radius_m: 0.4, min_angle_deg: 0.7 }, ground: { show: true, y_m: -1.2, opacity: 0.07 }, edges: { opacity: 0.35 }, fog: { use_depth_system: true } },
  blend: { curve: [[0, 1], [0.5, 0.92], [1, 0.5], [1.5, 0.2], [2.5, 0.1]], floor_alpha: 0.15, floor_scale: 0.8 },
  labels: { policy: "focus_and_gazed", gazed_weight: 0.7, near_m: 2.2, max_chars: 42, world_height_m: 0.14, badges: true },
  pick: { tolerance_deg: 4 },
};

const isObj = (v: unknown): v is Record<string, unknown> => typeof v === "object" && v !== null && !Array.isArray(v);
const rad = (deg: number) => (deg * Math.PI) / 180;
const deg = (r: number) => (r * 180) / Math.PI;
const clamp01 = (v: number) => Math.max(0, Math.min(1, v));
const finite = (v: unknown): number | null => (typeof v === "number" && Number.isFinite(v) ? v : null);

/** Piecewise-linear curve through [x, y] control points (sorted by x, clamped outside). An empty or broken curve is the identity. */
export function curveAt(curve: Curve, x: number): number {
  if (curve.length === 0) return x;
  const pts = [...curve].sort((a, b) => a[0] - b[0]);
  if (x <= pts[0][0]) return pts[0][1];
  for (let i = 1; i < pts.length; i++) {
    if (x <= pts[i][0]) {
      const [x0, y0] = pts[i - 1], [x1, y1] = pts[i];
      return x1 === x0 ? y1 : y0 + ((y1 - y0) * (x - x0)) / (x1 - x0);
    }
  }
  return pts[pts.length - 1][1];
}

export function readSpatialConfig(entry: unknown): SpatialConfig {
  const notes: string[] = [];
  const root = isObj(entry) ? entry : {};
  const section = (name: string): Record<string, unknown> => {
    const own = root[name], base = DEFAULT_RENDERER[name];
    if (own !== undefined && !isObj(own)) notes.push(`${name}: not an object`);
    return { ...(isObj(base) ? base : {}), ...(isObj(own) ? own : {}) };
  };
  const num = (o: Record<string, unknown>, key: string, where: string, fallback: number): number => {
    const v = o[key];
    if (v === undefined) return fallback;
    const n = finite(v);
    if (n === null) { notes.push(`${where}.${key}: not a number`); return fallback; }
    return n;
  };
  const curve = (o: Record<string, unknown>, key: string, where: string, fallback: Curve): Curve => {
    const v = o[key];
    if (v === undefined) return fallback;
    if (Array.isArray(v) && v.length > 0 && v.every(p => Array.isArray(p) && finite(p[0]) !== null && finite(p[1]) !== null)) return v.map(p => [p[0] as number, p[1] as number]);
    notes.push(`${where}.${key}: not a list of [x, y] points`);
    return fallback;
  };
  const sp = section("spatial"), blend = section("blend"), labels = section("labels"), pick = section("pick");
  const dist = isObj(sp.distance) ? sp.distance : {}, node = isObj(sp.node) ? sp.node : {}, ground = isObj(sp.ground) ? sp.ground : {}, edges = isObj(sp.edges) ? sp.edges : {}, fog = isObj(sp.fog) ? sp.fog : {};
  const d0 = (DEFAULT_RENDERER.spatial as Record<string, Record<string, unknown>>);
  return {
    directionOffsetDeg: num(sp, "direction_offset_deg", "spatial", 0),
    mirror: sp.mirror === true,
    nearM: num(dist, "near_m", "spatial.distance", 1),
    farM: num(dist, "far_m", "spatial.distance", 7),
    distanceCurve: curve(dist, "curve", "spatial.distance", d0.distance.curve as Curve),
    flatM: num(dist, "flat_m", "spatial.distance", 3),
    riseMPerM: num(dist, "rise_m_per_m", "spatial.distance", 0.12),
    mPerPx: num(node, "m_per_px", "spatial.node", 0.012),
    minRadiusM: num(node, "min_radius_m", "spatial.node", 0.03),
    maxRadiusM: num(node, "max_radius_m", "spatial.node", 0.4),
    minAngleDeg: num(node, "min_angle_deg", "spatial.node", 0.7),
    useDepthSystem: fog.use_depth_system !== false,
    groundShow: ground.show !== false,
    groundYM: num(ground, "y_m", "spatial.ground", -1.2),
    groundOpacity: num(ground, "opacity", "spatial.ground", 0.07),
    edgeOpacity: num(edges, "opacity", "spatial.edges", 0.35),
    blendCurve: curve(blend, "curve", "blend", DEFAULT_RENDERER.blend ? ((DEFAULT_RENDERER.blend as Record<string, unknown>).curve as Curve) : []),
    blendFloorAlpha: num(blend, "floor_alpha", "blend", 0.15),
    blendFloorScale: num(blend, "floor_scale", "blend", 0.8),
    labelPolicy: typeof labels.policy === "string" ? labels.policy : "focus_and_gazed",
    labelGazedWeight: num(labels, "gazed_weight", "labels", 0.7),
    labelNearM: num(labels, "near_m", "labels", 2.2),
    labelMaxChars: num(labels, "max_chars", "labels", 42),
    labelWorldHeightM: num(labels, "world_height_m", "labels", 0.14),
    badges: labels.badges !== false,
    pickToleranceDeg: num(pick, "tolerance_deg", "pick", 4),
    notes,
  };
}

// ── directions ──
/** The unit direction of a sector angle: 0 = ahead (-Z), 90 = right (+X), 180 = behind, -90 = left. The offset and the mirror are the renderer's data (reverse the past: 180). */
export function directionOf(angleDeg: number, cfg: Pick<SpatialConfig, "directionOffsetDeg" | "mirror">): [number, number, number] {
  const a = rad((cfg.mirror ? -angleDeg : angleDeg) + cfg.directionOffsetDeg);
  return [Math.sin(a), 0, -Math.cos(a)];
}
/** The azimuth (degrees, 0 = ahead, clockwise from above) of a direction vector. */
export function azimuthOf(x: number, z: number): number {
  return deg(Math.atan2(x, -z));
}
/** The sector angle of the declaration that a world azimuth corresponds to (the inverse of `directionOf`). */
export function declarationAngle(azimuthDeg: number, cfg: Pick<SpatialConfig, "directionOffsetDeg" | "mirror">): number {
  const a = azimuthDeg - cfg.directionOffsetDeg;
  return cfg.mirror ? -a : a;
}
/** The smallest signed difference a - b in degrees, in (-180, 180]. */
export function angleDiff(a: number, b: number): number {
  let d = (a - b) % 360;
  if (d > 180) d -= 360;
  if (d <= -180) d += 360;
  return d;
}
/** The distance in metres of a radius (a measure value) given the largest radius in the view: near + (far - near) * curve(r / max). */
export function distanceOf(radius: number, maxRadius: number, cfg: Pick<SpatialConfig, "nearM" | "farM" | "distanceCurve">): number {
  const t = maxRadius > 0 ? clamp01(radius / maxRadius) : 0;
  return cfg.nearM + (cfg.farM - cfg.nearM) * curveAt(cfg.distanceCurve, t);
}

// ── the sector blend by gaze angle ──
/** The weight (0..1+, from the curve) of a sector for a gaze azimuth: the curve is over the angular distance from the sector centre in HALF widths of the sector. */
export function sectorWeight(gazeAzimuthDeg: number, group: Pick<ViewGroup, "center_deg" | "width_deg">, cfg: Pick<SpatialConfig, "blendCurve" | "directionOffsetDeg" | "mirror">): number {
  const centre = finite(group.center_deg), width = finite(group.width_deg);
  if (centre === null || width === null || width <= 0) return 1;
  const delta = Math.abs(angleDiff(declarationAngle(gazeAzimuthDeg, cfg), centre));
  return curveAt(cfg.blendCurve, delta / (width / 2));
}
export interface Blend { alpha: number; scale: number; weight: number }
/** Per element: how much of the emphasis it gets from where the user looks. Elements without a sector (the focus, trees) always get full weight. */
export function blendFor(marks: Array<Pick<Mark3, "id" | "sector" | "focus">>, groups: Array<Pick<ViewGroup, "id" | "center_deg" | "width_deg">>, gazeAzimuthDeg: number, cfg: SpatialConfig): Map<string, Blend> {
  const byGroup = new Map<string, number>();
  for (const g of groups) byGroup.set(g.id, clamp01(sectorWeight(gazeAzimuthDeg, g, cfg)));
  const out = new Map<string, Blend>();
  for (const m of marks) {
    const w = m.focus || !m.sector ? 1 : byGroup.get(m.sector) ?? 1;
    out.set(m.id, { weight: w, alpha: cfg.blendFloorAlpha + (1 - cfg.blendFloorAlpha) * w, scale: cfg.blendFloorScale + (1 - cfg.blendFloorScale) * w });
  }
  return out;
}

/** Whether a label is shown by the label policy (data): `all`, `none`, otherwise the focus, the gazed sectors and what is near; `forceAll` is the labels toggle. */
export function labelVisible(m: Pick<Mark3, "focus" | "distance">, weight: number, cfg: Pick<SpatialConfig, "labelPolicy" | "labelGazedWeight" | "labelNearM">, forceAll: boolean): boolean {
  if (forceAll || cfg.labelPolicy === "all") return true;
  if (cfg.labelPolicy === "none") return false;
  return m.focus || weight >= cfg.labelGazedWeight || m.distance <= cfg.labelNearM;
}

// ── projection ──
export interface Mark3 {
  id: string;
  label: string;
  /** World position in metres, the user at the origin (x right, y up, -z ahead). */
  x: number;
  y: number;
  z: number;
  /** Distance from the user in metres, azimuth in degrees of the direction (0 = ahead). */
  distance: number;
  azimuth: number;
  radiusM: number;
  fill: string;
  hue: number;
  saturation: number;
  lightness: number;
  /** Alpha after the depth fog; the gaze blend multiplies it at draw time. */
  alpha: number;
  blur: number;
  badge?: string;
  focus: boolean;
  sector?: string;
  depth?: number;
}
export interface Wedge3 { id: string; group: ViewGroup; azimuth: number; widthDeg: number }
export interface Spatial3D {
  system: string | null;
  marks: Mark3[];
  edges: Array<{ from: string; to: string; source: string }>;
  wedges: Wedge3[];
  unplaced: ViewElement[];
  maxDistance: number;
  notes: string[];
}

const SPATIAL = ["sector", "tree_branch_angles", "cartesian"];

export function projectSpatial(model: ViewModel, cfg: SpatialConfig): Spatial3D {
  const notes: string[] = [];
  const spatial = model.coordinate_systems.find(c => c.supported && SPATIAL.includes(c.system) && model.elements.some(e => e.coords[c.id] !== undefined));
  const depthSystem = model.coordinate_systems.find(c => c.supported && c.system === "depth_from_distance");
  const place = new Map<string, { x: number; y: number; z: number; distance: number; azimuth: number }>();
  const polar = !!spatial && spatial.system !== "cartesian";
  const rise = (d: number) => d * cfg.riseMPerM;

  if (spatial && polar) {
    const key = spatial.system === "sector" ? "radius" : "depth";
    let maxR = 0;
    for (const e of model.elements) maxR = Math.max(maxR, finite(e.coords[spatial.id]?.[key]) ?? 0);
    for (const e of model.elements) {
      const c = e.coords[spatial.id];
      const r = finite(c?.[key]), a = finite(c?.angle_deg);
      if (r === null || a === null) continue;
      const dir = directionOf(a, cfg), d = distanceOf(r, maxR, cfg);
      place.set(e.id, { x: dir[0] * d, y: rise(d), z: dir[2] * d, distance: d, azimuth: azimuthOf(dir[0], dir[2]) });
    }
  } else if (spatial) {
    const xs = model.elements.map(e => finite(e.coords[spatial.id]?.x)).filter((v): v is number => v !== null);
    const ys = model.elements.map(e => finite(e.coords[spatial.id]?.y)).filter((v): v is number => v !== null);
    const [x0, x1] = [Math.min(...xs), Math.max(...xs)], [y0, y1] = [Math.min(...ys), Math.max(...ys)];
    const f = (v: number, lo: number, hi: number) => (hi > lo ? ((v - lo) / (hi - lo) - 0.5) * 2 * cfg.flatM * 0.8 : 0);
    for (const e of model.elements) {
      const x = finite(e.coords[spatial.id]?.x), y = finite(e.coords[spatial.id]?.y);
      if (x === null || y === null) continue;
      const px = f(x, x0, x1), py = f(y, y0, y1);
      place.set(e.id, { x: px, y: py, z: -cfg.flatM, distance: Math.hypot(px, py, cfg.flatM), azimuth: azimuthOf(px, -cfg.flatM) });
    }
  } else {
    notes.push("no_spatial_coordinate_system");
    const n = Math.max(1, model.elements.length);
    model.elements.forEach((e, i) => {
      const a = (i / n) * 360, dir = directionOf(a, cfg), d = cfg.flatM;
      place.set(e.id, { x: dir[0] * d, y: 0, z: dir[2] * d, distance: d, azimuth: azimuthOf(dir[0], dir[2]) });
    });
  }

  let maxDepth = 0;
  if (depthSystem) for (const e of model.elements) maxDepth = Math.max(maxDepth, Math.abs(finite(e.coords[depthSystem.id]?.depth) ?? 0));
  const setting = (key: string, fallback: number) => (cfg.useDepthSystem ? finite(depthSystem?.settings?.[key]) ?? fallback : 0);
  const fog = setting("fog", 0.6), desaturate = setting("desaturate", 0.7);

  const marks: Mark3[] = [], unplaced: ViewElement[] = [];
  let maxDistance = 0;
  for (const e of model.elements) {
    const p = place.get(e.id);
    if (!p) { unplaced.push(e); continue; }
    const ch = e.channels;
    const t = depthSystem && maxDepth > 0 ? Math.abs(finite(e.coords[depthSystem.id]?.depth) ?? 0) / maxDepth : 0;
    const hue = finite(ch.hue) ?? 210;
    const saturation = clamp01((finite(ch.saturation) ?? 0.55) * (1 - desaturate * t));
    const lightness = clamp01(finite(ch.lightness) ?? 0.55);
    const alpha = clamp01((finite(ch.alpha) ?? 1) * (1 - fog * t));
    const sizePx = finite(ch.size) ?? 10;
    maxDistance = Math.max(maxDistance, p.distance);
    marks.push({
      id: e.id, label: e.label, ...p, radiusM: Math.max(cfg.minRadiusM, Math.min(cfg.maxRadiusM, (sizePx / 2) * cfg.mPerPx), p.distance * Math.tan(rad(cfg.minAngleDeg) / 2)),
      fill: hsla(hue, saturation, lightness, alpha), hue, saturation, lightness, alpha, blur: finite(ch.blur) ?? 0,
      badge: typeof ch.badge === "string" ? ch.badge : undefined, focus: !!e.focus, sector: e.sector, ...(depthSystem ? { depth: t } : {}),
    });
  }
  const wedges: Wedge3[] = [];
  if (polar) {
    for (const g of model.groups) {
      const c = finite(g.center_deg), w = finite(g.width_deg);
      if (g.kind !== "sector" || c === null || w === null) continue;
      const dir = directionOf(c, cfg);
      wedges.push({ id: g.id, group: g, azimuth: azimuthOf(dir[0], dir[2]), widthDeg: w });
    }
  }
  const have = new Set(marks.map(m => m.id));
  const edges = model.edges.filter(e => have.has(e.src) && have.has(e.dst)).map(e => ({ from: e.src, to: e.dst, source: e.source }));
  return { system: spatial ? spatial.system : null, marks, edges, wedges, unplaced, maxDistance, notes };
}
