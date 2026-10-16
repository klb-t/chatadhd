import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import ApplicationProfiles, { profileStyle } from "./components/ApplicationProfiles";
import { profileAvailability, registerProfile, type ApplicationProfile } from "./profiles/runtime";
import ConversationList from "./components/ConversationList";
import GraphView from "./components/GraphView";
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
import { START_VIEW_PRESET, layoutOfMode, parseShellHash, readSaved, resolveStartView, startViewOrder, writeSaved, type Layout } from "./shell/start-view";
import { shellLanguage } from "./shell/language";
import { cssVariables, mergeData } from "./shell/animation";

type Theme = "dark" | "amoled";
type PanelId = "graph" | "memory" | "import" | "context" | "settings" | "logs" | "operations" | "methods" | "analysis" | "onboarding" | "user-knowledge" | "graph-entry" | "perspectives" | "view-engine";

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

const PANEL_LABELS: Record<PanelId, string> = {
  graph: "Graph",
  memory: "Memory",
  import: "Import",
  context: "Context",
  settings: "Settings",
  logs: "Logs",
  operations: "Operations",
  methods: "Methods",
  analysis: "Analysis",
  onboarding: "Onboarding",
  "user-knowledge": "What app knows",
  "graph-entry": "Entry",
  perspectives: "Perspectives",
  "view-engine": "View engine",
};

// Panels whose name comes from the shell words (data, PL/EN by ui_language); the others keep their English label.
const PANEL_TEXT_IDS: Partial<Record<PanelId, string>> = { graph: "menu_graph", perspectives: "perspectives_title", "view-engine": "view_engine_title" };

export default function App() {
  const account = useAccountSession();
  const [theme, setTheme] = useState<Theme>(() => (readStored("loom.theme", "dark") as Theme) || "dark");
  const [sidebarOpen, setSidebarOpen] = useState<boolean>(() =>
    typeof window !== "undefined" ? window.innerWidth > 720 : true,
  );
  const [activeConvId, setActiveConvId] = useState<string | null>(null);
  const [activePanel, setActivePanel] = useState<PanelId | null>(null);
  const [convRefreshKey, setConvRefreshKey] = useState(0);
  const [knowledgeOpen, setKnowledgeOpen] = useState(false);
  const [chatVisible, setChatVisible] = useState(true);
  // P17 5: on a narrow screen the navigation is a menu under the header; on a wide one it is part of the header and wraps. Same buttons either way.
  const [navOpen, setNavOpen] = useState(false);
  const [primaryProfile, setPrimaryProfile] = useState<ApplicationProfile | null>(null);
  // R56: the first screen. A deep link (#czat, #warsztat) states the view; otherwise the setting start_view (ordered) decides.
  const [layout, setLayout] = useState<Layout | null>(() => parseShellHash(window.location.hash)?.layout ?? null);
  const [config, setConfig] = useState<Record<string, unknown>>({});
  const [started, setStarted] = useState(() => parseShellHash(window.location.hash) !== null);
  const sidebarOperations = useRef(0);
  const sidebarActivity = useCallback((delta: number) => { sidebarOperations.current += delta; }, []);
  const userProfileHost = useMemo(() => createUserProfileHost(api, { onActivity: sidebarActivity }), [sidebarActivity]);
  const canRestoreWorkspace = useCallback(() => sidebarOperations.current === 0, []);

  useEffect(() => {
    const link = parseShellHash(window.location.hash);
    if (link) { if (link.conversationId) setActiveConvId(link.conversationId); return; }
    let live = true;
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
      setActiveConvId(resolved.conversationId);
      setStarted(true);
    })();
    return () => { live = false; };
  }, []);
  useEffect(() => { if (started && layout) writeSaved(localStorage, { layout, conversationId: activeConvId }); }, [started, layout, activeConvId]);
  // Animation is data: the resolved table becomes CSS variables (prefers-reduced-motion selects the reduced-motion style).
  useEffect(() => {
    const reduced = typeof window.matchMedia === "function" && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const vars = cssVariables(mergeData(config), reduced);
    for (const [k, v] of Object.entries(vars)) document.documentElement.style.setProperty(k, v);
  }, [config]);

  // A message can ask for a panel (the secrets of a storage are entered in Settings): the page does not know the shell, it sends an event.
  useEffect(() => {
    const onRequest = (event: Event) => {
      const panel = panelOfRequest(event);
      if (panel !== null && Object.prototype.hasOwnProperty.call(PANEL_LABELS, panel)) setActivePanel(panel as PanelId);
    };
    window.addEventListener(OPEN_PANEL_EVENT, onRequest);
    return () => window.removeEventListener(OPEN_PANEL_EVENT, onRequest);
  }, []);

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    writeStored("loom.theme", theme);
  }, [theme]);

  const toggleTheme = useCallback(() => {
    setTheme((t) => (t === "dark" ? "amoled" : "dark"));
  }, []);

  const openPanel = useCallback((p: PanelId) => { setActivePanel((cur) => (cur === p ? null : p)); setNavOpen(false); }, []);
  const closePanel = useCallback(() => setActivePanel(null), []);

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
  const profileUi = useMemo(() => ({
    selectConversation: onSelectConversation, conversationCreated: onConversationCreated,
    openKnowledge: () => setKnowledgeOpen(true), openPanel: (panel: PanelId) => setActivePanel(panel),
  }), [onSelectConversation, onConversationCreated]);
  // Existing views that can open an entry area's `expand` descriptor.
  const entryOpeners = useMemo<EntryOpeners>(() => ({
    "conversations.list": () => { setActivePanel(null); setSidebarOpen(true); },
    "graph.nodes": () => setActivePanel("graph"),
    "kb.query": () => { setActivePanel(null); setKnowledgeOpen(true); },
  }), []);
  // The words of the shell come from data (presentation.app_shell, PL/EN); without a catalog the machine names are shown.
  const shellCatalog = usePresentationFeature(userProfileHost, "app_shell");
  const shellText = (id: string): string => {
    if (!shellCatalog.feature) return id;
    try { return featureMessage(shellCatalog.feature, id); } catch { return id; }
  };
  const uiShell = usePresentationFeature(userProfileHost, "ui_shell");
  const uiLanguage = shellLanguage(config.ui_language, undefined, navigator.languages ?? [navigator.language], ["en", "pl"]);
  const t = useMemo(() => shellTranslator(uiShell.feature, uiLanguage), [uiShell.feature, uiLanguage]);
  const installedAdapters = useMemo(() => createLoomProfileRegistry(api, profileUi), [profileUi]);
  const installedProfile = useMemo(() => primaryProfile ? registerProfile(installedAdapters, primaryProfile) : null, [installedAdapters, primaryProfile]);

  const panels = activePanel && (

          <>
            <div className="drawer-backdrop" onClick={closePanel} data-testid="drawer-backdrop" />
            <div className="drawer" data-testid={`panel-${activePanel}`}>
              <div className="drawer-header">
                <h2>{PANEL_TEXT_IDS[activePanel] ? t(PANEL_TEXT_IDS[activePanel]!) : PANEL_LABELS[activePanel]}</h2>
                <button className="icon-btn" aria-label="Close panel" onClick={closePanel} data-testid="close-panel">
                  ×
                </button>
              </div>
              <div className="drawer-body">
                {activePanel === "graph" && <GraphView convId={activeConvId} />}
                {activePanel === "graph-entry" && <GraphEntryPanel profileHost={userProfileHost} openers={entryOpeners} />}
                {activePanel === "perspectives" && <PerspectivesPanel convId={activeConvId} />}
                {activePanel === "view-engine" && <ViewEnginePanel />}
                {activePanel === "memory" && <MemoryPanel />}
                {activePanel === "import" && <ImportPanel onImported={() => setConvRefreshKey((k) => k + 1)} />}
                {activePanel === "context" && <ContextSlider convId={activeConvId} />}
                {activePanel === "settings" && <SettingsPanel />}
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
  const workshop = (
    <div className="app-root" data-sidebar-side={primaryProfile?.presentation.sidebar.side ?? "left"}
      style={primaryProfile ? profileStyle(primaryProfile) : undefined}>
      <header className="app-header" onKeyDown={(event) => { if (event.key === "Escape") setNavOpen(false); }}>
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
        <aside className={`sidebar${sidebarOpen ? "" : " collapsed"}`} data-testid="sidebar">
          <ConversationList
            key={convRefreshKey}
            activeConvId={activeConvId}
            onSelect={onSelectConversation}
            onCreated={onConversationCreated}
            onActivity={sidebarActivity}
          />
        </aside>

        <div className={`main-panel${knowledgeOpen ? " with-workbench" : ""}`}>
          <div className={`chat-host${knowledgeOpen ? " beside-workbench" : ""}`} hidden={knowledgeOpen && !chatVisible}>
            <ApplicationProfiles profileHost={userProfileHost} convId={activeConvId} onConversationCreated={onConversationCreated}
              refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} ui={profileUi}
              onPrimaryProfile={setPrimaryProfile} onSharedConversationRestored={setActiveConvId}
              canRestoreWorkspace={canRestoreWorkspace} />
          </div>
          {knowledgeOpen && <KnowledgeWorkbench profileHost={userProfileHost} onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} />}
        </div>

        {panels}
      </div>
    </div>
  );
  if (layout === "chat") return (
    <ShellText.Provider value={t}><ShellLanguage.Provider value={uiLanguage}>
      <ChatShell profileHost={userProfileHost} convId={activeConvId} onSelect={setActiveConvId} onCreated={onConversationCreated}
        refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} planned={{ windows: true }}
        actions={{ panel: (id) => setActivePanel(id as PanelId), knowledge: () => setKnowledgeOpen(true), workshop: () => setLayout("workshop") }} />
      {panels}
      {knowledgeOpen && <>
        <div className="drawer-backdrop" onClick={() => setKnowledgeOpen(false)} />
        <div className="drawer" style={{ width: "min(960px, 100vw)" }} data-testid="panel-knowledge-sheet">
          <div className="drawer-header"><h2>{t("menu_knowledge")}</h2><button className="icon-btn" aria-label="×" onClick={() => setKnowledgeOpen(false)}>×</button></div>
          <div className="drawer-body"><KnowledgeWorkbench profileHost={userProfileHost} onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} /></div>
        </div>
      </>}
    </ShellLanguage.Provider></ShellText.Provider>
  );
  return <ShellText.Provider value={t}><ShellLanguage.Provider value={uiLanguage}>{workshop}</ShellLanguage.Provider></ShellText.Provider>;
}
