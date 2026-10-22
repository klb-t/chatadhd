import { useEffect, useRef, useState } from "react";
import type { ModelInfo } from "../api/types";
import type { PresentationFeature } from "../onboarding/presentation.mjs";
import UsagePendingBlock from "../components/UsagePendingBlock";
import type { ContextModify } from "../components/use-context-modify";
import type { ProfileChoice } from "../components/context-receipt-state";
import type { UsagePendingReceipt } from "../context/usage-pending";
import { issueText, itemState, labelText, MENU_FLAGS, type MenuContext, type MenuFlag, type MenuFlags, type MenuIssue, type MenuItem } from "./composer-menu";
import { useShellLanguage, useShellText } from "./text";

interface Props {
  items: MenuItem[];
  issues: MenuIssue[];
  ctx: MenuContext;
  /** The switches of this view (memory, knowledge). */
  flags: MenuFlags;
  /** The field switches that are on, by entry id. */
  switches: Record<string, boolean>;
  model: string;
  models: ModelInfo[];
  onAttach: (kind: "attach" | "camera") => void;
  onFlag: (flag: MenuFlag) => void;
  onSwitch: (id: string) => void;
  onModel: (id: string) => void;
  onPanel: (panel: string) => void;
  onClose: () => void;
  /** M1-W1b: the context steps and the selector profile (context.modify, next request only), the list of profiles, the x10 question's words. */
  modify: ContextModify;
  profiles: ProfileChoice[] | null;
  profilesError: string | null;
  onLoadProfiles: () => void;
  usageFeature: PresentationFeature | null;
  /** True while the carrier holds something this window asked for: the reset button is offered. */
  waiting: boolean;
  /** Opens the card of a graph node (node entries) and the preview of the request (preview entry). */
  onNode: (focus: string) => void;
  onPreview: () => void;
  /** The sentence after a change was applied, in the pack's words (computed by the screen from what waits for the next request). */
  appliedText: string;
}

const isFlag = (flag: string | undefined): flag is MenuFlag => (MENU_FLAGS as readonly string[]).includes(flag ?? "");

/** The + menu: entries from data, a stack for submenus and the model list, keyboard (arrows, Home/End, Esc, Backspace = back) and touch (44 px rows, a sheet at phone width). */
export default function ComposerMenu({ items, issues, ctx, flags, switches, model, models, onAttach, onFlag, onSwitch, onModel, onPanel, onClose, modify, profiles, profilesError, onLoadProfiles, usageFeature, waiting, onNode, onPreview, appliedText }: Props) {
  const t = useShellText();
  const lang = useShellLanguage();
  const [stack, setStack] = useState<string[]>([]);  // ids of the opened submenus; "@model" is the model list, "@profiles" the selector profiles
  const ref = useRef<HTMLDivElement>(null);
  // A label that is not a word of the shell is shown as it was written; the id only when there is no label at all.
  const word = (label: unknown, fallback: string): string => labelText(label, t, lang, fallback);
  let level: MenuItem[] | null = items;
  let title = t("cm_title");
  for (const id of stack) {
    if (id === "@model") { level = null; title = t("cm_model"); break; }
    if (id === "@profiles") { level = null; title = t("cm_selector_profile"); break; }
    const hit: MenuItem | undefined = level?.find(i => i.id === id);
    level = hit?.items ?? null;
    if (hit) title = word(hit.label, hit.id);
  }

  const listed = stack.includes("@profiles") ? "profiles" : stack.includes("@model") ? "model" : "items";
  // What was said to the kernel in this menu stays in this menu: a closed menu leaves no old answer behind.
  useEffect(() => () => modify.clear(), []);
  useEffect(() => {
    const first = ref.current?.querySelector<HTMLElement>("[data-cm-row]:not([aria-disabled='true'])") ?? ref.current?.querySelector<HTMLElement>("[data-cm-row]");
    first?.focus();
  }, [stack]);

  const onKey = (e: React.KeyboardEvent) => {
    const rows = Array.from(ref.current?.querySelectorAll<HTMLElement>("[data-cm-row]") ?? []);
    const at = rows.indexOf(document.activeElement as HTMLElement);
    const go = (n: number) => { e.preventDefault(); rows[(n + rows.length) % rows.length]?.focus(); };
    if (e.key === "ArrowDown") go(at + 1);
    else if (e.key === "ArrowUp") go(at <= 0 ? rows.length - 1 : at - 1);
    else if (e.key === "Home") go(0);
    else if (e.key === "End") go(rows.length - 1);
    else if (e.key === "Escape") { e.preventDefault(); e.stopPropagation(); onClose(); }
    else if ((e.key === "Backspace" || e.key === "ArrowLeft") && stack.length) { e.preventDefault(); setStack(s => s.slice(0, -1)); }
    else if (e.key === "Tab") { e.preventDefault(); go(e.shiftKey ? at - 1 : at + 1); }
  };

  const run = (item: MenuItem) => {
    if (item.kind === "submenu") setStack(s => [...s, item.id]);
    else if (item.kind === "model") setStack(s => [...s, "@model"]);
    else if (item.kind === "profiles") { setStack(s => [...s, "@profiles"]); onLoadProfiles(); }
    else if (item.kind === "step" && item.step) void modify.run({ step: item.step });
    else if (item.kind === "node" && item.focus) { onNode(item.focus); onClose(); }
    else if (item.kind === "preview") { onPreview(); onClose(); }
    else if (item.kind === "attach" || item.kind === "camera") { onAttach(item.kind); onClose(); }
    else if (item.kind === "panel" && item.panel) { onPanel(item.panel); onClose(); }
    else if (item.kind === "toggle" && item.field) onSwitch(item.id);
    else if (item.kind === "toggle" && isFlag(item.flag)) onFlag(item.flag);
  };
  // An unavailable entry does nothing but point to where it is set, when it says where.
  const click = (item: MenuItem, unavailable: boolean) => {
    if (!unavailable) run(item);
    else if (item.panel) { onPanel(item.panel); onClose(); }
  };

  return (
    <>
      <div className="composer-menu-backdrop" data-testid="composer-menu-backdrop" onClick={onClose} />
      <div className="composer-menu" role="menu" aria-label={title} data-testid="composer-menu" ref={ref} onKeyDown={onKey}>
        <div className="composer-menu-head">
          {stack.length > 0
            ? <button type="button" className="composer-menu-back" data-cm-row data-testid="composer-menu-back" onClick={() => setStack(s => s.slice(0, -1))}>‹ {t("cm_back")}</button>
            : <span className="composer-menu-title">{title}</span>}
          {stack.length > 0 && <span className="composer-menu-title" aria-hidden>{title}</span>}
        </div>
        {issues.length > 0 && <p className="composer-menu-issue" role="status" data-testid="composer-menu-issue">
          {t("cm_issue", { issue: issues.map(i => issueText(i, t, lang)).join("; ") })}</p>}
        {listed === "profiles" && <>
          <p className="composer-menu-reason" data-testid="composer-profiles-help">{t("cm_profiles_help")}</p>
          {profiles === null && profilesError === null && <p className="composer-menu-reason" data-testid="composer-profiles-loading">{t("cm_profiles_loading")}</p>}
          {profilesError !== null && <p className="composer-menu-reason" role="alert" data-testid="composer-profiles-error">{t("cm_profiles_failed", { reason: profilesError })}</p>}
          {profiles?.length === 0 && <p className="composer-menu-reason" data-testid="composer-profiles-empty">{t("cm_profiles_empty")}</p>}
          {profiles?.map(p => <button type="button" key={p.id} role="menuitem" data-cm-row className="composer-menu-row" disabled={modify.busy}
            data-testid={`composer-profile-${p.id}`} data-status={p.status} onClick={() => void modify.run({ profile: p.id })}>
            <span>{p.label}</span>{p.status && <span className="composer-menu-state" data-testid={`composer-profile-status-${p.id}`}>{t(`cm_status_${p.status}`)}</span>}</button>)}
        </>}
        {listed === "model" && <>
          <button type="button" role="menuitemradio" aria-checked={model === ""} data-cm-row className="composer-menu-row" data-testid="composer-model-default" onClick={() => { onModel(""); onClose(); }}>
            <span>{t("cm_model_default")}</span>{model === "" && <span aria-hidden>✓</span>}</button>
          {models.length === 0 && <p className="composer-menu-reason" data-testid="composer-model-empty">{t("cm_models_empty")}</p>}
          {models.map(m => <button type="button" key={m.id} role="menuitemradio" aria-checked={model === m.id} data-cm-row className="composer-menu-row"
            data-testid="composer-model-option" data-model={m.id} onClick={() => { onModel(m.id); onClose(); }}>
            <span>{m.name ?? m.id}</span>{model === m.id && <span aria-hidden>✓</span>}</button>)}
        </>}
        {listed === "items" && groupSteps(level ?? []).map(entry => {
          if (Array.isArray(entry)) return (
            <div key={`steps:${entry[0].id}`} className="composer-menu-item">
              <div className="composer-menu-chips" role="group" aria-label={t("cm_steps_help")} data-testid="composer-menu-steps">
                {entry.map(step => <button type="button" key={step.id} data-cm-row className="composer-menu-chip" disabled={modify.busy}
                  data-testid={`composer-menu-${step.id}`} data-step={step.step} onClick={() => run(step)}>{word(step.label, step.id)}</button>)}
              </div>
              <p className="composer-menu-reason" data-testid="composer-menu-steps-help">{t("cm_steps_help")}</p>
            </div>);
          const item = entry;
          const st = itemState(item, ctx);
          const label = word(item.label, item.id);
          const reason = st.state === "unavailable" ? word(st.reason, String(st.reason)) : null;
          const on = item.kind !== "toggle" ? undefined : item.field ? Boolean(switches[item.id]) : isFlag(item.flag) ? flags[item.flag] : undefined;
          const role = item.kind === "toggle" ? "menuitemcheckbox" : "menuitem";
          const current = item.kind === "model" ? t("cm_model_current", { model: model ? (models.find(m => m.id === model)?.name ?? model) : t("cm_model_default") }) : null;
          return (
            <div key={item.id} className="composer-menu-item">
              <button type="button" role={role} data-cm-row className="composer-menu-row" data-testid={`composer-menu-${item.id}`}
                aria-checked={on === undefined ? undefined : on} aria-disabled={reason !== null || undefined}
                aria-haspopup={item.kind === "submenu" || item.kind === "model" || item.kind === "profiles" ? "menu" : undefined}
                aria-describedby={reason !== null ? `cm-reason-${item.id}` : undefined}
                data-unavailable={reason !== null || undefined}
                onClick={() => click(item, reason !== null)}>
                <span>{label}</span>
                {on !== undefined && reason === null && <span className="composer-menu-state">{on ? t("cm_on") : t("cm_off")}</span>}
                {reason !== null && <span className="composer-menu-state">{t("cm_unavailable")}</span>}
                {(item.kind === "submenu" || item.kind === "model" || item.kind === "profiles") && <span aria-hidden>›</span>}
              </button>
              {current && <p className="composer-menu-reason">{current}</p>}
              {item.help !== undefined && reason === null && <p className="composer-menu-reason" data-testid={`composer-menu-help-${item.id}`}>{word(item.help, "")}</p>}
              {reason !== null && <p className="composer-menu-reason" id={`cm-reason-${item.id}`} data-testid={`composer-menu-reason-${item.id}`}>{reason}</p>}
            </div>
          );
        })}
        {modify.busy && <p className="composer-menu-reason" role="status" data-testid="composer-menu-modify-busy">{t("cm_modify_busy")}</p>}
        {modify.answer?.kind === "applied" && <p className="composer-menu-result" role="status" data-kind="applied" data-testid="composer-menu-modify-result">{appliedText}</p>}
        {modify.answer?.kind === "declined" && <p className="composer-menu-result" role="status" data-kind="declined" data-testid="composer-menu-modify-result">{t("cm_modify_declined")}</p>}
        {modify.answer?.kind === "failed" && <p className="composer-menu-result" role="alert" data-kind="failed" data-testid="composer-menu-modify-result">{t("cm_modify_failed", { reason: modify.answer.message })}</p>}
        {modify.answer?.kind === "question" && (
          <UsagePendingBlock testId="composer-menu-usage-pending" feature={usageFeature} busy={modify.busy}
            receipt={{ status: (modify.answer.usage.status as string | undefined) ?? "requires_confirmation", receipt_id: modify.answer.receiptId, operation_id: modify.answer.operationId,
              baseline_key: (modify.answer.usage.baseline_key as string | null | undefined) ?? null, projected: modify.answer.usage.projected as UsagePendingReceipt["projected"] }}
            onConfirm={() => void modify.decide(true)} onReject={() => void modify.decide(false)} />
        )}
        {waiting && listed !== "model" && <button type="button" data-cm-row className="composer-menu-row" disabled={modify.busy} data-testid="composer-menu-reset"
          onClick={() => void modify.run({ reset: true })}><span>{t("cm_modify_reset")}</span></button>}
      </div>
    </>
  );
}

/** The consecutive step entries of a level are drawn as one row of chips; everything else keeps its row. */
function groupSteps(items: MenuItem[]): (MenuItem | MenuItem[])[] {
  const out: (MenuItem | MenuItem[])[] = [];
  for (const item of items) {
    const last = out[out.length - 1];
    if (item.kind !== "step") out.push(item);
    else if (Array.isArray(last)) last.push(item);
    else out.push([item]);
  }
  return out;
}
