// W4 (OKNA §4.4, §6.3, §6.4): the page side of docking and floating - the handlers WindowHost calls (menu, drag, split grips), the keys of
// the keymap (data), the live preview of a drag computed from the state the drag started from (one journal entry at the end), and the
// advisory chips of the arrangement. The chips are answered through the catalogue action advisory.answer with the code and the
// configuration fingerprint (G3b's store, the same as the „Widok” bar): "don't ask again" is remembered by the kernel and read back with
// advisory.list (after a reload too); "keep" hides the chip while the arrangement lasts. Nothing here refuses an arrangement.
import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { api } from "../../api";
import { apply, redo, undo, type Rect, type WState } from "./model";
import { closeWindows } from "./ops";
import { WINDOW_CTX } from "./session";
import type { ArrangeHandlers } from "./WindowHost";
import { ARRANGE, applyFix, arrangeWindow, moveFloating, num, rememberedKeys, setRatio, visibleChips, windowAdvisories, type Env, type WinAdvisory } from "./arrange";
import type { Box } from "./dock";
import { LAYOUTS } from "../start-view";

type Lang = "pl" | "en" | string;
const say = (w: { pl?: string; en?: string } | undefined, lang: Lang) => (lang === "pl" ? w?.pl ?? w?.en : w?.en ?? w?.pl) ?? "";
const isPanel = (w: { params?: Record<string, unknown> }) => typeof w.params?.panel === "string";

function screen(): { area: Box; keyboard: Box | null } {
  const area = { x: 0, y: 0, w: window.innerWidth, h: window.innerHeight };
  const vv = window.visualViewport;
  const bottom = vv ? vv.offsetTop + vv.height : area.h;
  return { area, keyboard: bottom < area.h - 1 ? { x: 0, y: bottom, w: area.w, h: area.h - bottom } : null };
}

async function runAction(action: string, args: Record<string, unknown> = {}): Promise<unknown> {
  const actions = api.actions;
  if (!actions) throw new Error("actions_unavailable");
  const r = await actions.run({ action, element: { class: "application" }, args });
  if (r.status !== "completed") throw new Error(r.error?.message ?? r.status);
  return r.result?.data as unknown;
}

/** After a close: when nothing is maximized and no panel is docked, the window the layout's preset maximizes (the chat) comes back to the front. */
function settle(s: WState): WState {
  const l = s.layout;
  if (l.windows.some((w) => w.mode === "maximized") || l.windows.some((w) => isPanel(w) && w.dock)) return s;
  const front = LAYOUTS[l.id]?.windows.find((w) => w.mode === "maximized")?.id;
  if (!front || !l.windows.some((w) => w.id === front)) return s;
  const r = apply(s, { op: "maximize", id: front }, WINDOW_CTX).state;
  if (r === s) return s;
  return { ...r, past: s.past, future: [] };   // the close and the front coming back are one entry
}

export interface ArrangeHook { handlers: ArrangeHandlers; chips: ReactNode; advisories: WinAdvisory[] }

export function useArrange(win: WState, winRef: { current: WState }, commit: (s: WState) => void, lang: Lang, title: (id: string) => string): ArrangeHook {
  const [scr, setScr] = useState(screen);
  useEffect(() => {
    const on = () => setScr(screen());
    window.addEventListener("resize", on); window.visualViewport?.addEventListener("resize", on);
    return () => { window.removeEventListener("resize", on); window.visualViewport?.removeEventListener("resize", on); };
  }, []);
  const [remembered, setRemembered] = useState<Set<string>>(new Set());
  const [ask, setAsk] = useState(true);
  const [kept, setKept] = useState<Set<string>>(new Set());
  const load = useCallback(async () => {
    try {
      const d = await runAction("advisory.list") as { acks?: { code: string; fingerprint?: string; choice?: string }[]; effective?: { ask?: boolean } };
      setRemembered(rememberedKeys(d)); setAsk(d?.effective?.ask !== false);
    } catch { /* no kernel actions (a static page): every advisory asks */ }
  }, []);
  useEffect(() => { void load(); }, [load]);

  const deviceClass = scr.area.w < 600 ? "phone" : "desktop";
  const env: Env = useMemo(() => ({ area: scr.area, keyboard: scr.keyboard, deviceClass, title: (w) => title(w.id) }), [scr, deviceClass, title]);
  const all = useMemo(() => windowAdvisories(win.layout, env), [win.layout, env]);
  // a "keep" lasts while its arrangement lasts: when the advisory is gone, the same arrangement made again asks again
  useEffect(() => { setKept((k) => { const live = new Set(all.map((a) => `${a.code}|${a.fingerprint}`)); const n = new Set([...k].filter((x) => live.has(x))); return n.size === k.size ? k : n; }); }, [all]);
  const chips = visibleChips(all, remembered, kept, ask);

  const base = useRef<WState | null>(null);
  const handlers: ArrangeHandlers = useMemo(() => ({
    menu: (ARRANGE.menu ?? []).map((m) => ({ id: m.id, label: say(m.label, lang), icon: m.icon })),
    onArrange: (id, how, opts) => {
      if (how === "undo" || how === "redo") { commit((how === "undo" ? undo : redo)(winRef.current).state); return; }
      commit(arrangeWindow(winRef.current, id, how, opts ?? {}));
    },
    onClose: (id) => {
      const s = winRef.current; const w = s.layout.windows.find((x) => x.id === id);
      if (!w) return;
      const closed = isPanel(w) ? closeWindows(s, { target: { panel: String(w.params.panel) } }).state : apply(s, { op: "close", id }, WINDOW_CTX).state;
      commit(settle(closed));
    },
    onMove: (id, rect: Partial<Rect>, done) => {
      const from = base.current ?? winRef.current; base.current = done ? null : from;
      commit(moveFloating(from, id, rect));
    },
    onRatio: (path, ratio, done) => {
      const from = base.current ?? winRef.current; base.current = done ? null : from;
      commit(setRatio(from, path, ratio));
    },
    dropEdge: num(ARRANGE.drop_edge, 0.25),
    dragThreshold: num(ARRANGE.drag_threshold_px, 8),
    closeLabel: (t) => (lang === "pl" ? `Zamknij: ${t}` : `Close: ${t}`),
    menuLabel: (t) => (lang === "pl" ? `Ułóż okno: ${t}` : `Arrange window: ${t}`),
  }), [commit, lang, winRef]);

  // the keys of the keymap (data): the focused window is arranged; undo/redo walk the journal
  useEffect(() => {
    const keys = ARRANGE.keymap ?? {};
    const on = (e: KeyboardEvent) => {
      const combo = [e.ctrlKey || e.metaKey ? "ctrl" : "", e.altKey ? "alt" : "", e.shiftKey ? "shift" : "", e.key.toLowerCase()].filter(Boolean).join("+");
      const how = keys[combo];
      if (!how) return;
      const focus = winRef.current.layout.focus;
      if (!focus && how !== "undo" && how !== "redo") return;
      e.preventDefault();
      handlers.onArrange(focus ?? "", how);
    };
    window.addEventListener("keydown", on);
    return () => window.removeEventListener("keydown", on);
  }, [handlers, winRef]);

  const answer = useCallback((a: WinAdvisory, choice: string) => {
    const key = `${a.code}|${a.fingerprint}`;
    if (choice === "dont_ask") setRemembered((r) => new Set([...r, key])); else setKept((k) => new Set([...k, key]));
    void runAction("advisory.answer", { code: a.code, fingerprint: a.fingerprint, answer: choice, context: { view: "windows", text: say(a.text, lang) } }).then(load, () => { /* the chip stays hidden for this page */ });
  }, [lang, load]);

  const order = ARRANGE.advisory_button_order ?? ["keep", "dont_ask", "fix"];
  const choices = ARRANGE.choices ?? [{ id: "keep", label: { pl: "Zostaw", en: "Keep it" } }, { id: "dont_ask", label: { pl: "Zostaw, nie pytaj więcej", en: "Keep it, don't ask again" } }];
  const node = chips.length ? (
    <div className="win-chips" role="status" aria-live="polite" data-testid="window-advisories">
      {chips.map((a) => {
        const buttons: ReactNode[] = [];
        for (const id of order) {
          if (id === "fix") { if (a.fix) buttons.push(<button key="fix" type="button" data-testid="window-advisory-fix" data-fix={a.fix.id} onClick={() => commit(applyFix(winRef.current, a, env))}>{say(a.fix.label, lang)}</button>); continue; }
          const c = choices.find((x) => x.id === id);
          if (c) buttons.push(<button key={id} type="button" data-testid={`window-advisory-${id}`} onClick={() => answer(a, id)}>{say(c.label, lang)}</button>);
        }
        return (
          <div key={`${a.code}|${a.fingerprint}|${a.window ?? ""}`} className="win-chip" data-testid="window-advisory" data-code={a.code} data-fingerprint={a.fingerprint}>
            <span>{say(a.text, lang)} <strong>{say(a.question, lang)}</strong></span>
            <span className="win-chip-buttons">{buttons}</span>
          </div>
        );
      })}
    </div>
  ) : null;
  return { handlers, chips: node, advisories: all };
}
