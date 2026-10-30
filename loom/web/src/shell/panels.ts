// The panels of the shell: what the + menu (and a message) may ask to open. App.tsx draws them; the composer menu checks a panel it is
// given against this list, so an entry that names a panel the app does not have is said, not a dead row.
export const SHELL_PANELS = ["graph", "memory", "import", "context", "settings", "logs", "operations", "methods", "analysis", "onboarding",
  "user-knowledge", "graph-entry", "perspectives", "view-engine", "navigator", "request", "conversations", "trash", "structure"] as const;
export type PanelId = typeof SHELL_PANELS[number];

/** The catalog word (ui_shell, id `panel_<panel>`) that names each panel in the Workshop navigation when PANEL_TEXT_IDS has no word of its own.
 * No label text lives in code: the words are in the language packs. */
export const PANEL_LABELS: Record<PanelId, string> = {
  graph: "panel_graph",
  memory: "panel_memory",
  import: "panel_import",
  context: "panel_context",
  settings: "panel_settings",
  logs: "panel_logs",
  operations: "panel_operations",
  methods: "panel_methods",
  analysis: "panel_analysis",
  onboarding: "panel_onboarding",
  "user-knowledge": "panel_user_knowledge",
  "graph-entry": "panel_graph_entry",
  perspectives: "panel_perspectives",
  "view-engine": "panel_view_engine",
  navigator: "panel_navigator",
  request: "panel_request",
  conversations: "panel_conversations",
  trash: "panel_trash",
  structure: "panel_structure",
};

// Panels whose name comes from the shell words (data, PL/EN by ui_language); the others use the word panel_<id> from the same catalog.
// A control that opens the same screen as a ⋮ menu entry may carry the menu's word (settings, navigator = "Graf"); a control that opens
// ANOTHER screen must not: the old force-directed map and the old "what the app knows" panel live in the Workshop under their own words
// (workshop_graph_old, workshop_me_old: "(stary widok)"), as the old graph entry does (r1:b5). shell/label-guard.ts + its unit test walk this table
// and the menu data and fail when one label leads to two different screens (M1-G1b: "Ta rozmowa w grafie" did).
export const PANEL_TEXT_IDS: Partial<Record<PanelId, string>> = {
  settings: "menu_settings", navigator: "menu_graph", "graph-entry": "workshop_graph_entry", memory: "menu_memory",
  graph: "workshop_graph_old", "user-knowledge": "workshop_me_old", perspectives: "perspectives_title", "view-engine": "view_engine_title",
  request: "ri_title", structure: "ps_title",
};
