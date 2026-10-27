// W3 (OKNA §2.2, §3.1): the dock tree of a layout as something to draw. Pure: the tree of the model (binary splits, tab groups in the
// leaves) becomes render nodes with their flex sizing. A leaf whose windows are not shown (a hidden knowledge leaf, a collapsed list)
// drops out and its sibling takes the space - the ratio applies only while both sides are drawn. Degenerate ratios (0, 1, NaN) are
// drawn as they are (a side of 0 px) - the advisory about it is the model's (`zero_size`), not a refusal here.
import type { Tree, WLayout, Win } from "./model";

export type Sizing = "ratio" | "content";
export type DockNode =
  | { type: "split"; dir: "h" | "v"; ratio: number; a: DockNode; b: DockNode }
  | { type: "leaf"; leaf: string; windows: Win[]; active: Win; sizing: Sizing };

function isLeaf(t: Tree): t is { leaf: string } { return typeof (t as { leaf?: unknown }).leaf === "string"; }

/** The drawable dock tree, or null when nothing in it is shown. `shown` says whether a window is drawn now (a preset: not collapsed). */
export function dockView(l: WLayout, shown: (w: Win) => boolean = (w) => !w.collapsed): DockNode | null {
  const sizing = (l.leaf_sizing && typeof l.leaf_sizing === "object" ? l.leaf_sizing : {}) as Record<string, unknown>;
  const walk = (t: Tree): DockNode | null => {
    if (isLeaf(t)) {
      const info = l.leaves[t.leaf];
      if (!info) return null;
      const windows = info.windows.map((id) => l.windows.find((w) => w.id === id)).filter((w): w is Win => !!w && w.mode !== "maximized" && w.mode !== "minimized" && shown(w));
      if (windows.length === 0) return null;
      const active = windows.find((w) => w.id === info.active) ?? windows[0];
      return { type: "leaf", leaf: t.leaf, windows, active, sizing: sizing[t.leaf] === "content" ? "content" : "ratio" };
    }
    const a = walk(t.a), b = walk(t.b);
    if (!a) return b;
    if (!b) return a;
    const ratio = Number.isFinite(t.ratio) ? Math.min(1, Math.max(0, t.ratio)) : 0.5;
    return { type: "split", dir: t.split === "v" ? "v" : "h", ratio, a, b };
  };
  return walk(l.tree);
}

/** The flex of side `a`/`b` of a split: a content-sized leaf keeps its own size, otherwise `a` gets the ratio and `b` the rest. */
export function sideFlex(node: DockNode, side: "a" | "b", ratio: number, other: DockNode): string {
  if (node.type === "leaf" && node.sizing === "content") return "0 0 auto";
  if (other.type === "leaf" && other.sizing === "content") return "1 1 0";
  return side === "a" ? `0 0 ${(ratio * 100).toFixed(4)}%` : "1 1 0";
}

/** The windows of the dock tree in reading order (for the switcher and tests). */
export function dockedWindows(n: DockNode | null): Win[] {
  if (!n) return [];
  return n.type === "leaf" ? n.windows : [...dockedWindows(n.a), ...dockedWindows(n.b)];
}
