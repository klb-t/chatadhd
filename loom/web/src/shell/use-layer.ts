import { useEffect, useRef } from "react";
import { layerStack } from "./history";

/** A layer of the page (a sheet, a menu, a card, a path step): while `open`, it has one history entry; Back / popstate / Escape
 *  call `close`. When the page closes it itself, the entry is taken back. */
export function useLayer(id: string, open: boolean, close: () => void): void {
  const closeRef = useRef(close);
  closeRef.current = close;
  useEffect(() => {
    const stack = layerStack();
    if (!stack || !open) return;
    stack.push(id, () => closeRef.current());
    return () => { stack.remove(id); };
  }, [id, open]);
}
