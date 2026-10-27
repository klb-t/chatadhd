import type { ActionsApi, ClientDeclaration } from "../api/loom-api";
import { declaration } from "./client-device";
import type { DeviceHost, PendingText } from "./device-host";

/** Where the id of this client is kept so a reload of the tab is the same client for the kernel. */
const ID_KEY = "loom.client_id";
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

/** Where the id of this browser installation is kept (every tab of it, across restarts): the installation level of OKNA D19. */
const INSTALLATION_KEY = "loom.installation_id";
/** The id of this browser installation (local storage, shared by its tabs); without storage it lasts as long as the page. */
export function installationIdentity(store?: KeyValueStore): string {
  try {
    const kept = store ? store.getItem(INSTALLATION_KEY) : null;
    if (kept) return kept;
  } catch { /* storage can be blocked */ }
  const made = "inst-" + randomId();
  try { if (store) store.setItem(INSTALLATION_KEY, made); } catch { /* as above */ }
  return made;
}

/** How long a send waits for the declaration of the devices before it goes without one (ms). A preset: the constructor takes another. */
export const DECLARE_TIMEOUT_MS = 4000;

/** The declaration of this client to the kernel, made lazily: only when something the person started may need the devices (the first
 *  chat send of the page), and again only when what the browser can do has changed since (the payload differs - a permission was
 *  given or taken away) or the kernel's validity of the last one has run out. Never in the background: opening the page, a profile,
 *  the graph or the settings sends nothing. A transport without client support (an older host) leaves the id empty. */
export class ClientSession {
  private readonly actions: ActionsApi | undefined;
  private readonly host: DeviceHost;
  private readonly kind: string;
  private readonly label: string;
  private readonly now: () => number;
  /** How long a send waits for the declaration (ms); after that it goes without a client id. 0 waits without a bound. */
  private readonly declareTimeoutMs: number;
  private chain: PendingText | null = null;
  /** The payload of the last declaration the kernel answered (accepted or refused); an unanswered one (offline) is tried again. */
  private sent = "";
  readonly id: string;
  last: ClientDeclaration | null = null;

  constructor(options: { actions: ActionsApi | undefined; host: DeviceHost; id: string; label?: string; now?: () => number; declareTimeoutMs?: number }) {
    this.declareTimeoutMs = options.declareTimeoutMs ?? DECLARE_TIMEOUT_MS;
    this.actions = options.actions;
    this.host = options.host;
    this.id = options.id;
    this.kind = options.actions && options.actions.clientKind ? options.actions.clientKind : "web";
    this.label = options.label ?? "";
    this.now = options.now ?? (() => Date.now());
  }

  /** Declares when the payload changed (or `force`); returns the id to send with requests, "" without support or when the kernel
   *  does not take the declaration. Calls queue, so two sends at once make one declaration. */
  ensure(force = false): PendingText {
    const previous = this.chain;
    const run = (async () => {
      if (previous) await this.bounded(previous);
      return this.bounded(this.declareIfChanged(force));
    })();
    this.chain = run;
    return run;
  }

  /** The declaration never holds a send back: when it does not answer in time (or fails) the answer is "" - no client id - and the
   *  send goes on; a late answer still settles the session for the next send. */
  private bounded(work: PendingText): PendingText {
    if (!(this.declareTimeoutMs > 0)) return work.catch(() => "");
    return new Promise<string>((resolve) => {
      const timer = setTimeout(() => resolve(""), this.declareTimeoutMs);
      work.then((value) => { clearTimeout(timer); resolve(value); }, () => { clearTimeout(timer); resolve(""); });
    });
  }

  private expired(): boolean {
    const until = this.last ? this.last.expires : undefined;
    return typeof until === "string" && this.now() >= Date.parse(until);
  }

  private async declareIfChanged(force: boolean): PendingText {
    const actions = this.actions;
    if (!actions || !actions.clientDeclare) return "";
    try {
      const request = await declaration(this.host, this.id, this.kind, this.label);
      const payload = JSON.stringify(request);
      if (!force && payload === this.sent && !this.expired()) return this.last ? this.id : "";
      try {
        this.last = await actions.clientDeclare(request);
        this.sent = payload;
      } catch (error) {
        this.last = null;
        // The kernel answered with a refusal: that payload is not sent again. No answer at all (offline): the next send tries again.
        this.sent = error instanceof TypeError ? "" : payload;
      }
    } catch {
      return "";  // the browser could not even be asked: nothing is claimed
    }
    return this.last ? this.id : "";
  }
}

/** The one session of this page: every chat view of it is the same client for the kernel, and declares once. */
let shared: ClientSession | null = null;
let sharedFor: ActionsApi | undefined;
export function pageClientSession(options: { actions: ActionsApi | undefined; host: DeviceHost; id: string; label?: string }): ClientSession {
  if (!shared || sharedFor !== options.actions) {
    shared = new ClientSession(options);
    sharedFor = options.actions;
  }
  return shared;
}
