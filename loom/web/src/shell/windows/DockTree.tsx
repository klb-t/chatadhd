// W3 (OKNA §2.2): draws a dock tree (dock.ts) - splits as flex rows/columns sized by their ratio, leaves as tab groups. A leaf with one
// window shows no tab strip (the former Workshop looks as before); a leaf with several shows tabs and the active window.
import type { CSSProperties, ReactNode } from "react";
import type { Win } from "./model";
import { sideFlex, type DockNode } from "./dock";

export interface DockTreeProps {
  node: DockNode;
  renderWindow: (w: Win) => ReactNode;
  title: (w: Win) => string;
  onActivate?: (w: Win) => void;
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
  return (
    <div className="dock-split" data-split={node.dir} style={style}>
      <Node node={node.a} flex={sideFlex(node.a, "a", node.ratio, node.b)} props={props} />
      <Node node={node.b} flex={sideFlex(node.b, "b", node.ratio, node.a)} props={props} />
    </div>
  );
}

export default function DockTree(props: DockTreeProps) {
  return <Node node={props.node} flex="1 1 0" props={props} />;
}
