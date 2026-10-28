// R101 P0: the desktop layer under the windows. Always mounted (a draft of the sentence survives a window covering it); drawn where no
// window covers it (`cover`), hidden and inert under a full cover. Content from data (shell/data/desktop.json + settings): a sentence
// field routed to the chat composer, the minimized windows, every kind of window (planned/unopenable greyed with the reason, never
// hidden), the layouts. Keyboard: Tab walks the entries, the arrows move between them, focus comes here when the desktop is uncovered.
import { useEffect, useRef, useState, type KeyboardEvent } from "react";
import type { WLayout } from "./model";
import { DESKTOP, minimizedWindows, say, type DesktopPath, type DesktopSettings, type KindEntry, type KindTarget } from "./desktop";

export interface DesktopProps {
  layout: WLayout;
  cover: "full" | "partial" | "none";
  lang: string;
  settings: DesktopSettings;
  entries: KindEntry[];
  paths: DesktopPath[];
  layouts: { shipped: { id: string; label: string }[]; own: { id: string; label: string }[] };
  title: (id: string) => string;
  openKind(target: KindTarget): void;
  restore(id: string): void;
  loadLayout(id: string, own: boolean): void;
  sentence(text: string): void;
  enableEmergency(): void;
}

export default function Desktop({ layout, cover, lang, settings, entries, paths, layouts, title, openKind, restore, loadLayout, sentence, enableEmergency }: DesktopProps) {
  const w = DESKTOP.words;
  const [text, setText] = useState("");
  const [kept, setKept] = useState(false);
  const root = useRef<HTMLDivElement>(null);
  const covered = cover === "full";
  // focus comes to the desktop when it becomes the only thing on the screen (a keyboard user is never left on a hidden control)
  const prev = useRef(cover);
  useEffect(() => {
    if (prev.current !== "none" && cover === "none") {
      const a = document.activeElement as HTMLElement | null;
      if (!a || a === document.body || !a.offsetParent) root.current?.focus();
    }
    prev.current = cover;
  }, [cover]);
  const onKey = (e: KeyboardEvent<HTMLDivElement>) => {
    if (e.key !== "ArrowDown" && e.key !== "ArrowUp") return;
    const t = e.target as HTMLElement;
    if (t.tagName === "INPUT") return;
    const all = [...(root.current?.querySelectorAll<HTMLElement>("button:not([disabled]), input") ?? [])];
    const i = all.indexOf(t);
    const next = all[i + (e.key === "ArrowDown" ? 1 : -1)];
    if (next) { e.preventDefault(); next.focus(); }
  };
  const minimized = minimizedWindows(layout);
  const section = (id: string) => {
    if (id === "sentence") return (
      <form key={id} className="desktop-section desktop-sentence" data-testid="desktop-sentence" onSubmit={(e) => { e.preventDefault(); const v = text.trim(); if (v) { setText(""); sentence(v); } }}>
        <input aria-label={say(w, "sentence_label", lang)} placeholder={say(w, "sentence_label", lang)} value={text} onChange={(e) => setText(e.target.value)} data-testid="desktop-sentence-input" />
        <button type="submit" data-testid="desktop-sentence-send">{say(w, "sentence_send", lang)}</button>
      </form>);
    if (id === "restore") return minimized.length ? (
      <section key={id} className="desktop-section" aria-label={say(w, "restore", lang)} data-testid="desktop-restore">
        <h3>{say(w, "restore", lang)}</h3>
        <div className="desktop-items">{minimized.map((m) => <button key={m.id} type="button" data-testid="desktop-restore-item" data-window={m.id} onClick={() => restore(m.id)}>{title(m.id)}</button>)}</div>
      </section>) : null;
    if (id === "open") return (
      <section key={id} className="desktop-section" aria-label={say(w, "open", lang)} data-testid="desktop-open">
        <h3>{say(w, "open", lang)}</h3>
        <div className="desktop-items">{entries.map((e) => (
          <button key={e.kind} type="button" data-testid="desktop-open-kind" data-kind={e.kind} data-status={e.status} disabled={e.status !== "available"}
            title={e.reason} aria-describedby={e.reason ? `desktop-why-${e.kind}` : undefined} onClick={() => e.target && openKind(e.target)}>
            {e.label}{e.reason && <span className="sr-only" id={`desktop-why-${e.kind}`}>{e.reason}</span>}
          </button>))}</div>
      </section>);
    if (id === "layouts") return (
      <section key={id} className="desktop-section" aria-label={say(w, "layouts", lang)} data-testid="desktop-layouts">
        <h3>{say(w, "layouts", lang)}</h3>
        <div className="desktop-items">
          {layouts.shipped.map((l) => <button key={"s:" + l.id} type="button" data-testid="desktop-layout" data-layout={l.id} onClick={() => loadLayout(l.id, false)}>{l.label}</button>)}
          {layouts.own.map((l) => <button key={"o:" + l.id} type="button" data-testid="desktop-layout" data-layout={l.id} data-own="true" onClick={() => loadLayout(l.id, true)}>{say(w, "layouts_own", lang)}: {l.label}</button>)}
        </div>
      </section>);
    return null;
  };
  const emergency = paths.find((p) => p.emergency);
  const noPath = !paths.length;
  return (
    <div ref={root} className="desktop-layer" data-testid="desktop" data-cover={cover} role="region" aria-label={say(w, "title", lang)} tabIndex={-1}
      hidden={covered} aria-hidden={covered || undefined} {...(covered ? { inert: "" } : {})} onKeyDown={onKey}>
      {/* under a full cover only the layer stays mounted (its state - the draft of the sentence - stays); the entries are drawn when seen */}
      {!covered && <div className="desktop-content">
        <h2 className="desktop-title">{say(w, "title", lang)}</h2>
        <p className="desktop-hint">{say(w, "hint", lang)}</p>
        {settings.sections.map(section)}
        {emergency && <button type="button" className="desktop-emergency" data-testid="desktop-emergency" onClick={() => openKind({ chat: true })}>{say(w, "emergency", lang)}</button>}
        {noPath && !kept && <div className="win-chip" role="status" data-testid="desktop-advisory" data-code="desktop_no_path">
          <span>{say(w, "no_path", lang)}</span>
          <span className="win-chip-buttons">
            <button type="button" data-testid="desktop-advisory-fix" onClick={enableEmergency}>{say(w, "no_path_fix", lang)}</button>
            <button type="button" data-testid="desktop-advisory-keep" onClick={() => setKept(true)}>{say(w, "keep", lang)}</button>
          </span>
        </div>}
        {settings.unknownSections.map((id) => <div key={id} className="win-chip" role="status" data-testid="desktop-advisory" data-code="desktop_section_unknown"><span>{say(w, "section_unknown", lang, { id })}</span></div>)}
      </div>}
    </div>
  );
}

/** The end screen of last_window_close=close_app on a host that cannot (or may refuse to) close itself. */
export function DesktopEnded({ lang, back }: { lang: string; back(): void }) {
  const w = DESKTOP.words;
  return (
    <div className="desktop-layer desktop-ended" data-testid="app-ended" role="alertdialog" aria-label={say(w, "ended_title", lang)}>
      <div className="desktop-content">
        <h2 className="desktop-title">{say(w, "ended_title", lang)}</h2>
        <p data-testid="app-ended-advisory" data-code="exit_unavailable">{say(w, "ended_text", lang)}</p>
        <button type="button" data-testid="app-ended-back" onClick={back} autoFocus>{say(w, "ended_back", lang)}</button>
      </div>
    </div>
  );
}
