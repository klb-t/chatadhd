import { createContext, Fragment, useContext } from "react";
import type { ReactNode } from "react";
import { controlOrder, errorPresentation, message, PresentationError, resolvePresentation } from "./presentation.mjs";
import type { Presentation } from "./presentation.mjs";

const PresentationContext = createContext<Presentation | null>(null);
export function PresentationProvider({ presentation, children }: { presentation: Presentation; children: ReactNode }) {
  return <PresentationContext.Provider value={presentation}>{children}</PresentationContext.Provider>;
}
export function usePresentation(): Presentation { return useContext(PresentationContext) ?? resolvePresentation(); }
export function OrderedControls({ group, entries }: { group: string; entries: Record<string, ReactNode> }) {
  const p = usePresentation();
  return <>{controlOrder(p, group, entries).map(id => <Fragment key={id}>{entries[id]}</Fragment>)}</>;
}
export function ErrorDisplay({ error }: { error: unknown }) {
  const p = usePresentation();
  const shown = errorPresentation(error, p);
  return <><p role="alert">{shown.text}</p>{shown.details !== shown.text && <details><summary>{message(p, "error.diagnostics")}</summary><pre className="onboarding-json">{shown.details}</pre></details>}</>;
}

/** An explicitly suppressed catalog never mounts a generated-catalog consumer. */
export function PresentationFailure({ error }: { error: unknown }) {
  if (error instanceof PresentationError && error.machineOnly) return <p role="alert">{error.code}</p>;
  return <ErrorDisplay error={error} />;
}
