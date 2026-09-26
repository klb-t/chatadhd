import { useCallback, useEffect, useState } from "react";
import ChatView from "./components/ChatView";
import ConversationList from "./components/ConversationList";
import GraphView from "./components/GraphView";
import SettingsPanel from "./components/SettingsPanel";
import ContextSlider from "./components/ContextSlider";
import SemanticStatus from "./components/SemanticStatus";
import ImportPanel from "./components/ImportPanel";
import MemoryPanel from "./components/MemoryPanel";
import LogPanel from "./components/LogPanel";

type Theme = "dark" | "amoled";
type PanelId = "graph" | "memory" | "import" | "context" | "settings" | "logs";

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
};

export default function App() {
  const [theme, setTheme] = useState<Theme>(() => (readStored("loom.theme", "dark") as Theme) || "dark");
  const [sidebarOpen, setSidebarOpen] = useState<boolean>(() =>
    typeof window !== "undefined" ? window.innerWidth > 720 : true,
  );
  const [activeConvId, setActiveConvId] = useState<string | null>(null);
  const [activePanel, setActivePanel] = useState<PanelId | null>(null);
  const [convRefreshKey, setConvRefreshKey] = useState(0);

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

  return (
    <div className="app-root">
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
          />
        </aside>

        <div className="main-panel">
          <ChatView convId={activeConvId} onConversationCreated={onConversationCreated} />
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
              </div>
            </div>
          </>
        )}
      </div>
    </div>
  );
}
