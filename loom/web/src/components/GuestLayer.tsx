// R103 B1 / R105: what a person without an account sees on top of the FULL interface (no gate): a line saying which
// free model answers and what its provider does with the text (before the first send), the browser store of the
// history (saved after each answer, restored into a new session) with "Download archive" / "Load archive", and one
// non-blocking notice after the first answer with the period computed from the store the browser actually gave.
import { useCallback, useEffect, useRef, useState, type ReactNode } from "react";
import type { AccountsTextId, GuestInfo, GuestsOffer } from "../context/auth-state";
import {
  ANSWER_DONE_EVENT, WORKING_COPY_CHANGED_EVENT, browserStore, bundleFromText, bundleSize, bundleText, collectBundle,
  humanBytes, humanSeconds, noticePeriod, requestPersistence, restoreBundle, wipeEverything, type GuestBundle,
} from "../context/guest-store";
import { GUEST_LIMIT_EVENT, type GuestLimitDetail } from "../api/auth";
import { SETTINGS_PANEL, requestOpenPanel } from "../context/open-settings";
import { setSendGate, termsNeedConsent } from "../api/send-gate";
import { LAYER_STATE_KEY, modelLineId } from "../context/guest-line";
import { selectStore, writePolicy, type StoreSelection } from "../context/store-registry";
import type { StorePlugin, Traits } from "../context/store-plugin";

const NEVER_KEY = "loom.guest.notice.never";
const RESTORED_KEY = "loom.guest.restored";
const TERMS_KEY = "loom.guest.terms";  // + provider + observed_at: a new observation of the terms asks again
// R103 B1b: a change of the working copy (layout, pins, trash, memory, settings) is saved this long after the last one
// (preset; an answer saves at once).
export const SAVE_DEBOUNCE_MS = 1200;

function readFlag(storage: () => Storage, key: string): boolean {
  try { return storage().getItem(key) === "1"; } catch { return false; }
}
function writeFlag(storage: () => Storage, key: string): void {
  try { storage().setItem(key, "1"); } catch { /* storage refused: the notice may come back, nothing breaks */ }
}

function readLayer(): string | null { try { return localStorage.getItem(LAYER_STATE_KEY); } catch { return null; } }
function writeLayer(v: string): void { try { localStorage.setItem(LAYER_STATE_KEY, v); } catch { /* the toggle still works for this visit */ } }

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
  const [wipeAsk, setWipeAsk] = useState(false);
  const answered = useRef(false);
  // PH29 P1: the store chosen from the registry by its traits (OPFS + SQLite first, the IndexedDB bundle as fallback).
  const storeRef = useRef<StorePlugin | null>(null);
  const [sel, setSel] = useState<(Omit<StoreSelection, "plugins"> & { traits: Traits }) | null>(null);
  const showTraits = useCallback(async (s: StoreSelection) => {
    const traits = s.plugin ? await s.plugin.traits() : {};
    setSel({ plugin: s.plugin, candidates: s.candidates, advisories: s.advisories, policy: s.policy, traits });
  }, []);
  const selection = useRef<StoreSelection | null>(null);
  // R103 B1b: nothing is saved before the restore decision: a save of the still-empty new working copy (the desktop
  // writes its first pins at once) must never overwrite the bundle that is about to come back.
  const ready = useRef(false);
  // R103 B3: the source can change while the guest works (§4.4 switch); /api/auth/me is read again after each answer.
  const [src, setSrc] = useState(guest.modelSource);
  // R103 B3 (§4.3): the tap before the first message to a source whose data terms need it.
  const [consent, setConsent] = useState<null | ((ok: boolean) => void)>(null);
  const [declined, setDeclined] = useState(false);
  // R103 B2: a refusal of the guest protection - its words, the wait counted down in words, and the alternatives.
  const [limit, setLimit] = useState<(GuestLimitDetail & { until: number | null }) | null>(null);
  const [now, setNow] = useState(() => Date.now());
  useEffect(() => {
    const onLimit = (e: Event) => {
      const d = (e as CustomEvent<GuestLimitDetail>).detail;
      setLimit({ ...d, until: d.retryAfter ? Date.now() + d.retryAfter * 1000 : null });
      setNow(Date.now());
    };
    window.addEventListener(GUEST_LIMIT_EVENT, onLimit);
    return () => window.removeEventListener(GUEST_LIMIT_EVENT, onLimit);
  }, []);
  useEffect(() => {
    if (!limit?.until) return;
    const t = window.setInterval(() => setNow(Date.now()), 1000);
    return () => window.clearInterval(t);
  }, [limit]);
  const waitLeft = limit?.until ? Math.max(0, Math.ceil((limit.until - now) / 1000)) : null;
  // The layer has its own place in the layout (below the app, never over it). Open on the very first look, then folded
  // for the next visits; the person's toggle is remembered either way (localStorage; without it: open).
  const [open, setOpen] = useState<boolean>(() => readLayer() !== "folded");
  useEffect(() => { if (readLayer() === null) writeLayer("folded"); }, []);
  const toggle = () => { const next = !open; setOpen(next); writeLayer(next ? "open" : "folded"); };
  const fileInput = useRef<HTMLInputElement>(null);
  const askAgain = guests.notice.askAgain;
  const noticeKey = askAgain === "per_session" ? `${NEVER_KEY}.${guest.id}` : NEVER_KEY;
  const storage = () => (askAgain === "per_session" ? sessionStorage : localStorage);

  // The store: ask for persistence, then bring back what this browser kept when the working copy is empty.
  useEffect(() => {
    let live = true;
    let justRestored = false;
    try {
      const n = sessionStorage.getItem(RESTORED_KEY);
      if (n !== null) { justRestored = true; sessionStorage.removeItem(RESTORED_KEY); if (Number(n) > 0) setRestored(Number(n)); }
    } catch { /* no session storage: no message */ }
    void (async () => {
      const p = await requestPersistence();
      let bundle: GuestBundle | null = null;
      const s = await selectStore();
      selection.current = s;
      storeRef.current = s.plugin;
      let available = s.plugin !== null;
      try { bundle = s.plugin ? await s.plugin.load() : null; } catch { available = false; }
      // A store chosen for the first time (e.g. OPFS after the IndexedDB bundle of B1b) is empty: what another usable
      // store of this browser holds is brought back from there; the next save writes it into the chosen store.
      if (!bundle || bundleSize(bundle) === 0)
        for (const p of s.plugins) {
          if (p === s.plugin || !s.candidates.find((c) => c.id === p.id)?.available) continue;
          try { const other = await p.load(); if (other && bundleSize(other) > 0) { bundle = other; break; } } catch { /* nothing there */ }
        }
      if (live) void showTraits(s);
      if (!live) return;
      setStore({ ...p, available });
      // A bundle may hold no conversation at all (only e.g. desktop pins): the list below stays empty after the restore,
      // so the page that follows a restore never restores again, or it would reload for ever.
      if (!bundle || bundleSize(bundle) === 0 || justRestored) { ready.current = true; return; }
      try {
        const have = await (await fetch("/api/conversations?limit=1", { credentials: "same-origin" })).json();
        if (Array.isArray(have) && have.length > 0) { ready.current = true; return; }
        const n = await restoreBundle(bundle, fetch, "replace");
        // The panels already read the empty working copy; a reload shows the restored one everywhere at once.
        try { sessionStorage.setItem(RESTORED_KEY, String(n)); } catch { ready.current = true; return; }  // no mark, no reload
        if (live) window.location.reload();
      } catch { /* a failed restore leaves the bundle in place for the next try (and nothing overwrites it now) */ }
    })();
    return () => { live = false; };
  }, []);

  const [saving, setSaving] = useState(0);
  const save = useCallback(async () => {
    if (!ready.current) return;
    setSaving((n) => n + 1);
    try {
      const bundle = await collectBundle();
      if (!storeRef.current) throw new Error("no store");
      await storeRef.current.save(bundle);
      setSaved(bundle.saved_at);
      if (selection.current) void showTraits(selection.current);
    } catch (e) { console.warn("[store] save failed", e); setStore((s) => ({ ...s, available: false })); }
    finally { setSaving((n) => n - 1); }
  }, []);

  // Changes other than answers (layout, pins, trash, memory, settings): one save after they settle.
  useEffect(() => {
    let t: number | undefined;
    const onChange = () => { window.clearTimeout(t); t = window.setTimeout(() => void save(), SAVE_DEBOUNCE_MS); };
    window.addEventListener(WORKING_COPY_CHANGED_EVENT, onChange);
    window.addEventListener("loom:trash-changed", onChange);
    return () => { window.clearTimeout(t); window.removeEventListener(WORKING_COPY_CHANGED_EVENT, onChange); window.removeEventListener("loom:trash-changed", onChange); };
  }, [save]);

  useEffect(() => {
    const onAnswer = () => {
      void save();
      void fetch("/api/auth/me", { credentials: "same-origin" }).then((r) => r.json())
        .then((me: { guest?: { model_source?: typeof src } }) => { if (me?.guest?.model_source) setSrc(me.guest.model_source); })
        .catch(() => { /* the line keeps the last known source */ });
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
    const url = URL.createObjectURL(new Blob([bundleText(bundle)], { type: "application/json" }));
    const a = document.createElement("a");
    a.href = url; a.download = `chatadhd-archive-${bundle.saved_at.slice(0, 10)}.json`;
    document.body.appendChild(a); a.click(); a.remove();
    setTimeout(() => URL.revokeObjectURL(url), 1000);
  };
  const load = async (file: File) => {
    try {
      const bundle = bundleFromText(await file.text());
      if (!bundle) return;
      const n = await restoreBundle(bundle);
      try { sessionStorage.setItem(RESTORED_KEY, String(n)); } catch { /* the count is only a message */ }
      await save();
      window.location.reload();
    } catch { /* not an archive of ours */ }
  };

  const wipe = async () => {
    setWipeAsk(false);
    // Every store of the registry is cleared (closing its database first), then the rest of the site's data.
    for (const p of selection.current?.plugins ?? []) { try { await p.wipe(); } catch { /* nothing stored there */ } }
    await wipeEverything(storeRef.current ?? browserStore);
    window.location.reload();
  };

  const ttl = humanSeconds(guest.idleTtlSeconds);
  const which = noticePeriod(store);
  const period = which.period ? text(which.period, { free: humanBytes(store.freeBytes), ttl }) : text("guest.period_unknown", { ttl });
  const dataTerms = src && typeof src.data_terms === "object" && src.data_terms !== null
    ? (src.data_terms as { summary?: Record<string, string>; source_url?: string; observed_at?: string }) : undefined;
  const terms = dataTerms?.summary;
  const lang = typeof navigator !== "undefined" ? (navigator.language ?? "").split("-")[0] : "";
  const termsText = terms ? (terms[lang] ?? Object.values(terms)[0] ?? "") : "";
  const providerName = String(src?.display_name ?? src?.provider ?? "");
  const termsKey = `${TERMS_KEY}.${String(src?.provider ?? "")}.${String(dataTerms?.observed_at ?? "")}`;
  const switched = src && typeof src.switched === "object" && src.switched !== null ? (src.switched as { from?: string; why?: string }) : null;
  const needsTap = Boolean(src && typeof src.model === "string" && src.model && termsNeedConsent(dataTerms));

  // The gate is installed only while the current source needs a tap that this device has not given yet.
  useEffect(() => {
    if (!needsTap || readFlag(() => localStorage, termsKey)) { setSendGate(null); return; }
    setSendGate(() => new Promise<boolean>((resolve) => {
      setDeclined(false);
      setConsent(() => (ok: boolean) => {
        setConsent(null);
        if (ok) { writeFlag(() => localStorage, termsKey); setSendGate(null); }
        else { setDeclined(true); requestOpenPanel(SETTINGS_PANEL); }
        resolve(ok);
      });
    }));
    return () => setSendGate(null);
  }, [needsTap, termsKey]);

  return (
    <div className="guest-frame" data-testid="guest-frame">
      <div className="guest-body" data-testid="guest-body" style={{ flex: "1 1 auto", minHeight: 0, position: "relative", overflow: "hidden" }}>{children}</div>
      <div className="guest-layer" data-testid="guest-layer" data-open={open ? "true" : "false"} data-guest-id={guest.id} data-saved-at={saved ?? ""} data-saving={saving}
        style={{ flex: "0 0 auto", zIndex: 50, display: "flex", flexDirection: "column", gap: 6, alignItems: "stretch", boxSizing: "border-box", width: "100%", maxHeight: "45dvh", overflowY: "auto", padding: "4px 8px", borderTop: "1px solid var(--border, #555)", background: "var(--surface, #222)" }}>
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
        {limit && (
          <div role="alert" data-testid="guest-limit" data-mechanism={limit.mechanism} data-wait={waitLeft ?? ""}
            style={{ pointerEvents: "auto", background: "var(--surface, #222)", color: "inherit", border: "1px solid var(--border, #555)", borderRadius: 10, padding: "10px 12px" }}>
            {/* The server's sentence, its wait counted down in place ({{wait}} of the template); at zero: ready. */}
            <p data-testid="guest-limit-text" style={{ margin: "0 0 6px" }}>
              {waitLeft !== null && limit.template.includes("{{wait}}")
                ? (waitLeft > 0 ? limit.template.split("{{wait}}").join(humanSeconds(waitLeft)).split("{{limit}}").join(limit.limit) : text("guest.limit_ready"))
                : limit.text}
            </p>
            <p data-testid="guest-limit-alternatives" style={{ margin: "0 0 6px", fontSize: 13 }}>{limit.alternatives || text("guest.limit_alternatives")}</p>
            <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
              <button type="button" data-testid="guest-limit-own-key" onClick={() => requestOpenPanel(SETTINGS_PANEL)}>{text("guest.limit_own_key")}</button>
              <button type="button" data-testid="guest-limit-sign-in" onClick={onSignIn}>{text("guest.notice_sign_in")}</button>
              <button type="button" data-testid="guest-limit-close" onClick={() => setLimit(null)}>{text("guest.notice_close")}</button>
            </div>
          </div>
        )}
        {consent && (
          <div role="dialog" aria-modal="false" data-testid="guest-terms-consent" data-provider={String(src?.provider ?? "")}
            style={{ pointerEvents: "auto", background: "var(--surface, #222)", color: "inherit", border: "1px solid var(--border, #555)", borderRadius: 10, padding: "10px 12px" }}>
            <strong>{text("guest.terms_consent_title", { provider: providerName })}</strong>
            <p data-testid="guest-terms-consent-body" style={{ margin: "6px 0" }}>
              {text("guest.terms_consent_body", { terms: termsText, source: String(dataTerms?.source_url ?? ""), date: String(dataTerms?.observed_at ?? "") })}
            </p>
            <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
              <button type="button" data-testid="guest-terms-send" onClick={() => consent(true)}>{text("guest.terms_send")}</button>
              <button type="button" data-testid="guest-terms-other" onClick={() => consent(false)}>{text("guest.terms_other")}</button>
            </div>
          </div>
        )}
        {declined && !consent && <p role="status" data-testid="guest-terms-declined" style={{ pointerEvents: "auto", margin: 0, fontSize: 12 }}>{text("guest.terms_declined")}</p>}
        {switched && switched.from && (
          <p role="status" data-testid="guest-switched" style={{ pointerEvents: "auto", margin: 0, fontSize: 12 }}>
            {text("guest.switched", { from: switched.from, why: String(switched.why ?? ""), provider: providerName })}
          </p>
        )}
        {wipeAsk && (
          <div role="alertdialog" data-testid="guest-wipe-confirm"
            style={{ pointerEvents: "auto", background: "var(--surface, #222)", color: "inherit", border: "1px solid var(--border, #555)", borderRadius: 10, padding: "10px 12px" }}>
            <p style={{ margin: "0 0 6px" }}>{text("guest.wipe_confirm")}</p>
            <div style={{ display: "flex", flexWrap: "wrap", gap: 6 }}>
              <button type="button" data-testid="guest-wipe-yes" onClick={() => void wipe()}>{text("guest.wipe_yes")}</button>
              <button type="button" data-testid="guest-wipe-no" onClick={() => setWipeAsk(false)}>{text("guest.wipe_no")}</button>
            </div>
          </div>
        )}
        {restored !== null && <p role="status" data-testid="guest-restored" style={{ pointerEvents: "auto", margin: 0, fontSize: 12, opacity: 0.85 }}>{text("guest.restored", { count: String(restored) })}</p>}
        <div style={{ display: "flex", alignItems: "center", gap: 6, minWidth: 0 }}>
          <p data-testid="guest-model-line" style={{ margin: 0, fontSize: 12, opacity: 0.8, flex: "1 1 auto", minWidth: 0, ...(open ? { overflowWrap: "anywhere" } : { overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }) }}
            title={termsText ? `${text("guest.terms_title")}: ${termsText}` : undefined}>
            {src && typeof src.model === "string" && src.model
              ? text(modelLineId(src.tier), { model: src.model, provider: providerName, terms: termsText })
              : text("guest.no_model", { reason: String(src?.why ?? "unknown") })}
          </p>
          <button type="button" data-testid="guest-layer-toggle" aria-expanded={open} onClick={toggle}
            style={{ flex: "0 0 auto", fontSize: 11, padding: "2px 8px" }}>{text(open ? "guest.layer_hide" : "guest.layer_show")}</button>
        </div>
        {open && <div><button type="button" data-testid="guest-wipe" onClick={() => setWipeAsk(true)} style={{ fontSize: 11, padding: "0 6px" }}>{text("guest.wipe")}</button></div>}
        {open && sel && (
          <details data-testid="guest-store" data-store={sel.plugin?.id ?? ""} data-integrity={String(sel.traits.integrity?.value ?? "")}
            data-fallback={String(sel.traits.bundle_fallback?.value ?? "")} data-leader={String(sel.traits.leader_tab?.value ?? "")}
            style={{ pointerEvents: "auto", margin: 0, fontSize: 12, opacity: 0.85 }}>
            <summary>{text("guest.store", { label: sel.plugin?.label ?? "-" })}</summary>
            {sel.advisories.filter((a) => a.kind === "store_unavailable" || a.kind === "store_fallback" || a.kind === "store_requirements_unmet").map((a, i) => (
              <p key={i} role="status" data-testid="guest-store-advisory" data-kind={a.kind} data-plugin={a.plugin ?? ""} style={{ margin: "2px 0" }}>
                {a.plugin === sel.plugin?.id ? text("guest.store_fallback", { label: sel.plugin?.label ?? "", reason: a.reason ?? (a.missing ?? []).join(", ") })
                  : `${a.plugin ?? ""}: ${text("guest.store_unavailable", { reason: a.reason ?? (a.missing ?? []).join(", ") })}`}
              </p>
            ))}
            <label style={{ display: "block", margin: "4px 0" }}>{text("guest.store_choice")}{" "}
              <select data-testid="guest-store-choice" value={Array.isArray(sel.policy.targets) ? sel.policy.targets[0] : "auto"}
                onChange={(e) => { writePolicy(e.target.value === "auto" ? null : { targets: [e.target.value] }); window.location.reload(); }}>
                <option value="auto">{text("guest.store_auto")}</option>
                {sel.candidates.map((c) => <option key={c.id} value={c.id}>{c.id}{c.available ? "" : ` (${text("guest.store_unavailable", { reason: c.reason ?? "" })})`}</option>)}
              </select>
            </label>
            <p style={{ margin: "2px 0" }}>{text("guest.store_traits")}</p>
            <ul data-testid="guest-store-traits" style={{ margin: 0, paddingLeft: 16 }}>
              {Object.entries(sel.traits).map(([k, t]) => (
                <li key={k} data-trait={k} title={`${t.source ?? ""} ${t.observed_at ?? ""}`}>{k}: {Array.isArray(t.value) ? t.value.join(", ") : String(t.value)}{t.unit ? ` ${t.unit}` : ""}</li>
              ))}
            </ul>
          </details>
        )}
        <input ref={fileInput} type="file" accept="application/json,.json" hidden data-testid="guest-load-input"
          onChange={(e) => { const f = e.target.files?.[0]; if (f) void load(f); e.target.value = ""; }} />
      </div>
    </div>
  );
}
