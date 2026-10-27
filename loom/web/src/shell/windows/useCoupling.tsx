// W6 (OKNA §5): the page side of coupling - the runtime values of window parameters, the providers components read them through,
// the menu ⋯ entries (bind / unbind), the chain on the title bar and the coupling advisories (shown with the arrangement chips and
// answered the same way, advisory.answer with code + fingerprint). Bindings live in the layout (one journal entry per change).
import { useCallback, useMemo, useRef, useState, type ReactNode } from "react";
import type { WState, Win } from "./model";
import { afterBind, beforeUnbind, bindParam, chainOf, couplingAdvisories, couplingMenu, emptyValues, resolveParam, sayWords, setParam, unbindParam, COUPLING, type Values } from "./coupling";
import { WindowParams, type WindowParamsApi } from "./WindowParams";
import type { WinAdvisory } from "./arrange";

export interface CouplingHook {
  provide: (w: Win, node: ReactNode) => ReactNode;
  extraMenu: (w: Win) => { id: string; label: string; icon?: string }[];
  onExtra: (id: string, entry: string) => void;
  chain: (w: Win) => { icon: string; lines: string[]; title: string } | null;
  advisories: WinAdvisory[];
}

export function useCoupling(win: WState, winRef: { current: WState }, commit: (s: WState) => void, lang: string, title: (id: string) => string): CouplingHook {
  const [values, setValues] = useState<Values>(emptyValues);
  const valuesRef = useRef(values); valuesRef.current = values;
  const put = useCallback((v: Values) => { valuesRef.current = v; setValues(v); }, []);
  const named = useCallback((w: Win) => title(w.id), [title]);
  const apis = useMemo(() => new Map<string, WindowParamsApi>(), [values, win.layout]);   // eslint-disable-line react-hooks/exhaustive-deps
  const apiOf = useCallback((id: string): WindowParamsApi => {
    let a = apis.get(id);
    if (!a) {
      const l = win.layout, v = values;
      a = { window: id, get: (p) => resolveParam(l, v, id, p), set: (p, x) => put(setParam(winRef.current.layout, valuesRef.current, id, p, x).values) };
      apis.set(id, a);
    }
    return a;
  }, [apis, win.layout, values, put, winRef]);
  const provide = useCallback((w: Win, node: ReactNode) => <WindowParams.Provider value={apiOf(w.id)}>{node}</WindowParams.Provider>, [apiOf]);
  const extraMenu = useCallback((w: Win) => couplingMenu(winRef.current.layout, w.id, lang, named).map(({ id, label, icon }) => ({ id, label, icon })), [winRef, lang, named]);
  const onExtra = useCallback((id: string, entry: string) => {
    const s = winRef.current;
    const m = couplingMenu(s.layout, id, lang, named).find((x) => x.id === entry);
    if (!m) return;
    if (m.action.op === "bind") {
      const next = bindParam(s, id, m.action.param, m.action.binding);
      commit(next);
      put(afterBind(next.layout, valuesRef.current, id, m.action.param).values);
    } else {
      put(beforeUnbind(s.layout, valuesRef.current, id, m.action.param));
      commit(unbindParam(s, id, m.action.param));
    }
  }, [winRef, lang, named, commit, put]);
  const chain = useCallback((w: Win) => { const c = chainOf(win.layout, w.id, lang, named); return c ? { ...c, title: sayWords(COUPLING.words?.chain_title, lang) } : null; }, [win.layout, lang, named]);
  const advisories = useMemo(() => couplingAdvisories(win.layout, named, lang), [win.layout, named, lang]);
  return { provide, extraMenu, onExtra, chain, advisories };
}
