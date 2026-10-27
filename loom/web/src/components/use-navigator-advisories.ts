// G3c: the model of the navigator map with its advisories. The remembered answers are read (advisory.list) only once there is something to
// advise, so a level that never advises sends no request. Without the catalogue actions every advisory asks.
import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import type { ViewModel } from "../api/view-model";
import { navigatorAdvisories, readMapping, resolveAsk } from "./navigator-advisory";
import { rememberedKeys } from "../shell/windows/arrange";

export function useNavigatorAdvisories(model: ViewModel, mappingSetting?: unknown): ViewModel {
  const mappingKey = typeof mappingSetting === "string" ? mappingSetting : JSON.stringify(mappingSetting ?? null);
  const raw = useMemo(() => navigatorAdvisories(model, undefined, readMapping(mappingSetting)), [model, mappingKey]); // eslint-disable-line react-hooks/exhaustive-deps
  const [state, setState] = useState<{ remembered: Set<string>; ask: boolean } | null>(null);
  const needed = raw.length > 0;
  useEffect(() => {
    if (!needed || state) return;
    let alive = true;
    (async () => {
      let next = { remembered: new Set<string>(), ask: true };
      try {
        const actions = api.actions;
        if (actions) {
          const r = await actions.run({ action: "advisory.list", element: { class: "application" }, args: {} });
          const d = r.status === "completed" ? r.result?.data as { acks?: { code: string; fingerprint?: string; choice?: string }[]; effective?: { ask?: boolean } } : null;
          next = { remembered: rememberedKeys(d), ask: d?.effective?.ask !== false };
        }
      } catch { /* no kernel actions: every advisory asks */ }
      if (alive) setState(next);
    })();
    return () => { alive = false; };
  }, [needed, state]);
  return useMemo(() => state ? { ...model, advisories: resolveAsk(raw, state.remembered, state.ask) } : model, [model, raw, state]);
}
