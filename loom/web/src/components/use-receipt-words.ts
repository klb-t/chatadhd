import { useMemo } from "react";
import type { UserProfileHost } from "../api/onboarding-host";
import { shellTranslator, type ShellT } from "../shell/text";
import { usePresentationFeature } from "./ConversationSourceView";

/** The words of presentation.context_receipt in the language of the screen (a missing word shows its id): one translator for the sheet and for
 *  the line under an answer. */
export function useReceiptWords(profileHost: UserProfileHost | undefined, language: string): ShellT {
  const words = usePresentationFeature(profileHost, "context_receipt");
  return useMemo(() => shellTranslator(words.feature, language), [words.feature, language]);
}
