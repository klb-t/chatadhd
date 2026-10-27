import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { inertAttr, useDialogFocus } from "./use-dialog-focus";
import { api } from "../api";
import type { Conversation } from "../api/types";
import type { UserProfileHost } from "../api/onboarding-host";
import ChatView from "../components/ChatView";
import SemanticStatus from "../components/SemanticStatus";
import DiagnosticsSheet from "../components/DiagnosticsSheet";
import SemanticDot from "./SemanticDot";
import { useShellText } from "./text";
import LayoutsSheet, { type ShellLayouts } from "./LayoutsSheet";
import { CHAT_MENU, menuEntries, parseMenuAction, resolveNavigatorFocus, type SheetId } from "./menu";
import { useLayer } from "./use-layer";
import "./shell.css";

export interface ShellActions {
  panel(id: string): void;
  /** The graph navigator at a node ("" = root). */
  navigator(focus: string): void;
  knowledge(): void;
  workshop(): void;
}
/** W2: the switcher „Okna” (sheet): the open windows, the one in front marked; a tap brings one forward, × closes one. */
export interface ShellWindows {
  entries: { id: string; label: string; front: boolean; closable: boolean }[];
  focus(id: string): void;
  close(id: string): void;
  /** W4: every arrangement without dragging (dock left/right/top/bottom, tab, float, maximize) - entries from data. */
  arrangeMenu?: { id: string; label: string; icon?: string }[];
  arrange?(id: string, how: string): void;
  /** W5: the sheet „Układ ▾” (named layouts, undo/redo, back to the shipped layout). */
  layouts?: ShellLayouts;
}
interface Props {
  profileHost: UserProfileHost;
  convId: string | null;
  onSelect: (id: string | null) => void;
  onCreated: (id: string) => void;
  /** "Pokaż w grafie" of the sheet "Co wysłaliśmy": the app opens the navigator at the focus with these perspectives. */
  onShowInGraph?: (focus: string, perspectives: string[]) => void;
  refreshKey: number;
  onMessagesChanged: () => void;
  actions: ShellActions;
  windows?: ShellWindows;
  /** A panel or the knowledge sheet of the app lies over the shell: the whole shell is `inert` (no focus, no taps, hidden from a reader). */
  behindOverlay?: boolean;
}
type Sheet = SheetId | null;

function bridge(): { openConnection?: () => void } | undefined {
  return (window as unknown as { LoomBridge?: { openConnection?: () => void } }).LoomBridge;
}

/** The default view (R56): the full-screen standard chat app. Menu entries come from data (shell/data/layouts.json, profile Czat). */
export default function ChatShell({ profileHost, convId, onSelect, onCreated, refreshKey, onMessagesChanged, actions, windows, behindOverlay = false, onShowInGraph }: Props) {
  const t = useShellText();
  const [drawer, setDrawer] = useState(false);
  const [menu, setMenu] = useState(false);
  const [sheet, setSheet] = useState<Sheet>(null);
  const [list, setList] = useState<Conversation[]>([]);
  const [query, setQuery] = useState("");
  const [title, setTitle] = useState("");

  useEffect(() => { api.listConversations(100).then(setList, () => setList([])); }, [refreshKey, convId]);
  useEffect(() => { setTitle(list.find(c => c.id === convId)?.title ?? ""); }, [list, convId]);
  const shown = useMemo(() => list.filter(c => (c.title || "").toLowerCase().includes(query.toLowerCase())), [list, query]);

  // One history entry per open layer: Android Back / the browser's Back / Escape close the ⋮ menu, the conversation drawer and the sheet.
  useLayer("shell-menu", menu, () => setMenu(false));
  useLayer("shell-drawer", drawer, () => setDrawer(false));
  useLayer("shell-sheet", sheet !== null, () => setSheet(null));
  const sheetRef = useRef<HTMLDivElement>(null);
  useDialogFocus(sheet !== null, sheetRef, "shell-menu-toggle");   // a sheet is a dialog: focus enters it, Back returns it to ⋮

  const run = useCallback((action: string) => {
    setMenu(false);
    const target = parseMenuAction(action);
    if (target.kind === "panel") actions.panel(target.panel);
    else if (target.kind === "navigator") actions.navigator(resolveNavigatorFocus(target.focus, convId));   // "Ta rozmowa w grafie" = conv:<open conversation>, else the root
    else if (target.kind === "knowledge") actions.knowledge();
    else if (target.kind === "sheet") setSheet(target.sheet);
    else if (target.kind === "workshop") actions.workshop();
    else if (target.kind === "connection") bridge()?.openConnection?.();
    else console.debug("shell menu: no handler for the action", target.action);
  }, [actions, convId]);
  const entries = menuEntries(CHAT_MENU, { androidShell: typeof bridge()?.openConnection === "function" });

  let sheetBody: ReactNode = null;
  if (sheet === "views") sheetBody = <>
    <button className="shell-row" data-testid="layout-chat" aria-pressed onClick={() => setSheet(null)}>{t("layout_chat")}</button>
    <button className="shell-row" data-testid="layout-grid" onClick={() => { setSheet(null); actions.workshop(); }}>{t("layout_grid")}</button>
  </>;
  if (sheet === "windows") sheetBody = <div data-testid="windows-sheet" role="list">
    {(windows?.entries ?? []).map(w => <div key={w.id} role="listitem" className="shell-row" style={{ display: "flex", alignItems: "center", gap: 8 }} data-testid="windows-entry" data-window={w.id} data-front={w.front ? "true" : "false"}>
      <button className="shell-row" style={{ flex: 1, textAlign: "start", border: 0, background: "transparent" }} aria-current={w.front ? "true" : undefined} data-testid="windows-entry-focus"
        onClick={() => { setSheet(null); windows?.focus(w.id); }}>{w.label}{w.front ? ` · ${t("windows_active")}` : ""}</button>
      {w.closable && <button className="icon-btn" aria-label={t("windows_close", { screen: w.label })} data-testid="windows-entry-close" onClick={() => windows?.close(w.id)}>×</button>}
      {windows?.arrange && (windows.arrangeMenu ?? []).length > 0 && <select className="windows-arrange" aria-label={w.label} data-testid="windows-entry-arrange" value=""
        onChange={e => { const how = e.target.value; if (how) { setSheet(null); windows.arrange!(w.id, how); } }}>
        <option value="">⋯</option>
        {(windows.arrangeMenu ?? []).map(m => <option key={m.id} value={m.id}>{m.icon ? `${m.icon} ` : ""}{m.label}</option>)}
      </select>}
    </div>)}
  </div>;
  if (sheet === "layouts" && windows?.layouts) sheetBody = <LayoutsSheet layouts={windows.layouts} done={() => setSheet(null)} />;
  if (sheet === "semantic") sheetBody = <SemanticStatus profileHost={profileHost} />;
  if (sheet === "diagnostics") sheetBody = <DiagnosticsSheet convId={convId} />;
  const sheetTitle = sheet === "windows" ? t("windows_title") : sheet === "layouts" ? t("layouts_title") : sheet === "views" ? t("views_title") : sheet === "diagnostics" ? t("menu_diagnostics") : t("menu_semantic");

  return (
    <div className="shell-root" data-testid="chat-shell" {...inertAttr(behindOverlay)}>
      {/* while a sheet is open the bar, the drawer and the chat behind it are inert, so Tab, a reader and a stray tap stay in the sheet */}
      <header className="shell-bar" data-testid="shell-bar" {...inertAttr(sheet !== null)}>
        <button className="icon-btn shell-btn" aria-label={t("drawer_open")} aria-expanded={drawer} data-testid="shell-drawer-toggle" onClick={() => setDrawer(v => !v)}>☰</button>
        <h1 className="shell-title" data-testid="shell-title">{title || t("new_chat")}</h1>
        <span id="shell-model-slot" />
        <SemanticDot />
        <button className="icon-btn shell-btn" aria-label={t("more")} aria-expanded={menu} aria-haspopup="menu" data-testid="shell-menu-toggle" onClick={() => setMenu(v => !v)}>⋮</button>
        {menu && <div className="shell-menu" role="menu" data-testid="shell-menu">
          {entries.map(e => <button key={e.id} role="menuitem" data-testid={`shell-menu-${e.id}`} onClick={() => run(e.action)}>{t(e.label)}</button>)}
        </div>}
      </header>
      <div className="shell-body" onClick={() => { if (menu) setMenu(false); }}>
        {drawer && <>
          <div className="drawer-backdrop" style={{ zIndex: 21 }} onClick={() => setDrawer(false)} />
          <aside className="shell-drawer" data-testid="shell-drawer" aria-label={t("conversations")} {...inertAttr(sheet !== null)}>
            <div style={{ padding: 8, display: "flex", flexDirection: "column", gap: 8 }}>
              <button className="primary shell-btn" data-testid="shell-new-chat" onClick={() => { onSelect(null); setDrawer(false); }}>{t("new_chat")}</button>
              <input type="search" aria-label={t("search")} placeholder={t("search")} value={query} onChange={e => setQuery(e.target.value)} data-testid="shell-search" />
            </div>
            <div className="shell-list">
              {shown.length === 0 && <div className="empty-state">{t("no_conversations")}</div>}
              {shown.map(c => <button key={c.id} className={`shell-row${c.id === convId ? " active" : ""}`} data-testid="shell-conv" onClick={() => { onSelect(c.id); setDrawer(false); }}>{c.title || t("untitled")}</button>)}
            </div>
          </aside>
        </>}
        <main className="shell-main" {...inertAttr(sheet !== null)}>
          <ChatView profileHost={profileHost} settingsKey="shell" convId={convId} onConversationCreated={onCreated}
            refreshKey={refreshKey} onMessagesChanged={onMessagesChanged} variant="chat" onShowInGraph={onShowInGraph} />
        </main>
        {sheet && <>
          <div className="drawer-backdrop" onClick={() => setSheet(null)} />
          <div className="drawer" data-testid={`sheet-${sheet}`} ref={sheetRef} role="dialog" aria-modal="true" tabIndex={-1}
            aria-label={sheetTitle}>
            <div className="drawer-header"><h2>{sheetTitle}</h2>
              <button className="icon-btn" aria-label={t("panel_close", { screen: sheetTitle })} onClick={() => setSheet(null)}>×</button></div>
            <div className="drawer-body">{sheetBody}</div>
          </div>
        </>}
      </div>
    </div>
  );
}
