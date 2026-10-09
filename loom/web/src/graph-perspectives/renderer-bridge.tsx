import { useMemo, type ComponentType, type CSSProperties } from "react";
import type { KnowledgeRecord } from "../api/knowledge";
import { newPane, type Parameter, type Parameters } from "../workspace/state";
import type { ObjectRef, SelectionResult, SourceRelation } from "./types";
import { referenceKey } from "./navigation";

/** Additive KnowledgeGraph hook: a completed projection, never a second selector. */
export interface KnowledgeGraphProjection {
  nodes: KnowledgeRecord[];
  edges: KnowledgeRecord[];
  styles: Record<string, CSSProperties>;
  identity: Record<string, string>;
  explanations: Record<string, string>;
  onLayoutMeasured?(duration: number): void;
  labels: { graph: string; focus: string; inspectRelation: string };
}
export interface KnowledgeGraphRendererProps {
  data: { entities?: KnowledgeRecord[]; claims?: KnowledgeRecord[] };
  focus: string;
  onSelect(kind: string, row: KnowledgeRecord): void;
  parameters: Parameters;
  change<K extends Parameter>(parameter: K, value: Parameters[K]): void;
  projection?: KnowledgeGraphProjection;
}
export interface PerspectiveRendererProps {
  selection: SelectionResult;
  onSelect(ref: ObjectRef): void;
  onInspectRelation(relation: SourceRelation): void;
  labels: KnowledgeGraphProjection["labels"];
}

/** Host passes its existing renderer after applying the separately reviewed B hook. */
export function createKnowledgeGraphBridge(Renderer: ComponentType<KnowledgeGraphRendererProps>, options: { onLayoutMeasured?(duration: number): void } = {}): ComponentType<PerspectiveRendererProps> {
  return function KnowledgeGraphBridge({ selection, onSelect, onInspectRelation, labels }) {
    const parameters = useMemo(() => newPane("graph").parameters, []);
    const projection = useMemo<KnowledgeGraphProjection>(() => {
      const byKey = new Map(selection.objects.map((object) => [object.key, object]));
      return {
        nodes: selection.graph.nodes.map((node) => ({ ...node, label: byKey.get(node.id)?.visual.label ?? node.label, perspective_group: byKey.get(node.id)?.visual.group, evidence_class: byKey.get(node.id)?.evidence })),
        edges: selection.graph.edges.map((edge) => ({ ...edge, id: edge.id, subject: edge.src, object: edge.dst, predicate: edge.type ?? edge.link_type })),
        styles: Object.fromEntries(selection.objects.map((object) => [object.key, {
          opacity: object.visual.opacity,
          filter: object.visual.blur ? `blur(${object.visual.blur}px)` : undefined,
          "--accent": object.visual.color,
        } as CSSProperties])),
        identity: Object.fromEntries(selection.objects.map((object) => [object.key, object.canonicalKey])),
        explanations: Object.fromEntries(selection.objects.map((object) => [object.key, JSON.stringify({
          status: object.status, evidence: object.evidence, confidence: object.confidence ?? null,
          visual: object.visualReasons, resolution: object.resolution,
        })])),
        labels, onLayoutMeasured: options.onLayoutMeasured,
      };
    }, [selection, labels]);
    const focus = selection.plan.focus;
    const snapshot = selection.plan.snapshot ?? focus?.snapshot;
    const representation = snapshot === focus?.snapshot ? focus?.representation : undefined;
    const candidates = focus ? selection.objects.filter(object => object.ref.source === focus.source &&
      (snapshot === undefined || object.ref.snapshot === snapshot) &&
      (representation === undefined || object.ref.representation === representation)) : [];
    const exact = focus && candidates.find(object => object.ref.selector === focus.selector &&
      (focus.canonicalId === undefined || object.ref.canonicalId === focus.canonicalId));
    const canonical = focus?.canonicalId !== undefined && candidates.find(object => object.ref.canonicalId === focus.canonicalId);
    const focusKey = exact ? exact.key : canonical ? canonical.key : focus ? referenceKey(focus) : "";
    return <Renderer data={{ entities: projection.nodes, claims: projection.edges }}
      focus={focusKey}
      parameters={parameters} change={() => undefined} projection={projection}
      onSelect={(kind, row) => {
        if (kind === "entities") {
          const object = selection.objects.find((item) => item.key === row.id);
          if (object) onSelect(object.ref);
        } else {
          const relation = selection.relations.find((item) => item.id === row.id);
          if (relation) onInspectRelation(relation);
        }
      }} />;
  };
}
