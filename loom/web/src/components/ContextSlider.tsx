import { useEffect, useState } from "react";
import { api } from "../api";
import type { ContextSet } from "../api/types";

interface Props {
  convId: string | null;
}

export default function ContextSlider({ convId }: Props) {
  const [text, setText] = useState("What have we discussed so far?");
  const [depth, setDepth] = useState(2);
  const [maxTokens, setMaxTokens] = useState(2000);
  const [preview, setPreview] = useState<ContextSet | null>(null);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let cancelled = false;
    setLoading(true);
    setError(null);
    const t = setTimeout(() => {
      api
        .selectContext({ text, depth, max_tokens: maxTokens, conv_id: convId ?? undefined })
        .then((r) => {
          if (!cancelled) setPreview(r);
        })
        .catch((err) => {
          if (!cancelled) setError(err instanceof Error ? err.message : String(err));
        })
        .finally(() => {
          if (!cancelled) setLoading(false);
        });
    }, 300);
    return () => {
      cancelled = true;
      clearTimeout(t);
    };
  }, [text, depth, maxTokens, convId]);

  return (
    <div data-testid="context-slider">
      <div className="form-row">
        <label htmlFor="ctx-text">Probe text</label>
        <textarea id="ctx-text" rows={2} value={text} onChange={(e) => setText(e.target.value)} />
      </div>
      <div className="form-row">
        <label htmlFor="ctx-depth">
          Depth: {depth} (0 = none, 4 = deepest graph traversal)
        </label>
        <input
          id="ctx-depth"
          type="range"
          min={0}
          max={4}
          step={1}
          value={depth}
          onChange={(e) => setDepth(Number(e.target.value))}
          data-testid="context-depth"
        />
      </div>
      <div className="form-row">
        <label htmlFor="ctx-tokens">Token budget: {maxTokens}</label>
        <input
          id="ctx-tokens"
          type="range"
          min={200}
          max={16000}
          step={200}
          value={maxTokens}
          onChange={(e) => setMaxTokens(Number(e.target.value))}
          data-testid="context-tokens"
        />
      </div>

      <div className="section-title">Live preview</div>
      {loading && <div className="empty-state">Selecting…</div>}
      {error && <div className="empty-state" style={{ color: "var(--err)" }}>{error}</div>}
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
