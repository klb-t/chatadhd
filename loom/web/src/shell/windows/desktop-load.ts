// R101 D2: what the desktop reads from the kernel - the pins (perspective pinned_on around the desktop node, plus the metadata of the
// links: order, label_override, glyph), the first pins of the declaration written once (window.pin) when the desktop node does not exist
// yet, and the person's graph behind (graph.navigate around the declaration's focus, within the budget). The kernel is the only source;
// nothing is invented: a failed read is said (the desktop shows it), not replaced by made-up rows.
import type { LoomApi } from "../../api/loom-api";
import { pinsFrom, type BackEdge, type BackNode, type PinRow, type ResolvedDesktop } from "./desktop-icons";

const seeding = new Map<string, Promise<void>>();

/** The node of a desktop address (a representative node labelled with its address), or null when nothing was ever pinned on it. */
async function desktopNode(api: LoomApi, focus: string): Promise<string | null> {
  const rows = await api.getNodes({ label: focus, limit: 50 });
  const n = rows.find((r) => r.label === focus || r.metadata?.address === focus);
  return n ? n.id : null;
}

/** The pins of the declaration's desktop. When the desktop node does not exist yet and the declaration has first pins, they are written
 *  once (one window.pin each, in their order) - after that the node exists and a person who unpins everything keeps an empty desktop. */
export async function loadPins(api: LoomApi, pins: NonNullable<ResolvedDesktop["pins"]>): Promise<{ desktop: string | null; rows: PinRow[]; seeded: number }> {
  let desk = await desktopNode(api, pins.focus);
  let seeded = 0;
  if (!desk && pins.seed.length && api.actions) {
    const key = pins.focus;
    if (!seeding.has(key)) seeding.set(key, (async () => {
      for (const target of pins.seed) {
        await api.actions!.run({ action: "window.pin", element: { class: "application" }, args: { target, desktop: pins.focus } });
        seeded++;
      }
    })().finally(() => seeding.delete(key)));
    await seeding.get(key);
    desk = await desktopNode(api, pins.focus);
  }
  if (!desk) return { desktop: null, rows: [], seeded };
  const [side, links] = await Promise.all([
    api.renderPerspectives({ ids: ["pinned_on"], seeds: [desk] }),
    api.getEdges({ node_id: desk, link_type: "pinned_on", limit: 1000 }).catch((e) => { console.debug("desktop: the metadata of the pins is not read", e); return []; }),
  ]);
  const view = side.perspectives?.pinned_on;
  return { desktop: desk, rows: pinsFrom(desk, view?.nodes ?? [], links), seeded };
}

/** The graph behind: the focus and its neighbourhood (graph.navigate, depth and budget of the declaration) with the real edges among them. */
export async function loadAround(api: LoomApi, around: NonNullable<ResolvedDesktop["around"]>, lang: string): Promise<{ nodes: BackNode[]; edges: BackEdge[] }> {
  if (!api.actions?.graphNavigate || around.budget <= 0) return { nodes: [], edges: [] };
  const r = await api.actions.graphNavigate({ focus: around.focus, hops: Math.max(1, around.depth), limit: around.budget, lang });
  const nodes: BackNode[] = [{ address: r.focus.address, kind: r.focus.kind }, ...(r.children ?? []).map((c) => ({ address: c.address, kind: c.kind }))].slice(0, Math.max(1, around.budget));
  return { nodes, edges: (r.edges ?? []).map((e) => ({ src: e.src, dst: e.dst })) };
}
