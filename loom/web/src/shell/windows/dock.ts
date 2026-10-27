// W3 (OKNA §2.2, §3.1): the dock tree of a layout as something to draw. Pure: the tree of the model (binary splits, tab groups in the
// leaves) becomes render nodes with their flex sizing. A leaf whose windows are not shown (a hidden knowledge leaf, a collapsed list)
// is kept in place but hidden, and its sibling takes the space - the ratio applies only while both sides are drawn. The shape of the
// tree never changes with what is shown, so a window drawn in a leaf is never remounted when a neighbour opens or closes. Degenerate ratios (0, 1, NaN) are
// drawn as they are (a side of 0 px) - the advisory about it is the model's (`zero_size`), not a refusal here.
import type { Tree, WLayout, Win } from "./model";

export type Sizing = "ratio" | "content";
export type DockNode =
  | { type: "split"; dir: "h" | "v"; ratio: number; a: DockNode; b: DockNode; hidden: boolean; path: string }
  | { type: "leaf"; leaf: string; windows: Win[]; active: Win | null; sizing: Sizing; hidden: boolean };

function isLeaf(t: Tree): t is { leaf: string } { return typeof (t as { leaf?: unknown }).leaf === "string"; }

/** The drawable dock tree, or null when nothing in it is shown. `shown` says whether a window is drawn now (a preset: not collapsed). */
export function dockView(l: WLayout, shown: (w: Win) => boolean = (w) => !w.collapsed): DockNode | null {
  const sizing = (l.leaf_sizing && typeof l.leaf_sizing === "object" ? l.leaf_sizing : {}) as Record<string, unknown>;
  const walk = (t: Tree, path = ""): DockNode | null => {
    if (isLeaf(t)) {
      const info = l.leaves[t.leaf];
      const windows = (info ? info.windows : []).map((id) => l.windows.find((w) => w.id === id)).filter((w): w is Win => !!w && w.mode !== "maximized" && w.mode !== "minimized" && shown(w));
      const active = windows.find((w) => w.id === info?.active) ?? windows[0] ?? null;
      return { type: "leaf", leaf: t.leaf, windows, active, sizing: sizing[t.leaf] === "content" ? "content" : "ratio", hidden: windows.length === 0 };
    }
    const a = walk(t.a, path + "a")!, b = walk(t.b, path + "b")!;
    const ratio = Number.isFinite(t.ratio) ? Math.min(1, Math.max(0, t.ratio)) : 0.5;
    return { type: "split", dir: t.split === "v" ? "v" : "h", ratio, a, b, hidden: a.hidden && b.hidden, path };
  };
  const root = walk(l.tree);
  return root && !root.hidden ? root : null;
}

/** The flex of side `a`/`b` of a split: a content-sized leaf keeps its own size, otherwise `a` gets the ratio and `b` the rest. */
export function sideFlex(node: DockNode, side: "a" | "b", ratio: number, other: DockNode): string {
  if (node.hidden) return "0 0 0";
  if (node.type === "leaf" && node.sizing === "content") return "0 0 auto";
  if (other.hidden) return "1 1 0";
  if (other.type === "leaf" && other.sizing === "content") return "1 1 0";
  return side === "a" ? `0 0 ${(ratio * 100).toFixed(4)}%` : "1 1 0";
}

/** The windows of the dock tree in reading order (for the switcher and tests). */
export function dockedWindows(n: DockNode | null): Win[] {
  if (!n) return [];
  if (n.hidden) return [];
  return n.type === "leaf" ? n.windows : [...dockedWindows(n.a), ...dockedWindows(n.b)];
}

export interface Box { x: number; y: number; w: number; h: number }
/** W4: the box of every drawn leaf in an area, as the flex of DockTree lays it out: a hidden side gives its space to the other, a
 *  content-sized leaf keeps `contentPx(leaf)` (preset 0 when not measured). Pure; used for `dock_narrow` before anything is drawn. */
export function leafBoxes(n: DockNode | null, area: Box, contentPx: (leaf: string) => number = () => 0): Record<string, Box> {
  if (!n || n.hidden) return {};
  if (n.type === "leaf") return { [n.leaf]: { ...area } };
  const h = n.dir === "h"; const total = h ? area.w : area.h;
  const fixed = (x: DockNode) => (x.type === "leaf" && x.sizing === "content" ? Math.min(total, Math.max(0, contentPx(x.leaf))) : null);
  let sa: number;
  if (n.a.hidden) sa = 0; else if (n.b.hidden) sa = total;
  else sa = fixed(n.a) ?? (fixed(n.b) !== null ? total - fixed(n.b)! : total * n.ratio);
  const A: Box = h ? { ...area, w: sa } : { ...area, h: sa };
  const B: Box = h ? { x: area.x + sa, y: area.y, w: total - sa, h: area.h } : { x: area.x, y: area.y + sa, w: area.w, h: total - sa };
  return { ...leafBoxes(n.a, A, contentPx), ...leafBoxes(n.b, B, contentPx) };
}

export type Zone = "left" | "right" | "top" | "bottom" | "center";
/** W4: the drop zone of a point over a leaf box: the nearest edge within `edge` (a fraction of the box, preset in data) docks to that side,
 *  the middle is `center` (a tab). Outside the box: null. Degenerate boxes (0 px) take no drop. */
export function dropZone(box: Box, px: number, py: number, edge = 0.25): Zone | null {
  if (!(box.w > 0 && box.h > 0) || px < box.x || py < box.y || px > box.x + box.w || py > box.y + box.h) return null;
  const fx = (px - box.x) / box.w, fy = (py - box.y) / box.h;
  const d: [Zone, number][] = [["left", fx], ["right", 1 - fx], ["top", fy], ["bottom", 1 - fy]];
  d.sort((p, q) => p[1] - q[1]);
  return d[0][1] <= Math.max(0, Math.min(0.5, edge)) ? d[0][0] : "center";
}
/** W4: the ratio of a split when its edge is dragged to (px, py) inside the split's box (clamped to 0..1; the model advises on 0/1). */
export function ratioAt(box: Box, dir: "h" | "v", px: number, py: number): number {
  const r = dir === "h" ? (px - box.x) / box.w : (py - box.y) / box.h;
  return Number.isFinite(r) ? Math.min(1, Math.max(0, r)) : 0.5;
}
