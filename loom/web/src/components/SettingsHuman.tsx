// M1-W0 (3), package P5: Ustawienia in words (spec §3.5). The screen draws what settings.resolved answers: the groups in the order
// of the data with "N pól · M Twoje", the fields of a group (label, why, value in words, provenance chip, and in the details what it
// changes and which flows read it), "Pokaż tylko zmienione", planned fields with the kernel's own reason. Nothing here lists a
// group, a field or a word of its own (words: presentation.settings_human; a missing word shows its id). A change is made on the
// node card of the navigator (NodeCard.tsx: "Zmień" → "Dla kogo?"), so one place writes a value. Engine › All fields is the former
// settings screen in full (SettingsPanel.tsx: connection, models, keys, the table of every key, the JSON editor).
// Back: the open group is one history layer, the card is another (Android Back / browser Back / Escape close the last one first).
import { useCallback, useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { ResolvedField, SettingsResolved } from "../api/graph-projection";
import type { UserProfileHost } from "../api/onboarding-host";
import Disclosure from "../shell/Disclosure";
import { shellTranslator } from "../shell/text";
import { useLayer } from "../shell/use-layer";
import { usePresentationFeature } from "./ConversationSourceView";
import NodeCard from "./NodeCard";
import SettingsPanel from "./SettingsPanel";
import { ALL_FIELDS_GROUP_PRESET, countLine, countWord, editText, fieldChips, fieldsToShow, flowTitles, isPlanned, subgroupBlocks, subgroupLabels, summaries, totals } from "./settings-human-state";
import "./graph-navigator.css";
import "./settings-human.css";

interface Props {
  profileHost?: UserProfileHost;
  convId: string | null;
  /** The language of the words (ui_language or the browser's), the same the kernel is asked for. */
  language: string;
  /** A related node tapped on a card: the navigator opens there. */
  onNavigate?: (address: string) => void;
}

export default function SettingsHuman({ profileHost, convId, language, onNavigate }: Props) {
  const words = usePresentationFeature(profileHost, "settings_human");
  const graphWords = usePresentationFeature(profileHost, "graph_screen");
  const t = useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
  const graphT = useMemo(() => shellTranslator(graphWords.feature, language), [graphWords.feature, language]);
  const [doc, setDoc] = useState<SettingsResolved | null>(null);
  const [fieldsDoc, setFieldsDoc] = useState<unknown>(null);
  const [error, setError] = useState<string | null>(null);
  const [generation, setGeneration] = useState(0);
  const [openGroup, setOpenGroup] = useState<string | null>(null);
  const [onlyChanged, setOnlyChanged] = useState(false);
  const [detail, setDetail] = useState<string | null>(null);
  const [cardKey, setCardKey] = useState<string | null>(null);
  const [allFields, setAllFields] = useState(false);

  useEffect(() => {
    let alive = true;
    const actions = api.actions;
    if (!actions) { setError("actions_api_unavailable"); return; }
    Promise.all([
      actions.settingsResolved({ lang: language, ...(convId ? { conv_id: convId } : {}) }),
      api.getSettingsFields().catch(() => null),   // only the labels of the subgroups come from here; without them the ids show
    ]).then(([resolved, fields]) => { if (alive) { setDoc(resolved); setFieldsDoc(fields); setError(null); } })
      .catch(failure => { if (alive) setError(failure instanceof Error ? failure.message : String(failure)); });
    return () => { alive = false; };
  }, [language, convId, generation]);

  const reload = useCallback(() => setGeneration(g => g + 1), []);
  useLayer("settings-group", openGroup !== null, () => { setOpenGroup(null); setCardKey(null); setDetail(null); });

  const groups = useMemo(() => doc ? summaries(doc) : [], [doc]);
  const all = useMemo(() => doc ? totals(doc) : { fields: 0, yours: 0 }, [doc]);
  const group = doc && openGroup !== null ? doc.groups.find(g => g.id === openGroup) ?? null : null;
  const legacyHere = group !== null && group.id === ALL_FIELDS_GROUP_PRESET;
  const legacyBelow = doc !== null && openGroup === null && !doc.groups.some(g => g.id === ALL_FIELDS_GROUP_PRESET);

  const legacy = (
    <Disclosure testId="shum-all-fields" className="shum-disclosure" labels={{ show: t("all_fields.show"), hide: t("all_fields.hide") }}
      open={allFields} onToggle={setAllFields}>
      <p className="gnav-why" data-testid="shum-all-fields-why">{t("all_fields.why")}</p>
      <div className="shum-legacy" data-testid="shum-legacy">{allFields && <SettingsPanel />}</div>
    </Disclosure>
  );

  const filterButton = (
    <button type="button" role="switch" aria-checked={onlyChanged} className="shum-filter" data-testid="shum-filter" onClick={() => setOnlyChanged(v => !v)}>
      <span className="shum-switch" aria-hidden="true" />
      <span className="shum-filter-text">
        <span className="shum-filter-label">{t("filter.changed")}</span>
        <span className="gnav-why">{t("filter.help")}</span>
      </span>
    </button>
  );

  const renderField = (field: ResolvedField) => {
    const expanded = detail === field.key;
    const planned = isPlanned(field);
    const flows = flowTitles(field);
    return (
      <li key={field.key} className="shum-field" data-testid={`shum-field-${field.key}`} data-address={field.address} data-planned={planned ? "1" : undefined}>
        <div className="gnav-row-line">
          <button type="button" className="gnav-row-main shum-field-main" aria-expanded={expanded} data-testid={`shum-field-main-${field.key}`}
            onClick={() => setDetail(expanded ? null : field.key)}>
            <span className="gnav-row-top">
              <span className="gnav-label" data-testid={`shum-label-${field.key}`}>{field.human.label}</span>
              <span className="shum-value" data-testid={`shum-value-${field.key}`} aria-label={t("field.value")}>{field.value_human}</span>
            </span>
            <span className="gnav-chips">
              {fieldChips(field, t, graphT).map(chip => (
                <span key={chip.id} className={`gnav-chip${chip.kind === "evidence" ? " gnav-chip-evidence" : ""}${chip.kind === "status" ? " gnav-chip-status" : ""}`}
                  data-testid={`shum-chip-${chip.id}-${field.key}`}>{chip.text}</span>))}
            </span>
            <span className="gnav-why" data-testid={`shum-why-${field.key}`}>{field.human.why}</span>
            {planned && <span className="gnav-why shum-planned" data-testid={`shum-planned-${field.key}`}><b>{t("planned.reason")}: </b>{field.human.effect}</span>}
          </button>
          <button type="button" className="gnav-more" aria-label={t("field.card", { label: field.human.label })} data-testid={`shum-card-btn-${field.key}`}
            onClick={() => setCardKey(cardKey === field.key ? null : field.key)}>⋯</button>
        </div>
        {expanded && (
          <div className="shum-detail" data-testid={`shum-detail-${field.key}`}>
            {!planned && <p><b>{t("field.effect")}: </b><span data-testid={`shum-effect-${field.key}`}>{field.human.effect}</span></p>}
            <p data-testid={`shum-flows-${field.key}`}><b>{t("field.flows")}: </b>{flows.length ? flows.join(" · ") : t("field.flows.none")}</p>
            <p data-testid={`shum-edit-${field.key}`}><b>{t("field.edit")}: </b>{editText(field, t)}</p>
          </div>
        )}
        {cardKey === field.key && (
          <NodeCard key={field.address} address={field.address} language={language} convId={convId} t={graphT}
            onClose={() => setCardKey(null)} onNavigate={address => onNavigate?.(address)} onChanged={reload} />
        )}
      </li>
    );
  };

  const groupView = (g: SettingsResolved["groups"][number]) => {
    const shown = fieldsToShow(g.fields, onlyChanged);
    const blocks = subgroupBlocks(shown, subgroupLabels(fieldsDoc, g.id, language), t);
    const summary = groups.find(s => s.id === g.id)!;
    return (
      <section data-testid={`shum-group-view-${g.id}`} data-shown={shown.length} data-fields={g.fields.length}>
        <button type="button" className="shum-back" data-testid="shum-back" onClick={() => { setOpenGroup(null); setCardKey(null); setDetail(null); }}>{t("group.back")}</button>
        <h3 className="shum-group-title">{g.label}</h3>
        <p className="gnav-why" data-testid="shum-group-why">{g.why}</p>
        <p className="gnav-count shum-group-count" data-testid="shum-group-view-count">{countLine(summary, language, t)}</p>
        {filterButton}
        {g.fields.length === 0 && <p className="gnav-note" data-testid="shum-empty">{t("group.empty")}</p>}
        {g.fields.length > 0 && shown.length === 0 && <p className="gnav-note" data-testid="shum-empty">{t("filter.empty")}</p>}
        {blocks.map(block => (
          <div key={block.id ?? "_"} className="shum-block" data-testid={`shum-sub-${block.id ?? "none"}`}>
            {block.label && <h4 className="shum-sub-title">{block.label}</h4>}
            {block.why && <p className="gnav-why">{block.why}</p>}
            <ul className="gnav-rows" data-testid={`shum-fields-${g.id}`}>{block.fields.map(renderField)}</ul>
          </div>
        ))}
        {legacyHere && legacy}
      </section>
    );
  };

  return (
    <div className="shum" data-testid="settings-human">
      {!doc && !error && <p className="gnav-note" data-testid="shum-loading">{t("loading")}</p>}
      {error && (
        <div className="gnav-note gnav-error" role="alert" data-testid="shum-error">
          <p>{t("error", { reason: error })}</p>
          <button type="button" className="gnav-bar-btn" onClick={reload} data-testid="shum-retry">{t("retry")}</button>
        </div>
      )}
      {doc && group && groupView(group)}
      {doc && !group && (
        <section>
          <p className="gnav-why" data-testid="shum-intro">{t("intro")}</p>
          <p className="gnav-count shum-total" data-testid="shum-total" data-fields={all.fields} data-yours={all.yours}>{t("total.line", { fields: countWord("fields", all.fields, language, t), yours: countWord("yours", all.yours, language, t) })}</p>
          <p className="gnav-why" data-testid="shum-yours-help">{t("yours.help")}</p>
          {filterButton}
          <ul className="gnav-rows" data-testid="shum-groups" aria-label={t("groups.aria")}>
            {groups.map(g => (
              <li key={g.id} className="gnav-row" data-testid={`shum-group-${g.id}`} data-fields={g.fields} data-yours={g.yours}>
                <button type="button" className="gnav-row-main shum-group-main" aria-label={t("group.open", { label: g.label })} data-testid={`shum-group-open-${g.id}`}
                  onClick={() => setOpenGroup(g.id)}>
                  <span className="gnav-row-top">
                    <span className="gnav-label">{g.label}</span>
                    <span className="gnav-count" data-testid={`shum-group-count-${g.id}`}>{countLine(g, language, t)}</span>
                    <span className="gnav-chevron" aria-hidden="true">›</span>
                  </span>
                  <span className="gnav-why">{g.why}</span>
                </button>
              </li>))}
          </ul>
          {doc.hidden_keys.length > 0 && <p className="gnav-note" data-testid="shum-hidden">{t("hidden.line", { count: String(doc.hidden_keys.length) })}</p>}
          {doc.proposals_status?.today && (
            <p className="gnav-why" data-testid="shum-proposals"><b>{t("proposals.title")}: </b>{doc.proposals_status.today}{doc.proposals_status.reason ? ` ${doc.proposals_status.reason}` : ""}</p>)}
          {legacyBelow && legacy}
        </section>
      )}
    </div>
  );
}
