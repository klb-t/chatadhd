import { useEffect, useState } from "react";
import { api } from "../api";
import { breakdown, canExpand, countText, type EntryTexts, type GraphEntryArea, type GraphEntryCount, type GraphEntryExpand, type GraphEntryView } from "../api/graph-entry";
import type { UserProfileHost } from "../api/onboarding-host";
import { featureMessage } from "../onboarding/presentation.mjs";
import { usePresentationFeature } from "./ConversationSourceView";
import "./graph-entry.css";

export const GRAPH_ENTRY_FEATURE = "graph_entry";
export type EntryOpeners = Record<string, (params: Record<string, unknown>) => void>;
interface Transport { getGraphEntry: (options?: { lang?: string }) => Promise<GraphEntryView> }

function Counts({ counts, texts }: { counts: Record<string, GraphEntryCount>; texts: EntryTexts }) {
  const rows = Object.entries(counts);
  if (!rows.length) return null;
  return <dl className="graph-entry-counts">{rows.map(([name, count]) => <div key={name} data-known={count.known}>
    <dt>{name}</dt><dd data-testid={`graph-entry-count-${name}`}>{countText(count, texts)}
      {breakdown(count, texts).map(line => <small key={line}>{line}</small>)}</dd>
  </div>)}</dl>;
}

function Expand({ expand, openers, texts }: { expand: GraphEntryExpand | null; openers: EntryOpeners; texts: EntryTexts }) {
  if (expand && canExpand({ expand }, openers))
    return <button type="button" className="graph-entry-expand" onClick={() => openers[expand.view](expand.params)}>{texts("expand")}</button>;
  return <small className="graph-entry-noexpand">{texts("expand_unavailable")}</small>;
}

/** Read-only entry view of the graph: areas with counts. Unknown counts are shown as unknown. */
export default function GraphEntryPanel({ profileHost, openers = {}, transport = api }: {
  profileHost?: UserProfileHost;
  openers?: EntryOpeners;
  transport?: Transport;
}) {
  const catalog = usePresentationFeature(profileHost, GRAPH_ENTRY_FEATURE);
  const [view, setView] = useState<GraphEntryView | null>(null);
  const [error, setError] = useState<string | null>(null);
  const locale = catalog.feature?.locale;
  useEffect(() => {
    let live = true;
    transport.getGraphEntry(locale ? { lang: locale } : {}).then(next => { if (live) { setView(next); setError(null); } },
      cause => { if (live) setError(cause instanceof Error ? cause.message : String(cause)); });
    return () => { live = false; };
  }, [transport, locale]);
  const feature = catalog.feature;
  if (!feature) return <p role="alert">{catalog.error instanceof Error ? catalog.error.message : "presentation_unavailable"}</p>;
  const texts: EntryTexts = (id, parameters) => featureMessage(feature, id, parameters);
  if (error) return <p role="alert" data-testid="graph-entry-error">{texts("error", { reason: error })}</p>;
  if (!view) return <p>{texts("loading")}</p>;
  const area = (a: GraphEntryArea) => <li key={a.id} data-testid={`graph-entry-area-${a.id}`} data-origin={a.origin}>
    <h4>{a.label}{a.origin === "cell" && <small> ({texts("origin.cell")})</small>}</h4>
    <Counts counts={a.counts} texts={texts} />
    <Expand expand={a.expand} openers={openers} texts={texts} />
  </li>;
  return <section className="graph-entry" data-testid="graph-entry-panel">
    <h3>{texts("title")}</h3>
    {view.issues.length > 0 && <p role="status">{texts("issues", { count: view.issues.length })}</p>}
    <h4>{texts("seed")}</h4>
    <ul className="graph-entry-list">{view.seed.map(element => <li key={element.id} data-testid={`graph-entry-${element.id}`}>
      <h4>{element.label}</h4>
      <Counts counts={element.counts} texts={texts} />
      {element.node && <ul className="graph-entry-slots">{element.node.slots.map(slot => <li key={slot.id}>
        {texts("slot.state", { label: slot.label, state: texts(`state.${slot.state}`) })}</li>)}</ul>}
    </li>)}</ul>
    <h4>{texts("areas")}</h4>
    {view.areas.length ? <ul className="graph-entry-list">{view.areas.map(area)}</ul> : <p>{texts("empty")}</p>}
  </section>;
}
