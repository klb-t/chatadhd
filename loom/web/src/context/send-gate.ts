import { sourceHistorySendUnavailable, type ConversationView } from "../api/conversation-view";
import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

/** Layered default key of the user-initiated block (R43); the explicit choice is a ChatView setting. */
export const SEND_BLOCK_DEFAULT_KEY = "chat.send.block_without_source_history";
/** Presentation feature holding the gate texts (`presentation.chat_send`). */
export const SEND_GATE_FEATURE = "chat_send";

/** Mechanism vocabulary of the gate, translated only through presentation data. */
export type SendGateInitiator = "user" | "invariant:integrity";
export type SendGateMessage = "source_read_in_flight" | "view_loading" | "blocked_by_setting";
export interface SendGateBlock { initiator: SendGateInitiator; reason: string; message: SendGateMessage }
export interface ViewFailure { conversationId: string; reason: string; lastKnown: ConversationView | null }
export interface SendGateInput {
  conversationId: string | null;
  /** The host exposes the native conversation view reader. */
  readsView: boolean;
  view: ConversationView | null;
  failure: ViewFailure | null;
  sourceReadInFlight: boolean;
  blockWithoutSourceHistory: boolean;
}
export interface SendGate {
  /** null: sending is allowed. Otherwise who initiated the refusal and why (R43). */
  blocked: SendGateBlock | null;
  viewLoading: boolean;
  /** Failure of the last view request for this conversation; sending stays allowed. */
  viewFailure: string | null;
  /** Native reason why linked source history is not included, when the view says so. */
  sourceHistory: string | null;
}

/** A native envelope without a reason still states an unavailable capability. */
const UNSPECIFIED_REASON = "unspecified";

/** Decide whether Send may dispatch. Missing capability degrades and informs;
 * only a user choice or a transient integrity condition refuses (R43). */
export function chatSendGate(input: SendGateInput): SendGate {
  const id = input.conversationId;
  const current = id !== null && input.view?.conversation_id === id ? input.view : null;
  const failure = id !== null && input.failure?.conversationId === id ? input.failure : null;
  // After a failed refresh the last successful view of the same conversation still
  // says whether it has linked history; an unknown linkage never blocks.
  const known = current ?? (failure?.lastKnown?.conversation_id === id ? failure.lastKnown : null);
  const sourceHistory = known && sourceHistorySendUnavailable(known)
    ? known.capabilities.source_history_send.reason || UNSPECIFIED_REASON : null;
  const viewLoading = Boolean(id !== null && input.readsView && !current && !failure);
  const result = (blocked: SendGateBlock | null): SendGate =>
    ({ blocked, viewLoading, viewFailure: failure?.reason ?? null, sourceHistory });
  // The user's own choice outlasts any transient condition, so it is the explanation shown.
  if (sourceHistory !== null && input.blockWithoutSourceHistory)
    return result({ initiator: "user", reason: sourceHistory, message: "blocked_by_setting" });
  // A local source read replaces the displayed view; sending waits for it to settle.
  if (input.sourceReadInFlight) return result({ initiator: "invariant:integrity", reason: "source_read_in_flight", message: "source_read_in_flight" });
  // Until the view arrives, neither the history capability nor the user's choice can be evaluated.
  if (viewLoading) return result({ initiator: "invariant:integrity", reason: "conversation_view_loading", message: "view_loading" });
  return result(null);
}

/** Template parameters of a catalog message (an index signature keeps the file lexically scannable). */
interface TemplateParameters { [key: string]: JsonValue }
export interface SendGateTexts {
  /** Explanation of a refusal (also the disabled button's description). */
  blocked: string | null;
  sourceHistory: string | null;
  sourceReason: string | null;
  viewFailure: string | null;
  setting: string | null;
  settingHelp: string | null;
  unblock: string | null;
}
/** User-visible gate texts come from the `chat_send` presentation catalog. Without a
 * usable catalog (older profile, suppressed or malformed entry) the machine reason is
 * shown instead; a broken template never takes the composer down. */
export function sendGateTexts(feature: PresentationFeature | null, gate: SendGate): SendGateTexts {
  const text = (id: string, parameters: TemplateParameters = {}): string | null => {
    if (!feature) return null;
    try { return featureMessage(feature, id, parameters); }
    catch { return null; }
  };
  const setting = text("setting_label");
  const blocked = gate.blocked && ((gate.blocked.message === "blocked_by_setting"
    ? (setting === null ? null : text("blocked_by_setting", { setting }))
    : text(gate.blocked.message)) ?? gate.blocked.reason);
  const code = gate.sourceHistory;
  const known = code !== null && feature !== null && Object.prototype.hasOwnProperty.call(feature.catalog, "reason." + code);
  const explanation = code === null ? null : text(known ? "reason." + code : "reason.unrecognized");
  return {
    blocked,
    sourceHistory: code === null ? null : text("source_history_omitted") ?? code,
    sourceReason: code === null || explanation === null ? null : text("reason", { reason: code, explanation }),
    viewFailure: gate.viewFailure === null ? null : text("view_unavailable", { reason: gate.viewFailure }) ?? gate.viewFailure,
    setting, settingHelp: text("setting_help"), unblock: text("unblock"),
  };
}
