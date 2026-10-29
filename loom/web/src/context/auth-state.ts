// Account session state of the web UI (P1). Pure: the server decides (GET /api/auth/me); this only
// classifies the answer and picks which presentation text to show. All visible strings come from the
// `accounts` presentation feature (conversation_view.pack); without it the machine reason is shown.
import { featureMessage, type PresentationFeature } from "../onboarding/presentation.mjs";
import type { JsonValue } from "../onboarding/types";

export const ACCOUNTS_FEATURE = "accounts";

export type Registration = "open" | "invite" | "closed";
export interface AccountUser { id: string; login: string }
export type AuthState =
  | { phase: "loading" }
  /** The server runs without accounts: nothing to sign in to. */
  | { phase: "open" }
  | { phase: "signed_out"; registration: Registration }
  | { phase: "signed_in"; registration: Registration; user: AccountUser }
  /** R103: a session without an account. The full interface, no gate (R105); history lives in the browser store. */
  | { phase: "guest"; registration: Registration; guest: GuestInfo; guests: GuestsOffer }
  | { phase: "error"; reason: string }
  /** /api/auth/me unavailable (older or single-user server, fixture, network): the app is shown
   *  (R43: a missing capability degrades, never blocks); the server still enforces sessions, and
   *  the first 401 switches to the sign-in screen. */
  | { phase: "unverified"; reason: string };

/** What the server offers to people without an account (server_accounts `guests`, presets). */
export interface GuestsOffer {
  enabled: boolean;
  /** "auto" = a guest session is started on the first visit (preset); anything else = only on request. Open value. */
  start: string;
  idleTtlSeconds: number;
  notice: { on: boolean; askAgain: string };
}
export interface GuestInfo {
  id: string;
  idleTtlSeconds: number;
  /** The free provider/model the server picked for this guest (data terms included), or null with a reason. */
  modelSource: Record<string, unknown> | null;
}

function guestsOffer(value: unknown): GuestsOffer | null {
  if (!record(value) || value.enabled !== true) return null;
  const notice = record(value.first_answer_notice) ? value.first_answer_notice : {};
  return {
    enabled: true,
    start: typeof value.start === "string" ? value.start : "auto",
    idleTtlSeconds: typeof value.idle_ttl_seconds === "number" ? value.idle_ttl_seconds : 0,
    notice: { on: notice.on !== false, askAgain: typeof notice.ask_again === "string" ? notice.ask_again : "per_device" },
  };
}

/** True when the sign-in screen must not appear: the server wants a guest session started at once (R105). */
export function startsGuestAutomatically(me: unknown): boolean {
  if (!record(me) || me.accounts !== true || me.authenticated === true || record(me.guest)) return false;
  const offer = guestsOffer(me.guests);
  return offer !== null && offer.start === "auto";
}

function record(value: unknown): value is Record<string, unknown> {
  return value !== null && typeof value === "object" && !Array.isArray(value);
}

/** Classifies a GET /api/auth/me body. A malformed body is an error, never "open". */
export function authStateFromMe(me: unknown): AuthState {
  if (!record(me) || typeof me.accounts !== "boolean") return { phase: "unverified", reason: "auth_me_invalid" };
  if (!me.accounts) return { phase: "open" };
  const registration = me.registration === "open" || me.registration === "invite" || me.registration === "closed"
    ? me.registration : "closed";
  const user = me.user;
  if (me.authenticated === true && record(user) && typeof user.id === "string" && typeof user.login === "string")
    return { phase: "signed_in", registration, user: { id: user.id, login: user.login } };
  const offer = guestsOffer(me.guests);
  const guest = me.guest;
  if (offer && record(guest) && typeof guest.id === "string")
    return {
      phase: "guest", registration, guests: offer,
      guest: {
        id: guest.id,
        idleTtlSeconds: typeof guest.idle_ttl_seconds === "number" ? guest.idle_ttl_seconds : offer.idleTtlSeconds,
        modelSource: record(guest.model_source) ? guest.model_source : null,
      },
    };
  return { phase: "signed_out", registration };
}

export type AuthEvent =
  | { type: "me"; body: unknown }
  | { type: "me_failed"; reason: string }
  /** Any API call answered 401. Only meaningful while the server is in accounts mode. */
  | { type: "unauthorized" }
  | { type: "signed_out" };

export function authReduce(state: AuthState, event: AuthEvent): AuthState {
  switch (event.type) {
    case "me": return authStateFromMe(event.body);
    case "me_failed": return { phase: "unverified", reason: event.reason };
    case "unauthorized":
      if (state.phase === "unverified") return { phase: "signed_out", registration: "closed" };
      return state.phase === "signed_in" || state.phase === "guest" ? { phase: "signed_out", registration: state.registration } : state;
    case "signed_out":
      return state.phase === "signed_in" || state.phase === "guest" ? { phase: "signed_out", registration: state.registration } : state;
  }
}

/** The UI shows the app for `open`, `signed_in` and `guest`; the sign-in screen only for `signed_out`. */
export function showsApp(state: AuthState): boolean {
  return state.phase === "open" || state.phase === "signed_in" || state.phase === "guest" || state.phase === "unverified";
}

export type AccountsTextId =
  | "title" | "title_register" | "login_label" | "password_label" | "invite_label" | "sign_in" | "register"
  | "working" | "to_register" | "to_login" | "registration_closed" | "registration_invite" | "loading"
  | "signed_in_as" | "logout" | "error.auth" | "error.failed" | "error.unreachable"
  | "guest.start" | "guest.notice_title" | "guest.notice_body_browser" | "guest.notice_body_session" | "guest.notice_choose_store"
  | "guest.notice_sign_in" | "guest.notice_never" | "guest.notice_close" | "guest.download" | "guest.load" | "guest.restored"
  | "guest.model_line" | "guest.no_model" | "guest.terms_title" | "guest.period_unknown" | "guest.persisted" | "guest.not_persisted"
  | "guest.limit_ready" | "guest.limit_alternatives" | "guest.limit_own_key"
  | "guest.terms_consent_title" | "guest.terms_consent_body" | "guest.terms_send" | "guest.terms_other" | "guest.terms_declined"
  | "guest.switched" | "guest.wipe" | "guest.wipe_confirm" | "guest.wipe_yes" | "guest.wipe_no"
  | "guest.store" | "guest.store_traits" | "guest.store_choice" | "guest.store_auto" | "guest.store_unavailable" | "guest.store_fallback";

/** Text from the catalog, or the id (and reason) when the catalog is unusable. */
export function accountText(feature: PresentationFeature | null, id: AccountsTextId,
                            parameters: Record<string, JsonValue> = {}): string {
  if (feature) {
    try { return featureMessage(feature, id, parameters); }
    catch { /* a broken template never takes the sign-in screen down */ }
  }
  const reason = parameters.reason ?? parameters.login;
  return typeof reason === "string" ? `${id}: ${reason}` : id;
}

/** Which registration affordance the sign-in screen offers. */
export function registrationOffer(registration: Registration): "none" | "invite" | "open" {
  return registration === "closed" ? "none" : registration;
}
