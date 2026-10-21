// M1-W0 (2), package P2: the LIST half of the graph navigator (spec §3.2–3.4). One screen for Graph, Settings and You: the rows
// are what graph.navigate answers for the focus (never a list of this file), a tap on a row with children goes one step deeper,
// the path row goes back (each step is a history layer, so Android Back / browser Back / Escape close the last step first), the
// magnifier searches on the level, the bottom bar changes the budget (Szerzej/Węziej) and the depth (Głębiej/Płycej).
// Not in this package (their buttons say "planned" with the reason, from the catalogue): the map, Perspektywa, Działania, Widok.
// A row's ⋯ (or a tap on a leaf) opens the node card (NodeCard.tsx, package P3) under the rows; the card is a history layer, so
// Back closes it before the path step.
import { useEffect, useMemo, useState } from "react";
import NodeCard from "./NodeCard";
import { api } from "../api";
import type { GraphNavigateResult, GraphNodeRow } from "../api/graph-projection";
import type { UserProfileHost } from "../api/onboarding-host";
import { shellTranslator } from "../shell/text";
import { useLayer } from "../shell/use-layer";
import { usePresentationFeature } from "./ConversationSourceView";
import { BAR_BUTTONS, SEARCH_DEBOUNCE_PRESET_MS, chipText, countText, deeper, drillInto, evidenceText, focusOf, goBack, honestyLines,
  initialState, narrower, noTranslationText, requestFor, setQuery, shallower, statusText, wider, type BarButtonId, type NavigatorState } from "./navigator-state";
import "./graph-navigator.css";

interface Props {
  profileHost?: UserProfileHost;
  convId: string | null;
  /** The language of the words (ui_language or the browser's), the same the kernel is asked for. */
  language: string;
  /** Where the navigator opens ("" = the root of the graph). */
  initialFocus?: string;
}

/** One step of the path as one history layer: Back closes the topmost step (the last one). */
function StepLayer({ index, onClose }: { index: number; onClose: (index: number) => void }) {
  useLayer(`navigator-step-${index}`, true, () => onClose(index));
  return null;
}

export default function GraphNavigator({ profileHost, convId, language, initialFocus = "" }: Props) {
  const words = usePresentationFeature(profileHost, "graph_screen");
  const t = useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
  const [state, setState] = useState<NavigatorState>(() => initialState(initialFocus));
  const [result, setResult] = useState<GraphNavigateResult | null>(null);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const [searchOpen, setSearchOpen] = useState(false);
  const [draft, setDraft] = useState("");
  const [card, setCard] = useState<{ address: string; label: string } | null>(null);
  const [planned, setPlanned] = useState<BarButtonId | null>(null);

  const request = useMemo(() => requestFor(state, language, convId), [state, language, convId]);
  const requestKey = JSON.stringify(request);
  useEffect(() => {
    let alive = true;
    setLoading(true); setError(null);
    const actions = api.actions;
    if (!actions) { setError("actions_api_unavailable"); setLoading(false); return; }
    actions.graphNavigate(request).then(
      answer => { if (alive) { setResult(answer); setLoading(false); } },
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

  const closeStep = (index: number) => { setState(s => goBack(s, index)); setCard(null); };
  const drill = (row: Pick<GraphNodeRow, "address" | "label">) => { setState(s => drillInto(s, row)); setCard(null); setDraft(""); setSearchOpen(false); };
  const openCard = (row: Pick<GraphNodeRow, "address" | "label">) => setCard(c => c?.address === row.address ? null : { address: row.address, label: row.label });
  const effectiveLimit = result?.limit.value ?? 0, effectiveHops = result?.hops ?? 1, shown = result?.counts.shown ?? 0;
  const onBar = (id: BarButtonId) => {
    if (id === "wider") setState(s => wider(s, effectiveLimit));
    else if (id === "narrower") setState(s => narrower(s, effectiveLimit, shown));
    else if (id === "deeper") setState(s => deeper(s, effectiveHops));
    else if (id === "shallower") setState(s => shallower(s, effectiveHops));
    else setPlanned(p => p === id ? null : id);
  };
  const focus = focusOf(state);
  const crumbs = [{ address: "", label: t("root") }, ...state.path];
  const lines = result ? honestyLines(result, t) : [];
  const plannedButton = BAR_BUTTONS.find(b => b.id === planned);

  return (
    <div className="gnav" data-testid="graph-navigator" data-focus={focus}>
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
      {searchOpen && <input className="gnav-search" data-testid="nav-search-input" type="search" autoFocus value={draft}
        placeholder={t("query.placeholder")} onChange={e => setDraft(e.target.value)} />}
      {result && focus && result.focus.why && <p className="gnav-why gnav-focus-why" data-testid="nav-focus-why">{result.focus.why}</p>}

      <div className="gnav-body">
        {loading && <p className="gnav-note" data-testid="nav-loading">{t("loading")}</p>}
        {error && <p className="gnav-note gnav-error" data-testid="nav-error">{t("error", { reason: error })}</p>}
        {result && <ul className="gnav-rows" data-testid="nav-rows" data-shown={result.counts.shown} data-total={result.counts.total}>
          {result.children.map(row => {
            const count = countText(row, t), chip = chipText(row, t), evidence = evidenceText(row, t), status = statusText(row, t), noTr = noTranslationText(row, t);
            return <li key={row.address} className="gnav-row" data-kind={row.kind}>
              <div className="gnav-row-line">
                <button className="gnav-row-main" data-testid={`nav-row-${row.address}`} data-has-children={row.has_children}
                  onClick={() => row.has_children ? drill(row) : openCard(row)}>
                  <span className="gnav-row-top">
                    <span className="gnav-label">{row.label}</span>
                    {count && <span className="gnav-count" data-testid="nav-count">{count}</span>}
                    {row.has_children && <span className="gnav-chevron" aria-hidden>›</span>}
                  </span>
                  <span className="gnav-chips">
                    <span className="gnav-chip" data-testid="nav-chip">{chip}</span>
                    {evidence && <span className="gnav-chip gnav-chip-evidence" data-testid="nav-evidence">{evidence}</span>}
                    {status && <span className="gnav-chip gnav-chip-status" data-testid="nav-status">{status}</span>}
                    {noTr && <span className="gnav-chip gnav-chip-status">{noTr}</span>}
                  </span>
                  {row.why && <span className="gnav-why">{row.why}</span>}
                </button>
                <button className="gnav-more" data-testid={`nav-row-card-${row.address}`} aria-label={t("card")}
                  aria-expanded={card?.address === row.address} onClick={() => openCard(row)}>⋯</button>
              </div>
            </li>;
          })}
        </ul>}
        {card && <NodeCard key={card.address} address={card.address} language={language} convId={convId} t={t}
          onClose={() => setCard(null)} onNavigate={(address, label) => drill({ address, label })} />}
        {result && !loading && result.children.length === 0 && <p className="gnav-note" data-testid="nav-empty">{t("no_children")}</p>}
        {lines.map(line => <p key={line.id} className="gnav-note gnav-honesty" data-testid={`nav-${line.id}`}>{line.text}</p>)}
      </div>

      <nav className="gnav-bar" data-testid="nav-bar">
        {BAR_BUTTONS.map(b => <button key={b.id} className={`gnav-bar-btn${b.works ? "" : " gnav-bar-planned"}`} data-testid={`nav-bar-${b.id}`}
          aria-pressed={b.works ? undefined : planned === b.id} onClick={() => onBar(b.id)}>{t(b.word)}</button>)}
        {plannedButton && <p className="gnav-note gnav-planned" data-testid="nav-bar-planned">{t("bar.planned", { what: t(plannedButton.word) })}</p>}
        {result && <p className="gnav-budget" data-testid="nav-budget">
          {effectiveLimit === 0 ? t("bar.budget.unlimited", { hops: String(effectiveHops) }) : t("bar.budget", { limit: String(effectiveLimit), hops: String(effectiveHops) })}
          {" · "}{t("children", { shown: String(result.counts.shown), total: String(result.counts.total) })}
        </p>}
      </nav>
    </div>
  );
}
