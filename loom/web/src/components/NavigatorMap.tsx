// M1-W0 (2), package P4: the MAP half of the graph navigator (spec §3.2 "Mapa"). It draws the same answer as the list (the focus
// and its children from graph.navigate) through the existing view engine (view-engine/project.ts, the flat_2d renderer): the focus
// in the middle, the children around it, colours by kind, size by count. Every mark has an invisible 44 px touch target (a real
// button, so Back/keyboard/tests see it); a tap shows the whole label in the line under the map and opens the same card as the
// list's ⋯. One finger drags the plane, two fingers pinch, + / − step the zoom. The visible rectangle of the map is reported to
// the Android shell (window.LoomBridge.setGestureExclusion, CSS px) so the system's edge swipes do not eat the drag.
import { useEffect, useLayoutEffect, useMemo, useRef, useState, type PointerEvent as ReactPointerEvent } from "react";
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ShellT } from "../shell/text";
import { project } from "../view-engine/project";
import { chipText, countText } from "./navigator-state";
import { CAMERA_HOME, HIT_SIZE_PRESET, LABEL_FONT_PX_PRESET, MAP_HEIGHT_PRESET, MAP_MARGIN_PRESET, TAP_SLOP_PRESET, ZOOM_STEP_PRESET, elementIdOf, exclusionRects, panBy, pinch,
  placeLabels, sameRects, screenOf, viewModelFor, zoomBy, type Camera, type Point, type Rect } from "./navigator-map";

interface Props {
  result: GraphNavigateResult;
  /** Whether the disclosure around the map is open (a folded map reports no exclusion rectangle). */
  open: boolean;
  t: ShellT;
  /** The same as a tap on a list row's ⋯. */
  onCard: (row: Pick<GraphNodeRow, "address" | "label">) => void;
  /** The same as a tap on a list row with children. */
  onDrill: (row: Pick<GraphNodeRow, "address" | "label">) => void;
}

type Gesture = { kind: "pan"; from: Point; cam: Camera; moved: boolean } | { kind: "pinch"; a0: Point; b0: Point; ids: [number, number]; cam: Camera };

/** The shell's bridge for the gesture exclusion (optional: a browser has none; the Android shell installs it). */
function reportExclusion(rects: Rect[]): void {
  const bridge = (window as unknown as { LoomBridge?: { setGestureExclusion?: (json: string) => void } }).LoomBridge;
  bridge?.setGestureExclusion?.(JSON.stringify(rects));
}

export default function NavigatorMap({ result, open, t, onCard, onDrill }: Props) {
  const box = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(0);
  const [cam, setCam] = useState<Camera>(CAMERA_HOME);
  const [selected, setSelected] = useState<string | null>(null);
  const [reported, setReported] = useState<Rect[]>([]);
  const pointers = useRef(new Map<number, Point>());
  const gesture = useRef<Gesture | null>(null);
  const dragged = useRef(false);
  const height = MAP_HEIGHT_PRESET;

  // The width follows the box (the phone's width minus the margins); the projection is recomputed for it.
  useLayoutEffect(() => {
    const node = box.current;
    if (!node) return;
    const measure = () => setWidth(node.clientWidth);
    measure();
    const observer = typeof ResizeObserver !== "undefined" ? new ResizeObserver(measure) : null;
    observer?.observe(node);
    return () => observer?.disconnect();
  }, []);
  // A new level: the camera goes home and the selection is dropped (the rows are different ones).
  useEffect(() => { setCam(CAMERA_HOME); setSelected(null); }, [result.focus.address]);

  // The visible rectangle of the open map goes to the shell whenever it can change (fold, scroll, resize, a new level).
  useEffect(() => {
    const node = box.current;
    if (!node) return;
    const report = () => {
      const r = node.getBoundingClientRect();
      const rects = exclusionRects({ left: r.left, top: r.top, width: r.width, height: r.height }, open, { width: window.innerWidth, height: window.innerHeight });
      setReported(prev => sameRects(prev, rects) ? prev : rects);
    };
    report();
    window.addEventListener("scroll", report, true);
    window.addEventListener("resize", report);
    return () => { window.removeEventListener("scroll", report, true); window.removeEventListener("resize", report); };
  }, [open, width, result.focus.address]);
  useEffect(() => { reportExclusion(reported); }, [reported]);
  useEffect(() => () => reportExclusion([]), []);

  const model = useMemo(() => viewModelFor(result, t("root")), [result, t]);
  const projection = useMemo(() => width > 0 ? project(model, { width, height, margin: MAP_MARGIN_PRESET }) : null, [model, width, height]);
  const rowsById = useMemo(() => {
    const m = new Map<string, Pick<GraphNodeRow, "address" | "label" | "has_children"> & Partial<GraphNodeRow>>();
    m.set(elementIdOf(result.focus.address), { address: result.focus.address, label: result.focus.label || t("root"), has_children: true, kind: result.focus.kind, provenance: result.focus.provenance, evidence: result.focus.evidence });
    for (const row of result.children) m.set(elementIdOf(row.address), row);
    return m;
  }, [result, t]);
  const centre: Point = { x: width / 2, y: height / 2 };
  const marks = useMemo(() => (projection?.marks ?? []).map(m => ({ ...m, screen: screenOf({ x: m.x, y: m.y }, cam, centre), rScreen: Math.max(3, m.r * cam.zoom) })), [projection, cam, centre.x, centre.y]); // eslint-disable-line react-hooks/exhaustive-deps
  const edges = useMemo(() => (projection?.edges ?? []).map(e => ({ a: screenOf({ x: e.x1, y: e.y1 }, cam, centre), b: screenOf({ x: e.x2, y: e.y2 }, cam, centre) })), [projection, cam, centre.x, centre.y]); // eslint-disable-line react-hooks/exhaustive-deps
  const inside = (p: Point) => p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height;
  // Only the labels that can be read are drawn (inside the map, never over another one; the focus and the tapped mark first) — the
  // line under the map carries the whole label of the tapped mark, so a mark without a label on the plane is one tap away, not lost.
  const labels = useMemo(() => placeLabels(marks, { width, height }, selected), [marks, width, height, selected]);

  const local = (e: ReactPointerEvent): Point => { const r = box.current!.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  const onPointerDown = (e: ReactPointerEvent) => {
    if (e.pointerType === "mouse" && e.button !== 0) return;
    const p = local(e);
    pointers.current.set(e.pointerId, p);
    try { box.current?.setPointerCapture(e.pointerId); } catch { /* a synthetic pointer (tests, some WebViews) cannot be captured; the move still arrives by bubbling */ }
    if (pointers.current.size === 1) { gesture.current = { kind: "pan", from: p, cam, moved: false }; dragged.current = false; }
    else if (pointers.current.size === 2) {
      const [ia, ib] = [...pointers.current.keys()] as [number, number];
      gesture.current = { kind: "pinch", a0: pointers.current.get(ia)!, b0: pointers.current.get(ib)!, ids: [ia, ib], cam };
      dragged.current = true;
    }
  };
  const onPointerMove = (e: ReactPointerEvent) => {
    if (!pointers.current.has(e.pointerId)) return;
    const p = local(e);
    pointers.current.set(e.pointerId, p);
    const g = gesture.current;
    if (!g) return;
    if (g.kind === "pan") {
      const dx = p.x - g.from.x, dy = p.y - g.from.y;
      if (!g.moved && Math.hypot(dx, dy) < TAP_SLOP_PRESET) return;
      g.moved = true; dragged.current = true;
      setCam(panBy(g.cam, dx, dy));
    } else {
      const a = pointers.current.get(g.ids[0]), b = pointers.current.get(g.ids[1]);
      if (a && b) setCam(pinch(g.cam, g.a0, g.b0, a, b, centre));
    }
  };
  const onPointerUp = (e: ReactPointerEvent) => {
    pointers.current.delete(e.pointerId);
    if (pointers.current.size === 0) { gesture.current = null; return; }
    if (pointers.current.size === 1) {
      const [id] = [...pointers.current.keys()];
      gesture.current = { kind: "pan", from: pointers.current.get(id)!, cam, moved: true };
    }
  };
  const tap = (id: string) => {
    if (dragged.current) { dragged.current = false; return; }
    const row = rowsById.get(id);
    if (!row) return;
    setSelected(id);
    if (row.address) onCard(row);
  };
  const selectedRow = selected ? rowsById.get(selected) : undefined;
  const zoomStep = (factor: number) => { setCam(c => zoomBy(c, factor, centre)); };

  return (
    <div className="gnav-map-wrap" data-testid="nav-map-wrap">
      <div ref={box} className="gnav-map" data-testid="nav-map" data-zoom={cam.zoom.toFixed(3)} data-marks={marks.length}
        data-exclusion={JSON.stringify(reported)} data-system={projection?.system ?? ""} style={{ height }}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}>
        {projection && <svg className="gnav-map-svg" width={width} height={height} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={t("map.aria")}>
          {edges.map((e, i) => <line key={i} x1={e.a.x} y1={e.a.y} x2={e.b.x} y2={e.b.y} stroke="currentColor" strokeOpacity={0.3} strokeWidth={1} />)}
          {marks.map(m => <g key={m.id} data-testid="nav-map-mark" data-id={m.id} data-focus={m.focus ? "true" : "false"} data-selected={selected === m.id ? "true" : "false"}>
            <circle cx={m.screen.x} cy={m.screen.y} r={m.rScreen} fill={m.fill} stroke="currentColor" strokeWidth={m.focus || selected === m.id ? 2 : 0} />
          </g>)}
          {labels.map(l => <text key={l.id} data-testid="nav-map-label" data-id={l.id} x={l.x} y={l.y} fontSize={LABEL_FONT_PX_PRESET} fill="currentColor" opacity={0.85}
            textAnchor={l.anchor}>{l.text}</text>)}
        </svg>}
        {marks.filter(m => inside(m.screen)).map(m => <button key={m.id} type="button" className="gnav-map-hit" data-testid={`nav-map-node-${m.id}`}
          aria-label={m.label} aria-pressed={selected === m.id}
          style={{ left: m.screen.x - HIT_SIZE_PRESET / 2, top: m.screen.y - HIT_SIZE_PRESET / 2, width: HIT_SIZE_PRESET, height: HIT_SIZE_PRESET }}
          onClick={() => tap(m.id)} />)}
      </div>
      <div className="gnav-map-zoom">
        <button type="button" className="gnav-map-btn" data-testid="nav-map-zoom-in" aria-label={t("map.zoom_in")} onClick={() => zoomStep(ZOOM_STEP_PRESET)}>+</button>
        <button type="button" className="gnav-map-btn" data-testid="nav-map-zoom-out" aria-label={t("map.zoom_out")} onClick={() => zoomStep(1 / ZOOM_STEP_PRESET)}>−</button>
        <button type="button" className="gnav-map-btn gnav-map-home" data-testid="nav-map-home" aria-label={t("map.home")} onClick={() => setCam(CAMERA_HOME)}>⌂</button>
        <span className="gnav-map-zoom-value" data-testid="nav-map-zoom-value">{Math.round(cam.zoom * 100)}%</span>
      </div>
      {selectedRow ? <div className="gnav-map-selected" data-testid="nav-map-selected" data-address={selectedRow.address}>
        <span className="gnav-label">{selectedRow.label}</span>
        {selectedRow.provenance && selectedRow.evidence && selectedRow.address && <span className="gnav-count">{countText(selectedRow as GraphNodeRow, t) ?? ""}</span>}
        {selectedRow.provenance && <span className="gnav-chip">{chipText(selectedRow as GraphNodeRow, t)}</span>}
        {selectedRow.has_children && selectedRow.address && <button type="button" className="gnav-map-btn gnav-map-enter" data-testid="nav-map-enter"
          onClick={() => onDrill(selectedRow)}>{t("map.enter")} ›</button>}
      </div> : <p className="gnav-note gnav-map-hint" data-testid="nav-map-hint">{t("map.hint")}</p>}
      {projection && projection.unplaced.length > 0 && <p className="gnav-note" data-testid="nav-map-unplaced">{t("map.unplaced", { count: String(projection.unplaced.length) })}</p>}
    </div>
  );
}
