// W11a-1: the window „Struktura projektu” (kind project_structure, panel structure). A tree around any root of the one graph: the
// children graph.navigate gives, grouped by what the structure templates expect under the root (graph.structure, read only), and the
// expectations nothing fulfils drawn as ghosts (an address that is never stored). The look of each state is a mapping as data
// (structure-state.ts, shell/data/structure_mappings.json; preset me_inferred_opacity, the same setting as the screen „Ty”).
// The window parameters (root, templates, show_expected, mapping, depth, budget) live in the window (useWindowParam), so a layout
// saves and restores them and a binding can couple them with another window. Viewing writes nothing; filling a ghost is W11a-2.
// A tap on a real row opens its node card (NodeCard, the same as in the navigator); "Wejdź" makes the row the root.
import { useCallback, useEffect, useMemo, useState } from "react";
import { api as defaultApi } from "../api";
import type { LoomApi } from "../api/loom-api";
import type { UserProfileHost } from "../api/onboarding-host";
import type { GraphStructureResult } from "../api/graph-projection";
import { shellTranslator, useShellText } from "../shell/text";
import { useWindowParam } from "../shell/windows/WindowParams";
import { usePresentationFeature } from "./ConversationSourceView";
import NodeCard from "./NodeCard";
import { MAPPING_DATA, paramsOf, requestOf, resolveMapping, rowsOf, visibleAdvisories, type PageAdvisory } from "./structure-state";

interface Props {
  api?: Partial<Pick<LoomApi, "actions">>;
  profileHost?: UserProfileHost;
  convId: string | null;
  language: string;
  /** The setting me_inferred_opacity (the look of an inferred entry on the screen „Ty”), read by the mapping preset of the same name. */
  meInferredOpacity?: unknown;
}

async function runAction(a: Props["api"], action: string, args: Record<string, unknown> = {}): Promise<unknown> {
  const actions = a?.actions;
  if (!actions) throw new Error("actions_unavailable");
  const r = await actions.run({ action, element: { class: "application" }, args });
  if (r.status !== "completed") throw new Error(r.error?.message ?? r.status);
  return r.result?.data as unknown;
}

export default function ProjectStructure({ api = defaultApi, profileHost, convId, language, meInferredOpacity }: Props) {
  const t = useShellText();
  const graphWords = usePresentationFeature(profileHost, "graph_screen");
  const gt = useMemo(() => shellTranslator(graphWords.feature, language), [graphWords.feature, language]);
  const rootP = useWindowParam("root"), templatesP = useWindowParam("templates"), showP = useWindowParam("show_expected");
  const mappingP = useWindowParam("mapping"), depthP = useWindowParam("depth"), budgetP = useWindowParam("budget");
  // without a window (a test page) the parameters live here
  const [local, setLocal] = useState<Record<string, unknown>>({});
  const inWindow = rootP.window !== null;
  const values: Record<string, { value: unknown; set: (v: unknown) => void }> = { root: rootP, templates: templatesP, show_expected: showP, mapping: mappingP, depth: depthP, budget: budgetP };
  const get = (p: string) => (inWindow ? values[p]?.value : local[p]);
  const set = (p: string, v: unknown) => (inWindow ? values[p]?.set(v) : setLocal(l => ({ ...l, [p]: v })));
  const params = paramsOf(get);
  const reqKey = JSON.stringify(requestOf(params, language, convId));
  const [result, setResult] = useState<GraphStructureResult | null>(null);
  const [error, setError] = useState<string | null>(null);
  const [loading, setLoading] = useState(false);
  const [draft, setDraft] = useState(params.root);
  const [card, setCard] = useState<{ address: string; label: string } | null>(null);
  const [remembered, setRemembered] = useState<Set<string>>(new Set());
  const [kept, setKept] = useState<Set<string>>(new Set());
  useEffect(() => { setDraft(params.root); }, [params.root]);

  useEffect(() => {
    const read = api.actions?.graphStructure?.bind(api.actions);
    if (!read) { setError("graph.structure"); return; }
    let live = true;
    setLoading(true); setError(null);
    read(JSON.parse(reqKey)).then((r: GraphStructureResult) => { if (live) { setResult(r); setLoading(false); } }, (e: { message?: string }) => { if (live) { setError(String(e?.message ?? e)); setLoading(false); } });
    return () => { live = false; };
  }, [api, reqKey]);

  const mapping = useMemo(() => resolveMapping(params.mapping, language, { me_inferred_opacity: typeof meInferredOpacity === "number" ? meInferredOpacity : Number(meInferredOpacity ?? 0.55) }),
    [JSON.stringify(params.mapping), language, meInferredOpacity]);   // eslint-disable-line react-hooks/exhaustive-deps
  const kernelAdvice = result?.advisories ?? [];
  const pageAdvice = mapping.advisories;
  // the answers the person gave before ("nie pytaj więcej"), from the kernel (advisory.list); read only when there is something to ask
  const asking = kernelAdvice.length + pageAdvice.length > 0;
  useEffect(() => {
    if (!asking) return;
    let live = true;
    runAction(api, "advisory.list").then(d => {
      if (!live) return;
      const acks = (d as { acks?: { code: string; fingerprint?: string; choice?: string }[] })?.acks ?? [];
      setRemembered(new Set(acks.filter(a => a.choice === "dont_ask").map(a => `${a.code}|${a.fingerprint ?? ""}`)));
    }, () => { /* without the list every advisory is shown */ });
    return () => { live = false; };
  }, [api, asking]);
  const advice = visibleAdvisories(kernelAdvice, pageAdvice, remembered, kept);
  const answer = useCallback((a: PageAdvisory, choice: string) => {
    const key = `${a.code}|${a.fingerprint}`;
    if (choice === "dont_ask") {
      setRemembered(r => new Set([...r, key]));
      void runAction(api, "advisory.answer", { code: a.code, fingerprint: a.fingerprint, answer: choice, context: { view: "project_structure", text: a.text } }).catch(() => { /* hidden for this page */ });
    } else setKept(k => new Set([...k, key]));
  }, [api]);

  const drawn = result?.found ? rowsOf(result, mapping, t) : null;
  const goRoot = (address: string) => { setCard(null); set("root", address); };

  return (
    <section className="ps-window" style={{ padding: "8px 12px", overflowWrap: "anywhere" }} data-testid="project-structure" data-root={params.root} data-mapping={mapping.id} aria-label={t("ps_title")}>
      <form className="ps-root" style={{ display: "flex", flexWrap: "wrap", gap: 6, alignItems: "end" }} onSubmit={e => { e.preventDefault(); set("root", draft.trim()); }}>
        <label>{t("ps_root_field")} <input data-testid="ps-root-input" value={draft} onChange={e => setDraft(e.target.value)} /></label>
        <button type="submit" data-testid="ps-root-go">{t("ps_show")}</button>
      </form>
      <div className="ps-controls" style={{ display: "flex", flexDirection: "column", gap: 6 }}>
        <label><input type="checkbox" data-testid="ps-show-expected" checked={params.show_expected} onChange={e => set("show_expected", e.target.checked)} /> {t("ps_show_expected")}</label>
        <label>{t("ps_mapping")}{" "}
          <select data-testid="ps-mapping" value={typeof params.mapping === "string" && MAPPING_DATA.presets[params.mapping] ? params.mapping : mapping.id === "own" ? "" : MAPPING_DATA.default}
            onChange={e => set("mapping", e.target.value)}>
            {mapping.id === "own" && <option value="">—</option>}
            {Object.entries(MAPPING_DATA.presets).map(([id, p]) => <option key={id} value={id}>{(language === "pl" ? p.label?.pl : p.label?.en) ?? id}</option>)}
          </select>
        </label>
      </div>
      <p className="ps-note">{t("ps_read_only")}</p>
      {advice.map(a => (
        <div key={`${a.code}|${a.fingerprint}`} className="ps-advisory" role="status" data-testid="ps-advisory" data-code={a.code}>
          <span>{a.text}</span>{" "}
          <button type="button" data-testid="ps-advisory-keep" onClick={() => answer(a, "keep")}>{t("ps_keep")}</button>{" "}
          <button type="button" data-testid="ps-advisory-dont-ask" onClick={() => answer(a, "dont_ask")}>{t("ps_dont_ask")}</button>
        </div>
      ))}
      {loading && !result && <p>{t("ps_loading")}</p>}
      {error && <p role="alert">{t("ps_error", { error })}</p>}
      {result && !result.found && (
        <div data-testid="ps-not-found" data-reason={result.reason}>
          <p>{result.reason_human}</p>
          {result.reason === "root_empty" && <p>{t("ps_pick_root")}</p>}
          {(result.suggestions ?? []).map(s => (
            <div key={s.address}><strong>{s.label || s.address}</strong>
              <ul>{(s.items ?? []).map(i => <li key={i.address}><button type="button" data-testid="ps-suggestion" data-address={i.address} onClick={() => goRoot(i.address)}>{i.label || i.address}</button></li>)}</ul>
            </div>
          ))}
        </div>
      )}
      {result?.found && drawn && (
        <div className="ps-tree" data-testid="ps-tree">
          <h3 data-testid="ps-root-label">{result.root?.label ?? result.root?.address}</h3>
          {result.text && <p>{result.text}</p>}
          {!!result.templates_applied?.length && (
            <p className="ps-templates">{t("ps_templates")}: {result.templates_applied.map(x => `${x.label}${x.example ? ` (${t("ps_example")})` : ""}`).join("; ")}</p>
          )}
          <h4>{t("ps_expectations")}</h4>
          <ul className="ps-groups">
            {drawn.groups.map(g => (
              <li key={g.expectation.ref} data-testid="ps-expectation" data-ref={g.expectation.ref} data-state={g.expectation.state}>
                <span className="ps-exp-label">{g.expectation.label}</span> <span className="gnav-chip">{g.expectation.state_label}</span> <span>{t("ps_count", { count: String(g.expectation.count) })}</span>
                <ul>
                  {g.rows.map(r => (
                    <li key={r.key} data-testid={r.ghost ? "ps-ghost" : "ps-node"} data-address={r.address} data-state={r.state} style={{ ...r.style, padding: "2px 6px", margin: "2px 0" }}
                      title={r.ghost ? t("ps_ghost_title", { label: r.label }) : undefined}>
                      {r.icon && <span aria-hidden="true">{r.icon} </span>}
                      {r.ghost
                        ? <><span>{r.label}</span> <em>({t("ps_ghost")})</em>{r.detail && <small> — {r.detail}</small>}</>
                        : <button type="button" className="ps-row" onClick={() => setCard({ address: r.address, label: r.label })}>{r.label}</button>}
                      {!r.ghost && <> <button type="button" data-testid="ps-enter" onClick={() => goRoot(r.address)} aria-label={`${t("ps_enter")}: ${r.label}`}>›</button></>}
                    </li>
                  ))}
                </ul>
              </li>
            ))}
          </ul>
          {drawn.hidden > 0 && <p data-testid="ps-hidden">{t("ps_hidden_rows", { count: String(drawn.hidden) })}</p>}
          {drawn.other.length > 0 && (<>
            <h4>{t("ps_other_children")}</h4>
            <ul>{drawn.other.map(r => <li key={r.key} data-testid="ps-node" data-address={r.address}><button type="button" className="ps-row" onClick={() => setCard({ address: r.address, label: r.label })}>{r.label}</button> <button type="button" data-testid="ps-enter" onClick={() => goRoot(r.address)} aria-label={`${t("ps_enter")}: ${r.label}`}>›</button></li>)}</ul>
          </>)}
        </div>
      )}
      {card && <NodeCard key={card.address} address={card.address} label={card.label} language={language} convId={convId} t={gt} onClose={() => setCard(null)}
        onNavigate={(address) => goRoot(address)} />}
    </section>
  );
}
