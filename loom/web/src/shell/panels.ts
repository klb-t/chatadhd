// The panels of the shell: what the + menu (and a message) may ask to open. App.tsx draws them; the composer menu checks a panel it is
// given against this list, so an entry that names a panel the app does not have is said, not a dead row.
export const SHELL_PANELS = ["graph", "memory", "import", "context", "settings", "logs", "operations", "methods", "analysis", "onboarding",
  "user-knowledge", "graph-entry", "perspectives", "view-engine", "navigator", "request", "conversations", "trash"] as const;
export type PanelId = typeof SHELL_PANELS[number];

/** The English label of each panel in the Workshop navigation: used when the panel has no word of its own in PANEL_TEXT_IDS. */
export const PANEL_LABELS: Record<PanelId, string> = {
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
  navigator: "Navigator",
  request: "Request to the model",
  conversations: "Conversations",
  trash: "Trash",
};

// Panels whose name comes from the shell words (data, PL/EN by ui_language); the others keep their English label.
// A control that opens the same screen as a ⋮ menu entry may carry the menu's word (settings, navigator = "Graf"); a control that opens
// ANOTHER screen must not: the old force-directed map and the old "what the app knows" panel live in the Workshop under their own words
// (workshop_graph_old, workshop_me_old: "(stary widok)"), as the old graph entry does (r1:b5). shell/label-guard.ts + its unit test walk this table
// and the menu data and fail when one label leads to two different screens (M1-G1b: "Ta rozmowa w grafie" did).
export const PANEL_TEXT_IDS: Partial<Record<PanelId, string>> = {
  settings: "menu_settings", navigator: "menu_graph", "graph-entry": "workshop_graph_entry", memory: "menu_memory",
  graph: "workshop_graph_old", "user-knowledge": "workshop_me_old", perspectives: "perspectives_title", "view-engine": "view_engine_title",
  request: "ri_title",
};
