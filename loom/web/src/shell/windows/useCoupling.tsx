// W6 (OKNA §5): the page side of coupling - the runtime values of window parameters, the providers components read them through,
// the menu ⋯ entries (bind / unbind), the chain on the title bar and the coupling advisories (shown with the arrangement chips and
// answered the same way, advisory.answer with code + fingerprint). Bindings live in the layout (one journal entry per change).
import { useCallback, useMemo, useRef, useState, type ReactNode } from "react";
import type { WState, Win } from "./model";
import { afterBind, beforeUnbind, bindParam, bindingsOf, paramsOfKind, chainOf, couplingAdvisories, couplingMenu, emptyValues, resolveParam, sayWords, setParam, unbindParam, COUPLING, type Values } from "./coupling";
import { WindowParams, type WindowParamsApi } from "./WindowParams";
import type { WinAdvisory } from "./arrange";
import { nextChainMode, oneEntry } from "./steps";

export interface CouplingHook {
  provide: (w: Win, node: ReactNode) => ReactNode;
  extraMenu: (w: Win) => { id: string; label: string; icon?: string }[];
  onExtra: (id: string, entry: string) => void;
  chain: (w: Win) => { icon: string; lines: string[]; title: string } | null;
  advisories: WinAdvisory[];
  /** W7: a sentence's window.bind / window.unbind - a parameter of the given windows on a shared channel (or independent again); one journal entry. */
  bindAll: (ids: string[], param: string) => void;
  unbindAll: (ids: string[], param: string) => void;
  /** W7 (§5.3, gestures.json): a tap on the chain walks the binding of the window's first bound parameter through chain_cycle. */
  cycleChain: (id: string) => void;
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
  const bindAll = useCallback((ids: string[], param: string) => {
    const s = winRef.current;
    let next = s, values = valuesRef.current;
    for (const id of ids) next = bindParam(next, id, param, { binding: "shared", channel: param });
    for (const id of ids) values = afterBind(next.layout, values, id, param).values;
    commit(oneEntry(s, next, "bind"));
    put(values);
  }, [winRef, commit, put]);
  const unbindAll = useCallback((ids: string[], param: string) => {
    const s = winRef.current;
    let next = s, values = valuesRef.current;
    for (const id of ids) { values = beforeUnbind(next.layout, values, id, param); next = unbindParam(next, id, param); }
    put(values);
    commit(oneEntry(s, next, "unbind"));
  }, [winRef, commit, put]);
  const cycleChain = useCallback((id: string) => {
    const s = winRef.current;
    const w = s.layout.windows.find((x) => x.id === id);
    const bound = bindingsOf(w);
    const param = Object.keys(bound)[0] ?? paramsOfKind(String(w?.params?.panel ?? w?.kind ?? ""))[0];
    if (!w || !param) return;
    const source = s.layout.windows.find((o) => o.id !== id && paramsOfKind(String(o.params?.panel ?? o.kind)).includes(param));
    const mode = nextChainMode(bound[param]?.binding ?? "independent", !!source);
    if (mode === "independent") { put(beforeUnbind(s.layout, valuesRef.current, id, param)); commit(unbindParam(s, id, param)); return; }
    const binding = mode === "linked" && source ? { binding: "linked", from: { window: source.id, param }, dependency: { kind: "same" } } : { binding: mode, channel: param };
    const next = bindParam(s, id, param, binding);
    commit(next);
    put(afterBind(next.layout, valuesRef.current, id, param).values);
  }, [winRef, commit, put]);
  return { provide, extraMenu, onExtra, chain, advisories, bindAll, unbindAll, cycleChain };
}
