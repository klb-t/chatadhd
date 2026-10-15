import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { ViewDeclarationInfo, ViewModel, ViewReason } from "../api/view-model";
import type { GraphNode } from "../api/types";
import { dataLabel, useShellLanguage, useShellText } from "../shell/text";
import { project } from "../view-engine/project";

// The visualization engine, 2D renderer (P28 step 1): choose a view declaration (data) and a focus node; the kernel evaluates it into a view model
// and this panel draws it. Which encodings are promoted and which are only allowed (with the reason), and what the engine could not do, stay visible.
const W = 560, H = 460;

function reasonText(reason: ViewReason, language: string): string {
  return dataLabel(reason.text, language, reason.code);
}

export default function ViewEnginePanel() {
  const t = useShellText();
  const language = useShellLanguage();
  const [declarations, setDeclarations] = useState<ViewDeclarationInfo[] | null>(null);
  const [nodes, setNodes] = useState<GraphNode[]>([]);
  const [declaration, setDeclaration] = useState("");
  const [focus, setFocus] = useState("");
  const [budget, setBudget] = useState("");
  const [model, setModel] = useState<ViewModel | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  useEffect(() => {
    let live = true;
    Promise.all([api.listViewDeclarations(), api.getNodes({ limit: 500 })])
      .then(([list, found]) => {
        if (!live) return;
        setDeclarations(list.declarations);
        setNodes(found);
        setDeclaration(cur => cur || list.declarations[0]?.id || "");
        setFocus(cur => cur || found[0]?.id || "");
      })
      .catch(cause => { if (live) { setDeclarations([]); setError(cause instanceof Error ? cause.message : String(cause)); } });
    return () => { live = false; };
  }, []);

  const show = async () => {
    if (!declaration || !focus) { setError(t("view_engine_need_focus")); return; }
    setBusy(true); setError("");
    try {
      const trimmed = budget.trim();
      setModel(await api.evaluateView({ declaration, focus, ...(trimmed !== "" && Number.isFinite(Number(trimmed)) ? { budget: Number(trimmed) } : {}) }));
    } catch (cause) {
      setModel(null);
      setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setBusy(false);
    }
  };

  const projection = useMemo(() => (model ? project(model, { width: W, height: H }) : null), [model]);

  return (
    <div data-testid="view-engine-panel">
      <div className="form-row">
        <label htmlFor="ve-declaration">{t("view_engine_declaration")}</label>
        <select id="ve-declaration" value={declaration} onChange={e => setDeclaration(e.target.value)} data-testid="ve-declaration">
          {(declarations ?? []).map(d => <option key={d.id} value={d.id}>{dataLabel(d.entry?.label, language, d.id)} ({d.id})</option>)}
        </select>
      </div>
      <div className="form-row">
        <label htmlFor="ve-focus">{t("view_engine_focus")}</label>
        <select id="ve-focus" value={focus} onChange={e => setFocus(e.target.value)} data-testid="ve-focus">
          {nodes.map(n => <option key={n.id} value={n.id}>{n.label} ({n.kind})</option>)}
        </select>
      </div>
      <div className="form-row">
        <label htmlFor="ve-budget">{t("view_engine_budget")}</label>
        <input id="ve-budget" value={budget} onChange={e => setBudget(e.target.value)} placeholder="300" data-testid="ve-budget" />
      </div>
      <button onClick={() => void show()} disabled={busy} data-testid="ve-show">{t("view_engine_show")}</button>
      {declarations !== null && declarations.length === 0 && <p data-testid="ve-empty">{t("view_engine_empty")}</p>}
      {error && <p className="pill" style={{ color: "var(--err)" }} data-testid="ve-error">{error}</p>}
      {model && projection && (
        <div data-testid="ve-result">
          <div className="pill" data-testid="ve-counts">{t("view_engine_counts", { elements: String(model.elements.length), edges: String(model.edges.length) })}</div>
          {model.budget.collapsed > 0 && model.budget.reason && <p className="pill" data-testid="ve-budget-note">{reasonText(model.budget.reason, language)}</p>}
          <svg viewBox={`0 0 ${W} ${H}`} width="100%" role="img" aria-label={dataLabel(model.declaration.label, language, model.declaration.id ?? "")} data-testid="ve-canvas" data-system={projection.system ?? "none"}>
            {projection.wedges.map(w => (
              <g key={w.id} data-testid={`ve-wedge-${w.id}`}>
                <path d={w.path} fill="currentColor" opacity={0.05} stroke="currentColor" strokeOpacity={0.25} />
                <text x={w.labelX} y={w.labelY} fontSize={11} textAnchor="middle" fill="currentColor" opacity={0.7}>{dataLabel(w.group.label, language, w.id)}</text>
              </g>
            ))}
            {projection.edges.map((e, i) => <line key={i} x1={e.x1} y1={e.y1} x2={e.x2} y2={e.y2} stroke="currentColor" strokeOpacity={0.3} strokeWidth={1} />)}
            {projection.marks.map(m => (
              <g key={m.id} data-testid="ve-mark" data-id={m.id} data-sector={m.sector ?? ""} data-x={m.x.toFixed(1)} data-y={m.y.toFixed(1)} data-focus={m.focus ? "true" : "false"}>
                <circle cx={m.x} cy={m.y} r={m.r} fill={m.fill} stroke={m.focus ? "currentColor" : "none"} strokeWidth={m.focus ? 2 : 0} style={m.blur > 0 ? { filter: `blur(${m.blur}px)` } : undefined} />
                {m.badge && <text x={m.x + m.r + 2} y={m.y - m.r} fontSize={9} fill="currentColor" opacity={0.75} data-testid="ve-badge">{m.badge}</text>}
                <title>{m.label}</title>
              </g>
            ))}
          </svg>
          {projection.unplaced.length > 0 && (
            <p className="pill" data-testid="ve-unplaced">{t("view_engine_unplaced", { count: String(projection.unplaced.length) })}: {projection.unplaced.map(e => e.label).join(", ")}</p>
          )}
          <h3>{t("view_engine_encodings")}</h3>
          <ul style={{ listStyle: "none", padding: 0 }} data-testid="ve-encodings">
            {model.encodings.map(e => (
              <li key={e.id} data-testid={`ve-encoding-${e.id}`} data-status={e.status}>
                <strong>{e.channel}</strong> &larr; {e.measure} <span className="pill">{e.status === "promoted" ? t("view_engine_promoted") : t("view_engine_allowed")}</span>
                {e.reasons.filter(r => r.level !== "info" || e.status === "allowed").map((r, i) => <div key={i} data-testid={`ve-reason-${e.id}`} style={{ fontSize: "0.85em", opacity: 0.8 }}>{reasonText(r, language)}</div>)}
              </li>
            ))}
          </ul>
          {model.unsupported.length > 0 && (
            <>
              <h3>{t("view_engine_unsupported")}</h3>
              <ul style={{ listStyle: "none", padding: 0 }} data-testid="ve-unsupported">
                {model.unsupported.map((u, i) => (
                  <li key={i} data-testid={`ve-unsupported-${u.element}`}><code>{u.element}</code> ({u.kind}): {reasonText(u.reason, language)}</li>
                ))}
              </ul>
            </>
          )}
        </div>
      )}
    </div>
  );
}
