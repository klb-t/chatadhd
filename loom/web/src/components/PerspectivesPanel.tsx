import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { PerspectiveInfo, PerspectiveRender } from "../api/graph-views";
import { dataLabel, useShellLanguage, useShellText } from "../shell/text";
import { colorFor } from "./GraphView";

// Perspectives side by side (P24 + P25): choose one or more, each is the same graph seen through a different selection of edge kinds,
// drawn next to the others with the node colours of the graph view. Positions are a fixed circle: the panel compares selections, it does
// not simulate physics.
const W = 320, H = 260;

function PerspectiveGraph({ id, render }: { id: string; render: PerspectiveRender }) {
  const t = useShellText();
  const positions = useMemo(() => {
    const out = new Map<string, { x: number; y: number }>();
    render.nodes.forEach((n, i) => {
      const a = (i / Math.max(1, render.nodes.length)) * Math.PI * 2;
      out.set(n.id, { x: W / 2 + Math.cos(a) * (W / 2 - 24), y: H / 2 + Math.sin(a) * (H / 2 - 24) });
    });
    return out;
  }, [render.nodes]);
  return (
    <div className="perspective-col" data-testid={`perspective-col-${id}`}>
      <h3>{id}</h3>
      <div className="pill" data-testid={`perspective-counts-${id}`}>{t("perspectives_counts", { nodes: String(render.nodes.length), edges: String(render.edges.length) })}</div>
      <svg viewBox={`0 0 ${W} ${H}`} width="100%" role="img" aria-label={id}>
        {render.edges.map((e, i) => {
          const a = positions.get(e.src), b = positions.get(e.dst);
          return a && b ? <line key={i} x1={a.x} y1={a.y} x2={b.x} y2={b.y} stroke="#888" strokeWidth={1} /> : null;
        })}
        {render.nodes.map(n => {
          const p = positions.get(n.id)!;
          return <g key={n.id} data-testid={`perspective-node-${id}`}><circle cx={p.x} cy={p.y} r={6} fill={colorFor(n.kind)} /><title>{n.label}</title></g>;
        })}
      </svg>
    </div>
  );
}

export default function PerspectivesPanel({ convId }: { convId: string | null }) {
  const t = useShellText();
  const language = useShellLanguage();
  const [list, setList] = useState<PerspectiveInfo[] | null>(null);
  const [chosen, setChosen] = useState<string[]>([]);
  const [key, setKey] = useState("temperature");
  const [rendered, setRendered] = useState<Record<string, PerspectiveRender> | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let live = true;
    api.listPerspectives()
      .then(r => { if (live) setList(r.perspectives); })
      .catch(cause => { if (live) { setList([]); setError(cause instanceof Error ? cause.message : String(cause)); } });
    return () => { live = false; };
  }, []);

  const toggle = (id: string) => setChosen(cur => cur.includes(id) ? cur.filter(x => x !== id) : [...cur, id]);
  const show = async () => {
    if (!chosen.length) { setError(t("perspectives_none_chosen")); return; }
    setBusy(true); setError("");
    try {
      const r = await api.renderPerspectives({ ids: chosen, key: key.trim() || undefined, ...(convId ? { conversation_id: convId } : {}) });
      setRendered(r.perspectives);
    } catch (cause) {
      setRendered(null);
      setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setBusy(false);
    }
  };

  return (
    <div data-testid="perspectives-panel">
      <h3>{t("perspectives_choose")}</h3>
      {list === null && <p>{t("perspectives_loading")}</p>}
      {list !== null && list.length === 0 && <p data-testid="perspectives-empty">{t("perspectives_empty")}</p>}
      <ul style={{ listStyle: "none", padding: 0 }}>
        {(list ?? []).map(p => (
          <li key={p.id}>
            <label>
              <input type="checkbox" checked={chosen.includes(p.id)} onChange={() => toggle(p.id)} data-testid={`perspective-choose-${p.id}`} /> {dataLabel(p.entry?.label, language, p.id)} <code>{p.id}</code>
            </label>
          </li>
        ))}
      </ul>
      <div className="form-row">
        <label htmlFor="perspectives-key">{t("perspectives_key")}</label>
        <input id="perspectives-key" value={key} onChange={e => setKey(e.target.value)} data-testid="perspectives-key" />
      </div>
      <button onClick={() => void show()} disabled={busy} data-testid="perspectives-show">{t("perspectives_show")}</button>
      {error && <p className="pill" style={{ color: "var(--err)" }} data-testid="perspectives-error">{error}</p>}
      {rendered && (
        <div style={{ display: "flex", flexWrap: "wrap", gap: 12 }} data-testid="perspectives-side-by-side">
          {chosen.filter(id => rendered[id]).map(id => <PerspectiveGraph key={id} id={id} render={rendered[id]} />)}
        </div>
      )}
    </div>
  );
}
