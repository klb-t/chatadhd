// The panels of the shell: what the + menu (and a message) may ask to open. App.tsx draws them; the composer menu checks a panel it is
// given against this list, so an entry that names a panel the app does not have is said, not a dead row.
export const SHELL_PANELS = ["graph", "memory", "import", "context", "settings", "logs", "operations", "methods", "analysis", "onboarding",
  "user-knowledge", "graph-entry", "perspectives", "view-engine", "navigator"] as const;
export type PanelId = typeof SHELL_PANELS[number];
