// W10 (OKNA §8.3): the React side of keep.ts. `useKeptState` is a useState whose value outlives the window's unmount (kept in the page
// store under the window id, only for keys the kind declares in state_keys). `KeptScroll` keeps the scroll positions inside a window's
// body: read at unmount (the DOM is still there in a layout effect's cleanup), restored at the next mount for up to `restore_frames`
// animation frames (content that loads late).
import { useEffect, useLayoutEffect, useRef, useState, type ReactNode } from "react";
import { MOUNT, pageStore, stateKeysOf } from "./keep";

export function useKeptState<T>(win: { id: string; kind: string } | undefined, key: string, initial: T): [T, (v: T) => void] {
  const [v, setV] = useState<T>(() => {
    if (!win) return initial;
    const kept = pageStore.get(win.id);
    return key in kept ? (kept[key] as T) : initial;
  });
  const set = (next: T) => { setV(next); if (win) pageStore.keep(win, { [key]: next }); };
  return [v, set];
}

interface Spot { i: number; top: number; left: number }
const spotsOf = (root: HTMLElement): Spot[] => {
  const all = [root, ...root.querySelectorAll<HTMLElement>("*")];
  const out: Spot[] = [];
  all.forEach((el, i) => { if (el.scrollTop > 0 || el.scrollLeft > 0) out.push({ i, top: el.scrollTop, left: el.scrollLeft }); });
  return out;
};

export function KeptScroll({ win, className, children }: { win: { id: string; kind: string }; className?: string; children: ReactNode }) {
  const ref = useRef<HTMLDivElement>(null);
  const on = stateKeysOf(win.kind).includes("scroll");
  useLayoutEffect(() => {
    const root = ref.current;
    if (!on || !root) return;
    const spots = (pageStore.get(win.id).scroll as Spot[] | undefined) ?? [];
    let frames = typeof MOUNT.restore_frames === "number" ? MOUNT.restore_frames : 30;
    let raf = 0;
    const tryRestore = () => {
      const all = [root, ...root.querySelectorAll<HTMLElement>("*")];
      let left = 0;
      for (const s of spots) {
        const el = all[s.i];
        if (!el) { left++; continue; }
        el.scrollTop = s.top; el.scrollLeft = s.left;
        if (Math.abs(el.scrollTop - s.top) > 1 || Math.abs(el.scrollLeft - s.left) > 1) left++;
      }
      if (left && frames-- > 0) raf = requestAnimationFrame(tryRestore);
    };
    if (spots.length) tryRestore();
    return () => { cancelAnimationFrame(raf); pageStore.keep(win, { scroll: spotsOf(root) }); };
  }, [win.id]);   // eslint-disable-line react-hooks/exhaustive-deps
  return <div ref={ref} className={className} data-kept-scroll={on ? "1" : undefined}>{children}</div>;
}

/** Drops the kept state of closed windows when the layout's windows change. */
export function usePruneKept(l: { windows: { id: string }[] }) {
  const ids = l.windows.map((w) => w.id).join(",");
  useEffect(() => { pageStore.prune(l as never); }, [ids]);   // eslint-disable-line react-hooks/exhaustive-deps
}
