// W1 (OKNA §3.2): renders the active layout of the window model. A layout with a maximized window (the preset `czat`: the list collapsed,
// the chat maximized) shows that window's content full-screen — the plain chat app is this case, not a branch on the layout's name.
// Windows opened over the layout (panels, §3.3) are drawn after it, the top one visible. A layout without a maximized window is a grid of
// docks: W3 draws its dock tree (DockTree) and hands it to `grid` - the frame around it (the switcher bar of the former Workshop) - with
// the overlay to place inside it.
// W4 (OKNA §2.2, §6.3): a panel docked next to the layout's windows is drawn in its leaf inside a window frame (title bar, menu ⋯ with
// every arrangement, close); floating windows are drawn over everything else in the order of `z`. Dragging a title bar shows the drop
// zones of the leaf under the pointer (edges dock to that side, the middle makes a tab; outside any leaf the window floats); a floating
// window dragged moves (near a leaf's edge it docks). Every drag ends in ONE operation of the page (one journal entry); the preview while
// dragging is computed from the state the drag started from. Everything a drag does is also in the menu ⋯ and on the keys (accessibility).
import { useEffect, useRef, useState, type CSSProperties, type PointerEvent, type ReactNode } from "react";
import type { Rect, WState, Win } from "./model";
import { baseWindow, visibleOverlay } from "./session";
import DockTree from "./DockTree";
import { GESTURES } from "./steps";
import { dockView, dropZone, type Box, type DockNode, type Zone } from "./dock";
import { KeptScroll } from "./useKeep";

export interface ArrangeHandlers {
  /** The entries of the menu ⋯ of a window (data: layouts.json → window_arrange.menu). */
  menu: { id: string; label: string; icon?: string }[];
  onArrange: (id: string, how: string, opts?: { of?: string; rect?: Partial<Rect> }) => void;
  onClose: (id: string) => void;
  onMove: (id: string, rect: Partial<Rect>, commit: boolean) => void;
  onRatio: (path: string, ratio: number, commit: boolean) => void;
  dropEdge?: number;
  floatDropEdge?: number;
  dragThreshold?: number;
  closeLabel: (title: string) => string;
  menuLabel: (title: string) => string;
  /** W6: entries of the menu ⋯ that depend on the window (coupling: bind / unbind a parameter), and what choosing one does. */
  extraMenu?: (w: Win) => { id: string; label: string; icon?: string }[];
  onExtra?: (id: string, entry: string) => void;
  /** W6: the chain on the title bar - the bound parameters said with words (null = nothing bound, no icon). */
  chain?: (w: Win) => { icon: string; lines: string[]; title: string } | null;
  /** W7 (§5.3): a tap on the chain (gestures.json targets.chain.tap); a long press opens the menu ⋯ with the dependency choices. */
  onChainTap?: (id: string) => void;
  /** W9 (OKNA §7.2): the style of the window in its menu ⋯ - the current value ("" = inherit) and the choices; choosing one is one journal entry. */
  styleMenu?: (w: Win) => { label: string; value: string; options: { value: string; label: string }[] } | null;
  onStyle?: (id: string, value: string) => void;
}

export interface WindowHostProps {
  state: WState;
  /** The content of a window of the layout shown full-screen (kind `chat` → ChatShell). */
  renderWindow: (w: Win) => ReactNode;
  /** The content of the top window opened over the layout. */
  renderOverlay: (w: Win) => ReactNode;
  /** The frame of a layout without a maximized window: receives its drawn dock tree and the overlay node. */
  grid: (body: ReactNode, overlay: ReactNode) => ReactNode;
  /** W3: the content of a docked window (null = not drawn now: its leaf drops out and the sibling takes the space). */
  renderDocked?: (w: Win) => ReactNode;
  /** W3: whether a docked window is drawn now (e.g. the knowledge leaf only while it is open). */
  shown?: (w: Win) => boolean;
  /** W3: the title of a window on a tab of its leaf; activating a tab. */
  title?: (w: Win) => string;
  onActivate?: (w: Win) => void;
  /** W4: the content of a panel window docked or floating (the drawer shows only the maximized one). */
  renderPanel?: (w: Win) => ReactNode;
  /** W4: the arrangement handlers (drag, menu, split grips). Without them nothing is draggable (the W3 drawing). */
  arrange?: ArrangeHandlers;
  /** W4: the advisory chips (non-blocking), drawn over the layout. */
  chips?: ReactNode;
  /** W10: hidden windows kept mounted (keep.ts keptHidden under window_keep_mounted); drawn hidden, so they keep their live state. */
  keep?: ReadonlySet<string>;
}

type DragStart = (w: Win, e: PointerEvent<HTMLDivElement>) => void;
const isPanel = (w: Win) => typeof w.params?.panel === "string";
const viewport = (): Box => ({ x: 0, y: 0, w: window.innerWidth, h: window.innerHeight });
interface Drop { leaf: string; zone: Zone; box: Box }
const zoneBox = (d: Drop): Box => {
  const b = d.box;
  if (d.zone === "left") return { ...b, w: b.w / 2 };
  if (d.zone === "right") return { ...b, x: b.x + b.w / 2, w: b.w / 2 };
  if (d.zone === "top") return { ...b, h: b.h / 2 };
  if (d.zone === "bottom") return { ...b, y: b.y + b.h / 2, h: b.h / 2 };
  return b;
};
function leafBoxesOnScreen(fallbackLeaf: string | undefined): { leaf: string; box: Box }[] {
  const out: { leaf: string; box: Box }[] = [];
  document.querySelectorAll<HTMLElement>(".dock-leaf[data-leaf]").forEach((el) => {
    const r = el.getBoundingClientRect();
    if (r.width > 0 && r.height > 0) out.push({ leaf: el.dataset.leaf!, box: { x: r.left, y: r.top, w: r.width, h: r.height } });
  });
  if (!out.length && fallbackLeaf) out.push({ leaf: fallbackLeaf, box: viewport() });
  return out;
}

// W7 (§5.3, §6.3): gestures as data (gestures.json): a tap and a long press on a target; the press time is a preset.
const gestureOf = (target: string): Record<string, string> => GESTURES.targets?.[target] ?? {};
interface Press { timer: ReturnType<typeof setTimeout> | null; long: boolean }
function pressHandlers(p: Press, tap: () => void, longPress: () => void, g: Record<string, string>) {
  const ms = typeof GESTURES.long_press_ms === "number" ? GESTURES.long_press_ms : 500;
  const stop = () => { if (p.timer) clearTimeout(p.timer); p.timer = null; };
  return {
    onPointerDown: () => { p.long = false; stop(); if (g.long_press) p.timer = setTimeout(() => { p.long = true; longPress(); }, ms); },
    onPointerUp: stop,
    onPointerLeave: stop,
    onClick: () => { if (!p.long && g.tap) tap(); p.long = false; },
    onContextMenu: (e: { preventDefault: () => void }) => { if (g.long_press) { e.preventDefault(); p.long = true; longPress(); } },
  };
}

function WindowFrame({ w, title, a, children, floating, onDrag }: { w: Win; title: string; a?: ArrangeHandlers; children: ReactNode; floating: boolean; onDrag: DragStart }) {
  const [open, setOpen] = useState(false);
  const press = useRef<Press>({ timer: null, long: false });
  const key = String(w.params?.panel ?? w.kind);
  const chain = a?.chain?.(w) ?? null;
  const extra = open && a?.extraMenu ? a.extraMenu(w) : [];
  const style = open && a?.styleMenu ? a.styleMenu(w) : null;
  return (
    <section className={floating ? "win-frame win-floating" : "win-frame"} role="region" aria-label={title} data-testid={`window-${key}`} data-window={w.id} data-mode={w.mode}>
      <div className="win-titlebar" data-testid={`window-titlebar-${key}`}
        onDoubleClick={(e) => { if (!(e.target as HTMLElement).closest("button") && a && gestureOf("title").double_tap === "maximize_toggle") a.onArrange(w.id, w.mode === "maximized" ? "restore" : "maximize"); }}
        onPointerDown={(e) => { if (!(e.target as HTMLElement).closest("button")) onDrag(w, e); }}>
        <span className="win-title">{title}</span>
        {chain && <button type="button" className="win-chain" aria-label={`${chain.title}: ${chain.lines.join("; ")}`} title={chain.lines.join("\n")} data-testid={`window-chain-${key}`}
          {...pressHandlers(press.current, () => a?.onChainTap?.(w.id), () => setOpen(true), gestureOf("chain"))}>
          <span aria-hidden="true">{chain.icon}</span> <span className="win-chain-words">{chain.lines.join("; ")}</span>
        </button>}
        {a && <button type="button" className="icon-btn" aria-label={a.menuLabel(title)} aria-expanded={open} data-testid={`window-menu-${key}`} onClick={() => setOpen((v) => !v)}>⋯</button>}
        {a && <button type="button" className="icon-btn" aria-label={a.closeLabel(title)} data-testid={`window-close-${key}`} onClick={() => a.onClose(w.id)}>×</button>}
      </div>
      {open && a && (
        <div className="win-menu" role="menu" data-testid={`window-menu-list-${key}`}>
          {a.menu.map((m) => (
            <button key={m.id} type="button" role="menuitem" data-testid={`window-arrange-${m.id}`} onClick={() => { setOpen(false); a.onArrange(w.id, m.id); }}>
              {m.icon ? `${m.icon} ` : ""}{m.label}
            </button>
          ))}
          {style && <label className="win-menu-style" data-testid="window-style">{style.label}{" "}
            <select data-testid="window-style-select" aria-label={`${style.label}: ${title}`} value={style.value}
              onChange={(e) => { setOpen(false); a.onStyle?.(w.id, e.target.value); }}>
              {style.options.map((o) => <option key={o.value} value={o.value}>{o.label}</option>)}
            </select></label>}
          {extra.map((m) => (
            <button key={m.id} type="button" role="menuitem" data-testid="window-couple" data-entry={m.id} onClick={() => { setOpen(false); a.onExtra?.(w.id, m.id); }}>
              {m.icon ? `${m.icon} ` : ""}{m.label}
            </button>
          ))}
        </div>
      )}
      <KeptScroll win={w} className="win-body">{children}</KeptScroll>
    </section>
  );
}

export default function WindowHost({ state, renderWindow, renderOverlay, grid, renderDocked, shown, title, onActivate, renderPanel, arrange, chips, keep }: WindowHostProps) {
  const l = state.layout;
  const name = title ?? ((w: Win) => w.kind);
  const [drop, setDrop] = useState<Drop | null>(null);
  const drag = useRef<{ off: () => void } | null>(null);
  useEffect(() => () => drag.current?.off(), []);
  // W2: the overlay in front (maximized); others opened over the layout wait in the switcher (sheet „Okna”) and are not drawn.
  // One node, not a keyed list: the overlay element stays the same DOM node when one panel replaces another, as before W1
  const top = visibleOverlay(l);
  const overlay = top ? renderOverlay(top) : null;
  const base = baseWindow(l);

  const startDrag: DragStart = (w, e) => {
    const a = arrange; if (!a || e.button > 0) return;
    drag.current?.off();
    const x0 = e.clientX, y0 = e.clientY, area = viewport(), r0 = w.rect;
    const threshold = a.dragThreshold ?? 8;
    let moving = false, target: Drop | null = null, rect: Partial<Rect> | null = null;
    const find = (x: number, y: number, edge: number): Drop | null => {
      for (const { leaf, box } of leafBoxesOnScreen(base?.dock?.leaf)) {
        const zone = dropZone(box, x, y, edge);
        if (zone) return { leaf, zone, box };
      }
      return null;
    };
    const move = (ev: globalThis.PointerEvent) => {
      if (!moving && Math.hypot(ev.clientX - x0, ev.clientY - y0) < threshold) return;
      moving = true; ev.preventDefault();
      if (w.mode === "floating" && r0) {
        const dx = ev.clientX - x0, dy = ev.clientY - y0;
        rect = r0.unit === "px" ? { x: r0.x + dx, y: r0.y + dy } : { x: r0.x + dx / area.w, y: r0.y + dy / area.h };
        a.onMove(w.id, rect, false);
        const t = find(ev.clientX, ev.clientY, a.floatDropEdge ?? 0.08);
        target = t && t.zone !== "center" ? t : null;
      } else target = find(ev.clientX, ev.clientY, a.dropEdge ?? 0.25);
      setDrop(target);
    };
    const off = () => { window.removeEventListener("pointermove", move); window.removeEventListener("pointerup", up); window.removeEventListener("pointercancel", cancel); drag.current = null; setDrop(null); };
    const up = (ev: globalThis.PointerEvent) => {
      off();
      if (!moving) return;
      if (target) a.onArrange(w.id, target.zone === "center" ? "tab" : target.zone, { of: target.leaf });
      else if (w.mode === "floating") { if (rect) a.onMove(w.id, rect, true); }
      else {
        const fr = r0 && r0.unit === "fraction" ? r0 : { w: 0.88, h: 0.6 };
        a.onArrange(w.id, "float", { rect: { x: Math.max(0, Math.min(1 - fr.w, ev.clientX / area.w - fr.w / 2)), y: Math.max(0, Math.min(1 - fr.h, ev.clientY / area.h)), w: fr.w, h: fr.h, unit: "fraction" } });
      }
    };
    const cancel = () => { off(); if (moving && w.mode === "floating" && r0) a.onMove(w.id, r0, true); };
    window.addEventListener("pointermove", move); window.addEventListener("pointerup", up); window.addEventListener("pointercancel", cancel);
    drag.current = { off };
  };

  const framed = (w: Win, content: ReactNode, floating: boolean) => (
    <WindowFrame w={w} title={name(w)} a={arrange} floating={floating} onDrag={startDrag}>{content}</WindowFrame>
  );
  const docked = (w: Win): ReactNode => isPanel(w) ? framed(w, renderPanel?.(w) ?? null, false) : (renderDocked ?? renderWindow)(w);
  const floats = l.windows.filter((w) => w.mode === "floating" && w.rect).sort((a, b) => a.z - b.z);
  const floatLayer = floats.length ? (
    <div className="win-float-layer" data-testid="window-float-layer">
      {floats.map((w) => {
        const r = w.rect!; const u = r.unit === "px" ? "px" : "%"; const k = r.unit === "px" ? 1 : 100;
        const style: CSSProperties = { left: `${r.x * k}${u}`, top: `${r.y * k}${u}`, width: `${Math.max(0, r.w) * k}${u}`, height: `${Math.max(0, r.h) * k}${u}`, zIndex: w.z + 1 };
        return <div key={w.id} className="win-float" style={style} data-z={w.z}>{framed(w, isPanel(w) ? renderPanel?.(w) ?? null : (renderDocked ?? renderWindow)(w), true)}</div>;
      })}
    </div>
  ) : null;
  const zone = drop ? zoneBox(drop) : null;
  const dropNode = zone && drop ? <div className="win-drop" data-testid="drop-zone" data-zone={drop.zone} data-leaf={drop.leaf} style={{ left: zone.x, top: zone.y, width: zone.w, height: zone.h }} /> : null;
  // W10: kept hidden windows that are inactive tabs of a drawn leaf are drawn hidden in their leaf (DockTree); the others (minimized,
  // waiting in the switcher, in a hidden leaf) in a hidden parking box. Moving between the two remounts (React does not reparent) - the
  // state then comes back from state_keys, as under `visible`.
  const tree = !base ? dockView(l, shown) : null;
  const inLeaves = new Set<string>();
  const walkLeaves = (n: DockNode | null) => { if (!n || n.hidden) return; if (n.type === "leaf") n.windows.forEach((w) => inLeaves.add(w.id)); else { walkLeaves(n.a); walkLeaves(n.b); } };
  if (keep?.size) walkLeaves(tree);
  const parkedWins = keep?.size ? l.windows.filter((w) => keep.has(w.id) && !inLeaves.has(w.id) && w.id !== top?.id && w.id !== base?.id && !(w.mode === "floating" && w.rect)) : [];
  const parked = parkedWins.length ? (
    <div className="win-parked" hidden style={{ display: "none" }} data-testid="window-parked">
      {parkedWins.map((w) => <div key={w.id} data-window-parked={w.id}>{isPanel(w) ? renderPanel?.(w) ?? null : (renderDocked ?? renderWindow)(w)}</div>)}
    </div>
  ) : null;
  const extras = <>{floatLayer}{dropNode}{chips}{parked}</>;
  if (!base) {
    const body = tree ? <DockTree node={tree} renderWindow={docked} title={name} onActivate={onActivate} onRatio={arrange?.onRatio} keep={keep} /> : null;
    return <>{grid(body, <>{overlay}{extras}</>)}</>;
  }
  return <>{renderWindow(base)}{overlay}{extras}</>;
}
