import { useCallback, useEffect, useState } from "react";
import { api } from "../api";

const POLL_MS = 3000;

export default function LogPanel() {
  const [lines, setLines] = useState<string[]>([]);
  const [filter, setFilter] = useState("");
  const [auto, setAuto] = useState(true);

  const refresh = useCallback(() => {
    api
      .getLogs(300)
      .then(setLines)
      .catch(() => {});
  }, []);

  useEffect(() => {
    refresh();
    if (!auto) return;
    const t = setInterval(refresh, POLL_MS);
    return () => clearInterval(t);
  }, [refresh, auto]);

  const filtered = filter ? lines.filter((l) => l.toLowerCase().includes(filter.toLowerCase())) : lines;

  return (
    <div data-testid="log-panel">
      <div className="form-row" style={{ display: "flex", gap: 8, alignItems: "center" }}>
        <input
          type="search"
          placeholder="Filter…"
          value={filter}
          onChange={(e) => setFilter(e.target.value)}
          data-testid="log-filter"
        />
        <button onClick={refresh}>Refresh</button>
        <label style={{ fontSize: 12, whiteSpace: "nowrap" }}>
          <input type="checkbox" checked={auto} onChange={(e) => setAuto(e.target.checked)} /> auto
        </label>
      </div>
      <div data-testid="log-lines">
        {filtered.length === 0 && <div className="empty-state">No log lines.</div>}
        {filtered.map((l, i) => (
          <div className="log-line" key={i}>
            {l}
          </div>
        ))}
      </div>
    </div>
  );
}
