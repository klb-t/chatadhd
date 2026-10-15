// Page sizes of the web views as settings (P25): loom_web_message_window (messages per page of the chat view) and loom_web_graph_page
// (message nodes per page of the graph view). The numbers here are presets used only when the setting is missing or malformed;
// 0 means no cut (everything in one answer). Older pages are always reachable, so nothing is cut for good.
import { api } from "../api";

export const MESSAGE_WINDOW_KEY = "loom_web_message_window";
export const GRAPH_PAGE_KEY = "loom_web_graph_page";
export const MESSAGE_WINDOW_PRESET = 500;
export const GRAPH_PAGE_PRESET = 2000;

export interface WebPageSizes { messageWindow: number; graphPage: number }

export function pageSizeOf(config: Record<string, unknown> | undefined, key: string, preset: number): number {
  const raw = config?.[key];
  const n = typeof raw === "string" && raw.trim() !== "" ? Number(raw) : raw;
  return typeof n === "number" && Number.isInteger(n) && n >= 0 ? n : preset;
}

export async function readWebPageSizes(): Promise<WebPageSizes> {
  try {
    const config = await api.getConfig() as Record<string, unknown>;
    return { messageWindow: pageSizeOf(config, MESSAGE_WINDOW_KEY, MESSAGE_WINDOW_PRESET), graphPage: pageSizeOf(config, GRAPH_PAGE_KEY, GRAPH_PAGE_PRESET) };
  } catch (cause) {
    console.warn("page sizes: the settings could not be read, using the presets", cause);
    return { messageWindow: MESSAGE_WINDOW_PRESET, graphPage: GRAPH_PAGE_PRESET };
  }
}
