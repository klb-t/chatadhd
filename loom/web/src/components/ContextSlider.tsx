import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "../api";
import type { ContextSet } from "../api/types";
import { CONTEXT_PREVIEW_KEY, NATIVE_INT_MAX, useResourceParameters, useResourcePresets, validContextPreview } from "../context/resource-controls";
import ResourcePresetEditor from "./ResourcePresetEditor";

interface Props {
  convId: string | null;
}

export default function ContextSlider({ convId }: Props) {
  const { presets, error: presetError } = useResourcePresets();
  const { value: parameters, setValue: setParameters, storageError } = useResourceParameters(CONTEXT_PREVIEW_KEY, presets.context_preview.defaults, validContextPreview);
  const [preview, setPreview] = useState<ContextSet | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);
  const generation = useRef(0);
  const valid = validContextPreview(parameters);

  const select = useCallback(async () => {
    if (!validContextPreview(parameters)) return;
    const requestGeneration = ++generation.current;
    setLoading(true); setError(null);
    try {
      const result = await api.selectContext({ text: parameters.text, depth: parameters.depth,
        max_tokens: parameters.max_tokens, conv_id: convId ?? undefined });
      if (requestGeneration === generation.current) setPreview(result);
    } catch (failure) {
      if (requestGeneration === generation.current) setError(failure instanceof Error ? failure.message : String(failure));
    } finally { if (requestGeneration === generation.current) setLoading(false); }
  }, [parameters, convId]);
  useEffect(() => {
    setLoading(false);
    if (!valid || !parameters.auto_preview) return () => { generation.current++; };
    setLoading(true);
    const timer = setTimeout(() => { void select(); }, parameters.debounce_ms);
    return () => { clearTimeout(timer); generation.current++; };
  }, [parameters, valid, select]);
  const numeric = (key: "depth" | "max_tokens" | "debounce_ms", raw: string) => {
    setParameters(current => ({ ...current, [key]: raw === "" ? NaN : Number(raw) }));
  };

  return (
    <div data-testid="context-slider">
      <div className="form-row">
        <label htmlFor="ctx-text">Probe text</label>
        <textarea id="ctx-text" rows={2} value={parameters.text} onChange={event => setParameters(current => ({ ...current, text: event.target.value }))} />
      </div>
      {(["depth", "max_tokens"] as const).map(key => {
        const range = presets.context_preview.sliders[key];
        const label = key === "depth" ? "Depth" : "Token budget";
        return <div className="form-row" key={key}>
          <label htmlFor={`ctx-${key}-number`}>{label}: {Number.isNaN(parameters[key]) ? "" : parameters[key]}{key === "depth" ? " (0 = no graph traversal)" : ""}</label>
          <input id={`ctx-${key}-number`} aria-label={`Context preview ${label.toLowerCase()}`} type="number" min={0} max={NATIVE_INT_MAX} step={1}
            value={Number.isNaN(parameters[key]) ? "" : parameters[key]} onChange={event => numeric(key, event.target.value)} data-testid={`context-${key}-number`} />
          <label htmlFor={`ctx-${key}-slider`}>Suggested slider range: {range.min}–{range.max}; numeric values may exceed it</label>
          <input id={`ctx-${key}-slider`} aria-label={`Suggested context ${label.toLowerCase()} slider`} type="range" min={range.min} max={range.max} step={range.step}
            value={Number.isNaN(parameters[key]) ? range.min : parameters[key]} onChange={event => numeric(key, event.target.value)} data-testid={key === "depth" ? "context-depth" : "context-tokens"} />
        </div>;
      })}
      <div className="form-row"><label><input type="checkbox" checked={parameters.auto_preview} onChange={event => setParameters(current => ({ ...current, auto_preview: event.target.checked }))} data-testid="context-auto-preview" /> Automatic preview</label></div>
      <div className="form-row"><label htmlFor="ctx-debounce">Automatic preview delay (milliseconds)</label><input id="ctx-debounce" aria-label="Context preview delay in milliseconds" type="number" min={0} max={NATIVE_INT_MAX} step={1} value={Number.isNaN(parameters.debounce_ms) ? "" : parameters.debounce_ms} onChange={event => numeric("debounce_ms", event.target.value)} /></div>
      <button onClick={() => { void select(); }} disabled={loading || !valid} data-testid="context-preview-build">Build context preview</button>
      <p>Depth, budget, query and preview interaction settings are saved in this browser. Numeric fields use the current native signed-integer representation; suggested slider ranges are editable presets. The engine reports its own capability refusals.</p>
      <ResourcePresetEditor />
      {[presetError, storageError].filter(Boolean).map(message => <p role="alert" key={message}>{message}</p>)}
      {!valid && <p role="alert">Use finite nonnegative whole numbers representable by the native integer fields (0–{NATIVE_INT_MAX}).</p>}

      <div className="section-title">{parameters.auto_preview ? "Live preview" : "Manual preview"}</div>
      {loading && <div className="empty-state">Selecting…</div>}
      {error && <div className="empty-state" style={{ color: "var(--err)" }} role="alert">{error}</div>}
      {preview && !loading && (
        <div data-testid="context-preview">
          <div className="pill" style={{ marginBottom: 8 }}>
            ~{preview.token_estimate} tokens · {preview.items.length} items
            {preview.truncated ? " · truncated" : ""}
          </div>
          <pre
            style={{
              whiteSpace: "pre-wrap",
              fontSize: 12,
              background: "var(--input-bg)",
              padding: 10,
              borderRadius: 8,
              maxHeight: 320,
              overflowY: "auto",
            }}
          >
            {preview.prompt_text || "(empty)"}
          </pre>
        </div>
      )}
    </div>
  );
}
