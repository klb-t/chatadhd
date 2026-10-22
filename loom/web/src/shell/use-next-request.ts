// M1-W1b: what the next request of a conversation will carry as far as this window can know (shared by the strip over the composer and the
// preview of the retrieval plan): the profile (the scope chain, else the last receipt's, else "from the settings") and the changes that wait.
import { useEffect, useState, useSyncExternalStore } from "react";
import { api } from "../api";
import { profileChoices, type ProfileChoice } from "../components/context-receipt-state";
import { loadSelectorProfiles } from "../components/use-context-modify";
import { chosenProfile, type StripProfile } from "./context-strip";
import { pendingTurn, subscribePendingTurn } from "./turn-pending";

/** The rule key of the profile of a request (action_rules.pack): the scope chain gives the profile of the conversation, the unit or the person. */
export const PROFILE_KEY = "context.profile";

export interface NextRequest { profile: StripProfile | null; profiles: ProfileChoice[]; waiting: Record<string, unknown> }

export function useNextRequest(convId: string | null, language: string, lastProfile: StripProfile | null, messageCount: number): NextRequest {
  const [profiles, setProfiles] = useState<ProfileChoice[]>([]);
  const [chain, setChain] = useState<StripProfile | null | undefined>(undefined);  // undefined = the chain could not be read
  const waiting = useSyncExternalStore(subscribePendingTurn, () => pendingTurn(convId));
  useEffect(() => {
    let live = true;
    loadSelectorProfiles().then(data => { if (live) setProfiles(profileChoices(data, language)); }, () => undefined);
    return () => { live = false; };
  }, [language]);
  useEffect(() => {
    let live = true;
    api.resolveScope({ key: PROFILE_KEY, ...(convId ? { conversation_id: convId } : {}) }).then(
      resolution => { if (live) setChain(resolution.defined ? chosenProfile(resolution.value, profiles) : null); },
      () => { if (live) setChain(undefined); });
    return () => { live = false; };
  }, [convId, messageCount, profiles]);
  // The chain wins when it names a profile; if it names none the settings apply (null); if it cannot be read, the last receipt says what went.
  return { profile: chain === undefined ? lastProfile : chain, profiles, waiting };
}
