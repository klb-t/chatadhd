// R101 P0: the desktop layer under the windows. Always mounted (a draft of the sentence survives a window covering it); drawn where no
// window covers it (`cover`), hidden and inert under a full cover. Content from data (shell/data/desktop.json + settings): a sentence
// field routed to the chat composer, the minimized windows, every kind of window (planned/unopenable greyed with the reason, never
// hidden), the layouts. Keyboard: Tab walks the entries, the arrows move between them, focus comes here when the desktop is uncovered.
// R101 D2 (PULPIT §3.2, §4, §12, §13): the content follows the view declaration of the setting desktop_declaration (preset pulpit_graf):
// the icons of the pins (perspective pinned_on read from the kernel; the first pins written once from the declaration) in an icon_grid,
// and the person's graph behind them, faint, drawn on ONE canvas (not one DOM element per mark) and redrawn only when its data or size
// changes (the buffered background of §13). The P0 sections stay: those of sections_more behind "Więcej…" (and a long press on the
// empty desktop, the affordance of §12), the others as they were.
// R101 D3 (PULPIT §3.1, §8): the other concepts are further declarations of the same mechanism - icon sources other than the pins
// (window_kinds, minimized_windows, layouts, recent_conversations), a card of a node (pulpit_karta), the affordance a declaration asks for
// (pulpit_pusty), a background graph that follows the coupling channel focus (pulpit_graf_zywy); and the setting desktop_minimized puts
// the minimized windows on a shelf (list), on a strip of icons at the bottom, both or nowhere.
import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as RPointerEvent } from "react";
import type { WLayout } from "./model";
import type { LoomApi } from "../../api/loom-api";
import { DESKTOP, minimizedWindows, say, type DesktopPath, type DesktopSettings, type KindEntry, type KindTarget } from "./desktop";
import { DESKTOP_X, allIcons, backdropLayout, iconOf, liveFocus, minimizedMode, resolveDeclaration, type BackEdge, type BackNode, type DesktopIcon, type PinRow } from "./desktop-icons";
import type { Values } from "./coupling";
import { loadAround, loadPins } from "./desktop-load";
import { TRASH_CHANGED, trashBadge, trashStepOf, trashable } from "./desktop-trash";
import { ARRANGE, DESKTOP_ARRANGE, arrangeIcons, arrangementPatch, cellAt, moveIcon, placeIcons, readArrangement, readJournal, record, redo, undo, type ArrangeRequest, type Arrangement, type Journal } from "./desktop-arrange";

export interface DesktopProps {
  layout: WLayout;
  cover: "full" | "partial" | "none";
  lang: string;
  settings: DesktopSettings;
  entries: KindEntry[];
  paths: DesktopPath[];
  layouts: { shipped: { id: string; label: string }[]; own: { id: string; label: string }[] };
  title: (id: string) => string;
  openKind(target: KindTarget): void;
  restore(id: string): void;
  loadLayout(id: string, own: boolean): void;
  sentence(text: string): void;
  enableEmergency(): void;
  /** R101 D2: the kernel the pins and the graph behind are read from (absent: the desktop shows the P0 sections only). */
  api?: LoomApi;
  deviceClass?: string;
  /** The setting desktop_declaration: an id of desktop.json declarations, a declaration object or its JSON text; unset = the preset. */
  declaration?: unknown;
  openIcon?(icon: DesktopIcon): void;
  /** R101 D3: the coupling values (W6) a live background graph follows (declaration around.focus = {channel}). */
  coupling?: Values;
  /** R101 D3: the setting desktop_minimized (shelf | icons | both | none; unset = the preset per device class). */
  minimizedSetting?: unknown;
  /** R101 D4: the setting drop_on_trash (target | pin; unset = the preset target): what „Do kosza” on an icon moves. */
  dropOnTrash?: unknown;
  /** R101 D5: the setting desktop_arrangement ({<class>: arrangement} per person, installations.<id>.<class> per browser installation). */
  arrangement?: unknown;
  /** R101 D5: the id of this browser installation (the level `installation` of desktop_arrangement). */
  installation?: string;
  /** R101 D5: writes a merge patch of desktop_arrangement (merged on the host, as window_layout_active). */
  writeArrangement?(patch: Record<string, unknown>): void;
}

/** The faint graph behind the icons: one canvas, drawn when its data or its size changes; real nodes and real edges only. */
function GraphBackdrop({ nodes, edges, alpha, edgeAlpha, lang, focus }: { nodes: BackNode[]; edges: BackEdge[]; alpha: number; edgeAlpha: number; lang: string; focus: string }) {
  const ref = useRef<HTMLCanvasElement>(null);
  const laid = useMemo(() => backdropLayout(nodes, edges), [nodes, edges]);
  const [size, setSize] = useState({ w: 0, h: 0 });
  useEffect(() => {
    const on = () => setSize({ w: window.innerWidth, h: window.innerHeight });
    on(); window.addEventListener("resize", on); return () => window.removeEventListener("resize", on);
  }, []);
  useEffect(() => {
    const c = ref.current; if (!c || !size.w) return;
    const dpr = Math.min(2, window.devicePixelRatio || 1);
    c.width = Math.round(size.w * dpr); c.height = Math.round(size.h * dpr);
    const g = c.getContext("2d"); if (!g) return;
    g.setTransform(dpr, 0, 0, dpr, 0, 0); g.clearRect(0, 0, size.w, size.h);
    const ink = getComputedStyle(c).color || "#888";
    g.strokeStyle = ink; g.fillStyle = ink;
    g.globalAlpha = edgeAlpha; g.lineWidth = 1; g.beginPath();
    for (const e of laid.edges) { const a = laid.pos.get(e.src)!, b = laid.pos.get(e.dst)!; g.moveTo(a.x * size.w, a.y * size.h); g.lineTo(b.x * size.w, b.y * size.h); }
    g.stroke();
    g.globalAlpha = alpha;
    for (const p of laid.pos.values()) { g.beginPath(); g.arc(p.x * size.w, p.y * size.h, 3, 0, Math.PI * 2); g.fill(); }
  }, [laid, size, alpha, edgeAlpha]);
  return <canvas ref={ref} className="desktop-graph" data-testid="desktop-graph" aria-hidden="true" data-nodes={laid.pos.size} data-edges={laid.edges.length} data-alpha={alpha} data-edge-alpha={edgeAlpha} data-focus={focus}
    title={say(DESKTOP.words, "graph_label", lang, { n: String(laid.pos.size), e: String(laid.edges.length) })} />;
}

export default function Desktop({ layout, cover, lang, settings, entries, paths: sectionPaths, layouts, title, openKind, restore, loadLayout, sentence, enableEmergency, api, deviceClass = "desktop", declaration, openIcon, coupling, minimizedSetting, dropOnTrash, arrangement, installation, writeArrangement }: DesktopProps) {
  const w = DESKTOP.words;
  const [text, setText] = useState("");
  const [kept, setKept] = useState(false);
  const [more, setMore] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const covered = cover === "full";
  // R101 D2: the declaration, the pins and the graph behind (read again each time the desktop is uncovered: the graph is live)
  const decl = useMemo(() => resolveDeclaration(declaration, deviceClass), [declaration, deviceClass]);
  const showIcons = settings.sections.includes("icons") && ((!!decl.pins && !!api) || decl.sources.length > 0);
  const minimized = minimizedWindows(layout);
  const minMode = minimizedMode(minimizedSetting, deviceClass);
  // pulpit_graf_zywy: the focus of the graph behind follows the coupling channel (a navigator window's focus), else the fallback
  const liveF = decl.around ? liveFocus(decl.around, coupling, layout.windows, layout.focus ?? null) : null;
  const aroundFocus = liveF?.focus ?? "";
  const wantsRecent = decl.sources.some((x) => x.perspective === "recent_conversations");
  const [recent, setRecent] = useState<{ id: string; title: string }[]>([]);
  const [card, setCard] = useState<{ label: string; kind: string; items: { address: string; label: string }[]; state: "idle" | "ok" | "error" }>({ label: "", kind: "", items: [], state: "idle" });
  const [pins, setPins] = useState<{ rows: PinRow[]; state: "idle" | "loading" | "ok" | "error"; error?: string }>({ rows: [], state: "idle" });
  const [around, setAround] = useState<{ nodes: BackNode[]; edges: BackEdge[] }>({ nodes: [], edges: [] });
  // R101 D4: the trash icon carries the count of the trash (badge <- count); refreshed when the desktop shows and when the trash changes
  const [trashCount, setTrashCount] = useState(0);
  const [reload, setReload] = useState(0);
  const [menuFor, setMenuFor] = useState<DesktopIcon | null>(null);
  useEffect(() => {
    if (covered || !api?.actions) return;
    let live = true;
    const read = () => api.actions!.run({ action: "trash.list", element: { class: "application" }, args: {} })
      .then((r) => { const d = r.result?.data as { count?: number } | undefined; if (live && typeof d?.count === "number") setTrashCount(d.count); })
      .catch((e) => console.debug("desktop: the trash is not read", e));
    read();
    const on = () => { read(); setReload((n) => n + 1); };
    window.addEventListener(TRASH_CHANGED, on);
    return () => { live = false; window.removeEventListener(TRASH_CHANGED, on); };
  }, [covered, api]);
  // R101 D5 (PULPIT §6, §13 P3): the journal of the arrangement of this device class, kept for this tab (sessionStorage: it survives a reload)
  const cls = deviceClass;
  const jKey = `loom.desktop_arrangement_journal.${cls}`;
  const [journal, setJournal] = useState<Journal>(() => { try { return readJournal(sessionStorage.getItem(jKey)); } catch { return { past: [], future: [] }; } });
  useEffect(() => { try { setJournal(readJournal(sessionStorage.getItem(jKey))); } catch { /* storage unavailable: an empty journal */ } }, [jKey]);
  const keepJournal = (j: Journal) => { setJournal(j); try { sessionStorage.setItem(jKey, JSON.stringify(j)); } catch (e) { console.debug("desktop: the arrangement journal is kept in memory only", e); } };
  const arr = useMemo(() => readArrangement(arrangement, cls, installation), [arrangement, cls, installation]);
  const [arrNote, setArrNote] = useState<{ code: string; vars?: Record<string, string> } | null>(null);
  const writeArr = (value: Arrangement | null) => writeArrangement?.(arrangementPatch(arrangement, cls, value, installation));
  const commitArr = (op: string, after: Arrangement) => { keepJournal(record(journal, { op, before: arr.value, after, at: new Date().toISOString() })); writeArr(after); };
  const undoArr = () => { const u = undo(journal); if (!u) return; keepJournal(u.journal); writeArr(u.value); setArrNote(null); };
  const redoArr = () => { const r = redo(journal); if (!r) return; keepJournal(r.journal); writeArr(r.value); setArrNote(null); };
  const toTrash = async (ic: DesktopIcon) => {
    setMenuFor(null);
    if (!api?.actions) return;
    const st = trashStepOf(ic.address, dropOnTrash, decl.pins?.focus);
    try { await api.actions.run({ action: st.action, element: { class: "application" }, args: st.args }); } catch (e) { console.debug("desktop: to the trash failed", e); }
    window.dispatchEvent(new Event(TRASH_CHANGED));
  };
  useEffect(() => {
    if (covered || !api) return;
    let live = true;
    if (showIcons && decl.pins) {
      setPins((p) => ({ ...p, state: p.state === "ok" ? "ok" : "loading" }));
      loadPins(api, decl.pins).then((r) => { if (live) setPins({ rows: r.rows, state: "ok" }); })
        .catch((e) => { if (live) setPins({ rows: [], state: "error", error: String((e as Error)?.message ?? e) }); });
    }
    if (wantsRecent) api.listConversations(50).then((cs) => { if (live) setRecent(cs.map((c) => ({ id: c.id, title: c.title }))); }).catch((e) => console.debug("desktop: the conversations are not read", e));
    if (decl.card && api.actions?.graphNavigate) api.actions.graphNavigate({ focus: decl.card.focus, hops: 1, limit: Math.max(1, decl.card.limit), lang })
      .then((r) => { if (live) setCard({ label: r.focus.label || r.focus.address, kind: r.focus.kind, items: (r.children ?? []).slice(0, decl.card!.limit).map((c) => ({ address: c.address, label: c.label || c.address })), state: "ok" }); })
      .catch((e) => { console.debug("desktop: the card is not read", e); if (live) setCard((c) => ({ ...c, state: "error" })); });
    return () => { live = false; };
  }, [covered, api, showIcons, decl, lang, wantsRecent, reload]);
  useEffect(() => {
    if (covered || !api) return;
    let alive = true;
    if (decl.around) loadAround(api, { ...decl.around, focus: aroundFocus }, lang).then((r) => { if (alive) setAround(r); }).catch((e) => console.debug("desktop: the graph behind is not read", e));
    else setAround({ nodes: [], edges: [] });
    return () => { alive = false; };
  }, [covered, api, decl, lang, aroundFocus]);
  const cannot = say(w, "unavailable_cannot_open", lang);
  const titles = useMemo(() => minimized.map((m) => ({ id: m.id, title: title(m.id) })), [minimized.map((m) => m.id).join("|"), title]);   // eslint-disable-line react-hooks/exhaustive-deps
  const icons = useMemo(() => (showIcons ? allIcons(decl, decl.pins ? pins.rows.map((r) => iconOf(r, lang, entries, DESKTOP_X, cannot)) : [], { entries, minimized: titles, layouts, conversations: recent }, lang) : []),
    [showIcons, decl, pins.rows, lang, entries, cannot, titles, layouts, recent]);
  const tap = (ic: DesktopIcon) => { if (ic.act && "restore" in ic.act) restore(ic.act.restore); else if (ic.act && "layout" in ic.act) loadLayout(ic.act.layout, ic.act.own); else openIcon?.(ic); };
  const cardIcon = (a: { address: string; label: string }): DesktopIcon => iconOf({ node: a.address, address: a.address, nodeLabel: a.label, nodeKind: "", order: 0, labelOverride: a.label }, lang, entries, DESKTOP_X, cannot);
  const iconPaths: DesktopPath[] = icons.filter((i) => i.status === "available").map((i) => ({ section: "icons", icon: i.address }));
  const paths = iconPaths.length ? [...iconPaths, ...sectionPaths.filter((p) => !p.emergency)] : sectionPaths;
  const gridRef = useRef<HTMLDivElement>(null);
  const [gridW, setGridW] = useState(0);
  useEffect(() => {
    const el = gridRef.current; if (!el) return;
    const on = () => setGridW(el.clientWidth);
    on();
    const ro = typeof ResizeObserver !== "undefined" ? new ResizeObserver(on) : null; ro?.observe(el);
    return () => ro?.disconnect();
  }, [showIcons, covered]);
  // R101 D5: the icons placed by desktop_arrangement (hand positions snapped to the grid of this width; the others in the flow order).
  // An address shown twice (two sources) gets a key of its own for the second icon.
  const keys = useMemo(() => { const n = new Map<string, number>(); return icons.map((ic) => { const k = n.get(ic.address) ?? 0; n.set(ic.address, k + 1); return k ? `${ic.address}#${k}` : ic.address; }); }, [icons]);
  const place = placeIcons(keys, arr, gridW || decl.grid.cellPx * 4, decl.grid);
  const cellOf = new Map(place.placed.map((p) => [p.address, p]));
  const placeRef = useRef(place); placeRef.current = place;
  const arrangeBy = (by: string): { ok: boolean; code?: string; by?: string } => {
    if (!icons.length) return { ok: false, code: "arrange_no_icons" };
    const counts: Record<string, number> = { "trash:": trashCount };
    const r = arrangeIcons(arr, place, icons.map((ic, i) => ({ address: keys[i], label: ic.label, kind: ic.kind, count: counts[ic.address] })), by, lang);
    if (!r.value) { setArrNote(r.advisory ?? null); return { ok: false, code: r.advisory?.code, by }; }
    commitArr(`arrange:${by}`, r.value); setArrNote(null); setArrMenu(false);
    return { ok: true, by };
  };
  const arrangeRef = useRef(arrangeBy); arrangeRef.current = arrangeBy;
  useEffect(() => {
    const on = (e: Event) => { const d = (e as CustomEvent<ArrangeRequest>).detail; if (d) d.result = arrangeRef.current(d.by); };
    window.addEventListener(DESKTOP_ARRANGE, on);
    return () => window.removeEventListener(DESKTOP_ARRANGE, on);
  }, []);
  const [arrMenu, setArrMenu] = useState(false);
  // dragging an icon (pointer: mouse, pen or finger): past drag_threshold_px the icon follows the pointer; the drop cell is the one under it
  const drag = useRef<{ key: string; id: number; x0: number; y0: number; moved: boolean } | null>(null);
  const [dragging, setDragging] = useState<{ key: string; dx: number; dy: number } | null>(null);
  const justDragged = useRef(false);
  const dragStart = (e: RPointerEvent<HTMLButtonElement>, key: string) => {
    if (e.button !== 0) return;
    drag.current = { key, id: e.pointerId, x0: e.clientX, y0: e.clientY, moved: false };
  };
  const dragMove = (e: RPointerEvent<HTMLButtonElement>) => {
    const d = drag.current; if (!d || d.id !== e.pointerId) return;
    const dx = e.clientX - d.x0, dy = e.clientY - d.y0;
    if (!d.moved && Math.hypot(dx, dy) < Math.max(0, ARRANGE.drag_threshold_px ?? 8)) return;
    if (!d.moved) { d.moved = true; try { e.currentTarget.setPointerCapture(e.pointerId); } catch { /* capture is a nicety */ } }
    setDragging({ key: d.key, dx, dy });
  };
  const dragEnd = (e: RPointerEvent<HTMLButtonElement>) => {
    const d = drag.current; drag.current = null; setDragging(null);
    if (!d || d.id !== e.pointerId || !d.moved) return;
    justDragged.current = true; window.setTimeout(() => { justDragged.current = false; }, 0);
    const el = gridRef.current; if (!el) return;
    const r = el.getBoundingClientRect(); const pl = placeRef.current;
    const me = pl.placed.find((p) => p.address === d.key);
    const lx = e.clientX - r.left, ly = e.clientY - r.top;
    if (pl.mode === "free") {
      // the top-left corner of the icon where it was dropped, as a fraction of the area
      const x = ((me?.x ?? 0) + (e.clientX - d.x0)) / Math.max(1, pl.width), y = ((me?.y ?? 0) + (e.clientY - d.y0)) / Math.max(1, pl.height);
      commitArr("move", moveIcon(arr, pl, d.key, { x, y }));
    } else {
      const rowH = pl.rows ? r.height / pl.rows : pl.cellH;
      const to = cellAt(lx, ly, { columns: pl.columns, cellW: pl.cellW, cellH: rowH });
      if (me && me.col === to.col && me.row === to.row) return;
      if (arr.mode === "computed") setArrNote({ code: "arrange_computed_moved" });
      commitArr("move", moveIcon(arr, pl, d.key, to));
    }
  };
  // the affordance of §12: a long press (or the context menu) on the empty desktop opens "more" (every kind of window, the layouts)
  const press = useRef<number | null>(null);
  const emptyTarget = (t: EventTarget | null) => { const el = t as HTMLElement | null; return !!el && (el === root.current || el.classList.contains("desktop-content") || el.classList.contains("desktop-icons")); };
  const openMore = () => { setMore(true); window.setTimeout(() => root.current?.querySelector<HTMLElement>("[data-testid=desktop-more-panel] button:not([disabled])")?.focus(), 0); };
  const onPointerDown = (e: RPointerEvent<HTMLDivElement>) => {
    if (!emptyTarget(e.target)) return;
    if (press.current) window.clearTimeout(press.current);
    press.current = window.setTimeout(() => { press.current = null; openMore(); }, Math.max(0, DESKTOP_X.long_press_ms ?? 600));
  };
  const cancelPress = () => { if (press.current) { window.clearTimeout(press.current); press.current = null; } };
  // focus comes to the desktop when it becomes the only thing on the screen (a keyboard user is never left on a hidden control)
  const prev = useRef(cover);
  useEffect(() => {
    if (prev.current !== "none" && cover === "none") {
      const a = document.activeElement as HTMLElement | null;
      if (!a || a === document.body || !a.offsetParent) root.current?.focus();
    }
    prev.current = cover;
  }, [cover]);
  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    if (!["ArrowDown", "ArrowUp", "ArrowLeft", "ArrowRight"].includes(e.key)) return;
    const t = e.target as HTMLElement;
    if (t.tagName === "INPUT") return;
    const all = [...(root.current?.querySelectorAll<HTMLElement>("button:not([disabled]), input") ?? [])];
    const i = all.indexOf(t);
    const next = all[i + (e.key === "ArrowDown" || e.key === "ArrowRight" ? 1 : -1)];
    if (next) { e.preventDefault(); next.focus(); }
  };
  const moreIds = new Set(DESKTOP_X.sections_more ?? []);
  const section = (id: string) => {
    if (id === "icons") return showIcons ? (<div key={id} style={{ display: "contents" }}>
      <div ref={gridRef} className="desktop-icons" role="group" aria-label={say(w, "icons", lang)} data-testid="desktop-icons" data-declaration={decl.id}
        data-state={pins.state} data-count={icons.length} data-columns={place.columns} data-arrangement-mode={place.mode} data-arrangement-from={arr.from}
        style={place.mode === "free" ? { position: "relative", display: "block", height: `${Math.ceil(place.height)}px` } : { gridTemplateColumns: `repeat(${place.columns}, ${Math.floor(place.cellW)}px)` }}>
        {icons.map((ic, i) => { const c = cellOf.get(keys[i])!; const dr = dragging?.key === keys[i] ? dragging : null; return (
          <button key={ic.node} type="button" className="desktop-icon" data-testid="desktop-icon" data-address={ic.address} data-kind={ic.kind ?? ""} data-status={ic.status} data-source={ic.source ?? ""}
            disabled={ic.status !== "available"} title={ic.reason ?? ic.label} aria-label={ic.reason ? `${ic.label}. ${ic.reason}` : ic.label}
            data-col={c.col} data-row={c.row} data-pinned={c.pinned ? "true" : undefined} data-dragging={dr ? "true" : undefined}
            style={{ ...(place.mode === "free" ? { position: "absolute", left: `${Math.round(c.x)}px`, top: `${Math.round(c.y)}px`, width: `${Math.floor(place.cellW)}px` } : { gridColumn: c.col + 1, gridRow: c.row + 1 }),
              ...(dr ? { transform: `translate(${dr.dx}px, ${dr.dy}px)`, zIndex: 5, opacity: 0.85 } : {}), touchAction: "none" }}
            onPointerDown={(e) => dragStart(e, keys[i])} onPointerMove={dragMove} onPointerUp={dragEnd} onPointerCancel={() => { drag.current = null; setDragging(null); }}
            onClick={() => { if (!justDragged.current) tap(ic); }}
            onContextMenu={trashable(ic.address) && api?.actions ? (e) => { e.preventDefault(); e.stopPropagation(); setMenuFor(ic); } : undefined}
            data-badge={ic.address.startsWith("trash:") ? trashBadge(trashCount) : undefined}>
            <span className="desktop-icon-glyph" aria-hidden="true">{ic.glyph}</span>
            {ic.address.startsWith("trash:") && trashCount > 0 && <span className="desktop-icon-badge" data-testid="desktop-trash-badge" aria-label={say(w, "trash_badge", lang, { count: String(trashCount) })}>{trashBadge(trashCount)}</span>}
            {(decl.icon?.labelLines ?? 2) > 0 && <span className="desktop-icon-label" style={{ WebkitLineClamp: decl.icon?.labelLines ?? 2 }}>{ic.label}</span>}
          </button>); })}
        {menuFor && (
          <div className="desktop-icon-menu" role="menu" data-testid="desktop-icon-menu" data-address={menuFor.address}>
            <button type="button" role="menuitem" data-testid="desktop-icon-trash" onClick={() => toTrash(menuFor)}>{say(w, "icon_menu_trash", lang)}</button>
            <button type="button" role="menuitem" data-testid="desktop-icon-menu-close" onClick={() => setMenuFor(null)}>{say(w, "icon_menu_close", lang)}</button>
          </div>)}
        {(decl.pins ? pins.state === "ok" : true) && !icons.length && <p className="desktop-hint" data-testid="desktop-advisory" data-code="desktop_empty">{say(w, "desktop_empty", lang)}</p>}
        {pins.state === "error" && <p className="desktop-hint" role="status" data-testid="desktop-advisory" data-code="desktop_pins_error">{say(w, "icons_error", lang, { error: pins.error ?? "" })}</p>}
      </div>
      {/* R101 D5: arranging the icons - one journal entry per move or arrangement, undone and redone here */}
      {icons.length > 0 && <div className="desktop-arrange" data-testid="desktop-arrange" data-from={arr.from} data-mode={arr.modeWritten}>
        <button type="button" data-testid="desktop-arrange-toggle" aria-expanded={arrMenu} onClick={() => setArrMenu((v) => !v)}>{say(w, "arrange", lang)}</button>
        <button type="button" data-testid="desktop-arrange-undo" disabled={!journal.past.length} onClick={undoArr}>{say(w, "arrange_undo", lang)}</button>
        <button type="button" data-testid="desktop-arrange-redo" disabled={!journal.future.length} onClick={redoArr}>{say(w, "arrange_redo", lang)}</button>
        <span className="desktop-hint" data-testid="desktop-arrange-from">{say(w, `arrange_from_${arr.from}`, lang)} · {say(w, "arrange_move_hint", lang)}</span>
        {arrMenu && <div className="desktop-items" role="menu" data-testid="desktop-arrange-menu">
          {(ARRANGE.arrange_by ?? ["label"]).map((by) => <button key={by} type="button" role="menuitem" data-testid="desktop-arrange-by" data-by={by} onClick={() => arrangeBy(by)}>{say(w, `arrange_by_${by}`, lang)}</button>)}
        </div>}
        {[...arr.advisories, ...(arrNote ? [arrNote] : [])].map((a, i) => <div key={a.code + i} className="win-chip" role="status" data-testid="desktop-advisory" data-code={a.code}><span>{say(w, a.code, lang, a.vars ?? {})}</span></div>)}
      </div>}
    </div>) : null;
    if (id === "sentence") return (
      <form key={id} className="desktop-section desktop-sentence" data-testid="desktop-sentence" onSubmit={(e) => { e.preventDefault(); const v = text.trim(); if (v) { setText(""); sentence(v); } }}>
        <input aria-label={say(w, "sentence_label", lang)} placeholder={say(w, "sentence_label", lang)} value={text} onChange={(e) => setText(e.target.value)} data-testid="desktop-sentence-input" />
        <button type="submit" data-testid="desktop-sentence-send">{say(w, "sentence_send", lang)}</button>
      </form>);
    if (id === "restore") return minimized.length ? (<div key={id} style={{ display: "contents" }}>
      {(minMode.mode === "shelf" || minMode.mode === "both") && <section className="desktop-section" aria-label={say(w, "restore", lang)} data-testid="desktop-restore">
        <h3>{say(w, "restore", lang)}</h3>
        <div className="desktop-items">{minimized.map((m) => <button key={m.id} type="button" data-testid="desktop-restore-item" data-window={m.id} onClick={() => restore(m.id)}>{title(m.id)}</button>)}</div>
      </section>}
      {/* PULPIT §8: icons = a strip of the minimized windows at the bottom of the desktop (a taskbar); a tap restores */}
      {(minMode.mode === "icons" || minMode.mode === "both") && <nav className="desktop-minimized-strip" aria-label={say(w, "minimized_icons", lang)} data-testid="desktop-minimized">
        {minimized.map((m) => { const a = `window:${m.id}`; return <button key={m.id} type="button" className="desktop-icon desktop-icon-small" data-testid="desktop-minimized-icon" data-window={m.id} data-address={a} onClick={() => restore(m.id)}>
          <span className="desktop-icon-glyph" aria-hidden="true">{DESKTOP_X.glyphs?.["window:"] ?? "▭"}</span><span className="desktop-icon-label">{title(m.id)}</span></button>; })}
      </nav>}
    </div>) : null;
    if (id === "open") return (
      <section key={id} className="desktop-section" aria-label={say(w, "open", lang)} data-testid="desktop-open">
        <h3>{say(w, "open", lang)}</h3>
        <div className="desktop-items">{entries.map((e) => (
          <button key={e.kind} type="button" data-testid="desktop-open-kind" data-kind={e.kind} data-status={e.status} disabled={e.status !== "available"}
            title={e.reason} aria-describedby={e.reason ? `desktop-why-${e.kind}` : undefined} onClick={() => e.target && openKind(e.target)}>
            {e.label}{e.reason && <span className="sr-only" id={`desktop-why-${e.kind}`}>{e.reason}</span>}
          </button>))}</div>
      </section>);
    if (id === "layouts") return (
      <section key={id} className="desktop-section" aria-label={say(w, "layouts", lang)} data-testid="desktop-layouts">
        <h3>{say(w, "layouts", lang)}</h3>
        <div className="desktop-items">
          {layouts.shipped.map((l) => <button key={"s:" + l.id} type="button" data-testid="desktop-layout" data-layout={l.id} onClick={() => loadLayout(l.id, false)}>{l.label}</button>)}
          {layouts.own.map((l) => <button key={"o:" + l.id} type="button" data-testid="desktop-layout" data-layout={l.id} data-own="true" onClick={() => loadLayout(l.id, true)}>{say(w, "layouts_own", lang)}: {l.label}</button>)}
        </div>
      </section>);
    return null;
  };
  const emergency = paths.find((p) => p.emergency);
  const noPath = !paths.length;
  return (
    <div ref={root} className="desktop-layer" data-testid="desktop" data-cover={cover} data-declaration={decl.id} data-minimized={minMode.mode} data-focus-from={liveF?.from ?? ""} role="region" aria-label={say(w, "title", lang)} tabIndex={-1}
      hidden={covered} aria-hidden={covered || undefined} {...(covered ? { inert: "" } : {})} onKeyDown={onKey}
      onPointerDown={onPointerDown} onPointerUp={cancelPress} onPointerLeave={cancelPress} onPointerCancel={cancelPress}
      onContextMenu={(e) => { if (emptyTarget(e.target)) { e.preventDefault(); openMore(); } }}>
      {!covered && decl.around && <GraphBackdrop nodes={around.nodes} edges={around.edges} alpha={decl.alpha} edgeAlpha={decl.edgeAlpha} lang={lang} focus={aroundFocus} />}
      {/* under a full cover only the layer stays mounted (its state - the draft of the sentence - stays); the entries are drawn when seen */}
      {!covered && <div className="desktop-content">
        <h2 className="desktop-title">{say(w, "title", lang)}</h2>
        <p className="desktop-hint">{say(w, "hint", lang)}</p>
        {decl.card && <section className="desktop-section desktop-card" data-testid="desktop-card" data-focus={decl.card.focus} data-state={card.state} aria-label={say(w, "card_label", lang, { label: card.label || decl.card.focus })}>
          <h3>{card.label || decl.card.focus}</h3>
          {card.state === "ok" && !card.items.length && <p className="desktop-hint">{say(w, "card_empty", lang)}</p>}
          <div className="desktop-items">{card.items.map((it) => { const ic = cardIcon(it); return <button key={it.address} type="button" data-testid="desktop-card-item" data-address={it.address} disabled={ic.status !== "available"} title={ic.reason} onClick={() => tap(ic)}>{it.label}</button>; })}</div>
        </section>}
        {settings.sections.filter((id) => !moreIds.has(id)).map(section)}
        {settings.sections.some((id) => moreIds.has(id)) && <>
          <button type="button" className="desktop-more" data-testid="desktop-more" aria-expanded={more} onClick={() => setMore((v) => !v)}>{say(w, more ? "less" : "more", lang)}</button>
          {more && <div className="desktop-more-panel" data-testid="desktop-more-panel">{settings.sections.filter((id) => moreIds.has(id)).map(section)}</div>}
        </>}
        {[...(minMode.advisory ? [{ code: minMode.advisory.code, text: say(w, "minimized_unknown", lang, minMode.advisory.vars ?? {}) }] : []), ...decl.advisories.map((a) => ({ code: a.code, text: say(w, a.code === "desktop_declaration_unknown" ? "declaration_unknown" : a.code, lang, a.vars ?? {}) })),
          ...decl.unsupported.map((u) => ({ code: "desktop_element_unsupported", text: say(w, "element_unsupported", lang, { id: u.id, kind: u.kind }) }))].map((a, i) =>
          <div key={a.code + i} className="win-chip" role="status" data-testid="desktop-advisory" data-code={a.code}><span>{a.text}</span></div>)}
        {emergency && <button type="button" className="desktop-emergency" data-testid="desktop-emergency" onClick={() => openKind({ chat: true })}>{say(w, "emergency", lang)}</button>}
        {noPath && !kept && <div className="win-chip" role="status" data-testid="desktop-advisory" data-code="desktop_no_path">
          <span>{say(w, "no_path", lang)}</span>
          <span className="win-chip-buttons">
            <button type="button" data-testid="desktop-advisory-fix" onClick={enableEmergency}>{say(w, "no_path_fix", lang)}</button>
            <button type="button" data-testid="desktop-advisory-keep" onClick={() => setKept(true)}>{say(w, "keep", lang)}</button>
          </span>
        </div>}
        {settings.unknownSections.map((id) => <div key={id} className="win-chip" role="status" data-testid="desktop-advisory" data-code="desktop_section_unknown"><span>{say(w, "section_unknown", lang, { id })}</span></div>)}
      </div>}
    </div>
  );
}

/** The end screen of last_window_close=close_app on a host that cannot (or may refuse to) close itself. */
export function DesktopEnded({ lang, back }: { lang: string; back(): void }) {
  const w = DESKTOP.words;
  return (
    <div className="desktop-layer desktop-ended" data-testid="app-ended" role="alertdialog" aria-label={say(w, "ended_title", lang)}>
      <div className="desktop-content">
        <h2 className="desktop-title">{say(w, "ended_title", lang)}</h2>
        <p data-testid="app-ended-advisory" data-code="exit_unavailable">{say(w, "ended_text", lang)}</p>
        <button type="button" data-testid="app-ended-back" onClick={back} autoFocus>{say(w, "ended_back", lang)}</button>
      </div>
    </div>
  );
}
