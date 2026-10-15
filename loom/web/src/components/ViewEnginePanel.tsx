import { lazy, Suspense, useCallback, useEffect, useMemo, useRef, useState } from "react";
import { api } from "../api";
import type { InteractionBinding, InteractionBindings, ViewDeclarationInfo, ViewModel, ViewReason, ViewRendererInfo } from "../api/view-model";
import type { GraphNode } from "../api/types";
import { dataLabel, useShellLanguage, useShellText } from "../shell/text";
import { project } from "../view-engine/project";
import { LOCK_NAMES, LOCKS, MODS, applyAction, buildHint, initialLockModel, initialViewState, lockEffects, matchGesture, matchKey, neighbour, noMods, text as bindingText, unreadLocks, updateLocks } from "../view-engine/interaction";
import type { Lock, LockModel, Mod, Mods, Request } from "../view-engine/interaction";
import { buildXrHint, heldFromButtons, matchXr } from "../view-engine/xr-input";
import { sceneHintLines } from "../view-engine/scene-hint";
import type { HintContext } from "./ViewScene3D";
import type { XrPayload } from "../view-engine/scene3d";

// the 3D renderer (three.js) is a separate chunk: it is fetched only when a renderer of engine "three" is chosen
const ViewScene3D = lazy(() => import("./ViewScene3D"));

// The visualization engine, 2D renderer (P28 step 1): choose a view declaration (data) and a focus node; the kernel evaluates it into a view model
// and this panel draws it. Which encodings are promoted and which are only allowed (with the reason), and what the engine could not do, stay visible.
// Interaction (step 2) is data too: the kernel resolves the effective bindings of the keyboard and touch (presets + the user's overlay) and the panel only
// interprets them: held modifiers choose what the arrows move, locks switch alternative views (or whatever the user rebound them to), and the hint overlay
// says what the arrows do now, which locks are on and where a binding conflicts.
const W = 560, H = 460;
const INPUT_DEVICES = ["keyboard", "touch"];
const SWIPE_MIN = 30;
const isTyping = (t: EventTarget | null): boolean => t instanceof HTMLElement && (t.isContentEditable || ["INPUT", "SELECT", "TEXTAREA"].includes(t.tagName));

function reasonText(reason: ViewReason, language: string): string {
  return dataLabel(reason.text, language, reason.code);
}

export default function ViewEnginePanel() {
  const t = useShellText();
  const language = useShellLanguage();
  const [declarations, setDeclarations] = useState<ViewDeclarationInfo[] | null>(null);
  const [renderers, setRenderers] = useState<ViewRendererInfo[]>([]);
  const [rendererId, setRendererId] = useState("");
  const [selected, setSelected] = useState<string | null>(null);
  const [xrDown, setXrDown] = useState<string[]>([]);
  const [nodes, setNodes] = useState<GraphNode[]>([]);
  const [declaration, setDeclaration] = useState("");
  const [focus, setFocus] = useState("");
  const [budget, setBudget] = useState("");
  const [model, setModel] = useState<ViewModel | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);
  const [bindings, setBindings] = useState<InteractionBindings | null>(null);
  const [bindingsError, setBindingsError] = useState("");
  const [heldMods, setHeldMods] = useState<Mods>(noMods());
  const [touchMods, setTouchMods] = useState<Mods>(noMods());
  const [lockModel, setLockModel] = useState<LockModel>(initialLockModel());
  const [view, setView] = useState(initialViewState());
  const [shown, setShown] = useState(false);
  const [last, setLast] = useState<{ id: string; kind: string; label: string; note?: string } | null>(null);
  const [home, setHome] = useState("");
  const [switchedTo, setSwitchedTo] = useState<string | null>(null);

  const loadBindings = useCallback(() => {
    api.getInteractionBindings()
      .then(found => { setBindings(found); setBindingsError(""); })
      .catch(cause => { setBindings(null); setBindingsError(cause instanceof Error ? cause.message : String(cause)); });
  }, []);
  useEffect(() => { loadBindings(); }, [loadBindings]);

  useEffect(() => {
    let live = true;
    Promise.all([api.listViewDeclarations(), api.getNodes({ limit: 500 })])
      .then(([list, found]) => {
        if (!live) return;
        setDeclarations(list.declarations);
        setRenderers(list.renderers);
        setRendererId(cur => cur || list.renderers.find(r => r.entry?.implemented !== false)?.id || "");
        setNodes(found);
        setDeclaration(cur => cur || list.declarations[0]?.id || "");
        setFocus(cur => cur || found[0]?.id || "");
      })
      .catch(cause => { if (live) { setDeclarations([]); setError(cause instanceof Error ? cause.message : String(cause)); } });
    return () => { live = false; };
  }, []);

  const active = useMemo(() => (bindings?.bindings ?? []).filter(b => INPUT_DEVICES.includes(b.device)), [bindings]);
  const xrBindings = useMemo(() => (bindings?.bindings ?? []).filter(b => b.device === "xr"), [bindings]);
  const xrMods = useMemo(() => heldFromButtons(xrBindings, xrDown), [xrBindings, xrDown]);
  const mods = useMemo<Mods>(() => ({ shift: heldMods.shift || touchMods.shift || xrMods.shift, ctrl: heldMods.ctrl || touchMods.ctrl || xrMods.ctrl, alt: heldMods.alt || touchMods.alt || xrMods.alt, meta: heldMods.meta || touchMods.meta || xrMods.meta }), [heldMods, touchMods, xrMods]);
  const renderer = renderers.find(r => r.id === rendererId);
  const is3d = renderer?.entry?.engine === "three";
  const effects = useMemo(() => lockEffects(active, lockModel.state), [active, lockModel.state]);
  const known = (id: string | null): id is string => id !== null && (declarations ?? []).some(d => d.id === id);
  // an alternative view chosen by a lock (or by an action) wins over the one picked in the list, only while it lasts
  const effectiveDeclaration = known(effects.view) ? effects.view : known(switchedTo) ? switchedTo : declaration;
  const zoom = view.zoom * effects.zoom;

  const evaluate = useCallback(async () => {
    if (!effectiveDeclaration || !focus) { setError(t("view_engine_need_focus")); return; }
    setBusy(true); setError("");
    try {
      const trimmed = budget.trim();
      const entry = (declarations ?? []).find(d => d.id === effectiveDeclaration)?.entry;
      const main = (entry?.elements as Array<{ id?: string; kind?: string; role?: string; depth?: number }> | undefined)?.find(e => e.kind === "source" && e.role === "main");
      const sources = main?.id && view.depth !== 0 ? { [main.id]: { depth: Math.max(0, (typeof main.depth === "number" ? main.depth : 0) + view.depth) } } : undefined;
      setModel(await api.evaluateView({ declaration: effectiveDeclaration, focus, ...(sources ? { sources } : {}), ...(trimmed !== "" && Number.isFinite(Number(trimmed)) ? { budget: Number(trimmed) } : {}) }));
    } catch (cause) {
      setModel(null);
      setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setBusy(false);
    }
  }, [effectiveDeclaration, focus, budget, declarations, view.depth, t]);
  const show = async () => { setHome(focus); setShown(true); await evaluate(); };
  // once a view is shown, a new focus, depth or alternative view re-evaluates it
  useEffect(() => { if (shown) void evaluate(); }, [shown, effectiveDeclaration, focus, view.depth]); // eslint-disable-line react-hooks/exhaustive-deps

  const projection = useMemo(() => (model ? project(model, { width: W, height: H }) : null), [model]);

  const describe = useCallback((b: InteractionBinding) => bindingText(b.label, language, b.id), [language]);
  const perform = useCallback((b: InteractionBinding, picked?: string | null) => {
    const { state, request } = applyAction(view, b.action);
    setView(state);
    const label = describe(b);
    const handle = (r: Request): string | undefined => {
      if (r.type === "switch_view") { setSwitchedTo(r.declaration); return undefined; }
      if (r.type === "perspective") {
        const ids = (declarations ?? []).map(d => d.id);
        const at = ids.indexOf(effectiveDeclaration);
        const next = ids[(at + (r.direction < 0 ? ids.length - 1 : 1) + ids.length) % Math.max(1, ids.length)];
        if (next) setSwitchedTo(next);
        return undefined;
      }
      if (r.type === "focus") {
        if (r.target === "origin") { if (home) setFocus(home); return undefined; }
        if (r.target === "selected") { if (selected) setFocus(selected); return undefined; }
        if (r.target === "gazed") { if (picked) { setFocus(picked); return undefined; } return "gazed"; }
        if (!model || !projection) return undefined;
        const items = r.target === "time"
          ? model.elements.map(e => ({ id: e.id, value: typeof e.measures.age === "number" ? -e.measures.age : null }))
          : projection.marks.map(m => ({ id: m.id, value: r.target === "branch" ? m.x : m.y }));
        const to = neighbour(items, model.focus, r.direction);
        if (to) setFocus(to);
        return undefined;
      }
      return r.kind;
    };
    const unsupported = request ? handle(request) : undefined;
    setLast({ id: b.id, kind: b.action.kind, label, ...(unsupported ? { note: unsupported } : {}) });
  }, [view, describe, declarations, effectiveDeclaration, home, model, projection, selected]);

  // keyboard: held modifiers, lock states (getModifierState with a fallback), and the bindings
  const performRef = useRef(perform);
  performRef.current = perform;
  useEffect(() => {
    const sync = (e: KeyboardEvent) => {
      setHeldMods({ shift: e.shiftKey, ctrl: e.ctrlKey, alt: e.altKey, meta: e.metaKey });
      setLockModel(prev => updateLocks(prev, { key: e.key, type: e.type, repeat: e.repeat, read: typeof e.getModifierState === "function" ? (n: string) => e.getModifierState(n) : null }));
    };
    const down = (e: KeyboardEvent) => {
      sync(e);
      if (isTyping(e.target) || e.defaultPrevented) return;
      const held: Mods = { shift: e.shiftKey || touchMods.shift, ctrl: e.ctrlKey || touchMods.ctrl, alt: e.altKey || touchMods.alt, meta: e.metaKey || touchMods.meta };
      const b = matchKey(active, e.key, held, lockModel.state);
      if (!b || e.repeat && b.action.kind === "toggle_element") return;
      e.preventDefault();
      performRef.current(b);
    };
    const blur = () => setHeldMods(noMods());
    window.addEventListener("keydown", down);
    window.addEventListener("keyup", sync);
    window.addEventListener("blur", blur);
    return () => { window.removeEventListener("keydown", down); window.removeEventListener("keyup", sync); window.removeEventListener("blur", blur); };
  }, [active, lockModel.state, touchMods]);

  // touch: held-modifier buttons, lock buttons, swipes
  const buttons = active.filter(b => b.device === "touch" && (b.action.kind === "hold_modifier" || b.action.kind === "toggle_lock"));
  const hold = (m: Mod, on: boolean) => setTouchMods(prev => ({ ...prev, [m]: on }));
  const toggleLock = (l: Lock) => setLockModel(prev => ({ ...prev, state: { ...prev.state, [l]: !prev.state[l] } }));
  const swipeStart = useRef<{ x: number; y: number } | null>(null);
  const onPointerDown = (e: React.PointerEvent) => { if (e.pointerType !== "mouse") swipeStart.current = { x: e.clientX, y: e.clientY }; };
  const onPointerUp = (e: React.PointerEvent) => {
    const from = swipeStart.current; swipeStart.current = null;
    if (!from) return;
    const dx = e.clientX - from.x, dy = e.clientY - from.y;
    if (Math.max(Math.abs(dx), Math.abs(dy)) < SWIPE_MIN) return;
    const gesture = Math.abs(dx) >= Math.abs(dy) ? (dx < 0 ? "swipe_left" : "swipe_right") : (dy < 0 ? "swipe_up" : "swipe_down");
    const b = matchGesture(active, gesture, mods);
    if (b) perform(b);
  };

  const hint = useMemo(() => buildHint(active, bindings?.actions ?? [], mods, lockModel.state, language), [active, bindings, mods, lockModel.state, language]);
  const heldNames = MODS.filter(m => mods[m]).map(m => m[0].toUpperCase() + m.slice(1));

  // XR (P28 step 3): the scene reports pinch / trigger / thumbstick / large gestures; the same bindings and the same reducer as the keyboard decide what they do
  const xrCtx = useRef({ bindings: xrBindings, mods });
  xrCtx.current = { bindings: xrBindings, mods };
  const onXr = useCallback((p: XrPayload) => {
    const b = matchXr(xrCtx.current.bindings, p.event, xrCtx.current.mods);
    if (b) performRef.current(b, p.picked);
  }, []);
  const sceneView = useMemo(() => ({ panX: view.panX, panY: view.panY, zoom, angle: view.angle, labels: view.labels }), [view.panX, view.panY, zoom, view.angle, view.labels]);
  const hintLines = useCallback((ctx: HintContext) => sceneHintLines({
    xr: ctx.xr, flat: hint, immersive: buildXrHint(xrBindings, bindings?.actions ?? [], mods, language), heldNames, gazedSector: ctx.sector, mode: ctx.mode,
    conflicts: bindings?.conflicts.length ?? 0, unsupported: bindings?.unsupported.length ?? 0,
  }, t), [hint, xrBindings, bindings, mods, language, heldNames.join("+"), t]); // eslint-disable-line react-hooks/exhaustive-deps
  const inferred = lockModel.api ? unreadLocks(lockModel) : LOCKS;
  const recorded = (bindings?.bindings ?? []).filter(b => b.status === "recorded").length;
  const lockText = (row: (typeof hint.locks)[number]): string => {
    const state = row.on ? t("interaction_lock_on") : t("interaction_lock_off");
    const does = row.view ? t("interaction_lock_view", { view: row.view }) : row.zoom !== undefined ? t("interaction_lock_zoom", { factor: String(row.zoom) }) : row.bindings.length ? "" : t("interaction_lock_nothing");
    return `${LOCK_NAMES[row.lock]} ${state}${does ? ` \u2192 ${does}` : ""}`;
  };

  return (
    <div data-testid="view-engine-panel">
      <div data-testid="ve-hint" role="status" aria-live="polite" style={{ position: "sticky", top: 0, zIndex: 2, border: "1px solid var(--border)", borderRadius: "var(--radius)", padding: "0.4em 0.6em", marginBottom: "0.6em", background: "var(--card, var(--input-bg))", fontSize: "0.9em" }}>
        <strong>{t("interaction_title")}</strong>
        {bindingsError && <div data-testid="ve-hint-error" style={{ color: "var(--err)" }}>{t("interaction_unavailable")} {bindingsError}</div>}
        <div data-testid="ve-hint-arrows">{t("interaction_arrows_now", { what: `${heldNames.length ? `${heldNames.join("+")}: ` : ""}${hint.now ?? t("interaction_arrows_plain")}` })}{hint.arrows.length > 0 && <span data-testid="ve-hint-keys"> ({hint.arrows.map(a => `${{ ArrowLeft: "\u2190", ArrowRight: "\u2192", ArrowUp: "\u2191", ArrowDown: "\u2193" }[a.key] ?? a.key} ${a.label}`).join(" \u00b7 ")})</span>}</div>
        {hint.held.length > 0 && <div data-testid="ve-hint-held">{t("interaction_held", { mods: hint.held.map(h => `${h.mod[0].toUpperCase()}${h.mod.slice(1)} = ${h.what}`).join(" \u00b7 ") })}</div>}
        <div data-testid="ve-hint-locks">{t("interaction_locks")}: {hint.locks.map(row => <span key={row.lock} data-testid={`ve-lock-${row.lock}`} data-on={row.on ? "true" : "false"} style={{ marginRight: "0.8em", fontWeight: row.on ? 600 : 400 }}>{lockText(row)}</span>)}</div>
        {!lockModel.api && <div data-testid="ve-hint-no-api" style={{ color: "var(--warn)" }}>{t("interaction_locks_no_api")}</div>}
        {lockModel.api && inferred.length > 0 && <div data-testid="ve-hint-inferred" style={{ color: "var(--dim)" }}>{t("interaction_locks_inferred", { locks: inferred.map(l => LOCK_NAMES[l]).join(", ") })}</div>}
        {(bindings?.conflicts ?? []).map(c => <div key={c.key} data-testid="ve-hint-conflict" data-ids={c.ids.join(",")} style={{ color: "var(--warn)" }}>{t("interaction_conflict", { ids: c.ids.join(", ") })}</div>)}
        {(bindings?.unsupported.length ?? 0) > 0 && <div data-testid="ve-hint-unsupported" style={{ color: "var(--warn)" }}>{t("interaction_unsupported", { count: String(bindings?.unsupported.length ?? 0) })}: {bindings?.unsupported.map(u => `${u.binding} (${reasonText(u.reason, language)})`).join("; ")}</div>}
        {recorded > 0 && <div data-testid="ve-hint-recorded" style={{ color: "var(--dim)" }}>{t("interaction_recorded", { count: String(recorded) })}</div>}
        {buttons.length > 0 && (
          <div data-testid="ve-touch-buttons" aria-label={t("interaction_touch")} style={{ display: "flex", gap: "0.4em", marginTop: "0.3em", flexWrap: "wrap" }}>
            {buttons.map(b => b.action.kind === "hold_modifier"
              ? <button key={b.id} type="button" data-testid={`ve-touch-${String(b.input.button)}`} aria-pressed={touchMods[b.action.modifier as Mod] === true}
                  onPointerDown={() => hold(b.action.modifier as Mod, true)} onPointerUp={() => hold(b.action.modifier as Mod, false)} onPointerLeave={() => hold(b.action.modifier as Mod, false)} onPointerCancel={() => hold(b.action.modifier as Mod, false)}
                  style={{ fontWeight: touchMods[b.action.modifier as Mod] ? 700 : 400 }}>{String(b.action.modifier)}</button>
              : <button key={b.id} type="button" data-testid={`ve-touch-${String(b.input.button)}`} aria-pressed={lockModel.state[b.action.lock as Lock] === true} onClick={() => toggleLock(b.action.lock as Lock)}>{LOCK_NAMES[b.action.lock as Lock] ?? String(b.action.lock)}</button>)}
          </div>
        )}
        <div data-testid="ve-last-action" data-binding={last?.id ?? ""} data-kind={last?.kind ?? ""}>{last ? t("interaction_last", { label: last.label }) : ""}{last?.note ? ` (${last.note})` : ""}</div>
        <button type="button" onClick={loadBindings} data-testid="ve-bindings-reload" style={{ marginTop: "0.3em" }}>{t("interaction_reload")}</button>
      </div>
      <div data-testid="ve-view-state" data-pan-x={String(view.panX)} data-pan-y={String(view.panY)} data-zoom={String(Number(zoom.toFixed(4)))} data-angle={String(view.angle)} data-depth={String(view.depth)} data-labels={view.labels ? "true" : "false"} data-declaration={effectiveDeclaration} data-focus={focus} />
      <div className="form-row">
        <label htmlFor="ve-declaration">{t("view_engine_declaration")}</label>
        <select id="ve-declaration" value={effectiveDeclaration} onChange={e => { setDeclaration(e.target.value); setSwitchedTo(null); }} data-testid="ve-declaration">
          {(declarations ?? []).map(d => <option key={d.id} value={d.id}>{dataLabel(d.entry?.label, language, d.id)} ({d.id})</option>)}
        </select>
      </div>
      {renderers.length > 0 && (
        <div className="form-row">
          <label htmlFor="ve-renderer">{t("view_engine_renderer")}</label>
          <select id="ve-renderer" value={rendererId} onChange={e => setRendererId(e.target.value)} data-testid="ve-renderer">
            {renderers.map(r => <option key={r.id} value={r.id} disabled={r.entry?.implemented === false}>{dataLabel(r.entry?.label, language, r.id)} ({r.id})</option>)}
          </select>
        </div>
      )}
      <div className="form-row">
        <label htmlFor="ve-focus">{t("view_engine_focus")}</label>
        <select id="ve-focus" value={focus} onChange={e => setFocus(e.target.value)} data-testid="ve-focus">
          {nodes.map(n => <option key={n.id} value={n.id}>{n.label} ({n.kind})</option>)}
        </select>
      </div>
      <div className="form-row">
        <label htmlFor="ve-budget">{t("view_engine_budget")}</label>
        <input id="ve-budget" value={budget} onChange={e => setBudget(e.target.value)} placeholder="300" data-testid="ve-budget" />
      </div>
      <button onClick={() => void show()} disabled={busy} data-testid="ve-show">{t("view_engine_show")}</button>
      {declarations !== null && declarations.length === 0 && <p data-testid="ve-empty">{t("view_engine_empty")}</p>}
      {error && <p className="pill" style={{ color: "var(--err)" }} data-testid="ve-error">{error}</p>}
      {model && projection && (
        <div data-testid="ve-result">
          <div className="pill" data-testid="ve-counts">{t("view_engine_counts", { elements: String(model.elements.length), edges: String(model.edges.length) })}</div>
          {model.budget.collapsed > 0 && model.budget.reason && <p className="pill" data-testid="ve-budget-note">{reasonText(model.budget.reason, language)}</p>}
          {is3d && renderer ? (
            <Suspense fallback={<p className="pill" data-testid="ve3d-loading">{t("ve3d_loading")}</p>}>
              <ViewScene3D entry={renderer} model={model} view={sceneView} selected={selected} language={language} hintLines={hintLines}
                onPick={id => setSelected(id)} onXr={onXr} onXrButtons={setXrDown} />
            </Suspense>
          ) : (
            <svg viewBox={`0 0 ${W} ${H}`} width="100%" role="img" aria-label={dataLabel(model.declaration.label, language, model.declaration.id ?? "")} data-testid="ve-canvas" data-system={projection.system ?? "none"} onPointerDown={onPointerDown} onPointerUp={onPointerUp} style={{ touchAction: "none" }}>
            <g transform={`translate(${W / 2} ${H / 2}) scale(${zoom}) rotate(${view.angle}) translate(${-W / 2 - view.panX} ${-H / 2 - view.panY})`} data-testid="ve-scene">
            {projection.wedges.map(w => (
              <g key={w.id} data-testid={`ve-wedge-${w.id}`}>
                <path d={w.path} fill="currentColor" opacity={0.05} stroke="currentColor" strokeOpacity={0.25} />
                <text x={w.labelX} y={w.labelY} fontSize={11} textAnchor="middle" fill="currentColor" opacity={0.7}>{dataLabel(w.group.label, language, w.id)}</text>
              </g>
            ))}
            {projection.edges.map((e, i) => <line key={i} x1={e.x1} y1={e.y1} x2={e.x2} y2={e.y2} stroke="currentColor" strokeOpacity={0.3} strokeWidth={1} />)}
            {projection.marks.map(m => (
              <g key={m.id} data-testid="ve-mark" data-id={m.id} data-sector={m.sector ?? ""} data-x={m.x.toFixed(1)} data-y={m.y.toFixed(1)} data-focus={m.focus ? "true" : "false"}>
                <circle cx={m.x} cy={m.y} r={m.r} fill={m.fill} stroke={m.focus ? "currentColor" : "none"} strokeWidth={m.focus ? 2 : 0} style={m.blur > 0 ? { filter: `blur(${m.blur}px)` } : undefined} />
                {m.badge && <text x={m.x + m.r + 2} y={m.y - m.r} fontSize={9} fill="currentColor" opacity={0.75} data-testid="ve-badge">{m.badge}</text>}
                {view.labels && <text x={m.x + m.r + 2} y={m.y + 4} fontSize={10} fill="currentColor" data-testid="ve-label">{m.label}</text>}
                <title>{m.label}</title>
              </g>
            ))}
            </g>
            </svg>
          )}
          {is3d && selected && <p className="pill" data-testid="ve3d-selected" data-id={selected}>{t("ve3d_selected", { label: model.elements.find(e => e.id === selected)?.label ?? selected })}</p>}
          {projection.unplaced.length > 0 && (
            <p className="pill" data-testid="ve-unplaced">{t("view_engine_unplaced", { count: String(projection.unplaced.length) })}: {projection.unplaced.map(e => e.label).join(", ")}</p>
          )}
          <h3>{t("view_engine_encodings")}</h3>
          <ul style={{ listStyle: "none", padding: 0 }} data-testid="ve-encodings">
            {model.encodings.map(e => (
              <li key={e.id} data-testid={`ve-encoding-${e.id}`} data-status={e.status}>
                <strong>{e.channel}</strong> &larr; {e.measure} <span className="pill">{e.status === "promoted" ? t("view_engine_promoted") : t("view_engine_allowed")}</span>
                {e.reasons.filter(r => r.level !== "info" || e.status === "allowed").map((r, i) => <div key={i} data-testid={`ve-reason-${e.id}`} style={{ fontSize: "0.85em", opacity: 0.8 }}>{reasonText(r, language)}</div>)}
              </li>
            ))}
          </ul>
          {model.unsupported.length > 0 && (
            <>
              <h3>{t("view_engine_unsupported")}</h3>
              <ul style={{ listStyle: "none", padding: 0 }} data-testid="ve-unsupported">
                {model.unsupported.map((u, i) => (
                  <li key={i} data-testid={`ve-unsupported-${u.element}`}><code>{u.element}</code> ({u.kind}): {reasonText(u.reason, language)}</li>
                ))}
              </ul>
            </>
          )}
        </div>
      )}
    </div>
  );
}
