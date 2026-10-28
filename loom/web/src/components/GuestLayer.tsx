// R103 B1 / R105: what a person without an account sees on top of the FULL interface (no gate): a line saying which
// free model answers and what its provider does with the text (before the first send), the browser store of the
// history (saved after each answer, restored into a new session) with "Download archive" / "Load archive", and one
// non-blocking notice after the first answer with the period computed from the store the browser actually gave.
import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import type { AccountsTextId, GuestInfo, GuestsOffer } from "../context/auth-state";
import {
  ANSWER_DONE_EVENT, browserStore, collectBundle, humanBytes, humanSeconds, isBundle, noticePeriod, requestPersistence,
  restoreBundle, type GuestBundle,
} from "../context/guest-store";

const NEVER_KEY = "loom.guest.notice.never";
const RESTORED_KEY = "loom.guest.restored";

function readFlag(storage: () => Storage, key: string): boolean {
  try { return storage().getItem(key) === "1"; } catch { return false; }
}
function writeFlag(storage: () => Storage, key: string): void {
  try { storage().setItem(key, "1"); } catch { /* storage refused: the notice may come back, nothing breaks */ }
}

export default function GuestLayer({ guest, guests, text, onSignIn, children }: {
  guest: GuestInfo; guests: GuestsOffer;
  text: (id: AccountsTextId, parameters?: Record<string, string>) => string;
  onSignIn: () => void;
  children: ReactNode;
}) {
  const [store, setStore] = useState<{ persisted: boolean | null; freeBytes: number | null; available: boolean }>({ persisted: null, freeBytes: null, available: typeof indexedDB !== "undefined" });
  const [notice, setNotice] = useState(false);
  const [restored, setRestored] = useState<number | null>(null);
  const [saved, setSaved] = useState<string | null>(null);
  const answered = useRef(false);
  const fileInput = useRef<HTMLInputElement>(null);
  const askAgain = guests.notice.askAgain;
  const noticeKey = askAgain === "per_session" ? `${NEVER_KEY}.${guest.id}` : NEVER_KEY;
  const storage = () => (askAgain === "per_session" ? sessionStorage : localStorage);

  // The store: ask for persistence, then bring back what this browser kept when the working copy is empty.
  useEffect(() => {
    let live = true;
    try {
      const n = sessionStorage.getItem(RESTORED_KEY);
      if (n !== null) { sessionStorage.removeItem(RESTORED_KEY); setRestored(Number(n)); }
    } catch { /* no session storage: no message */ }
    void (async () => {
      const p = await requestPersistence();
      let available = typeof indexedDB !== "undefined";
      let bundle: GuestBundle | null = null;
      try { bundle = await browserStore.load(); } catch { available = false; }
      if (!live) return;
      setStore({ ...p, available });
      if (!bundle || bundle.conversations.length === 0) return;
      try {
        const have = await (await fetch("/api/conversations?limit=1", { credentials: "same-origin" })).json();
        if (Array.isArray(have) && have.length > 0) return;
        const n = await restoreBundle(bundle);
        // The panels already read the empty working copy; a reload shows the restored one everywhere at once.
        try { sessionStorage.setItem(RESTORED_KEY, String(n)); } catch { /* the count is only a message */ }
        if (live) window.location.reload();
      } catch { /* a failed restore leaves the bundle in place for the next try */ }
    })();
    return () => { live = false; };
  }, []);

  const save = useCallback(async () => {
    try {
      const bundle = await collectBundle();
      await browserStore.save(bundle);
      setSaved(bundle.saved_at);
    } catch { setStore((s) => ({ ...s, available: false })); }
  }, []);

  useEffect(() => {
    const onAnswer = () => {
      void save();
      if (answered.current) return;
      answered.current = true;
      if (guests.notice.on && askAgain !== "never" && !readFlag(storage, noticeKey)) setNotice(true);
    };
    window.addEventListener(ANSWER_DONE_EVENT, onAnswer);
    return () => window.removeEventListener(ANSWER_DONE_EVENT, onAnswer);
  // eslint-disable-next-line react-hooks/exhaustive-deps
  }, [save, guests.notice.on, askAgain, noticeKey]);

  const download = async () => {
    const bundle = await collectBundle();
    const url = URL.createObjectURL(new Blob([JSON.stringify(bundle)], { type: "application/json" }));
    const a = document.createElement("a");
    a.href = url; a.download = `chatadhd-archive-${bundle.saved_at.slice(0, 10)}.json`;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  const load = async (file: File) => {
    try {
      const bundle = JSON.parse(await file.text());
      if (!isBundle(bundle)) return;
      const n = await restoreBundle(bundle);
      try { sessionStorage.setItem(RESTORED_KEY, String(n)); } catch { /* the count is only a message */ }
      await save();
      window.location.reload();
    } catch { /* not an archive of ours */ }
  };

  const ttl = humanSeconds(guest.idleTtlSeconds);
  const which = noticePeriod(store);
  const period = which.period ? text(which.period, { free: humanBytes(store.freeBytes), ttl }) : text("guest.period_unknown", { ttl });
  const src = guest.modelSource;
  const terms = src && typeof src.data_terms === "object" && src.data_terms !== null
    ? (src.data_terms as { summary?: Record<string, string> }).summary : undefined;
  const lang = typeof document !== "undefined" && document.documentElement.lang?.startsWith("en") ? "en" : "pl";
  const termsText = terms ? (terms[lang] ?? terms.pl ?? terms.en ?? "") : "";

  return (
    <>
      {children}
      <div className="guest-layer" data-testid="guest-layer" data-guest-id={guest.id} data-saved-at={saved ?? ""}
        style={{ position: "fixed", left: 8, right: 8, bottom: 72, zIndex: 50, pointerEvents: "none", display: "flex", flexDirection: "column", gap: 6, alignItems: "stretch", maxWidth: 640, margin: "0 auto" }}>
        {notice && (
          <div role="status" data-testid="guest-notice" data-period={which.period ?? "session"}
            style={{ pointerEvents: "auto", background: "var(--surface, #222)", color: "inherit", border: "1px solid var(--border, #555)", borderRadius: 10, padding: "10px 12px" }}>
            <strong>{text("guest.notice_title")}</strong>
            <p data-testid="guest-notice-body" style={{ margin: "6px 0" }}>{text(which.body, { period })}</p>
            <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
              <button type="button" data-testid="guest-download" onClick={() => void download()}>{text("guest.download")}</button>
              <button type="button" data-testid="guest-load" onClick={() => fileInput.current?.click()}>{text("guest.load")}</button>
              <button type="button" data-testid="guest-sign-in" onClick={onSignIn}>{text("guest.notice_sign_in")}</button>
              <button type="button" data-testid="guest-never" onClick={() => { writeFlag(storage, noticeKey); setNotice(false); }}>{text("guest.notice_never")}</button>
              <button type="button" data-testid="guest-notice-close" onClick={() => setNotice(false)}>{text("guest.notice_close")}</button>
            </div>
          </div>
        )}
        {restored !== null && <p role="status" data-testid="guest-restored" style={{ pointerEvents: "auto", margin: 0, fontSize: 12, opacity: 0.85 }}>{text("guest.restored", { count: String(restored) })}</p>}
        <p data-testid="guest-model-line" style={{ pointerEvents: "auto", margin: 0, fontSize: 12, opacity: 0.8 }}
          title={termsText ? `${text("guest.terms_title")}: ${termsText}` : undefined}>
          {src && typeof src.model === "string" && src.model
            ? text("guest.model_line", { model: src.model, provider: String(src.display_name ?? src.provider ?? ""), terms: termsText })
            : text("guest.no_model", { reason: String(src?.why ?? "unknown") })}
        </p>
        <input ref={fileInput} type="file" accept="application/json,.json" hidden data-testid="guest-load-input"
          onChange={(e) => { const f = e.target.files?.[0]; if (f) void load(f); e.target.value = ""; }} />
      </div>
    </>
  );
}
