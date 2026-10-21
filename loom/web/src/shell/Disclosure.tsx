import { useCallback, useEffect, useId, useRef, useState, type ReactNode } from "react";
import { disclosureView, type DisclosureLabels } from "./disclosure-state";
import "./shell.css";

interface Props {
  labels: DisclosureLabels;
  children: ReactNode;
  defaultOpen?: boolean;
  open?: boolean;
  onToggle?: (open: boolean) => void;
  testId?: string;
  className?: string;
}

/** The disclosure of the chat screen: a state glyph (CSS, setting disclosure_glyphs; preset ▼ collapsed, ▲ expanded), a label that changes, aria-expanded, a 44 px target, content right under the header. */
export default function Disclosure({ labels, children, defaultOpen = false, open: controlled, onToggle, testId, className }: Props) {
  const [inner, setInner] = useState(defaultOpen);
  const open = controlled ?? inner;
  const view = disclosureView(open, labels);
  const id = useId();
  const root = useRef<HTMLDivElement>(null);
  const toggle = useCallback(() => { const next = !open; setInner(next); onToggle?.(next); }, [open, onToggle]);
  // Opening scrolls the content into view when it would land below the visible area — on a toggle only, never on the first render:
  // a screen that mounts with an open disclosure (the navigator's map under its list, preset navigator_layout = both) must open at
  // its top, not scrolled down to the map (P7: the first screenshot of Graf showed the last row and the map, not the rows).
  const seen = useRef(false);
  useEffect(() => {
    if (!seen.current) { seen.current = true; return; }
    if (!open || !root.current) return;
    const node = root.current;
    const frame = requestAnimationFrame(() => {
      const box = node.getBoundingClientRect();
      if (box.bottom > window.innerHeight || box.top < 0) node.scrollIntoView({ block: "nearest" });
    });
    return () => cancelAnimationFrame(frame);
  }, [open]);
  return (
    <div className={`disclosure${className ? " " + className : ""}`} ref={root} data-testid={testId} data-state={view.chevron}>
      <button type="button" className="disclosure-head" aria-expanded={view.ariaExpanded === "true"} aria-controls={id} onClick={toggle}>
        <span className="disclosure-chevron" aria-hidden="true" />
        <span className="disclosure-label">{view.label}</span>
      </button>
      <div className="disclosure-body" id={id} role="region">
        <div className="disclosure-inner">{children}</div>
      </div>
    </div>
  );
}
