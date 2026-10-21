import { useEffect, useRef, type RefObject } from "react";

/** What Tab can land on inside a dialog: the usual focusable elements that are not disabled, hidden or taken out of the order. */
const FOCUSABLE = 'a[href], button:not([disabled]), input:not([disabled]), select:not([disabled]), textarea:not([disabled]), summary, [tabindex]:not([tabindex="-1"])';
export const focusablesOf = (root: HTMLElement): HTMLElement[] =>
  [...root.querySelectorAll<HTMLElement>(FOCUSABLE)].filter(el => !el.hasAttribute("inert") && el.getClientRects().length > 0);

/** Attributes that take the layer behind an open dialog out of reach — `inert` (no focus, no clicks, hidden from a reader of the
 * screen), the same thing `aria-modal` promises; React 18 does not know the attribute, so it goes in as a plain string. */
export const inertAttr = (on: boolean): Record<string, string> => (on ? { inert: "" } : {}) as Record<string, string>;

/** Focus of an overlay (a panel drawer, a sheet): when it opens, focus moves into it — onto the drawer itself (tabIndex −1) unless a
 * child already took it (an autoFocus input); while it is open, Tab and Shift+Tab wrap inside it (the layer behind is also `inert`,
 * see inertAttr); when it closes, focus goes back to the element that had it, or, when that one is gone (a ⋮ menu item unmounts
 * with its menu before the drawer opens), to the opener named by `returnTo` (a data-testid). Nothing hides: a reader of the screen
 * hears the title (aria-label) of the dialog, and Back lands where the hand was. */
export function useDialogFocus(open: boolean, ref: RefObject<HTMLElement | null>, returnTo?: string): void {
  const opener = useRef<HTMLElement | null>(null);
  useEffect(() => {
    if (!open) return;
    const active = document.activeElement;
    opener.current = active instanceof HTMLElement && active !== document.body ? active : null;
    const el = ref.current;
    if (el && !(document.activeElement && el.contains(document.activeElement))) el.focus({ preventScroll: true });
    const onKey = (e: KeyboardEvent) => {
      if (e.key !== "Tab" || !el) return;
      const items = focusablesOf(el);
      if (items.length === 0) { e.preventDefault(); el.focus({ preventScroll: true }); return; }
      const first = items[0]!, last = items[items.length - 1]!, now = document.activeElement;
      if (e.shiftKey && (now === first || now === el)) { e.preventDefault(); last.focus(); }
      else if (!e.shiftKey && now === last) { e.preventDefault(); first.focus(); }
    };
    el?.addEventListener("keydown", onKey);
    return () => {
      el?.removeEventListener("keydown", onKey);
      const back = opener.current?.isConnected ? opener.current
        : returnTo ? document.querySelector<HTMLElement>(`[data-testid="${returnTo}"]`) : null;
      if (back?.isConnected) back.focus({ preventScroll: true });
    };
  }, [open, ref, returnTo]);
}
