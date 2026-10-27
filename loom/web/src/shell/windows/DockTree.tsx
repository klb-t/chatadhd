// W3 (OKNA §2.2): draws a dock tree (dock.ts) - splits as flex rows/columns sized by their ratio, leaves as tab groups. A leaf with one
// window shows no tab strip (the former Workshop looks as before); a leaf with several shows tabs and the active window.
// W4: between the two drawn sides of a split sized by its ratio sits a grip (role separator): dragging it or its arrow keys change the
// ratio (the page journals one entry per drag). A side sized by its content (the list) has no grip: its ratio is kept, not drawn.
import type { CSSProperties, KeyboardEvent, PointerEvent, ReactNode } from "react";
import type { Win } from "./model";
import { ratioAt, sideFlex, type DockNode } from "./dock";

export interface DockTreeProps {
  node: DockNode;
  renderWindow: (w: Win) => ReactNode;
  title: (w: Win) => string;
  onActivate?: (w: Win) => void;
  /** W4: a split's ratio changed by its grip; `commit` = the end of the drag (or a key), when the page writes one journal entry. */
  onRatio?: (path: string, ratio: number, commit: boolean) => void;
  /** W4: the step of one arrow key on a grip (preset 0.05). */
  keyStep?: number;
}

function Grip({ node, props }: { node: Extract<DockNode, { type: "split" }>; props: DockTreeProps }) {
  const h = node.dir === "h";
  const box = (el: HTMLElement) => { const r = el.parentElement!.getBoundingClientRect(); return { x: r.left, y: r.top, w: r.width, h: r.height }; };
  const down = (e: PointerEvent<HTMLDivElement>) => {
    const el = e.currentTarget; el.setPointerCapture?.(e.pointerId); e.preventDefault();
    const b = box(el);
    const move = (ev: globalThis.PointerEvent) => props.onRatio?.(node.path, ratioAt(b, node.dir, ev.clientX, ev.clientY), false);
    const up = (ev: globalThis.PointerEvent) => { el.removeEventListener("pointermove", move); el.removeEventListener("pointerup", up); el.removeEventListener("pointercancel", up); props.onRatio?.(node.path, ratioAt(b, node.dir, ev.clientX, ev.clientY), true); };
    el.addEventListener("pointermove", move); el.addEventListener("pointerup", up); el.addEventListener("pointercancel", up);
  };
  const key = (e: KeyboardEvent<HTMLDivElement>) => {
    const step = props.keyStep ?? 0.05;
    const d = (h ? { ArrowLeft: -step, ArrowRight: step } : { ArrowUp: -step, ArrowDown: step } as Record<string, number>)[e.key as "ArrowLeft"];
    if (d === undefined) return;
    e.preventDefault(); props.onRatio?.(node.path, Math.min(1, Math.max(0, node.ratio + d)), true);
  };
  return <div className="dock-grip" role="separator" tabIndex={0} aria-orientation={h ? "vertical" : "horizontal"} aria-valuemin={0} aria-valuemax={100}
    aria-valuenow={Math.round(node.ratio * 100)} data-testid={`dock-grip-${node.path || "root"}`} data-dir={node.dir} onPointerDown={down} onKeyDown={key} />;
}

function Node({ node, flex, props }: { node: DockNode; flex: string; props: DockTreeProps }) {
  if (node.type === "leaf") {
    const style: CSSProperties = { display: node.hidden ? "none" : "flex", flexDirection: "column", minWidth: 0, minHeight: 0, flex };
    return (
      <div className="dock-leaf" data-leaf={node.leaf} data-testid={`dock-leaf-${node.leaf}`} style={style} hidden={node.hidden}>
        {node.windows.length > 1 && (
          <div className="dock-tabs" role="tablist" data-testid={`dock-tabs-${node.leaf}`}>
            {node.windows.map((w) => (
              <button key={w.id} type="button" role="tab" aria-selected={w.id === node.active?.id} data-testid={`dock-tab-${w.id}`}
                onClick={() => props.onActivate?.(w)}>{props.title(w)}</button>
            ))}
          </div>
        )}
        {node.active && props.renderWindow(node.active)}
      </div>
    );
  }
  const style: CSSProperties = { display: node.hidden ? "none" : "flex", flexDirection: node.dir === "v" ? "column" : "row", minWidth: 0, minHeight: 0, flex };
  const content = (n: DockNode) => n.type === "leaf" && n.sizing === "content";
  const grip = props.onRatio && !node.a.hidden && !node.b.hidden && !content(node.a) && !content(node.b);
  return (
    <div className="dock-split" data-split={node.dir} data-path={node.path} style={style}>
      <Node node={node.a} flex={sideFlex(node.a, "a", node.ratio, node.b)} props={props} />
      {grip && <Grip node={node} props={props} />}
      <Node node={node.b} flex={sideFlex(node.b, "b", node.ratio, node.a)} props={props} />
    </div>
  );
}

export default function DockTree(props: DockTreeProps) {
  return <Node node={props.node} flex="1 1 0" props={props} />;
}
