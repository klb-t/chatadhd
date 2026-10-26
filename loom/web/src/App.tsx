import { useCallback, useEffect, useMemo, useRef, useState, type ReactNode } from "react";
import { inertAttr, useDialogFocus } from "./shell/use-dialog-focus";
import ApplicationProfiles, { profileStyle } from "./components/ApplicationProfiles";
import { profileAvailability, registerProfile, type ApplicationProfile } from "./profiles/runtime";
import ConversationList from "./components/ConversationList";
import GraphView from "./components/GraphView";
import SettingsHuman from "./components/SettingsHuman";
import SettingsPanel from "./components/SettingsPanel";
import { OPEN_PANEL_EVENT, panelOfRequest } from "./context/open-settings";
import ContextSlider from "./components/ContextSlider";
import SemanticStatus from "./components/SemanticStatus";
import ImportPanel from "./components/ImportPanel";
import MemoryPanel from "./components/MemoryPanel";
import LogPanel from "./components/LogPanel";
import KnowledgeWorkbench from "./components/KnowledgeWorkbench";
import OperationsPanel from "./components/OperationsPanel";
import MethodsPanel from "./components/MethodsPanel";
import AnalysisPanel from "./components/AnalysisPanel";
import UserProfilePanel from "./components/UserProfilePanel";
import GraphEntryPanel, { type EntryOpeners } from "./components/GraphEntryPanel";
import GraphNavigator from "./components/GraphNavigator";
import { createUserProfileHost } from "./api/onboarding-host";
import { api } from "./api";
import { usePresentationFeature } from "./components/ConversationSourceView";
import { featureMessage } from "./onboarding/presentation.mjs";
import { useAccountSession } from "./components/AuthGate";
import { createLoomProfileRegistry } from "./profiles/loom-adapter";
import ChatShell from "./shell/ChatShell";
import PerspectivesPanel from "./components/PerspectivesPanel";
import ViewEnginePanel from "./components/ViewEnginePanel";
import { ShellLanguage, ShellText, shellTranslator } from "./shell/text";
import { themeFromConfig } from "./shell/theme";
import { DEFAULT_LAYOUT, JOURNAL_LIMIT, LAYOUTS, START_VIEW_PRESET, layoutOfMode, parseShellHash, readSaved, resolveLayoutId, resolveStartView, startViewOrder, writeSaved } from "./shell/start-view";
import WindowHost from "./shell/windows/WindowHost";
import { activePanelOf, baseWindow, layoutState, setPanel, switchLayout, visibleOverlay } from "./shell/windows/session";
import { createState, type WState } from "./shell/windows/model";
import { WINDOW_DATA, closeWindows, focusWindow, runWindowStep, switcherEntries, visibleId, type WindowReceipt } from "./shell/windows/ops";
import { WINDOW_STEP_EVENT, type WindowStepDetail } from "./context/browser-host";
import { layerStack } from "./shell/history";
import { shellLanguage } from "./shell/language";
import { cssVariables, mergeData } from "./shell/animation";
import { glyphVariables } from "./shell/disclosure-glyphs";
import { PANEL_LABELS, PANEL_TEXT_IDS, type PanelId } from "./shell/panels";
import { useLayer } from "./shell/use-layer";

type Theme = "dark" | "amoled";
// The panels are one list (shell/panels.ts): the + menu checks the panel an entry names against it.

function readStored(key: string, fallback: string): string {
  try {
    return localStorage.getItem(key) ?? fallback;
  } catch {
    return fallback;
  }
}

function writeStored(key: string, value: string): void {
  try {
    localStorage.setItem(key, value);
  } catch {
    // ignore (private mode / storage disabled)
  }
}

export default function App() {
  const account = useAccountSession();
  const [theme, setTheme] = useState<Theme>(() => (readStored("loom.theme", "dark") as Theme) || "dark");
  const [sidebarOpen, setSidebarOpen] = useState<boolean>(() =>
    typeof window !== "undefined" ? window.innerWidth > 720 : true,
  );
  const [activeConvId, setActiveConvId] = useState<string | null>(null);
  // W1 (OKNA §3): the screen is a layout of the window model. A panel is a window opened maximized over the layout (journaled operations);
  // `activePanel` is read from the window state. R56: a deep link (#czat, #warsztat, #uklad/<id>) states the layout; otherwise start_view decides.
  const [win, setWin] = useState<WState>(() => layoutState(LAYOUTS[parseShellHash(window.location.hash)?.layout ?? DEFAULT_LAYOUT], JOURNAL_LIMIT));
  const [layoutKnown, setLayoutKnown] = useState(() => parseShellHash(window.location.hash) !== null);
  const activePanel = activePanelOf(win) as PanelId | null;
  const setActivePanel = useCallback((next: PanelId | null | ((cur: PanelId | null) => PanelId | null)) =>
    setWin(s => setPanel(s, next as string | null | ((cur: string | null) => string | null))), []);
  const layout = layoutKnown ? win.layout.id : null;
  const setLayout = useCallback((name: string) => {
    const id = resolveLayoutId(name);
    setWin(s => (s.layout.id === id ? s : switchLayout(s, LAYOUTS[id], JOURNAL_LIMIT)));
    setLayoutKnown(true);
  }, []);
  const [settingsView, setSettingsView] = useState<"" | "all_fields">("");   // M1-W0 (3): a request to enter a secret opens Settings at Silnik › Wszystkie pola
  const [navigatorPerspectives, setNavigatorPerspectives] = useState<string[]>([]);   // M1-W1a: perspectives the navigator opens with (from "Pokaż w grafie")
  const [navigatorFocus, setNavigatorFocus] = useState("");   // M1-W0: where menu ⋮ › Graf opens the navigator ("" = root)
  const [convRefreshKey, setConvRefreshKey] = useState(0);
  const [knowledgeOpen, setKnowledgeOpen] = useState(false);
  const [chatVisible, setChatVisible] = useState(true);
  // P17 5: on a narrow screen the navigation is a menu under the header; on a wide one it is part of the header and wraps. Same buttons either way.
  const [navOpen, setNavOpen] = useState(false);
  const [primaryProfile, setPrimaryProfile] = useState<ApplicationProfile | null>(null);
  const [config, setConfig] = useState<Record<string, unknown>>({});
  const [started, setStarted] = useState(() => parseShellHash(window.location.hash) !== null);
  const sidebarOperations = useRef(0);
  const sidebarActivity = useCallback((delta: number) => { sidebarOperations.current += delta; }, []);
  const userProfileHost = useMemo(() => createUserProfileHost(api, { onActivity: sidebarActivity }), [sidebarActivity]);
  const canRestoreWorkspace = useCallback(() => sidebarOperations.current === 0, []);

  useEffect(() => {
    const link = parseShellHash(window.location.hash);
    let live = true;
    if (link) {
      // The link states the view; the config still decides the words (ui_language), the theme and the navigator's layout.
      if (link.conversationId) setActiveConvId(link.conversationId);
      api.getConfig().then(cfg => { if (live) setConfig(cfg as Record<string, unknown>); }, () => { /* presets */ });
      return () => { live = false; };
    }
    (async () => {
      let cfg: Record<string, unknown> = {};
      try { cfg = await api.getConfig() as Record<string, unknown>; } catch { /* presets */ }
      const resolved = await resolveStartView(startViewOrder(cfg.start_view ?? START_VIEW_PRESET), {
        saved: () => readSaved(localStorage),
        conversationExists: (id) => api.getConversation(id).then(() => true, () => false),
        newest: async () => (await api.listConversations(1))[0]?.id ?? null,
      });
      if (!live) return;
      setConfig(cfg);
      setLayout(resolved.layout ?? layoutOfMode(cfg.layout_mode));
      // W2: the last session's windows come back with its layout (window_layout_last; preset window_restore_last, false = the layout only)
      const saved = resolved.source === "last_saved" && WINDOW_DATA.window_restore_last !== false ? readSaved(localStorage)?.windows ?? null : null;
      if (saved && saved.id === resolved.layout) {
        // window_restore_front (preset `layout`): the window the layout's preset maximizes (the chat) is in front after a start, the others
        // wait in the switcher, as the start looked before W2; `saved` = the window that was in front when the page was left
        let restored = createState(saved, JOURNAL_LIMIT);
        const presetFront = LAYOUTS[saved.id]?.windows.find(w => w.mode === "maximized")?.id;
        if ((WINDOW_DATA.window_restore_front ?? "layout") === "layout" && presetFront) restored = { ...focusWindow(restored, presetFront), past: [], future: [] };
        setWin(restored);
      }
      setActiveConvId(resolved.conversationId);
      setStarted(true);
    })();
    return () => { live = false; };
  }, []);
  useEffect(() => { if (started && layout) writeSaved(localStorage, { layout, conversationId: activeConvId, windows: WINDOW_DATA.window_restore_last === false ? null : win.layout }); }, [started, layout, activeConvId, win.layout]);

  // W2 (OKNA §4.2): the journal of visits. A change of the window in front made by a window step or the switcher is a visit; each visit
  // above the first is one layer of the LayerStack (`visit:<n>`), so Back walks the windows back (window_back: visits | to_chat |
  // close_window | none - a preset in data). Menus and sheets stay layers above them, as before.
  const winRef = useRef(win);
  winRef.current = win;
  const [visits, setVisits] = useState<string[]>([]);
  const moveTo = useCallback((next: WState) => {
    const before = visibleId(winRef.current.layout), after = visibleId(next.layout);
    winRef.current = next;
    setWin(next);
    if (before === after || after === null || (WINDOW_DATA.window_back ?? "visits") === "none") return;
    setVisits(v => (v.length ? v : before ? [before] : []).concat(after));
  }, []);
  const visitBack = useCallback(() => {
    const mode = WINDOW_DATA.window_back ?? "visits";
    setVisits(v => {
      const rest = v.slice(0, -1);
      const s = winRef.current;
      let next = s;
      if (mode === "close_window") next = closeWindows(s, {}).state;
      else if (mode === "to_chat") { const chat = s.layout.windows.find(w => w.kind === "chat" && typeof w.params?.panel !== "string"); if (chat) next = focusWindow(s, chat.id); }
      else { const prev = [...rest].reverse().find(id => s.layout.windows.some(w => w.id === id)); if (prev) next = focusWindow(s, prev); }
      winRef.current = next;
      setWin(next);
      return rest.length > 1 ? rest : [];
    });
  }, []);
  const visitLayers = useRef<string[]>([]);
  useEffect(() => {
    const stack = layerStack();
    if (!stack) return;
    const want = visits.slice(1).map((_, i) => `visit:${i + 1}`);
    for (const id of [...visitLayers.current].reverse()) if (!want.includes(id)) stack.remove(id);
    for (const id of want) if (!visitLayers.current.includes(id)) stack.push(id, visitBack);
    visitLayers.current = want;
  }, [visits, visitBack]);
  // A window that is gone drops out of the journal (closed from the switcher or by a sentence).
  useEffect(() => { setVisits(v => { const left = v.filter(id => win.layout.windows.some(w => w.id === id)); return left.length === v.length ? v : left.length > 1 ? left : []; }); }, [win.layout]);
  // Animation and disclosure glyphs are data: the resolved tables become CSS variables (prefers-reduced-motion selects the reduced-motion style).
  useEffect(() => {
    const reduced = typeof window.matchMedia === "function" && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const vars = { ...cssVariables(mergeData(config), reduced), ...glyphVariables(config.disclosure_glyphs, config.disclosure_glyph_sets) };
    for (const [k, v] of Object.entries(vars)) document.documentElement.style.setProperty(k, v);
  }, [config]);

  // A message can ask for a panel (the secrets of a storage are entered in Settings): the page does not know the shell, it sends an event.
  useEffect(() => {
    const onRequest = (event: Event) => {
      const panel = panelOfRequest(event);
      if (panel !== null && Object.prototype.hasOwnProperty.call(PANEL_LABELS, panel)) {
        // The secrets (keys, tokens) are entered on the former settings screen, which sits under Silnik › Wszystkie pola.
        if (panel === "settings") setSettingsView("all_fields");
        setActivePanel(panel as PanelId);
      }
    };
    window.addEventListener(OPEN_PANEL_EVENT, onRequest);
    return () => window.removeEventListener(OPEN_PANEL_EVENT, onRequest);
  }, []);

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    writeStored("loom.theme", theme);
  }, [theme]);
  // R67-fix: the setting `theme` (config.json) is read here: its value decides the palette once the config is known (a name without palette tokens leaves the default ones).
  useEffect(() => { const named = themeFromConfig(config.theme); if (named !== null) setTheme(named as Theme); }, [config.theme]);
  // A change made in Settings reaches the screen when Settings closes: the config is read again.
  const lastPanel = useRef<PanelId | null>(null);
  useEffect(() => {
    const closed = lastPanel.current !== null && activePanel === null;
    lastPanel.current = activePanel;
    if (closed) api.getConfig().then(cfg => setConfig(cfg as Record<string, unknown>), () => { /* the screen keeps what it has */ });
  }, [activePanel]);

  const toggleTheme = useCallback(() => {
    const next: Theme = theme === "dark" ? "amoled" : "dark";
    setTheme(next);
    setConfig(current => ({ ...current, theme: next }));
    api.setConfigKey("theme", next).catch(err => console.debug("theme: the choice is not saved", err));
  }, [theme]);

  // A setting a screen saved (the help "Co tu widzę?"): the app keeps the value, so the screen opened again starts as the person left it.
  const rememberSetting = useCallback((key: string, value: boolean | string) => setConfig(current => ({ ...current, [key]: value })), []);
  const openPanel = useCallback((p: PanelId) => { setActivePanel((cur) => (cur === p ? null : p)); setNavOpen(false); }, []);
  const closePanel = useCallback(() => { setActivePanel(null); setSettingsView(""); }, []);

  const onConversationCreated = useCallback((id: string) => {
    setActiveConvId(id);
    setConvRefreshKey((k) => k + 1);
    if (window.innerWidth <= 720) setSidebarOpen(false);
  }, []);

  const onSelectConversation = useCallback((id: string) => {
    setActiveConvId(id);
    if (window.innerWidth <= 720) setSidebarOpen(false);
  }, []);

  const onMessagesChanged = useCallback(() => setConvRefreshKey(key => key + 1), []);
  // M1-W0 r2:b4: one screen for "the graph" — the navigator (the same ⋮ › Graf opens); the old panels stay reachable in the Workshop.
  const openNavigator = useCallback((focus: string, perspectives: string[] = []) => { setNavigatorPerspectives(perspectives); setNavigatorFocus(focus); setActivePanel("navigator"); }, []);
  const profileUi = useMemo(() => ({
    selectConversation: onSelectConversation, conversationCreated: onConversationCreated,
    openKnowledge: () => setKnowledgeOpen(true),
    // the chat's `graph.open` operation lands where ⋮ › Graf lands (navigator root), not on the old force-directed panel
    openPanel: (panel: PanelId) => panel === "graph" ? openNavigator("") : setActivePanel(panel),
  }), [onSelectConversation, onConversationCreated, openNavigator]);
  // The openers of an entry area's `expand` descriptor (spec M1-W0 "Pliki": kb.policy/kb.query/conversations.list/graph.nodes → the navigator).
  // The focus is the area the descriptor stands for (graph_entry.pack defaults.areas[].expand): the words then come from graph.navigate.
  const entryOpeners = useMemo<EntryOpeners>(() => ({
    "graph.navigate": (params) => openNavigator(typeof params.focus === "string" ? params.focus : ""),
    "graph.nodes": () => openNavigator(""),
    "kb.query": (params) => openNavigator(params.what === "entities" ? "area:parties" : "area:knowledge"),
    "kb.policy": () => openNavigator("area:self_model"),
    "conversations.list": () => openNavigator("area:context_projects"),
  }), [openNavigator]);
  // The words of the shell come from data (presentation.app_shell, PL/EN); without a catalog the machine names are shown.
  const shellCatalog = usePresentationFeature(userProfileHost, "app_shell");
  const shellText = (id: string): string => {
    if (!shellCatalog.feature) return id;
    try { return featureMessage(shellCatalog.feature, id); } catch { return id; }
  };
  const uiShell = usePresentationFeature(userProfileHost, "ui_shell");
  const uiLanguage = shellLanguage(config.ui_language, undefined, navigator.languages ?? [navigator.language], ["en", "pl"]);
  // r2:b4: the document speaks the shell's language (index.html ships lang="en"); a screen reader (TalkBack) then picks the Polish voice.
  useEffect(() => { document.documentElement.lang = uiLanguage; }, [uiLanguage]);
  const t = useMemo(() => shellTranslator(uiShell.feature, uiLanguage), [uiShell.feature, uiLanguage]);
  const installedAdapters = useMemo(() => createLoomProfileRegistry(api, profileUi), [profileUi]);
  const installedProfile = useMemo(() => primaryProfile ? registerProfile(installedAdapters, primaryProfile) : null, [installedAdapters, primaryProfile]);

  // Back (Android, browser) and Escape close the panel drawer and the knowledge sheet before anything else; in the workshop the
  // knowledge view is docked beside the chat, not an overlay, so only the sheet of the chat layout is a layer.
  // W2: a panel reached through a visit is closed by Back through the journal of visits (the window waits in the switcher), not here
  const visited = visits.length > 1 && visits[visits.length - 1] === visibleOverlay(win.layout)?.id;
  useLayer("panel", activePanel !== null && !visited, () => setActivePanel(null));

  // W2: a client step of the kernel on the windows (window.open / close / focus / next / prev, from a sentence such as „otwórz graf obok”):
  // the step is done on the window state at once and answered with what was done, in words of the shell (the receipt).
  const windowLabel = useCallback((id: string) => {
    const w = winRef.current.layout.windows.find(x => x.id === id);
    const panel = w && typeof w.params?.panel === "string" ? w.params.panel as PanelId : null;
    if (!panel) return w?.kind === "chat" ? t("window_chat") : (w?.title_override ?? w?.kind ?? id);
    return PANEL_TEXT_IDS[panel] ? t(PANEL_TEXT_IDS[panel]!) : (PANEL_LABELS[panel] ?? panel);
  }, [t]);
  useEffect(() => {
    const known = (id: string) => Object.prototype.hasOwnProperty.call(PANEL_LABELS, id);
    const onStep = (event: Event) => {
      const detail = (event as CustomEvent<WindowStepDetail>).detail;
      if (!detail || detail.outcome) return;
      const deviceClass = window.innerWidth < 600 ? "phone" : "desktop";
      const { state, receipt } = runWindowStep(winRef.current, detail.action, detail.args ?? {}, known, deviceClass);
      moveTo(state);
      const say = (r: WindowReceipt): string => {
        const names = r.windows.map(windowLabel).join(", ");
        if (r.state === "none") return t("window_receipt_none", { word: r.word ?? "" });
        if (r.op === "close") return t("window_receipt_close", { count: names || "0" });
        if (r.op === "open") return t("window_receipt_open", { screen: names }) + (r.placementAsked && r.placementAsked !== r.placementUsed ? " " + t("window_receipt_placement", { asked: r.placementAsked }) : "");
        return t("window_receipt_focus", { screen: names });
      };
      const text = say(receipt);
      detail.outcome = receipt.state === "none"
        ? { state: "failed", error: { message: text } }
        : { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.window_receipt/1", ...receipt, layout: state.layout.id } };
    };
    window.addEventListener(WINDOW_STEP_EVENT, onStep);
    return () => window.removeEventListener(WINDOW_STEP_EVENT, onStep);
  }, [moveTo, t, windowLabel]);
  const shellWindows = useMemo(() => {
    const front = visibleId(win.layout);
    return {
      entries: switcherEntries(win.layout).map(w => ({ id: w.id, label: windowLabel(w.id), front: w.id === front, closable: typeof w.params?.panel === "string" })),
      focus: (id: string) => moveTo(focusWindow(winRef.current, id)),
      close: (id: string) => {
        const w = winRef.current.layout.windows.find(x => x.id === id);
        if (w && typeof w.params?.panel === "string") moveTo(closeWindows(winRef.current, { target: { panel: String(w.params.panel) } }).state);
      },
    };
  }, [win.layout, windowLabel, moveTo]);
  // the layout shows one window full-screen (the plain chat app is that case); a grid of docks (the former Workshop) is not
  const fullWindow = layout !== null && baseWindow(win.layout) !== null;
  useLayer("knowledge-sheet", knowledgeOpen && fullWindow, () => setKnowledgeOpen(false));
  // Each overlay is a dialog for a reader of the screen: focus enters it when it opens and returns to the ⋮ button when it closes.
  const panelRef = useRef<HTMLDivElement>(null), knowledgeRef = useRef<HTMLDivElement>(null);
  useDialogFocus(activePanel !== null, panelRef, "shell-menu-toggle");
  useDialogFocus(knowledgeOpen && fullWindow, knowledgeRef, "shell-menu-toggle");

  const panelTitle = activePanel ? (PANEL_TEXT_IDS[activePanel] ? t(PANEL_TEXT_IDS[activePanel]!) : PANEL_LABELS[activePanel]) : "";
  const topKind = visibleOverlay(win.layout)?.kind ?? null;   // the kind decides the component (settings = SettingsHuman, settings_fields = SettingsPanel)
  const panels = activePanel && (

          <>
            <div className="drawer-backdrop" onClick={closePanel} data-testid="drawer-backdrop" />
            <div className="drawer" data-testid={`panel-${activePanel}`} ref={panelRef} role="dialog" aria-modal="true" aria-label={panelTitle} tabIndex={-1}>
              <div className="drawer-header">
                <h2>{panelTitle}</h2>
                <button className="icon-btn" aria-label={t("panel_close", { screen: panelTitle })} onClick={closePanel} data-testid="close-panel">
                  ×
                </button>
              </div>
              <div className="drawer-body">
                {activePanel === "graph" && <GraphView convId={activeConvId} profileHost={userProfileHost} helpSetting={config.graph_view_help_open} onSetting={rememberSetting} />}
                {activePanel === "graph-entry" && <GraphEntryPanel profileHost={userProfileHost} openers={entryOpeners} />}
                {activePanel === "navigator" && <GraphNavigator profileHost={userProfileHost} convId={activeConvId} language={uiLanguage} initialFocus={navigatorFocus} initialPerspectives={navigatorPerspectives} layout={config.navigator_layout} helpSetting={config.navigator_help_open} onSetting={rememberSetting}
                  meOpenSetting={config.me_open_sections} meInferredOpacity={config.me_inferred_opacity} onOpenConversation={id => { onConversationCreated(id); closePanel(); }} />}
                {activePanel === "perspectives" && <PerspectivesPanel convId={activeConvId} />}
                {activePanel === "view-engine" && <ViewEnginePanel />}
                {activePanel === "memory" && <MemoryPanel />}
                {activePanel === "import" && <ImportPanel onImported={() => setConvRefreshKey((k) => k + 1)} />}
                {activePanel === "context" && <ContextSlider convId={activeConvId} />}
                {activePanel === "settings" && topKind === "settings_fields" && <SettingsPanel />}
                {activePanel === "settings" && topKind !== "settings_fields" && <SettingsHuman profileHost={userProfileHost} convId={activeConvId} language={uiLanguage} view={settingsView}
                  onNavigate={address => { setNavigatorFocus(address); setActivePanel("navigator"); }} />}
                {activePanel === "logs" && <LogPanel />}
                {activePanel === "methods" && <MethodsPanel transport={api} />}
                {activePanel === "analysis" && <AnalysisPanel />}
                {activePanel === "onboarding" && <UserProfilePanel view="onboarding" host={userProfileHost} />}
                {activePanel === "user-knowledge" && <UserProfilePanel view="knowledge" host={userProfileHost} />}
                {activePanel === "operations" && <OperationsPanel operations={api.operations}
                  profiles={primaryProfile ? [primaryProfile] : []} graphPacketStore={api.graphPacketStore?.bind(api)}
                  adapterEvidence={(primaryProfile?.actions ?? []).filter(action => {
                    const gaps = installedProfile ? profileAvailability(installedProfile, installedAdapters) : null;
                    return ![...(gaps?.requiredGaps ?? []), ...(gaps?.optionalGaps ?? [])].some(gap => gap.action_id === action.id);
                  }).map(action => ({
                    operation: action.operation, capability: action.capability, status: "native" as const,
                    detail: "Installed Loom adapter; source-service equivalence unverified.", evidence: ["host profile registry"],
                  }))} />}
              </div>
            </div>
          </>
        
  );
  if (layout === null) return null;
  const workshop = (overlay: ReactNode) => (
    <div className="app-root" data-sidebar-side={primaryProfile?.presentation.sidebar.side ?? "left"}
      style={primaryProfile ? profileStyle(primaryProfile) : undefined}>
      {/* an open panel is a modal dialog: the header, the sidebar and the main panel behind it are inert (no Tab, no taps, no reader) */}
      <header className="app-header" onKeyDown={(event) => { if (event.key === "Escape") setNavOpen(false); }} {...inertAttr(activePanel !== null)}>
        <button
          className="icon-btn"
          aria-label="Toggle conversation list"
          data-testid="toggle-sidebar"
          onClick={() => setSidebarOpen((v) => !v)}
        >
          ≡
        </button>
        <h1>Loom</h1>
        <div className="spacer" />
        <button className="nav-toggle" aria-label={shellText("menu_label")} aria-expanded={navOpen} aria-controls="app-nav" data-testid="toggle-nav"
          onClick={() => setNavOpen((v) => !v)}>
          {shellText("menu")}
        </button>
        <nav className={navOpen ? "app-nav open" : "app-nav"} id="app-nav" data-testid="app-nav">
          <button data-testid="nav-chat" onClick={() => { setLayout("chat"); setNavOpen(false); }}>{t("workshop_back")}</button>
          <SemanticStatus profileHost={userProfileHost} />
          <button data-testid="nav-knowledge" aria-pressed={knowledgeOpen} onClick={() => { setKnowledgeOpen((value) => !value); setNavOpen(false); }}>Knowledge</button>
          {knowledgeOpen && <button aria-pressed={chatVisible} onClick={() => { setChatVisible((value) => !value); setNavOpen(false); }}>{chatVisible ? "Hide chat" : "Show chat"}</button>}
          {(Object.keys(PANEL_LABELS) as PanelId[]).map((p) => (
            <button
              key={p}
              data-testid={`nav-${p}`}
              aria-pressed={activePanel === p}
              onClick={() => openPanel(p)}
              style={activePanel === p ? { borderColor: "var(--accent)", color: "var(--accent)" } : undefined}
            >
              {PANEL_TEXT_IDS[p] ? t(PANEL_TEXT_IDS[p]!) : PANEL_LABELS[p]}
            </button>
          ))}
          <button data-testid="toggle-theme" onClick={toggleTheme} title="Toggle theme">
            {theme === "dark" ? "Dark" : "AMOLED"}
          </button>
          {account && (
            <button data-testid="account-logout" onClick={account.logout} title={account.signedInAs}>
              {account.logoutLabel}
            </button>
          )}
        </nav>
      </header>

      <div className="app-layout">
        <aside className={`sidebar${sidebarOpen ? "" : " collapsed"}`} data-testid="sidebar" {...inertAttr(activePanel !== null)}>
          <ConversationList
            key={convRefreshKey}
            activeConvId={activeConvId}
            onSelect={onSelectConversation}
            onCreated={onConversationCreated}
            onActivity={sidebarActivity}
          />
        </aside>

        <div className={`main-panel${knowledgeOpen ? " with-workbench" : ""}`} {...inertAttr(activePanel !== null)}>
          <div className={`chat-host${knowledgeOpen ? " beside-workbench" : ""}`} hidden={knowledgeOpen && !chatVisible}>
            <ApplicationProfiles profileHost={userProfileHost} convId={activeConvId} onConversationCreated={onConversationCreated}
              refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} ui={profileUi}
              onPrimaryProfile={setPrimaryProfile} onSharedConversationRestored={setActiveConvId}
              canRestoreWorkspace={canRestoreWorkspace} />
          </div>
          {knowledgeOpen && <KnowledgeWorkbench profileHost={userProfileHost} onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} />}
        </div>

        {overlay}
      </div>
    </div>
  );
  const host = <WindowHost state={win} renderOverlay={() => panels} grid={workshop} renderWindow={w => w.kind !== "chat" ? null : (
      <ChatShell profileHost={userProfileHost} convId={activeConvId} onSelect={setActiveConvId} onCreated={onConversationCreated} onShowInGraph={openNavigator}
        refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} windows={shellWindows} behindOverlay={activePanel !== null || knowledgeOpen}
        actions={{ panel: (id) => { if (id === "settings") setSettingsView(""); setActivePanel(id as PanelId); }, navigator: (focus) => { setNavigatorFocus(focus); setActivePanel("navigator"); }, knowledge: () => setKnowledgeOpen(true), workshop: () => setLayout("workshop") }} />)} />;
  return (
    <ShellText.Provider value={t}><ShellLanguage.Provider value={uiLanguage}>
      {host}
      {fullWindow && knowledgeOpen && <>
        <div className="drawer-backdrop" onClick={() => setKnowledgeOpen(false)} />
        <div className="drawer" style={{ width: "min(960px, 100vw)" }} data-testid="panel-knowledge-sheet" ref={knowledgeRef} role="dialog" aria-modal="true" aria-label={t("menu_knowledge")} tabIndex={-1}>
          <div className="drawer-header"><h2>{t("menu_knowledge")}</h2><button className="icon-btn" aria-label={t("panel_close", { screen: t("menu_knowledge") })} onClick={() => setKnowledgeOpen(false)}>×</button></div>
          <div className="drawer-body"><KnowledgeWorkbench profileHost={userProfileHost} onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} /></div>
        </div>
      </>}
    </ShellLanguage.Provider></ShellText.Provider>
  );
}
