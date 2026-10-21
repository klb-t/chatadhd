import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { inertAttr, useDialogFocus } from "./use-dialog-focus";
import { api } from "../api";
import type { Conversation } from "../api/types";
import type { UserProfileHost } from "../api/onboarding-host";
import ChatView from "../components/ChatView";
import SemanticStatus from "../components/SemanticStatus";
import SemanticDot from "./SemanticDot";
import Disclosure from "./Disclosure";
import { useShellText } from "./text";
import { CHAT_MENU, menuEntries, parseMenuAction, resolveNavigatorFocus } from "./menu";
import { useLayer } from "./use-layer";
import "./shell.css";

export interface ShellActions {
  panel(id: string): void;
  /** The graph navigator at a node ("" = root). */
  navigator(focus: string): void;
  knowledge(): void;
  workshop(): void;
}
interface Props {
  profileHost: UserProfileHost;
  convId: string | null;
  onSelect: (id: string | null) => void;
  onCreated: (id: string) => void;
  refreshKey: number;
  onMessagesChanged: () => void;
  actions: ShellActions;
  planned: { windows: boolean };
  /** A panel or the knowledge sheet of the app lies over the shell: the whole shell is `inert` (no focus, no taps, hidden from a reader). */
  behindOverlay?: boolean;
}
type Sheet = "views" | "semantic" | null;

function bridge(): { openConnection?: () => void } | undefined {
  return (window as unknown as { LoomBridge?: { openConnection?: () => void } }).LoomBridge;
}

/** The default view (R56): the full-screen standard chat app. Menu entries come from data (shell/data/layouts.json, profile Czat). */
export default function ChatShell({ profileHost, convId, onSelect, onCreated, refreshKey, onMessagesChanged, actions, planned, behindOverlay = false }: Props) {
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
    <Disclosure testId="layout-windows" labels={{ show: `${t("layout_windows")} · ${t("planned")}`, hide: `${t("layout_windows")} · ${t("planned")}` }}>
      <p>{planned.windows ? t("planned_reason") : ""}</p></Disclosure>
  </>;
  if (sheet === "semantic") sheetBody = <SemanticStatus profileHost={profileHost} />;

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
            refreshKey={refreshKey} onMessagesChanged={onMessagesChanged} variant="chat" />
        </main>
        {sheet && <>
          <div className="drawer-backdrop" onClick={() => setSheet(null)} />
          <div className="drawer" data-testid={`sheet-${sheet}`} ref={sheetRef} role="dialog" aria-modal="true" tabIndex={-1}
            aria-label={t(sheet === "views" ? "views_title" : "menu_semantic")}>
            <div className="drawer-header"><h2>{t(sheet === "views" ? "views_title" : "menu_semantic")}</h2>
              <button className="icon-btn" aria-label={t("panel_close", { screen: t(sheet === "views" ? "views_title" : "menu_semantic") })} onClick={() => setSheet(null)}>×</button></div>
            <div className="drawer-body">{sheetBody}</div>
          </div>
        </>}
      </div>
    </div>
  );
}
