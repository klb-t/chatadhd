import { useState } from "react";
import { api } from "../api";
import type { ScopeResolution } from "../api/graph-views";
import { useShellText } from "../shell/text";

// "Where does this setting come from": the value of one key through conversation -> units -> user -> installation (P24 scope resolver),
// which level supplied it and which were overridden. Reads only; the words are the shell's (PL/EN by ui_language).
const KEY_PRESET = "temperature";

export default function ScopeExplain({ convId }: { convId: string | null }) {
  const t = useShellText();
  const [key, setKey] = useState(KEY_PRESET);
  const [result, setResult] = useState<ScopeResolution | null>(null);
  const [error, setError] = useState("");
  const [busy, setBusy] = useState(false);

  const explain = async () => {
    const name = key.trim();
    if (!name) return;
    setBusy(true); setError("");
    try {
      setResult(await api.resolveScope({ key: name, ...(convId ? { conversation_id: convId } : {}) }));
    } catch (cause) {
      setResult(null);
      setError(cause instanceof Error ? cause.message : String(cause));
    } finally {
      setBusy(false);
    }
  };

  return (
    <details className="chat-context-controls" data-testid="scope-explain">
      <summary>{t("scope_title")}</summary>
      <div className="form-row">
        <label htmlFor="scope-explain-key">{t("scope_key")}</label>
        <input id="scope-explain-key" data-testid="scope-explain-key" value={key} onChange={e => setKey(e.target.value)} />
        <button onClick={() => void explain()} disabled={busy || !key.trim()} data-testid="scope-explain-run">{t("scope_explain")}</button>
      </div>
      {error && <p className="pill" style={{ color: "var(--err)" }} data-testid="scope-explain-error">{error}</p>}
      {result && (
        <div data-testid="scope-explain-result">
          <p data-testid="scope-explain-value">{result.defined
            ? t("scope_value", { value: JSON.stringify(result.value), level: result.scope ?? "" })
            : t("scope_undefined")}</p>
          <ul>
            {result.chain.map(level => (
              <li key={level.level} data-testid={`scope-level-${level.level}`} data-role={level.role}>
                {level.label ?? level.level}: {t(`scope_role_${level.role}`)}
              </li>
            ))}
          </ul>
        </div>
      )}
    </details>
  );
}
