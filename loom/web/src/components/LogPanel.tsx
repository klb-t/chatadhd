import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "../api";
import { useShellText } from "../shell/text";

const POLL_MS = 3000;

export default function LogPanel() {
  const tx = useShellText();
  const [lines, setLines] = useState<string[]>([]);
  const [filter, setFilter] = useState("");
  const [auto, setAuto] = useState(true);
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState<string | null>(null);
  const generation = useRef(0);
  const inFlight = useRef(false);

  const refresh = useCallback(async () => {
    if (inFlight.current) return;
    inFlight.current = true;
    const request = ++generation.current;
    setLoading(true);
    try {
      const next = await api.getLogs(300);
      if (request !== generation.current) return;
      setLines(next);
      setError(null);
    } catch (failure) {
      if (request === generation.current) setError(failure instanceof Error ? failure.message : String(failure));
    } finally {
      if (request === generation.current) { inFlight.current = false; setLoading(false); }
    }
  }, []);

  useEffect(() => {
    void refresh();
    const timer = auto ? setInterval(() => void refresh(), POLL_MS) : undefined;
    return () => { clearInterval(timer); ++generation.current; inFlight.current = false; };
  }, [refresh, auto]);

  const filtered = filter ? lines.filter((l) => l.toLowerCase().includes(filter.toLowerCase())) : lines;

  return (
    <div data-testid="log-panel" aria-busy={loading}>
      <div className="form-row" style={{ display: "flex", gap: 8, alignItems: "center" }}>
        <input
          type="search"
          placeholder={tx("log_filter")}
          aria-label={tx("log_filter")}
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          data-testid="log-filter"
        />
        <button onClick={() => void refresh()} disabled={loading}>{tx("lit_refresh")}</button>
        <label style={{ fontSize: 12, whiteSpace: "nowrap" }}>
          <input type="checkbox" checked={auto} onChange={(e) => setAuto(e.target.checked)} /> {tx("log_auto")}
        </label>
      </div>
      {loading && <p role="status">{tx("log_loading")}</p>}
      {error !== null && <p role="alert" data-testid="log-error">{tx("log_read_failed", { error })}</p>}
      {error !== null && lines.length > 0 && <p>{tx("log_previous_lines")}</p>}
      <div data-testid="log-lines">
        {!loading && error === null && filtered.length === 0 && <div className="empty-state">{tx(lines.length > 0 ? "log_no_matches" : "log_empty")}</div>}
        {filtered.map((l, i) => (
          <div className="log-line" key={i}>
            {l}
          </div>
        ))}
      </div>
    </div>
  );
}
