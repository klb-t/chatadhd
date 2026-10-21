// M1-W0 (2), package P4: the MAP half of the graph navigator as pure logic, so a unit test drives it without a browser.
// The map draws the same answer as the list (graph.navigate: the focus and its children) through the existing view engine
// (view-engine/project.ts): this file only turns the kernel's rows into a view model (focus in the middle, children around it on
// the sector coordinate system, colours by kind from GraphView.colorFor, size by count), moves the camera (one finger drags, two
// fingers pinch, + / − step) and says which screen rectangle the Android shell must keep free of its own edge gestures
// (window.LoomBridge.setGestureExclusion, CSS px). Numbers here are presets, not rules; nothing lists nodes of its own.
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ViewEdge, ViewElement, ViewModel } from "../api/view-model";
import { colorFor } from "./kind-colors";

/** The height of the map on a phone (px); the width follows the screen. */
export const MAP_HEIGHT_PRESET = 300;
/** A touch target is at least this big (px), the mark under it can be smaller. */
export const HIT_SIZE_PRESET = 44;
/** One step of + / − (a factor), the pinch is continuous. */
export const ZOOM_STEP_PRESET = 1.25;
export const ZOOM_MIN_PRESET = 0.2;
export const ZOOM_MAX_PRESET = 8;
/** A finger that moves less than this (px) between down and up is a tap, not a drag. */
export const TAP_SLOP_PRESET = 8;
/** How many characters of a label the map draws next to a mark; the line under the map shows the whole label. */
export const LABEL_CHARS_PRESET = 16;
/** The element id of the graph root on the map (its address is the empty string). */
export const ROOT_ELEMENT_ID = "root";

export type Layout = "both" | "list" | "map";
export const LAYOUTS: readonly Layout[] = ["both", "list", "map"];
/** The setting navigator_layout read as a layout; anything else is the preset (both). */
export const layoutOf = (value: unknown): Layout => (LAYOUTS as readonly unknown[]).includes(value) ? value as Layout : "both";
/** Which of the two disclosures start open for a layout: "list" and "map" fold the other one (it stays one tap away, never absent). */
export const openByLayout = (layout: Layout): { list: boolean; map: boolean } => ({ list: layout !== "map", map: layout !== "list" });

export interface Point { x: number; y: number }
export interface Camera { zoom: number; panX: number; panY: number }
export const CAMERA_HOME: Camera = { zoom: 1, panX: 0, panY: 0 };

/** The HSL of a #rrggbb colour (hue in degrees, saturation and lightness 0..1), the channels project() understands. */
export function hslOfHex(hex: string): { hue: number; saturation: number; lightness: number } {
  const m = /^#?([0-9a-f]{6})$/i.exec(hex.trim());
  if (!m) return { hue: 210, saturation: 0.55, lightness: 0.55 };
  const n = parseInt(m[1], 16), r = ((n >> 16) & 255) / 255, g = ((n >> 8) & 255) / 255, b = (n & 255) / 255;
  const max = Math.max(r, g, b), min = Math.min(r, g, b), l = (max + min) / 2, d = max - min;
  if (d === 0) return { hue: 0, saturation: 0, lightness: Math.round(l * 1000) / 1000 };
  const s = d / (1 - Math.abs(2 * l - 1));
  const h = max === r ? ((g - b) / d + (g < b ? 6 : 0)) : max === g ? (b - r) / d + 2 : (r - g) / d + 4;
  return { hue: Math.round(h * 60) % 360, saturation: Math.round(s * 1000) / 1000, lightness: Math.round(l * 1000) / 1000 };
}

/** The size channel of a row: grows with the log of its count, so a hundred children is visible and a million still fits. */
export function sizeOf(row: Pick<GraphNodeRow, "count">): number {
  return 14 + (typeof row.count === "number" && row.count > 0 ? 6 * Math.log10(1 + row.count) : 0);
}
export const elementIdOf = (address: string): string => address || ROOT_ELEMENT_ID;

/** The view model of one level: the focus at the centre, the children around it (angle by their order, radius by their depth),
 *  the kernel's edges where both ends are on the level, a focus→child edge for every child the kernel gave no edge for. */
export function viewModelFor(result: Pick<GraphNavigateResult, "focus" | "children" | "edges" | "counts">, rootLabel: string): ViewModel {
  const focusId = elementIdOf(result.focus.address);
  const focusHsl = hslOfHex(colorFor(result.focus.kind));
  const elements: ViewElement[] = [{
    id: focusId, label: result.focus.label || rootLabel, kind: result.focus.kind, focus: true, measures: {},
    coords: { sector: { radius: 0, angle_deg: 0 } },
    channels: { hue: focusHsl.hue, saturation: focusHsl.saturation, lightness: focusHsl.lightness, size: 18 }, sources: ["graph.navigate"],
  }];
  const n = Math.max(1, result.children.length);
  result.children.forEach((row, i) => {
    const hsl = hslOfHex(colorFor(row.kind));
    const depth = typeof row.depth === "number" && row.depth > 0 ? row.depth : 1;
    elements.push({
      id: elementIdOf(row.address), label: row.label, kind: row.kind, measures: typeof row.count === "number" ? { count: row.count } : {},
      coords: { sector: { radius: depth, angle_deg: (i * 360) / n } },
      channels: { hue: hsl.hue, saturation: hsl.saturation, lightness: hsl.lightness, size: sizeOf(row) }, sources: ["graph.navigate"],
    });
  });
  const ids = new Set(elements.map(e => e.id));
  const edges: ViewEdge[] = [];
  const linked = new Set<string>();
  for (const e of result.edges) {
    const src = elementIdOf(e.src), dst = elementIdOf(e.dst);
    if (!ids.has(src) || !ids.has(dst)) continue;
    edges.push({ src, dst, type: e.kind, source: "graph.navigate" });
    if (src === focusId) linked.add(dst);
    if (dst === focusId) linked.add(src);
  }
  for (const row of result.children) {
    const id = elementIdOf(row.address);
    if (!linked.has(id) && (row.depth ?? 1) <= 1) edges.push({ src: focusId, dst: id, type: "child", source: "graph.navigate" });
  }
  return {
    schema: "loom.view_model/1", declaration: { id: "navigator_level", origin: "web GraphNavigator" }, focus: focusId, sources: [],
    elements, edges, groups: [], measure_stats: {},
    coordinate_systems: [{ id: "sector", system: "sector", axes: ["radius", "angle_deg"], bind: {}, supported: true }],
    encodings: [], unsupported: [], budget: { max_nodes: null, elements: elements.length, collapsed: result.counts.collapsed }, issues: [],
  };
}

/** A projected point on the screen: the camera scales around the centre of the map and then shifts. */
export const screenOf = (p: Point, cam: Camera, centre: Point): Point =>
  ({ x: centre.x + (p.x - centre.x) * cam.zoom + cam.panX, y: centre.y + (p.y - centre.y) * cam.zoom + cam.panY });

const clampZoom = (z: number) => Math.max(ZOOM_MIN_PRESET, Math.min(ZOOM_MAX_PRESET, z));

/** Zoom by a factor so that the screen point `around` (preset: the centre) stays where it is. */
export function zoomBy(cam: Camera, factor: number, centre: Point, around: Point = centre): Camera {
  const zoom = clampZoom(cam.zoom * factor);
  const f = zoom / cam.zoom;
  return { zoom, panX: around.x - centre.x - (around.x - centre.x - cam.panX) * f, panY: around.y - centre.y - (around.y - centre.y - cam.panY) * f };
}
/** One finger: the plane follows the finger. */
export const panBy = (cam: Camera, dx: number, dy: number): Camera => ({ ...cam, panX: cam.panX + dx, panY: cam.panY + dy });
/** Two fingers from (a0, b0) to (a, b): the plane zooms by the change of their distance and follows their midpoint. */
export function pinch(cam0: Camera, a0: Point, b0: Point, a: Point, b: Point, centre: Point): Camera {
  const d0 = Math.hypot(b0.x - a0.x, b0.y - a0.y), d = Math.hypot(b.x - a.x, b.y - a.y);
  const m0 = { x: (a0.x + b0.x) / 2, y: (a0.y + b0.y) / 2 }, m = { x: (a.x + b.x) / 2, y: (a.y + b.y) / 2 };
  const zoom = clampZoom(cam0.zoom * (d0 > 0 ? d / d0 : 1));
  const f = zoom / cam0.zoom;
  return { zoom, panX: m.x - centre.x - (m0.x - centre.x - cam0.panX) * f, panY: m.y - centre.y - (m0.y - centre.y - cam0.panY) * f };
}

export interface Rect { x: number; y: number; w: number; h: number }
export interface Box { left: number; top: number; width: number; height: number }
/** The rectangles (CSS px, whole numbers) the shell keeps free of its edge gestures: the visible part of the open map; nothing
 *  when the map is folded, off screen or has no size. Android clips what it accepts (200 dp per edge) — that is the shell's part. */
export function exclusionRects(box: Box | null, open: boolean, viewport: { width: number; height: number }): Rect[] {
  if (!open || !box) return [];
  const x0 = Math.max(0, box.left), y0 = Math.max(0, box.top);
  const x1 = Math.min(viewport.width, box.left + box.width), y1 = Math.min(viewport.height, box.top + box.height);
  if (x1 - x0 < 1 || y1 - y0 < 1) return [];
  return [{ x: Math.round(x0), y: Math.round(y0), w: Math.round(x1 - x0), h: Math.round(y1 - y0) }];
}
export const sameRects = (a: Rect[], b: Rect[]): boolean => JSON.stringify(a) === JSON.stringify(b);

/** A label short enough for the plane; the line under the map has the whole one. */
export const shortLabel = (label: string): string => label.length > LABEL_CHARS_PRESET ? label.slice(0, LABEL_CHARS_PRESET - 1) + "…" : label;
