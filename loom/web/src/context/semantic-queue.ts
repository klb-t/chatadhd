import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";
import type { SemanticRequeueResult, SemanticStatusInfo } from "../api/types";

/** Presentation feature holding the texts (`presentation.semantic_queue`). */
export const SEMANTIC_QUEUE_FEATURE = "semantic_queue";
/** Values the worker accepts for the retry setting. */
export const RETRY_MODES = ["explicit_requeue", "automatic"];

/** Index signature keeps the module lexically scannable by the literal guard. */
interface Row { [key: string]: JsonValue }

/** null/absent is unknown, never zero. */
function known(value: number | null | undefined): number | null {
  return typeof value === "number" && Number.isSafeInteger(value) && value >= 0 ? value : null;
}

export interface SemanticQueueView { stalled: number | null; quarantined: number | null; retryMode: string | null }

export function semanticQueueView(status: SemanticStatusInfo): SemanticQueueView {
  return { stalled: known(status.stalled), quarantined: known(status.quarantined),
    retryMode: RETRY_MODES.includes(status.retry_mode ?? "") ? status.retry_mode ?? null : null };
}

export interface SemanticQueueTexts {
  stalled: (count: number | null) => string; quarantined: (count: number | null) => string;
  requeue: string | null; retryMode: string | null; mode: (mode: string) => string;
  requeued: (result: SemanticRequeueResult) => string; failed: (reason: string) => string;
}

/** All user-visible words come from the catalog; without one the machine names are shown. */
export function semanticQueueTexts(feature: PresentationFeature | null): SemanticQueueTexts {
  const text = (id: string, parameters: Row = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); } catch { return null; }
  };
  const unknown = text("unknown") ?? "-";
  const shown = (count: number | null) => count === null ? unknown : String(count);
  return {
    stalled: (count) => text("stalled", { count: shown(count) }) ?? `stalled ${shown(count)}`,
    quarantined: (count) => text("quarantined", { count: shown(count) }) ?? `quarantined ${shown(count)}`,
    requeue: text("requeue"), retryMode: text("retry_mode"),
    mode: (mode) => text("mode_" + mode) ?? mode,
    requeued: (result) => text("requeued", { count: String(result.requeued),
      failed: String(result.failed_requeued), stalled: String(result.stalled_requeued) }) ?? String(result.requeued),
    failed: (reason) => text("action_failed", { reason }) ?? reason,
  };
}
