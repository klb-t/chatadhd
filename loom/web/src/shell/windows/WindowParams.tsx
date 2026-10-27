// W6 (OKNA §5): the parameters of the window a component is drawn in. A component that follows a parameter (the navigator's map: zoom, pan)
// reads its value and reports its own changes here; the page (App.tsx) propagates them through the bindings of coupling.ts. Without a
// provider (a component drawn outside a window, a test page) the hook gives no value and ignores writes: behaviour as before W6.
import { createContext, useContext, useMemo } from "react";

export interface WindowParamsApi { window: string; get: (param: string) => unknown; set: (param: string, value: unknown) => void }
export const WindowParams = createContext<WindowParamsApi | null>(null);

export function useWindowParam(param: string): { value: unknown; set: (value: unknown) => void; window: string | null } {
  const api = useContext(WindowParams);
  const value = api?.get(param);
  return useMemo(() => ({ value, window: api?.window ?? null, set: (v: unknown) => api?.set(param, v) }), [api, param, value]);
}
