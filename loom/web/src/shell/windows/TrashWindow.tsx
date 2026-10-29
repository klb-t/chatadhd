// R101 D4 (PULPIT_R101 §5): the window of the kind `trash`. Lists the trash by kind with the date and where from - the kernel's trash.list
// (conversations, memory entries, pins) plus the page's own layouts (trashed_at). „Przywróć” is reversible at once; „Usuń na zawsze” and
// „Opróżnij kosz” are today's irreversible steps and ask first: the kernel's consent (confirm:trash.empty, the reason in its words) for
// what the kernel keeps, the page's own question for a layout. trash_retention_days (preset null = never by itself) only raises an advisory.
import { useCallback, useEffect, useState } from "react";
import type { LoomApi } from "../../api/loom-api";
import type { ActionRun } from "../../api/types";
import DESKTOP_DATA from "../data/desktop.json";
import { say, type Words } from "./desktop";
import { TRASH_CHANGED, byKind, mergeTrash, restoreStepOf, retentionShort, type TrashItem } from "./desktop-trash";

const W = (DESKTOP_DATA as { words: Words }).words;
const SHORT = (DESKTOP_DATA as { trash?: { retention_short_days?: number } }).trash?.retention_short_days ?? 7;

export interface TrashWindowProps {
  api: LoomApi;
  lang: string;
  /** The page's own layouts (all, the trashed ones carry trashed_at). */
  layouts: { id: string; label: string; trashed_at?: string }[];
  /** layout.untrash / layout.delete of an own layout. */
  onLayout: (op: "untrash" | "delete", id: string) => void;
  retentionDays?: unknown;
}

type Ask = { text: string; run?: ActionRun; layouts: string[] };

export function TrashWindow({ api, lang, layouts, onLayout, retentionDays }: TrashWindowProps) {
  const [kernel, setKernel] = useState<{ items: unknown[]; due: number; state: "loading" | "ok" | "error"; error?: string }>({ items: [], due: 0, state: "loading" });
  const [ask, setAsk] = useState<Ask | null>(null);
  const [note, setNote] = useState("");
  const load = useCallback(() => {
    if (!api.actions) { setKernel({ items: [], due: 0, state: "error", error: "actions" }); return; }
    api.actions.run({ action: "trash.list", element: { class: "application" }, args: {} }).then((r) => {
      const d = (r.result?.data ?? {}) as { items?: unknown[]; retention?: { due?: number } };
      setKernel({ items: Array.isArray(d.items) ? d.items : [], due: d.retention?.due ?? 0, state: r.status === "completed" ? "ok" : "error", error: r.error?.message });
    }).catch((e) => setKernel({ items: [], due: 0, state: "error", error: String((e as Error)?.message ?? e) }));
  }, [api]);
  useEffect(() => {
    load();
    const on = () => load();
    window.addEventListener(TRASH_CHANGED, on);
    return () => window.removeEventListener(TRASH_CHANGED, on);
  }, [load]);
  const changed = () => { window.dispatchEvent(new Event(TRASH_CHANGED)); };
  const items = mergeTrash(kernel.items, layouts);
  const fail = (e: unknown) => setNote(say(W, "trash_error", lang, { error: String((e as Error)?.message ?? e) }));
  const restore = async (it: TrashItem) => {
    setNote("");
    if (it.kind === "layout") { onLayout("untrash", it.id); changed(); return; }
    const st = restoreStepOf(it);
    if (!st || !api.actions) return;
    try {
      const r = await api.actions.run({ action: st.action, element: { class: "application" }, args: st.args });
      if (r.status !== "completed") fail(r.error?.message ?? r.status);
      changed();
    } catch (e) { fail(e); }
  };
  // the kernel asks first (status proposed + pending receipt): the question is shown, a yes answers it with the receipt
  const proposeEmpty = async (args: Record<string, unknown>, text: string, layoutIds: string[]) => {
    setNote("");
    if (!api.actions) return;
    const kernelOnes = items.filter((i) => i.kind !== "layout" && (!args.id || i.id === args.id) && (args.older_than_days === undefined || i.due));
    if (!kernelOnes.length) { setAsk({ text, layouts: layoutIds }); return; }
    try {
      const r = await api.actions.run({ action: "trash.empty", element: { class: "application" }, args });
      if (r.status === "completed") { changed(); return; }
      if (!r.pending?.receipt_id) { fail(r.error?.message ?? r.status); return; }
      setAsk({ text, run: r, layouts: layoutIds });
    } catch (e) { fail(e); }
  };
  const answer = async (yes: boolean) => {
    const a = ask; setAsk(null);
    if (!a) return;
    try {
      if (a.run && api.actions) {
        const r = await api.actions.confirm({ run_id: a.run.id, approved: yes, receipt_id: String(a.run.pending?.receipt_id ?? "") });
        if (yes && r.status !== "completed") { fail(r.error?.message ?? r.status); return; }
      }
      if (yes) for (const id of a.layouts) onLayout("delete", id);
      changed();
    } catch (e) { fail(e); }
  };
  const del = (it: TrashItem) => proposeEmpty({ id: it.id }, say(W, "trash_confirm_one", lang, { title: it.title }), it.kind === "layout" ? [it.id] : []);
  const emptyAll = () => proposeEmpty({}, say(W, "trash_confirm_all", lang, { count: String(items.length) }), items.filter((i) => i.kind === "layout").map((i) => i.id));
  const days = typeof retentionDays === "number" ? retentionDays : null;
  return (
    <div className="trash-window" data-testid="trash-window" data-count={items.length} data-state={kernel.state}>
      {days !== null && retentionShort(days, SHORT) && <p className="desktop-hint" role="status" data-testid="trash-advisory" data-code="trash_retention_short">{say(W, "trash_retention_short", lang)}</p>}
      {days !== null && kernel.due > 0 && (
        <div className="desktop-hint" role="status" data-testid="trash-advisory" data-code="trash_retention_due">
          <p>{say(W, "trash_retention_due", lang, { count: String(kernel.due), days: String(days) })}</p>
          <button type="button" data-testid="trash-retention-run" onClick={() => proposeEmpty({ older_than_days: days }, say(W, "trash_confirm_all", lang, { count: String(kernel.due) }), [])}>{say(W, "trash_retention_run", lang)}</button>
        </div>)}
      {kernel.state === "error" && <p role="status" data-testid="trash-error">{say(W, "trash_error", lang, { error: kernel.error ?? "" })}</p>}
      {note && <p role="status" data-testid="trash-note">{note}</p>}
      {ask && (
        <div className="trash-consent" role="alertdialog" aria-label={ask.text} data-testid="trash-consent">
          <p>{ask.text}</p>
          <button type="button" data-testid="trash-consent-yes" onClick={() => answer(true)}>{say(W, "trash_yes", lang)}</button>
          <button type="button" data-testid="trash-consent-no" onClick={() => answer(false)} autoFocus>{say(W, "trash_no", lang)}</button>
        </div>)}
      {kernel.state !== "loading" && !items.length && <p data-testid="trash-empty-now">{say(W, "trash_empty_now", lang)}</p>}
      {byKind(items).map(([kind, group]) => (
        <section key={kind} data-testid="trash-group" data-kind={kind} aria-label={say(W, "trash_kind_" + kind, lang)}>
          <h3>{say(W, "trash_kind_" + kind, lang)}</h3>
          <ul>{group.map((it) => (
            <li key={kind + ":" + it.id} data-testid="trash-item" data-kind={it.kind} data-id={it.id}>
              <span className="trash-item-title">{it.title}</span>{" "}
              <small>{it.at.slice(0, 10)}{it.from ? " · " + say(W, "trash_from", lang, { from: it.from }) : ""}</small>{" "}
              <button type="button" data-testid="trash-restore" onClick={() => restore(it)}>{say(W, "trash_restore", lang)}</button>
              <button type="button" data-testid="trash-delete" onClick={() => del(it)}>{say(W, "trash_delete", lang)}</button>
            </li>))}</ul>
        </section>))}
      {items.length > 0 && <button type="button" className="trash-empty" data-testid="trash-empty" onClick={emptyAll}>{say(W, "trash_empty", lang)}</button>}
    </div>);
}
