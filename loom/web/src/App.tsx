import { useCallback, useEffect, useMemo, useRef, useState } from "react";
import ApplicationProfiles, { profileStyle } from "./components/ApplicationProfiles";
import { profileAvailability, registerProfile, type ApplicationProfile } from "./profiles/runtime";
import ConversationList from "./components/ConversationList";
import GraphView from "./components/GraphView";
import SettingsPanel from "./components/SettingsPanel";
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
import { createUserProfileHost } from "./api/onboarding-host";
import { api } from "./api";
import { createLoomProfileRegistry } from "./profiles/loom-adapter";

type Theme = "dark" | "amoled";
type PanelId = "graph" | "memory" | "import" | "context" | "settings" | "logs" | "operations" | "methods" | "analysis" | "onboarding" | "user-knowledge";

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
};

export default function App() {
  const [theme, setTheme] = useState<Theme>(() => (readStored("loom.theme", "dark") as Theme) || "dark");
  const [sidebarOpen, setSidebarOpen] = useState<boolean>(() =>
    typeof window !== "undefined" ? window.innerWidth > 720 : true,
  );
  const [activeConvId, setActiveConvId] = useState<string | null>(null);
  const [activePanel, setActivePanel] = useState<PanelId | null>(null);
  const [convRefreshKey, setConvRefreshKey] = useState(0);
  const [knowledgeOpen, setKnowledgeOpen] = useState(false);
  const [chatVisible, setChatVisible] = useState(true);
  const [primaryProfile, setPrimaryProfile] = useState<ApplicationProfile | null>(null);
  const sidebarOperations = useRef(0);
  const sidebarActivity = useCallback((delta: number) => { sidebarOperations.current += delta; }, []);
  const userProfileHost = useMemo(() => createUserProfileHost(api, { onActivity: sidebarActivity }), [sidebarActivity]);
  const canRestoreWorkspace = useCallback(() => sidebarOperations.current === 0, []);

  useEffect(() => {
    document.documentElement.setAttribute("data-theme", theme);
    writeStored("loom.theme", theme);
  }, [theme]);

  const toggleTheme = useCallback(() => {
    setTheme((t) => (t === "dark" ? "amoled" : "dark"));
  }, []);

  const openPanel = useCallback((p: PanelId) => setActivePanel((cur) => (cur === p ? null : p)), []);
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
  const installedAdapters = useMemo(() => createLoomProfileRegistry(api, profileUi), [profileUi]);
  const installedProfile = useMemo(() => primaryProfile ? registerProfile(installedAdapters, primaryProfile) : null, [installedAdapters, primaryProfile]);

  return (
    <div className="app-root" data-sidebar-side={primaryProfile?.presentation.sidebar.side ?? "left"}
      style={primaryProfile ? profileStyle(primaryProfile) : undefined}>
      <header className="app-header">
        <button
          className="icon-btn"
          aria-label="Toggle conversation list"
          data-testid="toggle-sidebar"
          onClick={() => setSidebarOpen((v) => !v)}
        >
          ≡
        </button>
        <h1>Loom</h1>
        <SemanticStatus />
        <div className="spacer" />
        <button data-testid="nav-knowledge" aria-pressed={knowledgeOpen} onClick={() => setKnowledgeOpen((value) => !value)}>Knowledge</button>
        {knowledgeOpen && <button aria-pressed={chatVisible} onClick={() => setChatVisible((value) => !value)}>{chatVisible ? "Hide chat" : "Show chat"}</button>}
        {(Object.keys(PANEL_LABELS) as PanelId[]).map((p) => (
          <button
            key={p}
            data-testid={`nav-${p}`}
            aria-pressed={activePanel === p}
            onClick={() => openPanel(p)}
            style={activePanel === p ? { borderColor: "var(--accent)", color: "var(--accent)" } : undefined}
          >
            {PANEL_LABELS[p]}
          </button>
        ))}
        <button data-testid="toggle-theme" onClick={toggleTheme} title="Toggle theme">
          {theme === "dark" ? "Dark" : "AMOLED"}
        </button>
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
            <ApplicationProfiles convId={activeConvId} onConversationCreated={onConversationCreated}
              refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} ui={profileUi}
              onPrimaryProfile={setPrimaryProfile} onSharedConversationRestored={setActiveConvId}
              canRestoreWorkspace={canRestoreWorkspace} />
          </div>
          {knowledgeOpen && <KnowledgeWorkbench onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} />}
        </div>

        {activePanel && (
          <>
            <div className="drawer-backdrop" onClick={closePanel} data-testid="drawer-backdrop" />
            <div className="drawer" data-testid={`panel-${activePanel}`}>
              <div className="drawer-header">
                <h2>{PANEL_LABELS[activePanel]}</h2>
                <button className="icon-btn" aria-label="Close panel" onClick={closePanel} data-testid="close-panel">
                  ×
                </button>
              </div>
              <div className="drawer-body">
                {activePanel === "graph" && <GraphView convId={activeConvId} />}
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
        )}
      </div>
    </div>
  );
}
