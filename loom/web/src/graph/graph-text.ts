// R35-4: every visible string of the graph panel, PL/EN by ui_language (shell language). Unknown language falls back to en.
export type GraphTextId = "filter_placeholder" | "reload" | "depth" | "counts" | "page_count" | "selected" | "canvas_label";

const TEXT: Record<string, Record<GraphTextId, string>> = {
  en: {
    filter_placeholder: "conv_id filter (empty = all)",
    reload: "Reload",
    depth: "Expand depth on click: {n}",
    counts: "{nodes} nodes · {edges} edges",
    page_count: "{shown} of {total} messages",
    selected: "selected: {label} ({kind})",
    canvas_label: "Knowledge graph",
  },
  pl: {
    filter_placeholder: "filtr conv_id (puste = wszystkie)",
    reload: "Odśwież",
    depth: "Głębokość rozwinięcia po kliknięciu: {n}",
    counts: "{nodes} węzłów · {edges} krawędzi",
    page_count: "{shown} z {total} wiadomości",
    selected: "wybrano: {label} ({kind})",
    canvas_label: "Graf wiedzy",
  },
};

export const GRAPH_TEXT_LANGUAGES = Object.keys(TEXT);

export function graphText(language: string, id: GraphTextId, params: Record<string, string | number> = {}): string {
  const table = TEXT[language.toLowerCase().split(/[-_]/)[0]] ?? TEXT.en;
  return (table[id] ?? TEXT.en[id]).replace(/\{(\w+)\}/g, (m, k) => (k in params ? String(params[k]) : m));
}
