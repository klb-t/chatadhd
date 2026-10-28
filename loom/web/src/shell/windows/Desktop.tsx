// R101 P0: the desktop layer under the windows. Always mounted (a draft of the sentence survives a window covering it); drawn where no
// window covers it (`cover`), hidden and inert under a full cover. Content from data (shell/data/desktop.json + settings): a sentence
// field routed to the chat composer, the minimized windows, every kind of window (planned/unopenable greyed with the reason, never
// hidden), the layouts. Keyboard: Tab walks the entries, the arrows move between them, focus comes here when the desktop is uncovered.
// R101 D2 (PULPIT §3.2, §4, §12, §13): the content follows the view declaration of the setting desktop_declaration (preset pulpit_graf):
// the icons of the pins (perspective pinned_on read from the kernel; the first pins written once from the declaration) in an icon_grid,
// and the person's graph behind them, faint, drawn on ONE canvas (not one DOM element per mark) and redrawn only when its data or size
// changes (the buffered background of §13). The P0 sections stay: those of sections_more behind "Więcej…" (and a long press on the
// empty desktop, the affordance of §12), the others as they were.
import { useEffect, useMemo, useRef, useState, type KeyboardEvent, type PointerEvent as RPointerEvent } from "react";
import type { WLayout } from "./model";
import type { LoomApi } from "../../api/loom-api";
import { DESKTOP, minimizedWindows, say, type DesktopPath, type DesktopSettings, type KindEntry, type KindTarget } from "./desktop";
import { DESKTOP_X, backdropLayout, iconGrid, iconOf, resolveDeclaration, type BackEdge, type BackNode, type DesktopIcon, type PinRow } from "./desktop-icons";
import { loadAround, loadPins } from "./desktop-load";

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
}

/** The faint graph behind the icons: one canvas, drawn when its data or its size changes; real nodes and real edges only. */
function GraphBackdrop({ nodes, edges, alpha, edgeAlpha, lang }: { nodes: BackNode[]; edges: BackEdge[]; alpha: number; edgeAlpha: number; lang: string }) {
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
  return <canvas ref={ref} className="desktop-graph" data-testid="desktop-graph" aria-hidden="true" data-nodes={laid.pos.size} data-edges={laid.edges.length} data-alpha={alpha} data-edge-alpha={edgeAlpha}
    title={say(DESKTOP.words, "graph_label", lang, { n: String(laid.pos.size), e: String(laid.edges.length) })} />;
}

export default function Desktop({ layout, cover, lang, settings, entries, paths: sectionPaths, layouts, title, openKind, restore, loadLayout, sentence, enableEmergency, api, deviceClass = "desktop", declaration, openIcon }: DesktopProps) {
  const w = DESKTOP.words;
  const [text, setText] = useState("");
  const [kept, setKept] = useState(false);
  const [more, setMore] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const covered = cover === "full";
  // R101 D2: the declaration, the pins and the graph behind (read again each time the desktop is uncovered: the graph is live)
  const decl = useMemo(() => resolveDeclaration(declaration, deviceClass), [declaration, deviceClass]);
  const showIcons = settings.sections.includes("icons") && !!decl.pins && !!api;
  const [pins, setPins] = useState<{ rows: PinRow[]; state: "idle" | "loading" | "ok" | "error"; error?: string }>({ rows: [], state: "idle" });
  const [around, setAround] = useState<{ nodes: BackNode[]; edges: BackEdge[] }>({ nodes: [], edges: [] });
  useEffect(() => {
    if (covered || !api) return;
    let live = true;
    if (showIcons && decl.pins) {
      setPins((p) => ({ ...p, state: p.state === "ok" ? "ok" : "loading" }));
      loadPins(api, decl.pins).then((r) => { if (live) setPins({ rows: r.rows, state: "ok" }); })
        .catch((e) => { if (live) setPins({ rows: [], state: "error", error: String((e as Error)?.message ?? e) }); });
    }
    if (decl.around) loadAround(api, decl.around, lang).then((r) => { if (live) setAround(r); }).catch((e) => console.debug("desktop: the graph behind is not read", e));
    else setAround({ nodes: [], edges: [] });
    return () => { live = false; };
  }, [covered, api, showIcons, decl, lang]);
  const icons = useMemo(() => (showIcons ? pins.rows.map((r) => iconOf(r, lang, entries, DESKTOP_X, say(w, "unavailable_cannot_open", lang))) : []), [showIcons, pins.rows, lang, entries, w]);
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
  const grid = iconGrid(icons.length, gridW || decl.grid.cellPx * 4, decl.grid);
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
  const minimized = minimizedWindows(layout);
  const moreIds = new Set(DESKTOP_X.sections_more ?? []);
  const section = (id: string) => {
    if (id === "icons") return showIcons ? (
      <div key={id} ref={gridRef} className="desktop-icons" role="group" aria-label={say(w, "icons", lang)} data-testid="desktop-icons" data-declaration={decl.id}
        data-state={pins.state} data-count={icons.length} data-columns={grid.columns}
        style={{ gridTemplateColumns: `repeat(${grid.columns}, ${Math.floor(grid.cellW)}px)` }}>
        {icons.map((ic, i) => (
          <button key={ic.node} type="button" className="desktop-icon" data-testid="desktop-icon" data-address={ic.address} data-kind={ic.kind ?? ""} data-status={ic.status}
            disabled={ic.status !== "available"} title={ic.reason ?? ic.label} aria-label={ic.reason ? `${ic.label}. ${ic.reason}` : ic.label}
            style={{ gridColumn: grid.cells[i].col + 1, gridRow: grid.cells[i].row + 1 }} onClick={() => openIcon?.(ic)}>
            <span className="desktop-icon-glyph" aria-hidden="true">{ic.glyph}</span>
            {(decl.icon?.labelLines ?? 2) > 0 && <span className="desktop-icon-label" style={{ WebkitLineClamp: decl.icon?.labelLines ?? 2 }}>{ic.label}</span>}
          </button>))}
        {pins.state === "ok" && !icons.length && <p className="desktop-hint" data-testid="desktop-advisory" data-code="desktop_empty">{say(w, "desktop_empty", lang)}</p>}
        {pins.state === "error" && <p className="desktop-hint" role="status" data-testid="desktop-advisory" data-code="desktop_pins_error">{say(w, "icons_error", lang, { error: pins.error ?? "" })}</p>}
      </div>) : null;
    if (id === "sentence") return (
      <form key={id} className="desktop-section desktop-sentence" data-testid="desktop-sentence" onSubmit={(e) => { e.preventDefault(); const v = text.trim(); if (v) { setText(""); sentence(v); } }}>
        <input aria-label={say(w, "sentence_label", lang)} placeholder={say(w, "sentence_label", lang)} value={text} onChange={(e) => setText(e.target.value)} data-testid="desktop-sentence-input" />
        <button type="submit" data-testid="desktop-sentence-send">{say(w, "sentence_send", lang)}</button>
      </form>);
    if (id === "restore") return minimized.length ? (
      <section key={id} className="desktop-section" aria-label={say(w, "restore", lang)} data-testid="desktop-restore">
        <h3>{say(w, "restore", lang)}</h3>
        <div className="desktop-items">{minimized.map((m) => <button key={m.id} type="button" data-testid="desktop-restore-item" data-window={m.id} onClick={() => restore(m.id)}>{title(m.id)}</button>)}</div>
      </section>) : null;
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
    <div ref={root} className="desktop-layer" data-testid="desktop" data-cover={cover} data-declaration={decl.id} role="region" aria-label={say(w, "title", lang)} tabIndex={-1}
      hidden={covered} aria-hidden={covered || undefined} {...(covered ? { inert: "" } : {})} onKeyDown={onKey}
      onPointerDown={onPointerDown} onPointerUp={cancelPress} onPointerLeave={cancelPress} onPointerCancel={cancelPress}
      onContextMenu={(e) => { if (emptyTarget(e.target)) { e.preventDefault(); openMore(); } }}>
      {!covered && decl.around && <GraphBackdrop nodes={around.nodes} edges={around.edges} alpha={decl.alpha} edgeAlpha={decl.edgeAlpha} lang={lang} />}
      {/* under a full cover only the layer stays mounted (its state - the draft of the sentence - stays); the entries are drawn when seen */}
      {!covered && <div className="desktop-content">
        <h2 className="desktop-title">{say(w, "title", lang)}</h2>
        <p className="desktop-hint">{say(w, "hint", lang)}</p>
        {settings.sections.filter((id) => !moreIds.has(id)).map(section)}
        {settings.sections.some((id) => moreIds.has(id)) && <>
          <button type="button" className="desktop-more" data-testid="desktop-more" aria-expanded={more} onClick={() => setMore((v) => !v)}>{say(w, more ? "less" : "more", lang)}</button>
          {more && <div className="desktop-more-panel" data-testid="desktop-more-panel">{settings.sections.filter((id) => moreIds.has(id)).map(section)}</div>}
        </>}
        {[...decl.advisories.map((a) => ({ code: a.code, text: say(w, a.code === "desktop_declaration_unknown" ? "declaration_unknown" : a.code, lang, a.vars ?? {}) })),
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
