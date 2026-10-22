// M1-W1b: the strip above the composer (ContextStrip.tsx) as pure functions: what the next request will carry, in words. Nothing here is
// invented: the profile is the one the scope chain gives (or, if the chain cannot be read, the one of the last receipt), and the changes that
// wait for the next request are the ones the kernel last told THIS window about (turn-pending.ts); the engine has no read of its carrier.
import type { ContextReceipt } from "../api/context-receipt";
import { profileName, recordedReceipt, requestOf, wordOr, type ProfileChoice } from "../components/context-receipt-state";
import type { ShellT } from "./text";

/** composer_context_strip: chips (preset) | hidden | full. A value the field does not list is the preset. */
export type StripMode = "chips" | "hidden" | "full";
export const STRIP_PRESET: StripMode = "chips";
export function stripMode(value: unknown): StripMode { return value === "hidden" || value === "full" || value === "chips" ? value : STRIP_PRESET; }

export interface StripProfile { id: string; label: string; status: string }

/** "szerzej ×2, profil Zgodność" from the carrier content the kernel reported: key -> list of steps, or a string (a profile id / one step). */
export function pendingParts(waiting: Record<string, unknown>, t: ShellT, profileLabel: (id: string) => string): string[] {
  const parts: string[] = [];
  for (const [key, value] of Object.entries(waiting)) {
    if (Array.isArray(value)) {
      const counted = new Map<string, number>();
      for (const step of value) if (typeof step === "string") counted.set(step, (counted.get(step) ?? 0) + 1);
      for (const [id, n] of counted) { const word = wordOr(t, `action.${id}`, id); parts.push(n > 1 ? `${word} ×${n}` : word); }
    } else if (typeof value === "string" && value) {
      parts.push(key.endsWith("profile") ? t("modifier.profile", { profile: profileLabel(value) }) : wordOr(t, `action.${value}`, value));
    }
  }
  return parts;
}

export interface StripView {
  mode: StripMode;
  /** "Profil: Loom (preset)" or "Profil: z ustawień". */
  profileText: string;
  profileKnown: boolean;
  pending: string[];
  /** "Następne zapytanie: szerzej" - empty when this window knows of no change. */
  nextText: string;
  /** full mode: one paragraph. */
  fullText: string;
}

export interface StripInput { mode: StripMode; waiting: Record<string, unknown>; profile: StripProfile | null; profiles: ProfileChoice[] }

export function stripView(input: StripInput, t: ShellT, language: string): StripView {
  const label = (id: string) => input.profiles.find(p => p.id === id)?.label ?? id;
  const pending = pendingParts(input.waiting, t, label);
  const p = input.profile;
  const shown = p ? (p.status ? t("strip.profile_with_status", { label: p.label, status: wordOr(t, `status.${p.status}`, p.status) }) : p.label) : "";
  const profileText = p ? t("strip.profile", { profile: shown }) : t("strip.profile_settings");
  const nextText = pending.length ? t("strip.next", { what: pending.join(", ") }) : "";
  const fullText = [p ? t("strip.full.profile", { profile: shown }) : t("strip.full.profile_settings"),
    pending.length ? t("strip.full.changes", { what: pending.join(", ") }) : t("strip.full.no_changes")].join(" ");
  void language;
  return { mode: input.mode, profileText, profileKnown: p !== null, pending, nextText, fullText };
}

/** The profile the last request of the conversation went with, from the receipt stored with the person's message (no call). */
export function lastReceiptProfile(messages: { id: string; role: string; parent_id?: string | null; metadata?: Record<string, unknown> | null }[], language: string): StripProfile | null {
  for (let i = messages.length - 1; i >= 0; i--) {
    if (messages[i].role !== "user") continue;
    const receipt: ContextReceipt | null = recordedReceipt(requestOf(messages, messages[i].id));
    const name = receipt ? profileName(receipt, language) : null;
    if (receipt && name !== null && receipt.profile?.id) return { id: receipt.profile.id, label: name, status: receipt.profile.status ?? "" };
  }
  return null;
}

/** The profile id the scope chain gives for the next request (context.profile: a rule of the conversation, the unit or the person, or the setting), as a strip profile. */
export function chosenProfile(id: unknown, profiles: ProfileChoice[]): StripProfile | null {
  if (typeof id !== "string" || id === "") return null;
  const row = profiles.find(p => p.id === id);
  return { id, label: row?.label ?? id, status: row?.status ?? "" };
}
