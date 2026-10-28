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
import { BUILTIN_PROFILE_DOCUMENTS } from "./profiles/builtins";
import { STYLES, findProfile, inheritLabel, refKey, refOf, refOfDoc, resolveStyle, sayStyle, setLayoutProfile, setWindowProfile, styleAdvisories, styleOfWord, withSidebarWidth, importedStyleDocs, type ProfileRef } from "./shell/windows/style";
import ChatShell from "./shell/ChatShell";
import PerspectivesPanel from "./components/PerspectivesPanel";
import ViewEnginePanel from "./components/ViewEnginePanel";
import { ShellLanguage, ShellText, shellTranslator } from "./shell/text";
import { themeFromConfig } from "./shell/theme";
import { DEFAULT_LAYOUT, JOURNAL_LIMIT, LAYOUTS, START_VIEW_PRESET, layoutOfMode, parseShellHash, readSaved, resolveLayoutId, resolveStartView, startViewOrder, writeSaved } from "./shell/start-view";
import WindowHost from "./shell/windows/WindowHost";
import { WINDOW_CTX, activePanelOf, baseWindow, layoutState, panelKind, restoreFront, setPanel, switchLayout, visibleOverlay } from "./shell/windows/session";
import { createState, redo as redoLayout, undo as undoLayout, type WState, type Win } from "./shell/windows/model";
import { activePatch, deleteNamed, namedPatch, deviceClassOf, findNamed, journalPatch, labelOf, lastOwn, readActive, readNamed, readWriterJournal, renameNamed, replaceLayout, resetTarget, saveNamed, shippedId, type JournalWho } from "./shell/windows/named";
import { mergePatch } from "./api/merge-patch";
import { clientIdentity, installationIdentity } from "./context/client-session";
import { layoutIdOfMode } from "./shell/windows/migrate";
import type { ShellLayouts } from "./shell/LayoutsSheet";
import { WINDOW_DATA, openArtifactWindow, closeWindows, windowDataOf, focusWindow, resolveWord, runWindowStep, switcherEntries, visibleId, type WindowReceipt } from "./shell/windows/ops";
import { WINDOW_STEP_EVENT, type WindowStepDetail } from "./context/browser-host";
import { layerStack } from "./shell/history";
import { shellLanguage } from "./shell/language";
import { cssVariables, mergeData } from "./shell/animation";
import { glyphVariables } from "./shell/disclosure-glyphs";
import { PANEL_LABELS, PANEL_TEXT_IDS, type PanelId } from "./shell/panels";
import { useLayer } from "./shell/use-layer";
import { useArrange } from "./shell/windows/useArrange";
import { useCoupling, type CouplingHook } from "./shell/windows/useCoupling";
import { couplingDataOf } from "./shell/windows/coupling";
import ArtifactWindow from "./shell/artifacts/ArtifactWindow";
import RequestInspector from "./shell/RequestInspector";
import { ARTIFACTS, OPEN_ARTIFACT_EVENT, artifactOf, scriptsOn } from "./shell/artifacts/artifacts";
import { runArrangeStep, STEP_DATA } from "./shell/windows/steps";
import { bindingsOf, paramOfWord, paramWord, paramsOfKind } from "./shell/windows/coupling";
import { ARRANGE, arrangeWindow } from "./shell/windows/arrange";
import { MOUNT, keepPolicy, keptHidden, mountAdvisories, mountedIds, shownIds, touchRecent } from "./shell/windows/keep";
import { usePruneKept } from "./shell/windows/useKeep";
import Desktop, { DesktopEnded } from "./shell/windows/Desktop";
import { DESKTOP, say, say as sayDesktop, closedLast, desktopCover, desktopPaths, desktopSettings, kindEntries, lastWindowClose, openFromDesktop, restoreFromDesktop, showDesktop, type KindTarget } from "./shell/windows/desktop";
import { handSentence } from "./shell/desktop-sentence";
import type { DesktopIcon } from "./shell/windows/desktop-icons";
import { oneEntry, comboOf } from "./shell/windows/steps";
import { apply as applyOp } from "./shell/windows/model";

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
  // W5: the settings as the screen knows them right now (a write updates the ref at once, before React renders again)
  const configRef = useRef(config);
  configRef.current = config;
  const saveSetting = useCallback((key: string, value: unknown) => {
    configRef.current = { ...configRef.current, [key]: value };
    setConfig(c => ({ ...c, [key]: value }));
    api.setConfigKey(key, value).catch(err => console.debug(`${key}: the value is not saved`, err));
  }, []);
  // W5b: a merge of one object value on the host (window_layout_journal, window_layout_active): other tabs' and devices' members of the
  // value stay; the host's merged value comes back into the screen's config. A host without the merge: fresh read, merge, write.
  const mergeSetting = useCallback((key: string, patch: unknown) => {
    const local = mergePatch(configRef.current[key], patch);
    configRef.current = { ...configRef.current, [key]: local };
    setConfig(c => ({ ...c, [key]: local }));
    const merged = api.mergeConfigKey ? api.mergeConfigKey(key, patch)
      : api.getConfig().then(fresh => api.setConfigKey(key, mergePatch((fresh as Record<string, unknown>)[key], patch)));
    merged.then(cfg => {
      const value = (cfg as Record<string, unknown> | undefined)?.[key];
      if (value === undefined) return;
      configRef.current = { ...configRef.current, [key]: value };
      setConfig(c => ({ ...c, [key]: value }));
    }, err => console.debug(`${key}: the value is not merged`, err));
  }, []);
  // W5b: who writes the journal - this tab (its client id, kept across reloads of the tab) in this browser installation (OKNA D19)
  const journalWho = useMemo<JournalWho>(() => ({
    writer: clientIdentity(typeof sessionStorage === "undefined" ? undefined : sessionStorage),
    installation: installationIdentity(typeof localStorage === "undefined" ? undefined : localStorage),
  }), []);
  const journalWhoRef = useRef(journalWho);
  // W5b: the journal as last loaded or written by this tab (JSON of {past, future}); a write happens only when the journal differs
  const journalWritten = useRef("");
  // W5: window_back / window_restore_* / window_placements - the settings fields override the presets of layouts.json
  const windowData = useMemo(() => windowDataOf(config), [config]);
  const windowDataRef = useRef(windowData);
  windowDataRef.current = windowData;
  // W5: the device class of this screen (key of window_layout_active and window_layout_journal), the own layout active on it
  const [deviceClass, setDeviceClass] = useState(() => deviceClassOf(window.innerWidth));
  useEffect(() => { const on = () => setDeviceClass(deviceClassOf(window.innerWidth)); window.addEventListener("resize", on); return () => window.removeEventListener("resize", on); }, []);
  const [ownActive, setOwnActive] = useState<string | null>(null);
  const journalReady = useRef(false);
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
      api.getConfig().then(cfg => {
        if (!live) return;
        setConfig(cfg as Record<string, unknown>);
        const j = readWriterJournal((cfg as Record<string, unknown>).window_layout_journal, deviceClassOf(window.innerWidth), journalWhoRef.current);
        journalWritten.current = JSON.stringify({ past: j.past, future: j.future });
        setWin(s => (s.past.length || s.future.length ? s : { ...s, past: j.past, future: j.future, limit: j.limit }));
        journalReady.current = true;
      }, () => { journalReady.current = true; });
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
      configRef.current = cfg;
      const wd = windowDataOf(cfg);
      // W5: the own layout active on this device class (window_layout_active) opens first; layout_mode=windows opens the last own layout
      // (the active one, else the most recently saved) and `layout:<id>` may name an own one; with no own layout the start is as before W5
      const cls = deviceClassOf(window.innerWidth);
      const named = readNamed(cfg.window_layouts);
      const modeId = layoutIdOfMode(cfg.layout_mode, (WINDOW_DATA as { layout_modes?: Record<string, string> }).layout_modes).id;
      const activeId = readActive(cfg.window_layout_active, cls, journalWhoRef.current.installation);
      const own = (activeId ? findNamed(named, activeId) : undefined) ?? findNamed(named, modeId) ?? (modeId === "last" ? lastOwn(named, null) : null) ?? null;
      const last = readSaved(localStorage);
      if (own) {
        setLayout(own.layout.id);
        // the last session's windows of the same own layout are newer than the saved layout itself
        const recent = resolved.source === "last_saved" && wd.window_restore_last !== false && last?.own === own.id && last.windows && last.windows.id === own.layout.id ? last.windows : own.layout;
        setWin(createState(recent, JOURNAL_LIMIT));
        if (recent === last?.windows) couplingRef.current?.restore(last?.values, recent);
        setOwnActive(own.id);
      } else setLayout(resolved.layout ?? layoutOfMode(cfg.layout_mode));
      // W2: the last session's windows come back with its layout (window_layout_last; preset window_restore_last, false = the layout only)
      const saved = !own && resolved.source === "last_saved" && wd.window_restore_last !== false ? last?.windows ?? null : null;
      if (saved && saved.id === resolved.layout) {
        // window_restore_front (preset `layout`): the window the layout's preset maximizes (the chat) is in front after a start, the others
        // wait in the switcher, as the start looked before W2; `saved` = the window that was in front when the page was left
        let restored = createState(saved, JOURNAL_LIMIT);
        const presetFront = LAYOUTS[saved.id]?.windows.find(w => w.mode === "maximized")?.id;
        restored = restoreFront(restored, presetFront, wd.window_restore_front ?? "layout");
        setWin(restored);
        couplingRef.current?.restore(last?.values, restored.layout);   // W8: zoom/pan… as they were (wait for the first read)
      }
      // W5: the journal of layout changes of this device class (window_layout_journal) survives a reload
      const j = readWriterJournal(cfg.window_layout_journal, cls, journalWhoRef.current);
      journalWritten.current = JSON.stringify({ past: j.past, future: j.future });
      setWin(s => ({ ...s, past: j.past, future: j.future, limit: j.limit }));
      journalReady.current = true;
      setActiveConvId(resolved.conversationId);
      setStarted(true);
    })();
    return () => { live = false; };
  }, []);

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
    if (before === after || after === null || (windowDataRef.current.window_back ?? "visits") === "none") return;
    setVisits(v => (v.length ? v : before ? [before] : []).concat(after));
  }, []);
  const visitBack = useCallback(() => {
    const mode = windowDataRef.current.window_back ?? "visits";
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
  // W5: the journal (undo/redo) is written a moment after the last change. W5b: as this tab's own record (writers.<client id>.<class>),
  // merged on the host - two tabs or two devices never overwrite each other's journal (before W5b the whole value was written: the
  // last writer won and the other tab's steps were lost)
  useEffect(() => {
    if (!journalReady.current) return;
    const timer = setTimeout(() => {
      // written only when the journal differs from what this tab loaded or wrote (a start with an empty journal writes nothing)
      const now = JSON.stringify({ past: win.past, future: win.future });
      if (now === journalWritten.current) return;
      journalWritten.current = now;
      mergeSetting("window_layout_journal", journalPatch(configRef.current.window_layout_journal, deviceClassOf(window.innerWidth), win, journalWhoRef.current, new Date().toISOString()));
    }, 600);
    return () => clearTimeout(timer);
  }, [win.past, win.future, mergeSetting]);
  // W5: the operations of named layouts - the sheet „Układ ▾” and the catalogue actions layout.* (a sentence) call the same functions
  const layoutOps = useMemo(() => {
    const cls = () => deviceClassOf(window.innerWidth);
    const named = () => readNamed(configRef.current.window_layouts);
    // W5b: only the layouts this tab changed go out (a merge patch, null = deleted): another tab's layouts saved meanwhile stay
    const writeNamed = (after: ReturnType<typeof named>) => mergeSetting("window_layouts", namedPatch(configRef.current.window_layouts, named(), after));
    const setActive = (id: string | null) => {
      setOwnActive(id);
      const inst = journalWhoRef.current.installation;
      if (readActive(configRef.current.window_layout_active, cls(), inst) !== id) mergeSetting("window_layout_active", activePatch(configRef.current.window_layout_active, cls(), id, inst));
    };
    return {
      save(label: string): string | null {
        if (!label.trim()) return null;
        const r = saveNamed(named(), label, winRef.current.layout, new Date().toISOString(), cls());
        writeNamed(r.list);
        setActive(r.entry.id);
        return r.entry.label;
      },
      /** An own layout first, then a shipped one; the label applied, or null when the name names none. */
      apply(name: string): string | null {
        const own = findNamed(named(), name);
        if (own) { moveTo(replaceLayout(winRef.current, own.layout, "layout.apply")); setLayoutKnown(true); setActive(own.id); return own.label; }
        const id = shippedId(name, LAYOUTS);
        if (!id) return null;
        moveTo(replaceLayout(winRef.current, LAYOUTS[id], "layout.apply")); setLayoutKnown(true); setActive(null);
        return labelOf(LAYOUTS[id], uiLanguageRef.current);
      },
      rename(id: string, label: string): string | null {
        const r = renameNamed(named(), id, label);
        if (r.conflict) return r.conflict;
        writeNamed(r.list);
        return null;
      },
      remove(id: string) {
        writeNamed(deleteNamed(named(), id));
        if (readActive(configRef.current.window_layout_active, cls(), journalWhoRef.current.installation) === id) setActive(null);
      },
      undo(): boolean { const r = undoLayout(winRef.current); if (r.applied) moveTo(r.state); return r.applied; },
      redo(): boolean { const r = redoLayout(winRef.current); if (r.applied) moveTo(r.state); return r.applied; },
      reset(): string {
        const id = resetTarget(winRef.current.layout.id, cls(), LAYOUTS);
        moveTo(replaceLayout(winRef.current, LAYOUTS[id], "layout.reset")); setLayoutKnown(true); setActive(null);
        return labelOf(LAYOUTS[id], uiLanguageRef.current);
      },
      /** A layout chosen from the menu (Czat / Siatka): no own layout is active on this class any more. */
      clearActive() { if (readActive(configRef.current.window_layout_active, cls(), journalWhoRef.current.installation)) setActive(null); },
    };
  }, [moveTo, saveSetting, mergeSetting]);
  const pickLayout = useCallback((name: string) => { setLayout(name); layoutOps.clearActive(); }, [setLayout, layoutOps]);
  // Animation and disclosure glyphs are data: the resolved tables become CSS variables (prefers-reduced-motion selects the reduced-motion style).
  useEffect(() => {
    const reduced = typeof window.matchMedia === "function" && window.matchMedia("(prefers-reduced-motion: reduce)").matches;
    const vars = { ...cssVariables(mergeData(config), reduced), ...glyphVariables(config.disclosure_glyphs, config.disclosure_glyph_sets) };
    for (const [k, v] of Object.entries(vars)) document.documentElement.style.setProperty(k, v);
  }, [config]);

  // W8 (OKNA §7.1): „Otwórz w oknie” on a block of an answer - a window of the kind `artifact` (placement: artifacts.json, preset float)
  useEffect(() => {
    const onOpen = (event: Event) => {
      const a = artifactOf((event as CustomEvent).detail?.artifact);
      if (!a) return;
      const r = openArtifactWindow(winRef.current, { ...a }, ARTIFACTS.placement ?? "float", deviceClass, WINDOW_CTX, { ...windowDataRef.current, window_placements: ARTIFACTS.placements ?? {} });
      if (r.receipt.state === "ok") moveTo(r.state);
    };
    window.addEventListener(OPEN_ARTIFACT_EVENT, onOpen);
    return () => window.removeEventListener(OPEN_ARTIFACT_EVENT, onOpen);
  }, [moveTo, deviceClass]);

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
  const uiLanguageRef = useRef(uiLanguage);
  uiLanguageRef.current = uiLanguage;
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
    if (!panel && w?.kind === "artifact" && !w.title_override) { const a = artifactOf(w.params?.artifact); return t("artifact_window", { format: a?.lang || a?.format || "" }); }
    if (!panel) return w?.kind === "chat" ? t("window_chat") : (w?.title_override ?? w?.kind ?? id);
    const name = PANEL_TEXT_IDS[panel] ? t(PANEL_TEXT_IDS[panel]!) : (PANEL_LABELS[panel] ?? panel);
    // W6: the second window of a panel ("open another one beside") is numbered, so its chain and menu name the right one
    const twins = winRef.current.layout.windows.filter(x => x.params?.panel === panel);
    const n = twins.findIndex(x => x.id === id);
    return twins.length > 1 && n > 0 ? `${name} ${n + 1}` : name;
  }, [t]);
  const couplingRef = useRef<CouplingHook | null>(null);
  const styleCtxRef = useRef<{ docs: ApplicationProfile[]; person: ProfileRef | null; mode: unknown }>({ docs: [], person: null, mode: undefined });
  useEffect(() => {
    const known = (id: string) => Object.prototype.hasOwnProperty.call(PANEL_LABELS, id);
    const onStep = (event: Event) => {
      const detail = (event as CustomEvent<WindowStepDetail>).detail;
      if (!detail || detail.outcome) return;
      if (detail.action.startsWith("layout.") && detail.action !== "layout.style") {   // layout.style: below, with window.style
        // W5: layout.save / apply / undo / redo / reset - the same functions as the sheet „Układ ▾”; the receipt says what was done
        const args = detail.args ?? {};
        const name = ["name", "label", "target"].map(k => args[k]).find((v): v is string => typeof v === "string" && v.trim() !== "")?.trim() ?? "";
        const op = detail.action.slice(7);
        const ok = (text: string, extra: Record<string, unknown> = {}) => ({ state: "ok" as const, parts: [{ kind: "text", text }], data: { schema: "loom.layout_receipt/1", op, name, layout: winRef.current.layout.id, device_class: deviceClassOf(window.innerWidth), ...extra } });
        const fail = (text: string) => ({ state: "failed" as const, error: { message: text } });
        if (op === "save") { const label = layoutOps.save(name); detail.outcome = label ? ok(t("layout_receipt_saved", { name: label })) : fail(t("layout_receipt_noname")); }
        else if (op === "apply") { const label = layoutOps.apply(name); detail.outcome = label ? ok(t("layout_receipt_applied", { name: label })) : fail(t("layout_receipt_none", { name })); }
        else if (op === "undo" || op === "redo") {
          const done = op === "undo" ? layoutOps.undo() : layoutOps.redo();
          detail.outcome = ok(t(done ? `layout_receipt_${op}` : `layout_receipt_nothing_${op}`), { state: done ? "ok" : "noop" });
        }
        else if (op === "reset") detail.outcome = ok(t("layout_receipt_reset", { name: layoutOps.reset() }));
        else if (op === "rename" || op === "delete") {
          // W7: a sentence renames or deletes an own layout (delete only after the proposal and a yes: layout.delete is not direct)
          const own = findNamed(readNamed(configRef.current.window_layouts), name);
          const label = typeof args.label === "string" ? args.label.trim() : "";
          if (!own) detail.outcome = fail(t(shippedId(name, LAYOUTS) ? "layout_receipt_shipped" : "layout_receipt_none", { name }));
          else if (op === "delete") { layoutOps.remove(own.id); detail.outcome = ok(t("layout_receipt_deleted", { name: own.label })); }
          else if (!label) detail.outcome = fail(t("layout_receipt_noname"));
          else { const clash = layoutOps.rename(own.id, label); detail.outcome = clash ? fail(t("layouts_rename_clash", { name: label })) : ok(t("layout_receipt_renamed", { name: own.label, label })); }
        }
        else detail.outcome = fail(t("layout_receipt_none", { name: detail.action }));
        return;
      }
      if (detail.action.startsWith("artifact.")) return;   // W-gaps: the chat page that shows the answers carries these out (ChatView)
      if (detail.action === "desktop.show") {
        // R101 D1: the client step of the catalogue action desktop.show = Ctrl+Shift+D (every shown window minimized, one journal entry)
        const before = winRef.current; const next = showDesktop(before); moveTo(next);
        const text = sayDesktop(DESKTOP.words, next === before ? "show_receipt_noop" : "show_receipt", uiLanguageRef.current);
        detail.outcome = { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.window_receipt/1", op: "desktop.show", state: next === before ? "noop" : "ok", windows: [], layout: next.layout.id } };
        return;
      }
      const args = detail.args ?? {};
      const failWith = (text: string) => { detail.outcome = { state: "failed", error: { message: text } }; };
      if (detail.action === "window.style" || detail.action === "layout.style") {
        // W-gaps: the style of a window / of the layout from a sentence - the same operation as the menu „Styl okna” / „Styl układu ▾”
        const sc = styleCtxRef.current;
        const word = typeof args.style === "string" ? args.style : "";
        const found = styleOfWord(word, sc.docs);
        if (!found) { failWith(t("style_receipt_none", { word, known: [...new Set(sc.docs.map(d => d.label ?? d.id))].join(", ") })); return; }
        const ref = "doc" in found ? refOfDoc(found.doc) : null;
        const styleName = "doc" in found ? (found.doc.label ?? found.doc.id) : "";
        const before = winRef.current;
        if (detail.action === "layout.style") {
          moveTo(withSidebarWidth(before, setLayoutProfile(before, ref), sc.person, sc.docs, sc.mode, window.innerWidth));
          const text = ref ? t("style_receipt_layout", { style: styleName }) : t("style_receipt_layout_cleared");
          detail.outcome = { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.style_receipt/1", op: "layout", profile: ref, layout: winRef.current.layout.id } };
          return;
        }
        const tw = typeof args.target === "string" ? args.target.trim() : "";
        const l = before.layout;
        let id: string | null = l.focus ?? null;
        if (tw) {
          const target = resolveWord(tw, known);
          const w = target ? l.windows.find(x => "chat" in target ? x.kind === "chat" && typeof x.params?.panel !== "string" : x.params?.panel === target.panel) : undefined;
          id = w?.id ?? null;
        }
        if (!id || !l.windows.some(x => x.id === id)) { failWith(t("style_receipt_nowindow", { word: tw || "—" })); return; }
        moveTo(withSidebarWidth(before, setWindowProfile(before, id, ref), sc.person, sc.docs, sc.mode, window.innerWidth));
        const text = ref ? t("style_receipt_window", { screen: windowLabel(id), style: styleName }) : t("style_receipt_cleared", { screen: windowLabel(id) });
        detail.outcome = { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.style_receipt/1", op: "window", window: id, profile: ref, layout: winRef.current.layout.id } };
        return;
      }
      if (detail.action === "window.bind" || detail.action === "window.unbind") {
        // W7: a sentence binds (or unbinds) a parameter of every open window of a kind on a shared channel named after the parameter
        const pw = typeof args.param === "string" ? args.param : "";
        const param = paramOfWord(pw);
        if (!param) { failWith(t("window_receipt_noparam", { word: pw })); return; }
        const word = typeof args.target === "string" ? args.target : "";
        const target = word ? resolveWord(word, known) : null;
        if (word && !target) { failWith(t("window_receipt_none", { word })); return; }
        const l = winRef.current.layout;
        const kindOf = (w: Win) => String(w.params?.panel ?? w.kind);
        let ids = l.windows.filter(w => paramsOfKind(kindOf(w)).includes(param) && (!target || ("chat" in target ? w.kind === "chat" && !w.params?.panel : w.params?.panel === target.panel))).map(w => w.id);
        const pName = paramWord(param, uiLanguageRef.current);
        if (detail.action === "window.bind") {
          if (ids.length < 2) { failWith(t("window_receipt_fewer", { param: pName, count: String(ids.length) })); return; }
          couplingRef.current?.bindAll(ids, param);
        } else {
          ids = ids.filter(id => param in bindingsOf(l.windows.find(w => w.id === id)));
          couplingRef.current?.unbindAll(ids, param);
        }
        const text = t(detail.action === "window.bind" ? "window_receipt_bound" : "window_receipt_unbound", { param: pName, screen: ids.map(windowLabel).join(", ") || "0" });
        detail.outcome = { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.window_receipt/1", op: detail.action.slice(7), state: ids.length ? "ok" : "noop", windows: ids, param, layout: winRef.current.layout.id } };
        return;
      }
      if (/^window\.(maximize|restore|minimize|dock|float|tab|move|resize)$/.test(detail.action)) {
        // W7: arranging a window by a sentence - the same functions as the menu ⋯ and the keys, one journal entry
        const r = runArrangeStep(winRef.current, detail.action, args, known, deviceClassOf(window.innerWidth), { w: window.innerWidth, h: window.innerHeight }, undefined, { ...windowDataRef.current, ...STEP_DATA });
        moveTo(r.state);
        const names = r.receipt.windows.map(windowLabel).join(", ");
        if (r.receipt.state === "none") {
          const sideWord = typeof args.side === "string" ? args.side : typeof args.direction === "string" ? args.direction : "";
          failWith(r.receipt.word && r.receipt.word === sideWord ? t("window_receipt_noside", { word: sideWord }) : t("window_receipt_none", { word: r.receipt.word ?? "" }));
          return;
        }
        const text = t("window_receipt_arranged", { screen: names, how: r.receipt.how ?? r.receipt.op });
        detail.outcome = { state: "ok", parts: [{ kind: "text", text }], data: { schema: "loom.window_receipt/1", ...r.receipt, layout: r.state.layout.id } };
        return;
      }
      const deviceClass = deviceClassOf(window.innerWidth);
      const { state, receipt } = runWindowStep(winRef.current, detail.action, detail.args ?? {}, known, deviceClass, undefined, windowDataRef.current);
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
  }, [moveTo, t, windowLabel, layoutOps]);
  // W9 (OKNA §7.2, D14): the style of a window / of the layout = an application profile named in the layout (window.profile, layout.profile),
  // inherited window → layout → the person's profile (the one chosen in the workbench, else none) → the preset fallback. Choosing one is a
  // journaled operation of the layout (undo, last layout, named layouts written by the W5b merge). UI17: only CSS variables change.
  // W-gaps (W10 gap 4): the profiles imported in the Workshop join the list (re-read when the person's profile changes there)
  const styleDocs = useMemo(() => [...(BUILTIN_PROFILE_DOCUMENTS as ApplicationProfile[]), ...importedStyleDocs<ApplicationProfile>()], [primaryProfile]);   // eslint-disable-line react-hooks/exhaustive-deps
  const personStyle = useMemo<ProfileRef | null>(() => (primaryProfile ? refOfDoc(primaryProfile) : null), [primaryProfile]);
  styleCtxRef.current = { docs: styleDocs, person: personStyle, mode: config.profile_layout_fields };
  const styleOf = useCallback((id: string | null) => resolveStyle(win.layout, id, personStyle, styleDocs), [win.layout, personStyle, styleDocs]);
  const styleChoice = useCallback((current: unknown, inherit: string, label: string) => {
    const ref = refOf(current); const doc = findProfile(styleDocs, ref);
    const value = ref ? (doc ? `${doc.id}@${doc.profile_revision}` : refKey(ref)) : "";
    const options = [{ value: "", label: inherit }, ...styleDocs.map(d => ({ value: `${d.id}@${d.profile_revision}`, label: `${d.label} · r${d.profile_revision}` }))];
    if (ref && !doc) options.push({ value, label: `? ${ref.id}${ref.revision === null ? "" : `@${ref.revision}`}` });
    return { label, value, options };
  }, [styleDocs]);
  const windowStyleMenu = useCallback((id: string) => {
    const w = win.layout.windows.find(x => x.id === id);
    return w ? styleChoice(w.profile, inheritLabel(win.layout, id, personStyle, styleDocs, uiLanguage), sayStyle(STYLES.words.window_menu, uiLanguage)) : null;
  }, [win.layout, personStyle, styleDocs, uiLanguage, styleChoice]);
  const setWindowStyle = useCallback((id: string, value: string) => moveTo(withSidebarWidth(winRef.current, setWindowProfile(winRef.current, id, value ? refOf(value) : null), personStyle, styleDocs, config.profile_layout_fields, window.innerWidth)), [moveTo, personStyle, styleDocs, config.profile_layout_fields]);
  const styleAdvice = useMemo(() => styleAdvisories(win.layout, personStyle, styleDocs, config.profile_layout_fields), [win.layout, personStyle, styleDocs, config.profile_layout_fields]);
  /** The content of a window inside its style: a wrapper only when the window or the layout names a style (otherwise nothing changes). */
  const styled = (w: Win, node: ReactNode): ReactNode => {
    const r = styleOf(w.id);
    // W-gaps: the same element whether a style applies or not, so choosing or clearing a style never remounts the window's content (a chat
    // that asked „przywróć styl domyślny” must stay mounted to answer its own client step); without a style it is an empty display:contents box.
    // CSS that addresses the content of a leaf as its child (`.dock-leaf > .main-panel`) also names `.dock-leaf > .win-box > …`
    if (!r.profile || (r.source !== "window" && r.source !== "layout")) return <div className="win-box" style={{ display: "contents" }}>{node}</div>;
    return <div className="win-box win-style" style={{ display: "contents", ...profileStyle(r.profile) }} data-testid="window-style-root" data-window={w.id}
      data-style-profile={r.profile.id} data-style-revision={r.profile.profile_revision} data-style-source={r.source}>{node}</div>;
  };
  // W5: what the sheet „Układ ▾” shows (the own layouts of the person, the one active on this device class, the shipped ones)
  const shellLayouts = useMemo<ShellLayouts>(() => {
    const own = readNamed(config.window_layouts);
    return {
      own: own.map(l => ({ id: l.id, label: l.label, active: l.id === ownActive })),
      shipped: Object.keys(LAYOUTS).map(id => ({ id, label: labelOf(LAYOUTS[id], uiLanguage), current: ownActive === null && win.layout.id === id })),
      deviceClass, canUndo: win.past.length > 0, canRedo: win.future.length > 0,
      save: (label: string) => { layoutOps.save(label); },
      apply: (id: string) => { layoutOps.apply(id); },
      rename: (id: string, label: string) => layoutOps.rename(id, label),
      remove: (id: string) => layoutOps.remove(id),
      undo: () => { layoutOps.undo(); }, redo: () => { layoutOps.redo(); }, reset: () => { layoutOps.reset(); },
      style: { ...styleChoice(win.layout.profile, inheritLabel(win.layout, null, personStyle, styleDocs, uiLanguage), sayStyle(STYLES.words.layout_menu, uiLanguage)),
        set: (value: string) => moveTo(withSidebarWidth(winRef.current, setLayoutProfile(winRef.current, value ? refOf(value) : null), personStyle, styleDocs, config.profile_layout_fields, window.innerWidth)) },
    };
  }, [config.window_layouts, ownActive, uiLanguage, win.layout.id, win.layout.profile, win.layout, personStyle, styleDocs, styleChoice, moveTo, win.past.length, win.future.length, deviceClass, layoutOps, config.profile_layout_fields]);
  const shellWindows = useMemo(() => {
    const front = visibleId(win.layout);
    return {
      // R101 P0: every window closes from the switcher (the chat too): the desktop stays under the last one
      entries: switcherEntries(win.layout).map(w => ({ id: w.id, label: windowLabel(w.id), front: w.id === front, closable: true })),
      focus: (id: string) => moveTo(focusWindow(winRef.current, id)),
      arrangeMenu: (ARRANGE.menu ?? []).map(m => ({ id: m.id, label: (uiLanguage === "pl" ? m.label.pl : m.label.en) ?? m.id, icon: m.icon })),
      arrange: (id: string, how: string) => moveTo(arrangeWindow(winRef.current, id, how)),
      close: (id: string) => {
        const w = winRef.current.layout.windows.find(x => x.id === id);
        if (w && typeof w.params?.panel === "string") moveTo(closeWindows(winRef.current, { target: { panel: String(w.params.panel) } }).state);
        else if (w) moveTo(oneEntry(winRef.current, applyOp(winRef.current, { op: "close", id }, WINDOW_CTX).state, "close"));
      },
      layouts: shellLayouts,
      style: windowStyleMenu, setStyle: setWindowStyle,
    };
  }, [win.layout, windowLabel, moveTo, uiLanguage, shellLayouts, windowStyleMenu, setWindowStyle]);
  // W4: docking and floating (menu ⋯, drag, split grips, keys) and the advisory chips of the arrangement
  // W6: coupling of window parameters (bindings in the layout, values at runtime, chips with the arrangement ones)
  const couplingData = useMemo(() => couplingDataOf(config), [config.coupling_max_iterations]);   // eslint-disable-line react-hooks/exhaustive-deps
  const coupling = useCoupling(win, winRef, moveTo, uiLanguage, windowLabel, couplingData);
  couplingRef.current = coupling;
  useEffect(() => { if (started && layout) writeSaved(localStorage, { layout, conversationId: activeConvId, windows: windowData.window_restore_last === false ? null : win.layout, own: ownActive, values: coupling.values }); }, [started, layout, activeConvId, win.layout, windowData, ownActive, coupling.values]);
  // W10 (OKNA §8.3): which hidden windows stay mounted (window_keep_mounted, preset per device class) and the advisory many_windows
  const mountShown = useCallback((w: Win) => !w.collapsed && (w.kind !== "knowledge" || knowledgeOpen), [knowledgeOpen]);
  const recentRef = useRef<string[]>([]);
  const mount = useMemo(() => {
    const { policy, unknown } = keepPolicy(config.window_keep_mounted, deviceClass);
    recentRef.current = touchRecent(recentRef.current, shownIds(win.layout, mountShown), win.layout);
    const keep = keptHidden(win.layout, policy, recentRef.current, mountShown);
    const soft = config.window_mounted_soft;
    const data = typeof soft === "number" && Number.isFinite(soft) ? { ...MOUNT, mounted_soft: { [deviceClass]: soft } } : MOUNT;
    return { keep, advisories: mountAdvisories(win.layout, mountedIds(win.layout, policy, recentRef.current, mountShown), deviceClass, policy, unknown, data) };
  }, [config.window_keep_mounted, config.window_mounted_soft, deviceClass, win.layout, mountShown]);
  usePruneKept(win.layout);
  // R101 P0 (PULPIT §7, §10, §12): the desktop under the windows - always mounted, seen where no window covers it; content from data
  // (shell/data/desktop.json) and settings (desktop_sections, desktop_emergency_path, last_window_close).
  const deskSettings = useMemo(() => desktopSettings(config), [config.desktop_sections, config.desktop_emergency_path]);   // eslint-disable-line react-hooks/exhaustive-deps
  const deskEntries = useMemo(() => kindEntries((id) => Object.prototype.hasOwnProperty.call(PANEL_LABELS, id), win.layout.id, uiLanguage), [win.layout.id, uiLanguage]);
  const deskCover = desktopCover(win.layout, mountShown);
  const deskLayouts = useMemo(() => ({ shipped: shellLayouts.shipped.map(l => ({ id: l.id, label: l.label })), own: shellLayouts.own.map(l => ({ id: l.id, label: l.label })) }), [shellLayouts]);
  const deskPaths = useMemo(() => desktopPaths(win.layout, deskSettings, deskEntries, [...deskLayouts.shipped, ...deskLayouts.own].map(l => l.id)), [win.layout, deskSettings, deskEntries, deskLayouts]);
  const deskOpen = useCallback((target: KindTarget) => moveTo(openFromDesktop(winRef.current, target, deviceClassOf(window.innerWidth))), [moveTo]);
  // R101 D2: a tap on a desktop icon opens its target (open_targets in desktop.json): a navigator icon with its focus, a conversation in the chat
  const deskIcon = useCallback((icon: DesktopIcon) => {
    if (!icon.open) return;
    if (icon.open.focus) setNavigatorFocus(icon.open.focus);
    if (icon.open.conversation) setActiveConvId(icon.open.conversation);
    deskOpen(icon.open.target);
  }, [deskOpen]);
  const deskLoad = useCallback((id: string, own: boolean) => {
    if (!own && winRef.current.layout.id === id && ownActive === null) layoutOps.reset(); else layoutOps.apply(own ? (readNamed(configRef.current.window_layouts).find(l => l.id === id)?.label ?? id) : id);
    setLayoutKnown(true);
  }, [layoutOps, ownActive]);
  const [ended, setEnded] = useState(false);
  const lastLayoutRef = useRef(win.layout);
  useEffect(() => {
    const before = lastLayoutRef.current; lastLayoutRef.current = win.layout;
    if (!closedLast(before, win)) return;
    const what = lastWindowClose(config.last_window_close);
    if (what.do === "close_app") {
      setEnded(true);
      const shell = (window as unknown as { LoomBridge?: { finish?: () => void } }).LoomBridge;
      try { if (typeof shell?.finish === "function") shell.finish(); else window.close(); } catch (e) { console.debug("last_window_close: the host refused to close", e); }
    } else if (what.do === "reopen") { const e = deskEntries.find(x => x.kind === what.kind); if (e?.target) deskOpen(e.target); }
    else if (what.do === "layout") deskLoad(what.id, readNamed(configRef.current.window_layouts).some(l => l.id === what.id));
    else if ("unknown" in what) console.debug(`last_window_close: unknown value ${what.unknown}, the desktop stays`);
  }, [win]);   // eslint-disable-line react-hooks/exhaustive-deps
  // the keys of desktop.json (preset ctrl+shift+d: show the desktop = every window minimized, one journal entry)
  useEffect(() => {
    const on = (e: KeyboardEvent) => {
      const what = DESKTOP.keys?.[comboOf(e)];
      if (what !== "show_desktop") return;
      const tg = e.target as HTMLElement | null;
      if (tg && (tg.isContentEditable || /^(INPUT|TEXTAREA|SELECT)$/.test(tg.tagName))) return;
      e.preventDefault(); moveTo(showDesktop(winRef.current));
    };
    window.addEventListener("keydown", on);
    return () => window.removeEventListener("keydown", on);
  }, [moveTo]);
  const desktop = <Desktop layout={win.layout} cover={deskCover} lang={uiLanguage} settings={deskSettings} entries={deskEntries} paths={deskPaths} layouts={deskLayouts}
    title={windowLabel} openKind={deskOpen} restore={(id) => moveTo(restoreFromDesktop(winRef.current, id))} loadLayout={deskLoad}
    sentence={(text) => { deskOpen({ chat: true }); handSentence(text, DESKTOP.sentence.send !== false); }} enableEmergency={() => saveSetting("desktop_emergency_path", true)}
    api={api} deviceClass={deviceClass} declaration={config.desktop_declaration} openIcon={deskIcon} />;
  const advice = useMemo(() => {
    const more = [...styleAdvice, ...mount.advisories];
    return more.length ? [...coupling.advisories, ...more] : coupling.advisories;
  }, [coupling.advisories, styleAdvice, mount.advisories]);
  const arrangedRaw = useArrange(win, winRef, moveTo, uiLanguage, windowLabel, advice);
  const arranged = useMemo(() => ({ ...arrangedRaw, handlers: { ...arrangedRaw.handlers, extraMenu: coupling.extraMenu, onExtra: coupling.onExtra, chain: coupling.chain, onChainTap: coupling.cycleChain,
    styleMenu: (w: Win) => windowStyleMenu(w.id), onStyle: setWindowStyle } }), [arrangedRaw, coupling, windowStyleMenu, setWindowStyle]);
  const [panelMenu, setPanelMenu] = useState(false);
  useEffect(() => { if (activePanel === null) setPanelMenu(false); }, [activePanel]);
  // the layout shows one window full-screen (the plain chat app is that case); a grid of docks (the former Workshop) is not
  const fullWindow = layout !== null && baseWindow(win.layout) !== null;
  useLayer("knowledge-sheet", knowledgeOpen && fullWindow, () => setKnowledgeOpen(false));
  // Each overlay is a dialog for a reader of the screen: focus enters it when it opens and returns to the ⋮ button when it closes.
  const panelRef = useRef<HTMLDivElement>(null), knowledgeRef = useRef<HTMLDivElement>(null);
  useDialogFocus(activePanel !== null, panelRef, "shell-menu-toggle");
  useDialogFocus(knowledgeOpen && fullWindow, knowledgeRef, "shell-menu-toggle");

  const panelTitle = activePanel ? (PANEL_TEXT_IDS[activePanel] ? t(PANEL_TEXT_IDS[activePanel]!) : PANEL_LABELS[activePanel]) : "";
  const topKind = visibleOverlay(win.layout)?.kind ?? null;   // the kind decides the component (settings = SettingsHuman, settings_fields = SettingsPanel)
  // W4: the content of a panel, the same in the drawer (maximized), in a dock leaf and in a floating window
  const panelContent = (p: PanelId, kind: string | null): ReactNode => (<>
                {p === "graph" && <GraphView convId={activeConvId} profileHost={userProfileHost} helpSetting={config.graph_view_help_open} onSetting={rememberSetting} />}
                {p === "graph-entry" && <GraphEntryPanel profileHost={userProfileHost} openers={entryOpeners} />}
                {p === "navigator" && <GraphNavigator profileHost={userProfileHost} convId={activeConvId} language={uiLanguage} initialFocus={navigatorFocus} initialPerspectives={navigatorPerspectives} layout={config.navigator_layout} mapping={config.navigator_mapping} helpSetting={config.navigator_help_open} onSetting={rememberSetting}
                  meOpenSetting={config.me_open_sections} meInferredOpacity={config.me_inferred_opacity} onOpenConversation={id => { onConversationCreated(id); closePanel(); }} />}
                {p === "perspectives" && <PerspectivesPanel convId={activeConvId} />}
                {p === "view-engine" && <ViewEnginePanel />}
                {p === "request" && <RequestInspector api={api} convId={activeConvId} />}
                {p === "conversations" && <ConversationList key={convRefreshKey} activeConvId={activeConvId}
                  onSelect={id => { setActiveConvId(id); closePanel(); deskOpen({ chat: true }); }} onCreated={id => { onConversationCreated(id); closePanel(); deskOpen({ chat: true }); }} onActivity={sidebarActivity} />}
                {p === "memory" && <MemoryPanel />}
                {p === "import" && <ImportPanel onImported={() => setConvRefreshKey((k) => k + 1)} />}
                {p === "context" && <ContextSlider convId={activeConvId} />}
                {p === "settings" && kind === "settings_fields" && <SettingsPanel />}
                {p === "settings" && kind !== "settings_fields" && <SettingsHuman profileHost={userProfileHost} convId={activeConvId} language={uiLanguage} view={settingsView}
                  onNavigate={address => { setNavigatorFocus(address); setActivePanel("navigator"); }} />}
                {p === "logs" && <LogPanel />}
                {p === "methods" && <MethodsPanel transport={api} />}
                {p === "analysis" && <AnalysisPanel />}
                {p === "onboarding" && <UserProfilePanel view="onboarding" host={userProfileHost} />}
                {p === "user-knowledge" && <UserProfilePanel view="knowledge" host={userProfileHost} />}
                {p === "operations" && <OperationsPanel operations={api.operations}
                  profiles={primaryProfile ? [primaryProfile] : []} graphPacketStore={api.graphPacketStore?.bind(api)}
                  adapterEvidence={(primaryProfile?.actions ?? []).filter(action => {
                    const gaps = installedProfile ? profileAvailability(installedProfile, installedAdapters) : null;
                    return ![...(gaps?.requiredGaps ?? []), ...(gaps?.optionalGaps ?? [])].some(gap => gap.action_id === action.id);
                  }).map(action => ({
                    operation: action.operation, capability: action.capability, status: "native" as const,
                    detail: "Installed Loom adapter; source-service equivalence unverified.", evidence: ["host profile registry"],
                  }))} />}
  </>);
  const panels = activePanel && (

          <>
            <div className="drawer-backdrop" onClick={closePanel} data-testid="drawer-backdrop" />
            <div className="drawer" data-testid={`panel-${activePanel}`} ref={panelRef} role="dialog" aria-modal="true" aria-label={panelTitle} tabIndex={-1}>
              <div className="drawer-header">
                <h2>{panelTitle}</h2>
                <button className="icon-btn" aria-label={arranged.handlers.menuLabel(panelTitle)} aria-expanded={panelMenu} data-testid="panel-arrange-menu" onClick={() => setPanelMenu(v => !v)}>⋯</button>
                <button className="icon-btn" aria-label={t("panel_close", { screen: panelTitle })} onClick={closePanel} data-testid="close-panel">
                  ×
                </button>
              </div>
              {panelMenu && <div className="win-menu" role="menu" data-testid="panel-arrange-list">
                {arranged.handlers.menu.map(m => <button key={m.id} type="button" role="menuitem" data-testid={`panel-arrange-${m.id}`}
                  onClick={() => { setPanelMenu(false); const id = visibleOverlay(winRef.current.layout)?.id; if (id) arranged.handlers.onArrange(id, m.id); }}>{m.icon ? `${m.icon} ` : ""}{m.label}</button>)}
              </div>}
              <div className="drawer-body">
                {(() => { const top = visibleOverlay(win.layout); const body = panelContent(activePanel, topKind); return top ? styled(top, coupling.provide(top, body)) : body; })()}
              </div>
            </div>
          </>
        
  );
  if (layout === null) return null;
  // W3 (OKNA §3.1): the former Workshop is the layout `siatka` drawn from its dock tree (WindowHost → DockTree). Its header is gone: the
  // buttons it had are the entries of the switcher bar (switcher.style "bar" in layouts.json) - the panels as tabs, the list handle,
  // knowledge, theme, account. An open panel is a modal window over the layout: the bar and the docked windows behind it are inert.
  // W4: a layout whose preset shows one window full-screen (czat) and now draws its dock tree (a panel docked next to the chat) has no
  // switcher bar: the chat keeps its own bar and menu.
  const presetFront = LAYOUTS[win.layout.id]?.windows.find(w => w.mode === "maximized")?.id;
  // W7 (§6.4): a reader of the screen hears which window is in front when it changes (aria-live polite, words of the shell)
  const frontId = visibleId(win.layout) ?? win.layout.focus;
  const announce = <div className="sr-only" role="status" aria-live="polite" data-testid="window-announce">{deskCover === "none" ? t("window_announce", { screen: say(DESKTOP.words, "title", uiLanguage) }) : frontId ? t("window_announce", { screen: windowLabel(frontId) }) : ""}</div>;
  const grid = (body: ReactNode, overlay: ReactNode) => presetFront ? (
    <div className="app-root dock-root" data-layout={win.layout.id} data-testid="dock-root">
      <div className="app-layout">{body}{overlay}</div>
    </div>
  ) : workshop(body, overlay);
  // W9: a style named by the layout replaces the person's profile on the root of the grid (the window ones sit inside their windows).
  // The list LEAF of the dock tree follows only the person's profile (data-sidebar-leaf-side, W3); a style named by the layout or a window
  // only suggests its side (advisory profile_moves_dock, fix „Przestaw”) - before W-gaps that held only because its wrapper hid the leaf from the CSS.
  const layoutStyle = (() => { const r = styleOf(null); return r.source === "layout" ? r.profile : null; })();
  const workshop = (body: ReactNode, overlay: ReactNode) => (
    <div className="app-root" data-sidebar-side={(layoutStyle ?? primaryProfile)?.presentation.sidebar.side ?? "left"} data-sidebar-leaf-side={primaryProfile?.presentation.sidebar.side ?? "left"} data-layout={win.layout.id}
      data-style-profile={layoutStyle?.id} style={layoutStyle ? profileStyle(layoutStyle) : primaryProfile ? profileStyle(primaryProfile) : undefined}>
      <div className="app-header window-switcher" role="toolbar" aria-label={shellText("menu_label")} data-testid="window-switcher"
        onKeyDown={(event) => { if (event.key === "Escape") setNavOpen(false); }} {...inertAttr(activePanel !== null)}>
        <button
          className="icon-btn"
          aria-label="Toggle conversation list"
          data-testid="toggle-sidebar"
          aria-pressed={sidebarOpen}
          onClick={() => setSidebarOpen((v) => !v)}
        >
          ≡
        </button>
        <div className="spacer" />
        <button className="nav-toggle" aria-label={shellText("menu_label")} aria-expanded={navOpen} aria-controls="app-nav" data-testid="toggle-nav"
          onClick={() => setNavOpen((v) => !v)}>
          {shellText("menu")}
        </button>
        <nav className={navOpen ? "app-nav open" : "app-nav"} id="app-nav" data-testid="app-nav">
          <button data-testid="nav-chat" onClick={() => { pickLayout("chat"); setNavOpen(false); }}>{t("workshop_back")}</button>
          <SemanticStatus profileHost={userProfileHost} />
          <button data-testid="nav-knowledge" data-tab="knowledge" aria-pressed={knowledgeOpen} onClick={() => { setKnowledgeOpen((value) => !value); setNavOpen(false); }}>Knowledge</button>
          {knowledgeOpen && <button aria-pressed={chatVisible} onClick={() => { setChatVisible((value) => !value); setNavOpen(false); }}>{chatVisible ? "Hide chat" : "Show chat"}</button>}
          {(Object.keys(PANEL_LABELS) as PanelId[]).map((p) => (
            <button
              key={p}
              data-testid={`nav-${p}`}
              data-tab={p}
              data-window-kind={panelKind(p, win.layout.id)}
              aria-pressed={activePanel === p}
              onClick={() => openPanel(p)}
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
      </div>

      <div className="app-layout">
        {body}
        {overlay}
      </div>
    </div>
  );
  // W3: the docked windows of `siatka` by kind (the dock tree decides where and how big; the kind decides what).
  const docked = (w: Win): ReactNode => {
    if (w.kind === "artifact") return <ArtifactWindow value={w.params?.artifact} scripts={scriptsOn(config)} win={w} />;
    if (w.kind === "conversation_list") return (
      <aside className={`sidebar${sidebarOpen ? "" : " collapsed"}`} data-testid="sidebar" {...inertAttr(activePanel !== null)}>
        <ConversationList key={convRefreshKey} activeConvId={activeConvId} onSelect={onSelectConversation} onCreated={onConversationCreated} onActivity={sidebarActivity} />
      </aside>);
    if (w.kind === "chat" && w.id === presetFront) return <div className="chat-host dock-chat" data-window={w.id}>{renderMain(w)}</div>;
    if (w.kind === "chat") return (
      <div className="chat-host main-panel" data-window={w.id} hidden={knowledgeOpen && !chatVisible} {...inertAttr(activePanel !== null)}>
        <ApplicationProfiles profileHost={userProfileHost} convId={activeConvId} onConversationCreated={onConversationCreated}
          refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} ui={profileUi}
          onPrimaryProfile={setPrimaryProfile} onSharedConversationRestored={setActiveConvId}
          canRestoreWorkspace={canRestoreWorkspace} styleOverride={(() => { const r = styleOf(w.id); return r.profile && (r.source === "window" || r.source === "layout") ? r.profile : null; })()} />
      </div>);
    if (w.kind === "knowledge") return (
      <div className="main-panel with-workbench" data-window={w.id} {...inertAttr(activePanel !== null)}>
        <KnowledgeWorkbench profileHost={userProfileHost} onClose={() => setKnowledgeOpen(false)} onDataChanged={() => setConvRefreshKey((key) => key + 1)} />
      </div>);
    return null;   // a kind without a docked view yet (W4+): its leaf is drawn empty, the advisory is the model's
  };
  const dockShown = (w: Win) => !w.collapsed && (w.kind !== "knowledge" || knowledgeOpen);
  function renderMain(w: Win): ReactNode { return w.kind !== "chat" ? null : (
      <ChatShell profileHost={userProfileHost} convId={activeConvId} onSelect={setActiveConvId} onCreated={onConversationCreated} onShowInGraph={openNavigator}
        refreshKey={convRefreshKey} onMessagesChanged={onMessagesChanged} windows={shellWindows} behindOverlay={activePanel !== null || knowledgeOpen}
        actions={{ panel: (id) => { if (id === "settings") setSettingsView(""); setActivePanel(id as PanelId); }, navigator: (focus) => { setNavigatorFocus(focus); setActivePanel("navigator"); }, knowledge: () => setKnowledgeOpen(true), workshop: () => pickLayout("workshop") }} />); }
  const host = <WindowHost state={win} renderOverlay={() => panels} grid={grid} renderDocked={w => styled(w, docked(w))} shown={dockShown} title={(w) => windowLabel(w.id)} renderWindow={w => styled(w, renderMain(w))}
    renderPanel={w => styled(w, coupling.provide(w, panelContent(String(w.params.panel) as PanelId, w.kind)))} arrange={arranged.handlers} chips={arranged.chips} keep={mount.keep} />;
  return (
    <ShellText.Provider value={t}><ShellLanguage.Provider value={uiLanguage}>
      {host}
      {desktop}
      {ended && <DesktopEnded lang={uiLanguage} back={() => setEnded(false)} />}
      {announce}
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
