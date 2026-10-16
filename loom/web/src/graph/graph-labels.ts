// R35-4: readable node labels. Truncation with an ellipsis (the full text stays in a title/tooltip) and a greedy collision pass:
// labels are placed in priority order (forced > degree > input order) and a label that would overlap one already placed is hidden.
// Hidden labels are shown on hover or tap (those nodes are "forced"). All thresholds are a preset, not hard limits: pass overrides.

export interface GraphLabelOptions {
  /** Longest label drawn, in characters, before "…". 0 = never truncate. */
  maxChars: number;
  /** Font size in screen px. */
  fontPx: number;
  /** Nodes with fewer edges than this get no label unless forced (0 = collision decides alone). */
  minDegree: number;
  /** Gap in screen px kept free around each label box. */
  padding: number;
  /** Most labels drawn at once; 0 = no cap. */
  maxLabels: number;
}

/** Preset for a wide screen. */
export const GRAPH_LABEL_PRESET: GraphLabelOptions = { maxChars: 24, fontPx: 12, minDegree: 0, padding: 3, maxLabels: 0 };
/** Preset for a narrow (phone) viewport: shorter labels, smaller font, a little more air. */
export const GRAPH_LABEL_PRESET_NARROW: GraphLabelOptions = { maxChars: 14, fontPx: 11, minDegree: 0, padding: 4, maxLabels: 0 };
/** Below this container width (CSS px) the narrow preset applies. */
export const GRAPH_NARROW_WIDTH = 520;

export function graphLabelOptions(containerWidth: number, overrides: Partial<GraphLabelOptions> = {}): GraphLabelOptions {
  return { ...(containerWidth < GRAPH_NARROW_WIDTH ? GRAPH_LABEL_PRESET_NARROW : GRAPH_LABEL_PRESET), ...overrides };
}

export function truncateLabel(label: string, maxChars: number): string {
  const chars = Array.from(label);
  if (maxChars <= 0 || chars.length <= maxChars) return label;
  return chars.slice(0, Math.max(1, maxChars - 1)).join("").trimEnd() + "…";
}

export function degreeMap(edges: readonly { src: string; dst: string }[]): Map<string, number> {
  const d = new Map<string, number>();
  for (const e of edges) {
    d.set(e.src, (d.get(e.src) ?? 0) + 1);
    d.set(e.dst, (d.get(e.dst) ?? 0) + 1);
  }
  return d;
}

export interface LabelCandidate { id: string; x: number; y: number; text: string; degree: number; forced?: boolean }
export interface PlacedLabel { id: string; text: string; x: number; y: number; width: number; height: number }

/** Plan the labels in SCREEN coordinates. `measure` gives the text width in px. A label is drawn to the right of its node
 * (x + radius + 2); its box is [x, x+width] x [y-height/2, y+height/2]. Forced labels are never hidden and show the full text. */
export function planLabels(items: readonly LabelCandidate[], opts: GraphLabelOptions, measure: (text: string) => number, radius = 8): PlacedLabel[] {
  const order = items.map((c, i) => ({ c, i })).sort((a, b) =>
    Number(!!b.c.forced) - Number(!!a.c.forced) || b.c.degree - a.c.degree || a.i - b.i);
  const placed: PlacedLabel[] = [];
  const h = opts.fontPx + 2;
  let drawn = 0;
  for (const { c } of order) {
    if (!c.forced) {
      if (c.degree < opts.minDegree) continue;
      if (opts.maxLabels > 0 && drawn >= opts.maxLabels) continue;
    }
    const text = c.forced ? c.text : truncateLabel(c.text, opts.maxChars);
    const box = { id: c.id, text, x: c.x + radius + 2, y: c.y, width: measure(text), height: h };
    if (!c.forced && placed.some(p => overlaps(p, box, opts.padding))) continue;
    placed.push(box);
    drawn++;
  }
  return placed;
}

export function overlaps(a: PlacedLabel, b: PlacedLabel, pad = 0): boolean {
  return a.x < b.x + b.width + pad && b.x < a.x + a.width + pad &&
    a.y - a.height / 2 < b.y + b.height / 2 + pad && b.y - b.height / 2 < a.y + a.height / 2 + pad;
}
