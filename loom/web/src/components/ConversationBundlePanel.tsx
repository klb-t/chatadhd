import { useEffect, useId, useRef, useState } from "react";
import { api } from "../api";
import type { Conversation } from "../api/types";
import {
  bundleCapabilities, exportConversationBundle, isBundleHash, restoreConversationBundle,
  type BundleExportReceipt, type BundleRestoreReceipt,
} from "../api/conversation-bundle";
import words from "../content/conversation-bundle.words.json";
import { dataLabel, useShellLanguage } from "../shell/text";
import { useAccountSession } from "./AuthGate";

type Word = Exclude<keyof typeof words, "defaults" | "downloadName" | "errors" | "limits">;
interface Props { onImported: () => void; disabled?: boolean; onBusyChange?: (busy: boolean) => void }
interface Task { controller: AbortController; importDispatched: boolean }

export default function ConversationBundlePanel(props: Props) {
  const language = useShellLanguage(), account = useAccountSession();
  const [open, setOpen] = useState(false);
  return <details data-testid="conversation-bundle-panel" onToggle={event => setOpen(event.currentTarget.open)}>
    <summary>{dataLabel(words.title, language, words.title.en)}</summary>
    {open && <BundleBrowser key={account?.login ?? ""} {...props} />}
  </details>;
}

function BundleBrowser({ onImported, disabled = false, onBusyChange }: Props) {
  const language = useShellLanguage(), uid = useId();
  const w = (id: Word) => dataLabel(words[id], language, words[id].en);
  const [conversations, setConversations] = useState<Conversation[] | null>(null);
  const [selected, setSelected] = useState(""), [limit, setLimit] = useState(String(words.defaults.conversationLimit));
  const [loadedLimit, setLoadedLimit] = useState<number | null>(null);
  const [exportIncomplete, setExportIncomplete] = useState(false), [restoreIncomplete, setRestoreIncomplete] = useState(false);
  const [file, setFile] = useState<File | null>(null), [expectedHash, setExpectedHash] = useState("");
  const [exported, setExported] = useState<BundleExportReceipt | null>(null);
  const [restored, setRestored] = useState<BundleRestoreReceipt | null>(null);
  const [download, setDownload] = useState<string | null>(null);
  const [busy, setBusy] = useState(false), [error, setError] = useState("");
  const [notice, setNotice] = useState<"cancelled" | "uncertain" | null>(null);
  const task = useRef<Task | null>(null), alive = useRef(true), objectUrl = useRef<string | null>(null);
  const callbacks = useRef({ onImported, onBusyChange });
  callbacks.current = { onImported, onBusyChange };
  const capabilities = bundleCapabilities(api);
  const blocked = disabled || busy;
  // listConversations crosses the existing signed-int host boundary; the scope is user editable.
  const validLimit = limit.trim() !== "" && Number.isSafeInteger(Number(limit)) && Number(limit) > 0 && Number(limit) <= 2147483647;
  const validPin = expectedHash.trim() === "" || isBundleHash(expectedHash.trim());

  const releaseDownload = () => {
    if (objectUrl.current) URL.revokeObjectURL(objectUrl.current);
    objectUrl.current = null;
  };
  const clearExport = () => { releaseDownload(); setDownload(null); setExported(null); };
  useEffect(() => {
    alive.current = true;
    return () => {
      alive.current = false;
      const pending = task.current;
      task.current = null;
      pending?.controller.abort();
      releaseDownload();
      if (pending) callbacks.current.onBusyChange?.(false);
    };
  }, []);

  const current = (mine: Task) => alive.current && task.current === mine && !mine.controller.signal.aborted;
  const run = async (operation: (mine: Task) => Promise<void>) => {
    if (disabled || task.current) return;
    const mine: Task = { controller: new AbortController(), importDispatched: false };
    task.current = mine;
    setBusy(true); setError("");
    callbacks.current.onBusyChange?.(true);
    try { await operation(mine); }
    catch (failure) {
      if (alive.current && task.current === mine) {
        if (mine.importDispatched) setNotice("uncertain");
        else if (mine.controller.signal.aborted) setNotice("cancelled");
        if (!mine.controller.signal.aborted) setError(failure instanceof Error ? failure.message : String(failure));
      }
    } finally {
      if (alive.current && task.current === mine) {
        task.current = null; setBusy(false); callbacks.current.onBusyChange?.(false);
      }
    }
  };
  const load = async (mine: Task) => {
    const requested = Number(limit);
    const rows = await api.listConversations(requested);
    if (!current(mine)) return;
    setConversations(rows); setLoadedLimit(requested);
    if (!rows.some(row => row.id === selected)) { setSelected(""); clearExport(); }
  };
  const prepare = async (mine: Task) => {
    clearExport(); setNotice(null);
    const result = await exportConversationBundle(api, selected, {
      allowIncomplete: exportIncomplete, signal: mine.controller.signal,
    });
    if (!current(mine)) return;
    const url = URL.createObjectURL(result.blob);
    objectUrl.current = url; setDownload(url); setExported(result.receipt);
  };
  const restore = async (mine: Task) => {
    if (!file) return;
    setRestored(null); setNotice(null);
    const receipt = await restoreConversationBundle(api, file, {
      expectedSha256: expectedHash, allowIncomplete: restoreIncomplete, signal: mine.controller.signal,
      onImportDispatched: () => { mine.importDispatched = true; },
    });
    if (!current(mine)) return;
    setRestored(receipt); setNotice(null);
    // Refresh the shell's list only. Never switch conversation or rewrite its context selector.
    callbacks.current.onImported();
  };
  const cancel = () => {
    const pending = task.current;
    if (!pending) return;
    pending.controller.abort();
    setNotice(pending.importDispatched ? "uncertain" : "cancelled");
    // Hold the local operation lock until the host promise settles. Abort does not undo a native commit.
  };
  const errorWords = words.errors[error as keyof typeof words.errors];
  const limitText = (code: string) => {
    const label = words.limits[code as keyof typeof words.limits];
    return label ? dataLabel(label, language, label.en) : `${w("unknownLimit")}: ${code}`;
  };

  return <div data-testid="conversation-bundle-browser" style={{ overflowWrap: "anywhere" }}>
    <p>{w("scope")}</p><p>{w("restoreLimits")}</p><p>{w("selector")}</p>
    {(!capabilities.export || !capabilities.restore) && <p role="status">{w("unsupported")}</p>}
    <fieldset disabled={blocked}>
      <legend>{w("conversation")}</legend>
      <label htmlFor={uid + "limit"}>{w("limit")}</label>
      <input id={uid + "limit"} type="number" min="1" max="2147483647" value={limit} onChange={event => setLimit(event.target.value)} data-testid="bundle-conversation-limit" />
      <button type="button" onClick={() => void run(load)} disabled={!validLimit} data-testid="bundle-load-conversations">{w("reload")}</button>
      {conversations && <p>{w("window")}: {conversations.length} / {loadedLimit}</p>}
      {conversations?.length === 0 && <p>{w("empty")}</p>}
      <label htmlFor={uid + "conversation"}>{w("conversation")}</label>
      <select id={uid + "conversation"} value={selected} data-testid="bundle-conversation" onChange={event => { setSelected(event.target.value); clearExport(); }}>
        <option value="">{w("choose")}</option>
        {conversations?.map(row => <option key={row.id} value={row.id}>{row.title || w("untitled")} · {row.id}</option>)}
      </select>
      <label><input type="checkbox" checked={exportIncomplete} onChange={event => setExportIncomplete(event.target.checked)} data-testid="bundle-export-incomplete" />{w("allowExportIncomplete")}</label>
      <button type="button" onClick={() => void run(prepare)} disabled={!selected || !capabilities.export} data-testid="bundle-export">{w("export")}</button>
    </fieldset>

    {exported && <div role="status" data-testid="bundle-export-result" data-complete={exported.complete_assets}>
      <p>{w(exported.complete_assets ? "complete" : "incomplete")}</p>
      <p>{w("archiveHash")}: <code>{exported.hash}</code></p>
      <p>{w("manifestHash")}: <code>{exported.manifest_sha256}</code></p>
      <p>{w("pinHint")}</p>
      {download && <a href={download} download={words.downloadName.replace("{hash}", exported.hash)} data-testid="bundle-download">{w("download")}</a>}
    </div>}

    <fieldset disabled={blocked}>
      <legend>{w("restore")}</legend>
      <p>{w("fileHint")}</p>
      <label htmlFor={uid + "file"}>{w("file")}</label>
      <input id={uid + "file"} type="file" accept=".zip,application/zip" data-testid="bundle-restore-file" onChange={event => {
        setFile(event.target.files?.[0] ?? null); setRestored(null); setError("");
      }} />
      <label htmlFor={uid + "hash"}>{w("expectedHash")}</label>
      <input id={uid + "hash"} type="text" value={expectedHash} spellCheck={false} autoComplete="off" aria-invalid={!validPin} onChange={event => setExpectedHash(event.target.value)} data-testid="bundle-expected-hash" />
      {!validPin && <p role="alert">{dataLabel(words.errors.bundle_expected_hash_invalid, language, words.errors.bundle_expected_hash_invalid.en)}</p>}
      <p>{w("pinHint")}</p>
      <label><input type="checkbox" checked={restoreIncomplete} onChange={event => setRestoreIncomplete(event.target.checked)} data-testid="bundle-restore-incomplete" />{w("allowRestoreIncomplete")}</label>
      <button type="button" onClick={() => void run(restore)} disabled={!file || !validPin || !capabilities.restore} data-testid="bundle-restore">{w("restore")}</button>
    </fieldset>

    {busy && <p role="status">{w("busy")} <button type="button" onClick={cancel} data-testid="bundle-cancel">{w("cancel")}</button></p>}
    {notice && <p role="alert" data-testid="bundle-operation-notice">{w(notice)}</p>}
    {error && <p role="alert" data-testid="bundle-error">{w("error")}: {errorWords ? dataLabel(errorWords, language, errorWords.en) : error}</p>}
    {restored && <div role="status" data-testid="bundle-restore-result" data-status={restored.status}>
      <p>{w(restored.complete_assets ? "complete" : "incomplete")}</p>
      <p>{w("restored")}: <code>{restored.conversation_id}</code></p>
      <p>{w("archiveHash")}: <code>{restored.archive_sha256}</code></p>
      <p>{w("manifestHash")}: <code>{restored.manifest_sha256}</code></p>
      <p>{w("verified")}: {restored.verified_slots} / {restored.attachment_slots}</p>
      {!restored.complete_assets && <p>{w("missing")}: {restored.attachment_slots - restored.verified_slots}</p>}
      <p>{w("nativeLimits")}</p><ul>{restored.limits.map((code, index) => <li key={index}>{limitText(code)}</li>)}</ul>
      <details><summary>{w("mapping")}</summary>
        <table><thead><tr><th>{w("sourcePointer")}</th><th>{w("targetId")}</th></tr></thead>
          <tbody>{Object.entries(restored.message_mapping).map(([pointer, id]) => <tr key={pointer}><td><code>{pointer}</code></td><td><code>{id}</code></td></tr>)}</tbody>
        </table>
      </details>
      <details><summary>{w("details")}</summary><pre data-audit-skip>{JSON.stringify(restored, null, 2)}</pre></details>
    </div>}
  </div>;
}
