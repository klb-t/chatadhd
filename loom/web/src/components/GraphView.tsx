import { useCallback, useEffect, useRef, useState } from "react";
import { api } from "../api";
import type { GraphEdge, GraphNode } from "../api/types";

interface Props {
  convId: string | null;
}

interface SimNode extends GraphNode {
  x: number;
  y: number;
  vx: number;
  vy: number;
}

const KIND_COLORS: Record<string, string> = {
  entity: "#4d8cf2",
  topic: "#f2a94d",
  concept: "#7ed957",
  code_ref: "#d957c8",
  file: "#57d9c8",
  person: "#f25d5d",
  org: "#c8d957",
  message: "#8c8c99",
};

function colorFor(kind: string): string {
  return KIND_COLORS[kind] ?? "#9d9dff";
}

// The regex-based analyzer (core/semantic.py, ported byte-for-byte to
// include/loom/semantic_analyzer.h) is intentionally loose: its CODE_REF
// pattern is `\b(?:class|def|import|from)\s+(\w+)` with no case-insensitivity
// or dictionary check, so ordinary prose like "...every import records..." or
// "...built from the ground up..." mints entity/code_ref nodes labelled
// "records" or "the". That is correct, tested parity with the Python engine
// (core/semantic.py has the exact same behaviour) and the graph/db layers
// must keep producing those nodes unfiltered - other consumers (search,
// context selection, the archive pipeline) may still want them. This is a
// presentation-only filter: it only decides what the force-directed canvas
// draws, never what gets written or returned by the API.
const GRAPH_NOISE_WORDS = new Set([
  // English function words / stopwords the CODE_REF and relation regexes
  // most often latch onto right after "class/def/import/from/see/cf.".
  "the", "a", "an", "and", "or", "nor", "but", "so", "yet", "for", "from",
  "with", "without", "about", "into", "onto", "over", "under", "after",
  "before", "during", "while", "when", "where", "what", "which", "who",
  "whom", "whose", "this", "that", "these", "those", "there", "here", "it",
  "its", "is", "are", "was", "were", "be", "been", "being", "have", "has",
  "had", "will", "would", "can", "could", "should", "may", "might", "must",
  "shall", "not", "no", "than", "then", "also", "very", "just", "only",
  "more", "most", "some", "any", "all", "each", "every", "other", "another",
  "such", "own", "same", "few", "many", "much", "both", "either", "neither",
  "one", "two", "new", "old", "in", "on", "at", "by", "to", "of", "as", "if",
  // Polish equivalents (the keyword lists are bilingual PL/EN).
  "i", "oraz", "ale", "lub", "z", "do", "na", "od", "dla", "przez", "to",
  "ten", "ta", "te", "tego", "tej", "tym",
  // Common English nouns that repeatedly show up as CODE_REF false
  // positives after "import"/"from" in prose (not code) in this corpus.
  "records", "record", "data", "files", "file", "info", "information",
]);

function isNoiseNode(n: GraphNode): boolean {
  const label = (n.label ?? "").trim().toLowerCase();
  if (!label) return true;
  if (label.length <= 2) return true;
  return GRAPH_NOISE_WORDS.has(label);
}

function filterGraphNoise<N extends GraphNode>(nodes: N[], edges: GraphEdge[]): { nodes: N[]; edges: GraphEdge[] } {
  const kept = nodes.filter((n) => !isNoiseNode(n));
  const keptIds = new Set(kept.map((n) => n.id));
  return { nodes: kept, edges: edges.filter((e) => keptIds.has(e.src) && keptIds.has(e.dst)) };
}

export default function GraphView({ convId }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null);
  const containerRef = useRef<HTMLDivElement>(null);
  const nodesRef = useRef<Map<string, SimNode>>(new Map());
  const edgesRef = useRef<GraphEdge[]>([]);
  const transformRef = useRef({ x: 0, y: 0, scale: 1 });
  const dragRef = useRef<{ mode: "pan" | "node"; startX: number; startY: number; nodeId?: string } | null>(null);
  const rafRef = useRef<number>(0);

  const [convFilter, setConvFilter] = useState(convId ?? "");
  const [depth, setDepth] = useState(1);
  const [nodeCount, setNodeCount] = useState(0);
  const [edgeCount, setEdgeCount] = useState(0);
  const [error, setError] = useState<string | null>(null);
  const [selected, setSelected] = useState<GraphNode | null>(null);

  useEffect(() => setConvFilter(convId ?? ""), [convId]);

  const load = useCallback(() => {
    api
      .getGraphData(convFilter || undefined)
      .then((raw) => {
        const data = filterGraphNoise(raw.nodes, raw.edges);
        const map = new Map<string, SimNode>();
        const w = containerRef.current?.clientWidth ?? 400;
        const h = containerRef.current?.clientHeight ?? 400;
        data.nodes.forEach((n, i) => {
          const angle = (i / Math.max(1, data.nodes.length)) * Math.PI * 2;
          map.set(n.id, {
            ...n,
            x: w / 2 + Math.cos(angle) * 80,
            y: h / 2 + Math.sin(angle) * 80,
            vx: 0,
            vy: 0,
          });
        });
        nodesRef.current = map;
        edgesRef.current = data.edges;
        setNodeCount(map.size);
        setEdgeCount(data.edges.length);
        setError(null);
      })
      .catch((err) => setError(err instanceof Error ? err.message : String(err)));
  }, [convFilter]);

  useEffect(load, [load]);

  // Force simulation + render loop.
  useEffect(() => {
    const canvas = canvasRef.current;
    if (!canvas) return;
    const ctx = canvas.getContext("2d");
    if (!ctx) return;

    function resize() {
      const el = containerRef.current;
      if (!el || !canvas) return;
      canvas.width = el.clientWidth * devicePixelRatio;
      canvas.height = el.clientHeight * devicePixelRatio;
      canvas.style.width = `${el.clientWidth}px`;
      canvas.style.height = `${el.clientHeight}px`;
    }
    resize();
    const ro = new ResizeObserver(resize);
    if (containerRef.current) ro.observe(containerRef.current);

    function tick() {
      const nodes = Array.from(nodesRef.current.values());
      const w = (canvas!.width / devicePixelRatio) || 400;
      const h = (canvas!.height / devicePixelRatio) || 400;

      // Repulsion between all pairs (fine for the small graphs this view targets).
      for (let i = 0; i < nodes.length; i++) {
        for (let j = i + 1; j < nodes.length; j++) {
          const a = nodes[i];
          const b = nodes[j];
          let dx = a.x - b.x;
          let dy = a.y - b.y;
          let d2 = dx * dx + dy * dy;
          if (d2 < 1) d2 = 1;
          const force = 2200 / d2;
          const d = Math.sqrt(d2);
          dx /= d;
          dy /= d;
          a.vx += dx * force;
          a.vy += dy * force;
          b.vx -= dx * force;
          b.vy -= dy * force;
        }
      }
      // Spring attraction along edges.
      for (const e of edgesRef.current) {
        const a = nodesRef.current.get(e.src);
        const b = nodesRef.current.get(e.dst);
        if (!a || !b) continue;
        const dx = b.x - a.x;
        const dy = b.y - a.y;
        const d = Math.max(1, Math.sqrt(dx * dx + dy * dy));
        const force = (d - 90) * 0.02;
        a.vx += (dx / d) * force;
        a.vy += (dy / d) * force;
        b.vx -= (dx / d) * force;
        b.vy -= (dy / d) * force;
      }
      // Centering + damping + integrate.
      for (const n of nodes) {
        if (dragRef.current?.nodeId === n.id) continue;
        n.vx += (w / 2 - n.x) * 0.0015;
        n.vy += (h / 2 - n.y) * 0.0015;
        n.vx *= 0.85;
        n.vy *= 0.85;
        n.x += n.vx;
        n.y += n.vy;
      }

      // Render.
      const { x: tx, y: ty, scale } = transformRef.current;
      ctx!.save();
      ctx!.scale(devicePixelRatio, devicePixelRatio);
      ctx!.fillStyle = "#00000000";
      ctx!.clearRect(0, 0, w, h);
      ctx!.translate(tx, ty);
      ctx!.scale(scale, scale);

      ctx!.strokeStyle = "rgba(140,140,153,0.35)";
      ctx!.lineWidth = 1 / scale;
      for (const e of edgesRef.current) {
        const a = nodesRef.current.get(e.src);
        const b = nodesRef.current.get(e.dst);
        if (!a || !b) continue;
        ctx!.beginPath();
        ctx!.moveTo(a.x, a.y);
        ctx!.lineTo(b.x, b.y);
        ctx!.stroke();
      }
      for (const n of nodes) {
        ctx!.beginPath();
        ctx!.arc(n.x, n.y, 8, 0, Math.PI * 2);
        ctx!.fillStyle = colorFor(n.kind);
        ctx!.fill();
        ctx!.font = `${12 / scale}px sans-serif`;
        ctx!.fillStyle = "#e8e8ec";
        ctx!.fillText(n.label ?? n.id, n.x + 10, n.y + 4);
      }
      ctx!.restore();

      rafRef.current = requestAnimationFrame(tick);
    }
    rafRef.current = requestAnimationFrame(tick);
    return () => {
      cancelAnimationFrame(rafRef.current);
      ro.disconnect();
    };
  }, []);

  const toWorld = useCallback((clientX: number, clientY: number) => {
    const rect = canvasRef.current!.getBoundingClientRect();
    const { x: tx, y: ty, scale } = transformRef.current;
    return {
      x: (clientX - rect.left - tx) / scale,
      y: (clientY - rect.top - ty) / scale,
    };
  }, []);

  const hitTest = useCallback((wx: number, wy: number): SimNode | null => {
    for (const n of nodesRef.current.values()) {
      const dx = n.x - wx;
      const dy = n.y - wy;
      if (dx * dx + dy * dy < 14 * 14) return n;
    }
    return null;
  }, []);

  const onPointerDown = useCallback(
    (e: React.PointerEvent<HTMLCanvasElement>) => {
      const world = toWorld(e.clientX, e.clientY);
      const hit = hitTest(world.x, world.y);
      dragRef.current = hit
        ? { mode: "node", startX: e.clientX, startY: e.clientY, nodeId: hit.id }
        : { mode: "pan", startX: e.clientX, startY: e.clientY };
      (e.target as HTMLCanvasElement).setPointerCapture(e.pointerId);
    },
    [toWorld, hitTest],
  );

  const onPointerMove = useCallback(
    (e: React.PointerEvent<HTMLCanvasElement>) => {
      const drag = dragRef.current;
      if (!drag) return;
      if (drag.mode === "pan") {
        transformRef.current.x += e.clientX - drag.startX;
        transformRef.current.y += e.clientY - drag.startY;
        drag.startX = e.clientX;
        drag.startY = e.clientY;
      } else if (drag.mode === "node" && drag.nodeId) {
        const world = toWorld(e.clientX, e.clientY);
        const n = nodesRef.current.get(drag.nodeId);
        if (n) {
          n.x = world.x;
          n.y = world.y;
          n.vx = 0;
          n.vy = 0;
        }
      }
    },
    [toWorld],
  );

  const onPointerUp = useCallback(
    async (e: React.PointerEvent<HTMLCanvasElement>) => {
      const drag = dragRef.current;
      dragRef.current = null;
      if (!drag) return;
      const moved = Math.abs(e.clientX - drag.startX) > 4 || Math.abs(e.clientY - drag.startY) > 4;
      if (drag.mode === "node" && drag.nodeId && !moved) {
        const node = nodesRef.current.get(drag.nodeId) ?? null;
        setSelected(node);
        try {
          const rawExpansion = await api.expandGraph([drag.nodeId], depth);
          const expansion = filterGraphNoise(rawExpansion.nodes, rawExpansion.edges);
          const w = containerRef.current?.clientWidth ?? 400;
          const h = containerRef.current?.clientHeight ?? 400;
          const origin = nodesRef.current.get(drag.nodeId);
          for (const n of expansion.nodes) {
            if (!nodesRef.current.has(n.id)) {
              nodesRef.current.set(n.id, {
                ...n,
                x: (origin?.x ?? w / 2) + (Math.random() - 0.5) * 60,
                y: (origin?.y ?? h / 2) + (Math.random() - 0.5) * 60,
                vx: 0,
                vy: 0,
              });
            }
          }
          const existingEdgeKeys = new Set(edgesRef.current.map((e2) => `${e2.src}->${e2.dst}`));
          for (const e2 of expansion.edges) {
            const key = `${e2.src}->${e2.dst}`;
            if (!existingEdgeKeys.has(key)) {
              edgesRef.current.push(e2);
              existingEdgeKeys.add(key);
            }
          }
          setNodeCount(nodesRef.current.size);
          setEdgeCount(edgesRef.current.length);
        } catch (err) {
          setError(err instanceof Error ? err.message : String(err));
        }
      }
    },
    [depth],
  );

  const onWheel = useCallback((e: React.WheelEvent<HTMLCanvasElement>) => {
    e.preventDefault();
    const factor = e.deltaY > 0 ? 0.9 : 1.1;
    const t = transformRef.current;
    const rect = canvasRef.current!.getBoundingClientRect();
    const cx = e.clientX - rect.left;
    const cy = e.clientY - rect.top;
    t.x = cx - (cx - t.x) * factor;
    t.y = cy - (cy - t.y) * factor;
    t.scale = Math.min(4, Math.max(0.2, t.scale * factor));
  }, []);

  return (
    <div data-testid="graph-view" style={{ display: "flex", flexDirection: "column", height: "100%", minHeight: 0 }}>
      <div className="form-row" style={{ display: "flex", gap: 6, alignItems: "center", flexWrap: "wrap" }}>
        <input
          type="text"
          placeholder="conv_id filter (empty = all)"
          value={convFilter}
          onChange={(e) => setConvFilter(e.target.value)}
          style={{ flex: 1, minWidth: 140 }}
          data-testid="graph-conv-filter"
        />
        <button onClick={load} data-testid="graph-reload">
          Reload
        </button>
      </div>
      <div className="form-row">
        <label htmlFor="graph-depth">Expand depth on click: {depth}</label>
        <input
          id="graph-depth"
          type="range"
          min={1}
          max={4}
          value={depth}
          onChange={(e) => setDepth(Number(e.target.value))}
        />
      </div>
      {error && <div className="empty-state" style={{ color: "var(--err)" }}>{error}</div>}
      <div className="pill" data-testid="graph-counts">
        {nodeCount} nodes · {edgeCount} edges
      </div>
      {selected && (
        <div className="pill" style={{ marginLeft: 6 }}>
          selected: {selected.label} ({selected.kind})
        </div>
      )}
      <div
        ref={containerRef}
        style={{
          flex: "1 1 auto",
          minHeight: 280,
          marginTop: 8,
          borderRadius: 8,
          overflow: "hidden",
          border: "1px solid var(--border)",
        }}
      >
        <canvas
          ref={canvasRef}
          data-testid="graph-canvas"
          style={{ touchAction: "none", cursor: "grab", display: "block" }}
          onPointerDown={onPointerDown}
          onPointerMove={onPointerMove}
          onPointerUp={onPointerUp}
          onWheel={onWheel}
        />
      </div>
    </div>
  );
}
