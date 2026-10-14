import { createContext, useContext } from "react";
import { featureInLanguage, featureMessage } from "../onboarding/presentation.mjs";

type Feature = Parameters<typeof featureMessage>[0];
export type ShellT = (id: string, params?: Record<string, string>) => string;

/** Words of the shell from the data catalog presentation.ui_shell in one language; without a catalog the machine name is shown. */
export function shellTranslator(feature: Feature | null | undefined, language: string): ShellT {
  const f = feature ? featureInLanguage(feature, language) : null;
  return (id, params = {}) => {
    if (!f) return id;
    try { return featureMessage(f, id, params); } catch { return id; }
  };
}
export const ShellText = createContext<ShellT>((id) => id);
export const useShellText = (): ShellT => useContext(ShellText);

/** The first-run card is shown when the provider key is known to be missing; a failed lookup shows nothing. */
export function needsProviderKey(hasKey: boolean | null): boolean { return hasKey === false; }
