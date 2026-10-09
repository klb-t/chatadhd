import { useCallback, useEffect, useMemo, useRef, useState, useSyncExternalStore } from "react";
import { api } from "../api";
import type { UserProfileHost } from "../api/onboarding-host";
import type { SemanticStatusInfo } from "../api/types";
import { PresentationFailure } from "../onboarding/presentation-context";
import { message, PresentationError, resolvePresentation } from "../onboarding/presentation.mjs";

const POLL_MS = 4000;

function knownCount(value: number | null | undefined): value is number {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0;
}

export default function SemanticStatus({ profileHost }: { profileHost: UserProfileHost }) {
  const identity = useSyncExternalStore(profileHost.subscribe, profileHost.getState, profileHost.getState);
  const initialRead = useRef<string | null>(null);
  const [status, setStatus] = useState<SemanticStatusInfo | null>(null);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState<unknown>(null);

  useEffect(() => {
    const key = JSON.stringify([identity.userId, identity.viewEpoch]);
    if (identity.session?.active) {
      // Another mounted consumer already owns the queued read. Its request
      // settles before the host acknowledges a snapshot; do not enqueue a
      // competing read in the brief active=0 / revision=null transition.
      initialRead.current = key;
      return;
    }
    if (!identity.userId || identity.session?.reloadRequired ||
        profileHost.currentSnapshot() || initialRead.current === key) return;
    const adapter = profileHost.getAdapter();
    if (!adapter) return;
    initialRead.current = key;
    // The shared host owns this read and its failure state. Never abort a read
    // needed by another pane, or replay an operation with an uncertain outcome.
    void adapter.getSnapshot().catch(() => undefined);
  }, [profileHost, identity.userId, identity.viewEpoch, identity.session?.active, identity.session?.reloadRequired]);

  const presentation = useMemo(() => {
    try {
      const snapshot = profileHost.currentSnapshot();
      if (identity.userId !== null && !snapshot?.presentation) {
        throw new PresentationError("error.presentation", { reason: "profile_snapshot_unavailable" }, true);
      }
      return { value: resolvePresentation(identity.userId === null ? undefined : snapshot!.presentation), error: null };
    } catch (cause) { return { value: null, error: cause }; }
  }, [profileHost, identity.userId, identity.session?.revision, identity.session?.reloadRequired]);

  const refresh = useCallback(async () => {
    try {
      setStatus(await api.getSemanticStatus());
      setError(null);
    } catch (cause) { setStatus(null); setError(cause); }
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
      setError(null);
    } catch (cause) {
      setStatus(null); setError(cause);
    } finally {
      setBusy(false);
    }
  }, [status]);

  if (!presentation.value) return <div className="pill warn" data-testid="semantic-status">
    <PresentationFailure error={presentation.error} />
  </div>;
  if (!status && !error) return null;
  const p = presentation.value;
  const complete = status && status.counts_known !== false &&
    [status.pending, status.executing, status.failed, status.processed].every(knownCount);
  const failed = status && knownCount(status.failed) && status.failed > 0;
  const cls = error || !complete || status?.paused || failed ? "warn" :
    status && (status.pending > 0 || status.executing! > 0) ? "" : "ok";
  const counts = status && {
    pending: knownCount(status.pending) ? status.pending : null,
    executing: knownCount(status.executing) ? status.executing : null,
    failed: knownCount(status.failed) ? status.failed : null,
    processed: knownCount(status.processed) ? status.processed : null,
  };
  const details = JSON.stringify(status ? { ...status, ...counts } : {
    error: error instanceof Error ? error.message : String(error),
  }, null, p.defaults.json_indent);

  return (
    <div className={`pill ${cls}`} data-testid="semantic-status" aria-live="polite">
      <details data-testid="semantic-status-details">
        <summary title={details}>
          {message(p, error || failed ? "error.failure" : !complete ? "status.unknown" : "error.diagnostics")}
          {counts && <> <code>{JSON.stringify(counts)}</code></>}
        </summary>
        <pre>{details}</pre>
      </details>
      {status && <button type="button" onClick={togglePause} disabled={busy} data-testid="semantic-toggle-pause">
        {message(p, status.paused ? "session.resume" : "session.pause")}
      </button>}
    </div>
  );
}
