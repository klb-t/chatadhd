import { useEffect, useRef, type RefObject } from "react";

/** Focus of an overlay (a panel drawer, a sheet): when it opens, focus moves into it — onto the drawer itself (tabIndex −1) unless a
 * child already took it (an autoFocus input); when it closes, focus goes back to the element that had it, or, when that one is gone
 * (a ⋮ menu item unmounts with its menu before the drawer opens), to the opener named by `returnTo` (a data-testid). Nothing hides:
 * a reader of the screen hears the title (aria-label) of the dialog, and Back lands where the hand was. */
export function useDialogFocus(open: boolean, ref: RefObject<HTMLElement | null>, returnTo?: string): void {
  const opener = useRef<HTMLElement | null>(null);
  useEffect(() => {
    if (!open) return;
    const active = document.activeElement;
    opener.current = active instanceof HTMLElement && active !== document.body ? active : null;
    const el = ref.current;
    if (el && !(document.activeElement && el.contains(document.activeElement))) el.focus({ preventScroll: true });
    return () => {
      const back = opener.current?.isConnected ? opener.current
        : returnTo ? document.querySelector<HTMLElement>(`[data-testid="${returnTo}"]`) : null;
      if (back?.isConnected) back.focus({ preventScroll: true });
    };
  }, [open, ref, returnTo]);
}
