import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { SemanticStatusInfo } from "../api/types";

const POLL_MS = 4000;

export default function SemanticStatus() {
  const [status, setStatus] = useState<SemanticStatusInfo | null>(null);
  const [busy, setBusy] = useState(false);

  const refresh = useCallback(async () => {
    try {
      setStatus(await api.getSemanticStatus());
    } catch {
      // Server may not be reachable yet on first paint - stay silent, retry next tick.
    }
  }, []);

  useEffect(() => {
    refresh();
    const t = setInterval(refresh, POLL_MS);
    return () => clearInterval(t);
  }, [refresh]);

  const togglePause = useCallback(async () => {
    if (!status) return;
    setBusy(true);
    try {
      setStatus(status.paused ? await api.resumeSemantic() : await api.pauseSemantic());
    } finally {
      setBusy(false);
    }
  }, [status]);

  if (!status) return null;

  const cls = status.paused ? "warn" : status.pending > 0 ? "" : "ok";

  return (
    <div className={`pill ${cls}`} data-testid="semantic-status" title={`mode=${status.mode} rate=${status.rate}`}>
      <span>
        semantic: {status.pending} pending / {status.processed} done
      </span>
      <button onClick={togglePause} disabled={busy} data-testid="semantic-toggle-pause">
        {status.paused ? "Resume" : "Pause"}
      </button>
    </div>
  );
}
