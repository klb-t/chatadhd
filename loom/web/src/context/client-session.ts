import type { ActionsApi, ClientDeclaration } from "../api/loom-api";
import { declaration, type DeviceHost } from "./client-device";

/** Where the id of this client is kept so a reload of the tab is the same client for the kernel. */
const ID_KEY = "loom.client_id";
/** How long a declaration is trusted before it is made again (the permission state of a device can change under the page). */
const FRESH_MS = 30000;

interface KeyValueStore { getItem(key: string): string | null; setItem(key: string, value: string): void }

function randomId(): string {
  const bytes = new Uint8Array(6);
  crypto.getRandomValues(bytes);
  let id = "";
  for (const byte of bytes) id += byte.toString(16).padStart(2, "0");
  return id;
}

/** The id this tab uses towards the kernel: chosen once, kept in the session storage when there is one. */
export function clientIdentity(store?: KeyValueStore): string {
  try {
    const kept = store ? store.getItem(ID_KEY) : null;
    if (kept) return kept;
  } catch { /* storage can be blocked: the id then lasts as long as the page */ }
  const made = "web-" + randomId();
  try { if (store) store.setItem(ID_KEY, made); } catch { /* as above */ }
  return made;
}

/** The declaration of this client to the kernel: what the browser can do now, repeated when it may have changed. A transport without
 *  client support (an older host) leaves the id empty and nothing is declared. */
export class ClientSession {
  private readonly actions: ActionsApi | undefined;
  private readonly host: DeviceHost;
  private readonly kind: string;
  private readonly label: string;
  private readonly now: () => number;
  private readonly freshMs: number;
  private declaredAt = 0;
  private inFlight: Promise<ClientDeclaration | null> | null = null;
  readonly id: string;
  last: ClientDeclaration | null = null;

  constructor(options: { actions: ActionsApi | undefined; host: DeviceHost; id: string; label?: string; now?: () => number; freshMs?: number }) {
    this.actions = options.actions;
    this.host = options.host;
    this.id = options.id;
    this.kind = options.actions && options.actions.clientKind ? options.actions.clientKind : "web";
    this.label = options.label ?? "";
    this.now = options.now ?? (() => Date.now());
    this.freshMs = options.freshMs ?? FRESH_MS;
  }

  /** Declares (again) when the declaration is older than the freshness; returns the id to send with requests, "" without support. */
  async ensure(force = false): Promise<string> {
    const actions = this.actions;
    if (!actions || !actions.clientDeclare) return "";
    if (!force && this.last && this.now() - this.declaredAt < this.freshMs) return this.id;
    if (!this.inFlight) {
      this.inFlight = (async () => {
        try {
          const made = await actions.clientDeclare!(await declaration(this.host, this.id, this.kind, this.label));
          this.last = made;
          this.declaredAt = this.now();
          return made;
        } catch {
          return null;  // the kernel is not reachable now: the next request tries again; nothing is claimed in the meantime
        } finally {
          this.inFlight = null;
        }
      })();
    }
    await this.inFlight;
    return this.last ? this.id : "";
  }
}
