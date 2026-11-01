// M1-W0 (2), package P4: the MAP half of the graph navigator (spec §3.2 "Mapa"). It draws the same answer as the list (the focus
// and its children from graph.navigate) through the existing view engine (view-engine/project.ts, the flat_2d renderer): the focus
// in the middle, the children around it, every channel from the setting navigator_mapping (G3d; preset: colours by kind, size by count). Every mark has an invisible 44 px touch target (a real
// button, so Back/keyboard/tests see it); a tap shows the whole label in the line under the map and opens the same card as the
// list's ⋯. One finger drags the plane, two fingers pinch, + / − step the zoom. The visible rectangle of the map is reported to
// the Android shell (window.LoomBridge.setGestureExclusion, CSS px) so the system's edge swipes do not eat the drag.
import { useEffect, useLayoutEffect, useMemo, useRef, useState, type MouseEvent as ReactMouseEvent, type PointerEvent as ReactPointerEvent } from "react";
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { ShellT } from "../shell/text";
import { project } from "../view-engine/project";
import { chipText, countText } from "./navigator-state";
import { CAMERA_HOME, HIT_SIZE_PRESET, LABEL_FONT_PX_PRESET, MAP_HEIGHT_PRESET, MAP_MARGIN_PRESET, TAP_SLOP_PRESET, ZOOM_STEP_PRESET, elementIdOf, exclusionRects, nearestMark, panBy,
  pinch, placeLabels, sameRects, screenOf, viewModelFor, zoomBy, type Camera, type Point, type Rect } from "./navigator-map";
import PersonText from "./PersonText";
import NavigatorMapControls from "./NavigatorMapControls";
import NavigatorMap3D from "./NavigatorMap3D";
import { NAV_VIS, visText, localHome, localModel, navigatorIdentity } from "./navigator-visibility";
import ViewBar from "./ViewBar";
import { useNavigatorAdvisories } from "./use-navigator-advisories";
import { useWindowParam } from "../shell/windows/WindowParams";
import { heard, wrote } from "../shell/windows/param-echo";

interface Props {
  result: GraphNavigateResult;
  /** Whether the disclosure around the map is open (a folded map reports no exclusion rectangle). */
  open: boolean;
  t: ShellT;
  /** The language of the words (for the advisories of the map). */
  language?: string;
  /** The setting navigator_mapping (channel -> what it shows) as loaded; absent = the shipped default. */
  mapping?: unknown;
  selectedAddress?: string | null;
  contextId?: string | null;
  spatialRequest?: { address: string; seq: number } | null;
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

// A label on the plane is cut to fit ("anthropic_2…"), so it cannot be split around the person's values: a label that contains one is marked
// as content as a whole (the line under the map shows the whole label, split exactly).
function personIn(row: { label?: string; person_values?: string[] } | undefined): boolean {
  return !!row?.label && !!row.person_values?.some(v => v !== "" && row.label!.includes(v));
}

export default function NavigatorMap({ result, open, t, language = "pl", mapping, selectedAddress, contextId, spatialRequest, onCard, onDrill }: Props) {
  const box = useRef<HTMLDivElement>(null);
  const spatialBox = useRef<HTMLDivElement>(null);
  const [width, setWidth] = useState(0);
  // W8 (R101 D3 gate, windows-w6 step 5): the camera STARTS at the value the window already has (restored after a reload, or given by a
  // binding), not at home - a map mounted late used to show and report its home zoom first and the window's value a moment later.
  const zoomParam = useWindowParam("zoom"), panParam = useWindowParam("pan");
  const [cam, setCam] = useState<Camera>(() => {
    const z = zoomParam.value, p = panParam.value as { x?: unknown; y?: unknown } | undefined;
    const zoom = typeof z === "number" && Number.isFinite(z) && z > 0 ? z : CAMERA_HOME.zoom;
    const pan = p && typeof p.x === "number" && typeof p.y === "number" && Number.isFinite(p.x) && Number.isFinite(p.y) ? { panX: p.x, panY: p.y } : {};
    return { ...CAMERA_HOME, zoom, ...pan };
  });
  const [selection, setSelection] = useState<string | null>(null);
  const [localView, setLocalView] = useState(localHome);
  const [dimension, setDimension] = useState<"2d" | "3d">("2d");
  const [fallback, setFallback] = useState("");
  const identity = useMemo(() => navigatorIdentity(result), [result]);
  const selected = selection === null ? null : identity.idOf(selection);
  useEffect(() => { if (selectedAddress !== undefined && selectedAddress !== null) setSelection(selectedAddress); }, [selectedAddress]);
  useEffect(() => {
    if (spatialRequest) { setSelection(spatialRequest.address); setFallback(""); setDimension("3d"); }
  }, [spatialRequest]);
  const [reported, setReported] = useState<Rect[]>([]);
  const pointers = useRef(new Map<number, Point>());
  const gesture = useRef<Gesture | null>(null);
  const dragged = useRef(false);
  useEffect(() => { dragged.current = false; gesture.current = null; pointers.current.clear(); }, [dimension]);
  const height = MAP_HEIGHT_PRESET;

  // The width follows the box (the phone's width minus the margins); the projection is recomputed for it.
  useLayoutEffect(() => {
    const node = dimension === "3d" ? spatialBox.current : box.current;
    if (!node) return;
    const measure = () => setWidth(node.clientWidth);
    measure();
    const observer = typeof ResizeObserver !== "undefined" ? new ResizeObserver(measure) : null;
    observer?.observe(node);
    return () => observer?.disconnect();
  }, [dimension]);
  // W6 (OKNA §5): zoom and pan are parameters of the window the map is drawn in. The map reports its own changes and follows a value
  // the coupling of windows gives it (a binding ×2 to another graph window, a shared channel...). The value is honoured as given (not
  // clamped: a binding asking for an unusual zoom is shown as asked). Without a window (no provider) nothing changes.
  // The echo of the map's own report is not followed (param-echo.ts: on a slow device it can arrive after a newer own change, CI 6ab55aae).
  const zoomOwn = useRef<unknown[]>([]), panOwn = useRef<unknown[]>([]);
  useEffect(() => {
    const z = zoomParam.value;
    if (typeof z !== "number" || !Number.isFinite(z) || z <= 0) return;
    const h = heard(zoomOwn.current, z); zoomOwn.current = h.queue;
    if (h.apply && Math.abs(z - cam.zoom) > 1e-9) setCam(c => ({ ...c, zoom: z }));
  }, [zoomParam.value]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    if (zoomParam.window) zoomOwn.current = wrote(zoomOwn.current, cam.zoom, zoomParam.value);
    zoomParam.set(cam.zoom);
  }, [cam.zoom]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    const p = panParam.value as { x?: unknown; y?: unknown } | undefined;
    if (!p || typeof p.x !== "number" || typeof p.y !== "number" || !Number.isFinite(p.x) || !Number.isFinite(p.y)) return;
    const h = heard(panOwn.current, p); panOwn.current = h.queue;
    if (h.apply && (p.x !== cam.panX || p.y !== cam.panY)) { const x = p.x, y = p.y; setCam(c => ({ ...c, panX: x, panY: y })); }
  }, [panParam.value]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => {
    const v = { x: cam.panX, y: cam.panY };
    if (panParam.window) panOwn.current = wrote(panOwn.current, v, panParam.value);
    panParam.set(v);
  }, [cam.panX, cam.panY]); // eslint-disable-line react-hooks/exhaustive-deps
  // A new level homes the camera; selection remains an address, including when absent from this fragment.
  // W8: not at mount - the camera starts home anyway, and a value the window restored (zoom/pan after a reload) must win over it.
  const shownAddress = useRef(result.focus.address);
  useEffect(() => {
    if (shownAddress.current === result.focus.address) return;
    shownAddress.current = result.focus.address;
    setCam(CAMERA_HOME);
  }, [result.focus.address]);

  // The visible rectangle of the open map goes to the shell whenever it can change (fold, scroll, resize, a new level).
  useEffect(() => {
    const node = dimension === "3d" ? spatialBox.current : box.current;
    if (!node) return;
    const report = () => {
      const r = node.getBoundingClientRect();
      const rects = exclusionRects({ left: r.left, top: r.top, width: r.width, height: r.height }, open, { width: window.innerWidth, height: window.innerHeight });
      setReported(prev => sameRects(prev, rects) ? prev : rects);
    };
    report();
    window.addEventListener("scroll", report, true);
    window.addEventListener("resize", report);
    const observer = typeof ResizeObserver !== "undefined" ? new ResizeObserver(report) : null;
    observer?.observe(node);
    return () => { observer?.disconnect(); window.removeEventListener("scroll", report, true); window.removeEventListener("resize", report); };
  }, [open, width, result.focus.address, dimension]);
  useEffect(() => { reportExclusion(reported); }, [reported]);
  useEffect(() => () => reportExclusion([]), []);

  const mappingKey = typeof mapping === "string" ? mapping : JSON.stringify(mapping ?? null);
  const baseModel = useMemo(() => viewModelFor(result, t("root"), mapping), [result, t, mappingKey]); // eslint-disable-line react-hooks/exhaustive-deps
  // Freeze the age clock per response. Presentation changes cannot move the data domains with elapsed time.
  const measuredAt = useMemo(() => Date.now(), [result]);
  const local = useMemo(() => localModel(baseModel, result, localView, selection, measuredAt), [baseModel, result, localView, selection, measuredAt]);
  const model = useNavigatorAdvisories(local, mapping);   // G3c: the generic advisories of the map, answered like the "Widok" bar's
  const projection = useMemo(() => width > 0 ? project(model, { width, height, margin: MAP_MARGIN_PRESET }) : null, [model, width, height]);
  useEffect(() => {
    const node = box.current;
    if (!node || dimension !== "2d" || !open) return;
    const wheel = (event: WheelEvent) => {
      event.preventDefault();
      const r = node.getBoundingClientRect();
      setCam(c => zoomBy(c, Math.exp(-event.deltaY * NAV_VIS.motion.wheel_gain), { x: width / 2, y: height / 2 }, { x: event.clientX - r.left, y: event.clientY - r.top }));
    };
    node.addEventListener("wheel", wheel, { passive: false });
    return () => node.removeEventListener("wheel", wheel);
  }, [dimension, open, width, height]);
  const rowsById = useMemo(() => {
    const m = new Map<string, Pick<GraphNodeRow, "address" | "label" | "has_children"> & Partial<GraphNodeRow>>();
    m.set(elementIdOf(result.focus.address), { address: result.focus.address, label: result.focus.label || t("root"), has_children: true, person_values: (result.focus as Partial<GraphNodeRow>).person_values, kind: result.focus.kind, provenance: result.focus.provenance, evidence: result.focus.evidence });
    for (const row of result.children) m.set(elementIdOf(row.address), row);
    return m;
  }, [result, t]);
  const centre: Point = { x: width / 2, y: height / 2 };
  const marks = useMemo(() => (projection?.marks ?? []).map(m => ({ ...m, screen: screenOf({ x: m.x, y: m.y }, cam, centre), rScreen: Math.max(3, m.r * cam.zoom) })), [projection, cam, centre.x, centre.y]); // eslint-disable-line react-hooks/exhaustive-deps
  const alphaById = useMemo(() => new Map(model.elements.map(e => [e.id, typeof e.channels.alpha === "number" ? e.channels.alpha : 1])), [model]);
  const edges = useMemo(() => {
    const placed = new Map(marks.map(m => [m.id, m]));
    const alpha = new Map(model.elements.map(e => [e.id, typeof e.channels.alpha === "number" ? e.channels.alpha : 1]));
    return model.edges.flatMap(e => {
      const a = placed.get(e.src), b = placed.get(e.dst);
      return a && b ? [{ a: a.screen, b: b.screen, alpha: Math.min(alpha.get(e.src) ?? 1, alpha.get(e.dst) ?? 1) }] : [];
    });
  }, [marks, model]); // eslint-disable-line react-hooks/exhaustive-deps
  const inside = (p: Point) => p.x >= 0 && p.x <= width && p.y >= 0 && p.y <= height;
  // Only the labels that can be read are drawn (inside the map, never over another one; the focus and the tapped mark first) — the
  // line under the map carries the whole label of the tapped mark, so a mark without a label on the plane is one tap away, not lost.
  const visibleMarks = useMemo(() => marks.filter(m => m.focus || m.id === selected || alphaById.get(m.id) !== 0), [marks, alphaById, selected]);
  const labels = useMemo(() => placeLabels(visibleMarks, { width, height }, selected), [visibleMarks, width, height, selected]);

  const localPoint = (e: { clientX: number; clientY: number }): Point => { const r = box.current!.getBoundingClientRect(); return { x: e.clientX - r.left, y: e.clientY - r.top }; };
  // A tap on a hit button goes to the mark whose centre is nearest to the finger, not to the button on top (on a dense level the
  // 44 px targets overlap and DOM order would pick the neighbour); a keyboard activation (Enter/Space: detail 0, no coordinates)
  // keeps the button's own mark.
  const tapAt = (e: ReactMouseEvent, own: string) => tap(e.detail === 0 ? own : (nearestMark(visibleMarks, localPoint(e))?.id ?? own));
  const onPointerDown = (e: ReactPointerEvent) => {
    if (e.pointerType === "mouse" && e.button !== 0) return;
    const p = localPoint(e);
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
    const p = localPoint(e);
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
    setSelection(row.address);
    if (row.address) onCard(row);
  };
  const selectedRow = selected ? rowsById.get(selected) : undefined;
  const centerSelected = () => {
    const m = marks.find(m => m.id === selected);
    if (m) setCam(c => ({ ...c, panX: c.panX + centre.x - m.screen.x, panY: c.panY + centre.y - m.screen.y }));
  };
  const fit = () => setCam(CAMERA_HOME);
  const zoomStep = (factor: number) => { setCam(c => zoomBy(c, factor, centre)); };

  return (
    <div className="gnav-map-wrap" data-testid="nav-map-wrap">
      <ViewBar model={model} language={language} compact />
      <p className="gnav-note gnav-map-scope" data-testid="nav-map-scope" data-focus-address={result.focus.address} data-selected-address={selection ?? ""} data-context-id={contextId ?? ""}>
        <PersonText text={visText("scope", { focus: result.focus.label || t("root"), selected: selectedRow?.label ?? selection ?? visText("none"), context: contextId ?? visText("none") })} person={selectedRow?.person_values ?? []} />
      </p>
      <div className="gnav-dimensions" role="group" aria-label={visText("title")}>
        <button type="button" aria-pressed={dimension === "2d"} onClick={() => setDimension("2d")}>{visText("flat")}</button>
        <button type="button" data-testid="nav-map-open-3d" aria-pressed={dimension === "3d"} onClick={() => { setFallback(""); setDimension("3d"); }}>{visText("spatial")}</button>
      </div>
      <NavigatorMapControls value={localView} maxDepth={local.local.maxDepth} mapping={mapping} report={local.mapping_report} language={language} onChange={setLocalView} />
      <p className="gnav-note" data-testid="nav-local-counts">{visText("counts", { loaded: result.children.length + 1, hidden: local.local.hidden, missing: projection?.unplaced.length ?? 0, depthMissing: local.local.depthMissing })}</p>
      {localView.system === "cartesian" && <p className="gnav-note">{(["x", "y", "z"] as const).map(axis => {
        const values = local.local.axes[axis], unit = NAV_VIS.axes[localView[axis]].unit;
        return values.length ? visText("range", { axis: axis.toUpperCase(), min: Math.min(...values).toFixed(2), max: Math.max(...values).toFixed(2), present: values.length, total: model.elements.length, unit }) : visText("unknownRange", { axis: axis.toUpperCase(), unit });
      }).join(" · ")}</p>}
      {selection !== null && !selected && <p role="status" data-testid="nav-selection-missing">{visText("selectionMissing", { address: selection })}</p>}
      {fallback && <p role="status" data-testid="nav-3d-fallback">{visText("fallback", { reason: fallback })}</p>}
      {dimension === "3d" && open && <div ref={spatialBox}>
        <p className="gnav-note">{visText(localView.system === "sector" ? "sector3d" : "xyz")}</p>
        <NavigatorMap3D model={model} selected={selected} language={language} onPick={tap}
          onUnavailable={reason => { setFallback(reason); setDimension("2d"); }} />
      </div>}
      <div hidden={dimension !== "2d"}>
      <div ref={box} className="gnav-map" data-testid="nav-map" data-zoom={cam.zoom.toFixed(3)} data-marks={marks.length}
        data-exclusion={JSON.stringify(reported)} data-mapping={mappingKey} data-system={projection?.system ?? ""} style={{ height }}
        tabIndex={0} aria-label={visText("flatHelp")}
        onKeyDown={e => {
          if (e.target !== e.currentTarget || e.altKey || e.ctrlKey || e.metaKey) return;
          const step = NAV_VIS.motion.keyboard_pan_px;
          const moves: Record<string, Point> = { ArrowLeft: { x: step, y: 0 }, ArrowRight: { x: -step, y: 0 }, ArrowUp: { x: 0, y: step }, ArrowDown: { x: 0, y: -step } };
          if (moves[e.key]) setCam(c => panBy(c, moves[e.key].x, moves[e.key].y));
          else if (e.key === "+" || e.key === "=") zoomStep(ZOOM_STEP_PRESET);
          else if (e.key === "-") zoomStep(1 / ZOOM_STEP_PRESET);
          else if (e.key === "Home") fit();
          else if (e.key.toLowerCase() === "f") centerSelected();
          else return;
          e.preventDefault(); e.stopPropagation();
        }}
        onPointerDown={onPointerDown} onPointerMove={onPointerMove} onPointerUp={onPointerUp} onPointerCancel={onPointerUp}>
        {projection && <svg className="gnav-map-svg" width={width} height={height} viewBox={`0 0 ${width} ${height}`} role="img" aria-label={t("map.aria")}>
          {edges.map((e, i) => <line key={i} x1={e.a.x} y1={e.a.y} x2={e.b.x} y2={e.b.y} stroke="currentColor" strokeOpacity={0.3 * e.alpha} strokeWidth={1} />)}
          {visibleMarks.map(m => <g key={m.id} data-testid="nav-map-mark" data-id={m.id} data-focus={m.focus ? "true" : "false"} data-selected={selected === m.id ? "true" : "false"}>
            <circle cx={m.screen.x} cy={m.screen.y} r={m.rScreen} fill={m.fill} stroke="currentColor" strokeWidth={m.focus || selected === m.id ? 2 : 0} />
          </g>)}
          {labels.map(l => <text key={l.id} data-testid="nav-map-label" data-id={l.id} data-person-content={personIn(rowsById.get(l.id)) ? "" : undefined} x={l.x} y={l.y} fontSize={LABEL_FONT_PX_PRESET} fill="currentColor" opacity={l.id === selected || l.id === model.focus ? 1 : 0.85 * (alphaById.get(l.id) ?? 1)}
            textAnchor={l.anchor}>{l.text}</text>)}
        </svg>}
        {visibleMarks.filter(m => inside(m.screen)).map(m => <button key={m.id} type="button" className="gnav-map-hit" data-testid={`nav-map-node-${m.id}`}
          aria-label={m.label} aria-pressed={selected === m.id} data-person-content={personIn(rowsById.get(m.id)) ? "" : undefined}
          style={{ left: m.screen.x - HIT_SIZE_PRESET / 2, top: m.screen.y - HIT_SIZE_PRESET / 2, width: HIT_SIZE_PRESET, height: HIT_SIZE_PRESET }}
          onClick={(e) => tapAt(e, m.id)} />)}
      </div>
      <div className="gnav-map-zoom">
        <button type="button" className="gnav-map-btn" data-testid="nav-map-zoom-in" aria-label={t("map.zoom_in")} onClick={() => zoomStep(ZOOM_STEP_PRESET)}>+</button>
        <button type="button" className="gnav-map-btn" data-testid="nav-map-zoom-out" aria-label={t("map.zoom_out")} onClick={() => zoomStep(1 / ZOOM_STEP_PRESET)}>−</button>
        <button type="button" className="gnav-map-btn gnav-map-home" data-testid="nav-map-home" aria-label={t("map.home")} onClick={fit}>⌂</button>
        <span className="gnav-map-zoom-value" data-testid="nav-map-zoom-value">{Math.round(cam.zoom * 100)}%</span>
      </div>
        <button type="button" className="gnav-map-btn gnav-map-enter" onClick={centerSelected} disabled={!marks.some(m => m.id === selected)}>{visText("center")}</button>
      <p className="gnav-note">{visText("flatHelp")}</p>
      {selected && !marks.some(m => m.id === selected) && <p role="status">{visText("noPosition")}</p>}
      </div>
      <p className="gnav-note">{visText("list")}</p>
      {selectedRow ? <div className="gnav-map-selected" data-testid="nav-map-selected" data-address={selectedRow.address}>
        <span className="gnav-label"><PersonText text={selectedRow.label} person={selectedRow.person_values ?? []} /></span>
        {selectedRow.provenance && selectedRow.evidence && selectedRow.address && <span className="gnav-count">{countText(selectedRow as GraphNodeRow, t) ?? ""}</span>}
        {selectedRow.provenance && <span className="gnav-chip">{chipText(selectedRow as GraphNodeRow, t)}</span>}
        {selectedRow.has_children && selectedRow.address && <button type="button" className="gnav-map-btn gnav-map-enter" data-testid="nav-map-enter"
          onClick={() => onDrill(selectedRow)}>{t("map.enter")} ›</button>}
      </div> : <p className="gnav-note gnav-map-hint" data-testid="nav-map-hint">{t("map.hint")}</p>}
      {projection && projection.unplaced.length > 0 && <p className="gnav-note" data-testid="nav-map-unplaced">{t("map.unplaced", { count: String(projection.unplaced.length) })}</p>}
    </div>
  );
}
