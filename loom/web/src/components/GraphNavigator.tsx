// M1-W0 (2), packages P2–P4: the graph navigator (spec §3.2–3.4). One screen for Graph, Settings and You: the rows and the map
// are what graph.navigate answers for the focus (never a list of this file). The LIST (P2): a tap on a row with children goes one
// step deeper, the path row goes back (each step is a history layer, so Android Back / browser Back / Escape close the last step
// first), the magnifier searches on the level, the bottom bar changes the budget (Szerzej/Węziej) and the depth (Głębiej/Płycej).
// The MAP (P4, NavigatorMap.tsx): the same level drawn through the view engine; list and map sit one under the other, each in a
// Disclosure, which ones start open comes from the setting navigator_layout (preset both; Widok ▾ changes it and saves it for you).
// A row's ⋯, a tap on a leaf or a tap on a mark opens the node card (NodeCard.tsx, P3) under them; the card is a history layer.
// Perspektywa ▾ (r1:b2) ticks the kernel's perspectives (loom_perspectives), sent as `perspectives` to graph.navigate: the rows are then what
// those relations reach from the focus. Działania opens the focus's card at "Co możesz zrobić" (the actions live on the card).
import { useEffect, useMemo, useRef, useState } from "react";
import NodeCard from "./NodeCard";
import NavigatorMap from "./NavigatorMap";
import { api } from "../api";
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { UserProfileHost } from "../api/onboarding-host";
import Disclosure from "../shell/Disclosure";
import GraphHelp from "./GraphHelp";
import { shellTranslator } from "../shell/text";
import { useLayer } from "../shell/use-layer";
import { usePresentationFeature } from "./ConversationSourceView";
import { LAYOUTS, layoutOf, openByLayout, type Layout } from "./navigator-map";
import { emptyText, receiptLines, rowMarks, rowWhy } from "./navigator-receipt";
import { useReceiptWords } from "./use-receipt-words";
import { runOutcome } from "./node-card-state";
import { BAR_BUTTONS, SEARCH_DEBOUNCE_PRESET_MS, budgetLine, chipText, countText, deeper, drillInto, evidenceText, focusOf, goBack, honestyLines,
  initialState, narrower, noTranslationText, requestFor, setQuery, shallower, statusText, wider, type BarButtonId, type NavigatorState, withFocusLabel,
  actionsTarget, clearPerspectives, perspectiveOptions, togglePerspective, type PerspectiveOption } from "./navigator-state";
import "./graph-navigator.css";

interface Props {
  profileHost?: UserProfileHost;
  convId: string | null;
  /** The language of the words (ui_language or the browser's), the same the kernel is asked for. */
  language: string;
  /** Where the navigator opens ("" = the root of the graph). */
  initialFocus?: string;
  /** The perspectives ticked when it opens (e.g. "Wysłane do modelu" from the sheet "Co wysłaliśmy"); [] = the node's own relations. */
  initialPerspectives?: string[];
  /** The setting navigator_layout as loaded (both | list | map); anything else is the preset both. */
  layout?: unknown;
  /** The setting navigator_help_open as loaded: whether the help "Co tu widzę?" starts open (preset closed). */
  helpSetting?: unknown;
  /** The app learns a setting this screen saved (so the next opening starts as the person left it). */
  onSetting?: (key: string, value: boolean) => void;
}

/** One step of the path as one history layer: Back closes the topmost step (the last one). */
function StepLayer({ index, onClose }: { index: number; onClose: (index: number) => void }) {
  useLayer(`navigator-step-${index}`, true, () => onClose(index));
  return null;
}

export default function GraphNavigator({ profileHost, convId, language, initialFocus = "", initialPerspectives, layout: layoutSetting, helpSetting, onSetting }: Props) {
  const words = usePresentationFeature(profileHost, "graph_screen");
  const t = useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
  const receiptT = useReceiptWords(profileHost, language);   // the words of "Co wysłaliśmy": the reason of a sent element is said in them (K1c-4)
  const [state, setState] = useState<NavigatorState>(() => ({ ...initialState(initialFocus), ...(initialPerspectives?.length ? { perspectives: initialPerspectives } : {}) }));
  const [result, setResult] = useState<GraphNavigateResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [searchOpen, setSearchOpen] = useState(false);
  const [draft, setDraft] = useState("");
  const [card, setCard] = useState<{ address: string; label: string } | null>(null);
  const [perspMenu, setPerspMenu] = useState(false);
  const [perspList, setPerspList] = useState<{ options: PerspectiveOption[] } | { error: string } | null>(null);
  const [actionsNote, setActionsNote] = useState<string | null>(null);
  const [cardSection, setCardSection] = useState<"actions" | null>(null);
  const openedFocusCard = useRef(false);
  const [layout, setLayout] = useState<Layout>(() => layoutOf(layoutSetting));
  const [open, setOpen] = useState(() => openByLayout(layoutOf(layoutSetting)));
  const [viewMenu, setViewMenu] = useState(false);
  const [viewNote, setViewNote] = useState<string | null>(null);
  // The setting arrives after the config loads: follow it until the person chooses here.
  useEffect(() => { const next = layoutOf(layoutSetting); setLayout(next); setOpen(openByLayout(next)); }, [layoutSetting]);

  const request = useMemo(() => requestFor(state, language, convId), [state, language, convId]);
  const requestKey = JSON.stringify(request);
  useEffect(() => {
    let alive = true;
    setLoading(true); setError(null);
    const actions = api.actions;
    if (!actions) { setError(t("error.no_actions")); setLoading(false); return; }   // a catalogue sentence, never a developer id
    actions.graphNavigate(request).then(
      answer => {
        if (!alive) return;
        setResult(answer); setState(s => withFocusLabel(s, answer.focus)); setLoading(false);   // a focus from the menu gets its name
        // M1-W1b: a node opened from the + menu or a sheet that has nothing below it (a setting) shows its card at once - there is no list to look at.
        if (initialFocus && !openedFocusCard.current && answer.focus.address === initialFocus && answer.children.length === 0) {
          openedFocusCard.current = true;
          setCardSection(null); setCard({ address: answer.focus.address, label: answer.focus.label });
        }
      },
      failure => { if (alive) { setError(failure instanceof Error ? failure.message : String(failure)); setLoading(false); } });
    return () => { alive = false; };
    // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [requestKey]);

  // The search field asks the kernel a moment after the last keystroke.
  useEffect(() => {
    const timer = setTimeout(() => setState(s => setQuery(s, draft.trim())), SEARCH_DEBOUNCE_PRESET_MS);
    return () => clearTimeout(timer);
  }, [draft]);
  useLayer("navigator-search", searchOpen, () => { setSearchOpen(false); setDraft(""); });
  useLayer("navigator-view", viewMenu, () => setViewMenu(false));
  useLayer("navigator-perspective", perspMenu, () => setPerspMenu(false));
  // Perspektywa ▾ lists the kernel's perspectives (GET perspectives / loom_perspectives), read when the menu first opens, again per language.
  useEffect(() => { setPerspList(null); }, [language]);
  useEffect(() => {
    if ((!perspMenu && state.perspectives.length === 0) || perspList) return;   // also when it opens with a perspective ticked: the line under the bar names it
    let alive = true;
    api.listPerspectives().then(
      list => { if (alive) setPerspList({ options: perspectiveOptions(list, language) }); },
      failure => { if (alive) setPerspList({ error: failure instanceof Error ? failure.message : String(failure) }); });
    return () => { alive = false; };
  }, [perspMenu, perspList, language, state.perspectives.length]);

  const closeStep = (index: number) => { setState(s => goBack(s, index)); setCard(null); };
  const drill = (row: Pick<GraphNodeRow, "address" | "label">) => { setState(s => drillInto(s, row)); setCard(null); setDraft(""); setSearchOpen(false); };
  const openCard = (row: Pick<GraphNodeRow, "address" | "label">) => { setCardSection(null); setCard(c => c?.address === row.address ? null : { address: row.address, label: row.label }); };
  const showCard = (row: Pick<GraphNodeRow, "address" | "label">) => { setCardSection(null); setCard({ address: row.address, label: row.label }); };
  const effectiveLimit = result?.limit.value ?? 0, effectiveHops = result?.hops ?? 1, shown = result?.counts.shown ?? 0;
  const onBar = (id: BarButtonId) => {
    if (id === "wider") setState(s => wider(s, effectiveLimit));
    else if (id === "narrower") setState(s => narrower(s, effectiveLimit, shown));
    else if (id === "deeper") setState(s => deeper(s, effectiveHops));
    else if (id === "shallower") setState(s => shallower(s, effectiveHops));
    else if (id === "view") { setViewMenu(v => !v); setPerspMenu(false); }
    else if (id === "perspective") { setPerspMenu(v => !v); setViewMenu(false); }
    else if (id === "actions") {
      // Działania: the actions are on the node card ("Co możesz zrobić"), so the focus's card opens there; the root is no node, and the bar says where actions live.
      const target = actionsTarget(state, result?.focus.label ?? "");
      if (!target) { setActionsNote(n => n ? null : t("actions.no_focus")); return; }
      setActionsNote(null); setCardSection("actions"); setCard({ address: target.address, label: target.label });
    }
  };
  // Widok ▾: the choice applies at once and is saved as yours (settings.set navigator_layout); the line under the options says what happened.
  const chooseLayout = async (next: Layout) => {
    setLayout(next); setOpen(openByLayout(next)); setViewMenu(false); setViewNote(null);
    const actions = api.actions;
    if (!actions) { setViewNote(t("view.save_error", { reason: t("error.no_actions") })); return; }
    try {
      const run = await actions.run({ action: "settings.set", args: { key: "navigator_layout", value: next } });
      // the same reading of a run as the card (completed = saved; anything else, a gate included, is said with the kernel's reason)
      setViewNote(runOutcome(run, t).ok ? t("view.saved", { layout: t(`view.${next}`) }) : t("view.save_error", { reason: String(run.error?.message ?? run.error?.code ?? run.status) }));
    } catch (failure) { setViewNote(t("view.save_error", { reason: failure instanceof Error ? failure.message : String(failure) })); }
  };
  const focus = focusOf(state);
  const crumbs = [{ address: "", label: t("root") }, ...state.path];
  const lines = result ? [...honestyLines(result, t, language), ...receiptLines(result, t)] : [];
  const perspectiveNames = state.perspectives.map(id => (perspList && "options" in perspList ? perspList.options.find(o => o.id === id)?.label : undefined) ?? id);

  return (
    <div className="gnav" data-testid="graph-navigator" data-focus={focus} data-layout={layout} data-perspectives={state.perspectives.join(" ")}>
      {state.path.map((_, i) => <StepLayer key={i} index={i} onClose={closeStep} />)}
      <div className="gnav-path" data-testid="nav-path" aria-label={t("path")}>
        {state.path.length > 0 && <button className="gnav-back" data-testid="nav-back" aria-label={t("back")}
          onClick={() => closeStep(state.path.length - 1)}>‹</button>}
        <div className="gnav-crumbs">
          {crumbs.map((step, i) => <span key={`${i}-${step.address}`} className="gnav-crumb-item">
            {i > 0 && <span className="gnav-sep" aria-hidden>›</span>}
            <button className="gnav-crumb" data-testid={`nav-crumb-${i}`} aria-current={i === crumbs.length - 1 ? "page" : undefined}
              onClick={() => closeStep(i)}>{step.label || step.address}</button>
          </span>)}
        </div>
        <button className="gnav-search-toggle" data-testid="nav-search-toggle" aria-expanded={searchOpen} aria-label={searchOpen ? t("search.close") : t("search")}
          onClick={() => { setSearchOpen(v => !v); if (searchOpen) setDraft(""); }}>⌕</button>
      </div>
      {searchOpen && <input className="gnav-search" data-testid="nav-search-input" type="search" autoFocus value={draft} aria-label={t("search")}
        placeholder={t("query.placeholder")} onChange={e => setDraft(e.target.value)} />}
      <GraphHelp screen="navigator" t={t} setting={helpSetting} onSetting={onSetting} testId="nav-help" />
      {result && focus && result.focus.why && <p className="gnav-why gnav-focus-why" data-testid="nav-focus-why">{result.focus.why}</p>}

      <div className="gnav-body">
        {loading && <p className="gnav-note" data-testid="nav-loading">{t("loading")}</p>}
        {error && <p className="gnav-note gnav-error" data-testid="nav-error">{t("error", { reason: error })}</p>}
        {result && <Disclosure testId="nav-list-disclosure" className="gnav-disclosure" labels={{ show: t("list.show"), hide: t("list.hide") }}
          open={open.list} onToggle={v => setOpen(o => ({ ...o, list: v }))}>
          <ul className="gnav-rows" data-testid="nav-rows" data-shown={result.counts.shown} data-total={result.counts.total}>
          {result.children.map(row => {
            const count = countText(row, t), chip = chipText(row, t), evidence = evidenceText(row, t), status = statusText(row, t), noTr = noTranslationText(row, t);
            const marks = rowMarks(result, row, t);   // the record of a request: tokens (estimate), left out and why, "widok zapisu" (K1c-3b)
            const why = rowWhy(row, receiptT);   // a sent element of the record says why in the pack's words (K1c-4); any other row: the kernel's sentence
            return <li key={row.address} className="gnav-row" data-kind={row.kind} data-derived={row.derived ? "true" : undefined}
              data-sent-tokens={marks.weight ?? undefined} data-left-out={marks.leftOut ? "true" : undefined}>
              <div className="gnav-row-line">
                <button className="gnav-row-main" data-testid={`nav-row-${row.address}`} data-has-children={row.has_children}
                  onClick={() => row.has_children ? drill(row) : openCard(row)}>
                  <span className="gnav-row-top">
                    <span className="gnav-label">{row.label}</span>
                    {count && <span className={`gnav-count${typeof row.count === "number" ? "" : " gnav-count-unknown"}`} data-testid="nav-count"
                      data-known={typeof row.count === "number"}>{count}</span>}
                    {row.has_children && <span className="gnav-chevron" aria-hidden>›</span>}
                  </span>
                  <span className="gnav-chips">
                    <span className="gnav-chip" data-testid="nav-chip">{chip}</span>
                    {evidence && <span className="gnav-chip gnav-chip-evidence" data-testid="nav-evidence">{evidence}</span>}
                    {status && <span className="gnav-chip gnav-chip-status" data-testid="nav-status">{status}</span>}
                    {noTr && <span className="gnav-chip gnav-chip-status">{noTr}</span>}
                    {marks.derived && <span className="gnav-chip gnav-chip-derived" data-testid="nav-derived">{marks.derived}</span>}
                    {marks.sent && <span className="gnav-chip gnav-chip-sent" data-testid="nav-sent">{marks.sent}</span>}
                    {marks.leftOut && <span className="gnav-chip gnav-chip-left-out" data-testid="nav-left-out">{marks.leftOut}</span>}
                  </span>
                  {marks.reason && <span className="gnav-why" data-testid="nav-left-out-reason">{marks.reason}</span>}
                  {why && <span className="gnav-why" data-testid="nav-row-why">{why}</span>}
                </button>
                <button className="gnav-more" data-testid={`nav-row-card-${row.address}`} aria-label={t("card")}
                  aria-expanded={card?.address === row.address} onClick={() => openCard(row)}>⋯</button>
              </div>
            </li>;
          })}
          </ul>
          {!loading && result.children.length === 0 && <p className="gnav-note" data-testid="nav-empty" data-missing-reason={result.receipt && !result.receipt.present ? result.receipt.missing_reason : undefined}>{emptyText(result, state.perspectives.length > 0, t)}</p>}
        </Disclosure>}
        {result && <Disclosure testId="nav-map-disclosure" className="gnav-disclosure" labels={{ show: t("map.show"), hide: t("map.hide") }}
          open={open.map} onToggle={v => setOpen(o => ({ ...o, map: v }))}>
          <NavigatorMap result={result} open={open.map} t={t} onCard={showCard} onDrill={drill} />
        </Disclosure>}
        {card && <NodeCard key={card.address} address={card.address} label={card.label} language={language} convId={convId} t={t} receiptT={receiptT} section={cardSection}
          onClose={() => setCard(null)} onNavigate={(address, label) => drill({ address, label })} />}
        {lines.map(line => <p key={line.id} className="gnav-note gnav-honesty" data-testid={`nav-${line.id}`}>{line.text}</p>)}
      </div>

      <nav className="gnav-bar" data-testid="nav-bar">
        {viewMenu && <div className="gnav-view-menu" data-testid="nav-view-menu" role="radiogroup" aria-label={t("view.title")}>
          <p className="gnav-note gnav-view-title">{t("view.title")}</p>
          {LAYOUTS.map(id => <button key={id} type="button" role="radio" className="gnav-view-option" data-testid={`nav-view-${id}`}
            aria-checked={layout === id} onClick={() => chooseLayout(id)}>{t(`view.${id}`)}</button>)}
        </div>}
        {perspMenu && <div className="gnav-view-menu" data-testid="nav-perspective-menu" role="group" aria-label={t("perspective.title")}>
          <p className="gnav-note gnav-view-title">{t("perspective.title")}</p>
          <p className="gnav-why">{t("perspective.help")}</p>
          {!perspList && <p className="gnav-note" data-testid="nav-perspective-loading">{t("perspective.loading")}</p>}
          {perspList && "error" in perspList && <p className="gnav-note gnav-error" data-testid="nav-perspective-error">{t("perspective.error", { reason: perspList.error })}</p>}
          {perspList && "options" in perspList && <>
            <button type="button" role="checkbox" className="gnav-view-option" data-testid="nav-perspective-none" aria-checked={state.perspectives.length === 0}
              onClick={() => { setState(s => clearPerspectives(s)); setPerspMenu(false); }}>{t("perspective.none")}</button>
            {perspList.options.map(o => <button key={o.id} type="button" role="checkbox" className="gnav-view-option" data-testid={`nav-perspective-${o.id}`}
              aria-checked={state.perspectives.includes(o.id)} onClick={() => setState(s => togglePerspective(s, o.id))}>{o.label}</button>)}
          </>}
        </div>}
        {BAR_BUTTONS.map(b => <button key={b.id} className="gnav-bar-btn" data-testid={`nav-bar-${b.id}`}
          aria-pressed={b.id === "perspective" ? state.perspectives.length > 0 : b.id === "view" ? viewMenu : undefined}
          aria-expanded={b.id === "view" ? viewMenu : b.id === "perspective" ? perspMenu : undefined}
          onClick={() => onBar(b.id)}>{t(b.word)}</button>)}
        {state.perspectives.length > 0 && <p className="gnav-note gnav-planned" data-testid="nav-perspectives">{t("perspective.active", { list: perspectiveNames.join(" · ") })}</p>}
        {actionsNote && <p className="gnav-note gnav-planned" data-testid="nav-actions-note" role="status">{actionsNote}</p>}
        {viewNote && <p className="gnav-note gnav-planned" data-testid="nav-view-note">{viewNote}</p>}
        {result && <p className="gnav-budget" data-testid="nav-budget">{budgetLine(effectiveLimit, effectiveHops, result.counts, t, language)}</p>}
      </nav>
    </div>
  );
}
