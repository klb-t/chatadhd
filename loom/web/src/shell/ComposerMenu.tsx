import { useEffect, useRef, useState } from "react";
import type { ModelInfo } from "../api/types";
import { itemState, type MenuContext, type MenuFlags, type MenuItem } from "./composer-menu";
import { dataLabel, useShellLanguage, useShellText } from "./text";

interface Props {
  items: MenuItem[];
  issues: string[];
  ctx: MenuContext;
  flags: MenuFlags;
  model: string;
  models: ModelInfo[];
  onAttach: (kind: "attach" | "camera") => void;
  onToggle: (flag: keyof MenuFlags) => void;
  onModel: (id: string) => void;
  onPanel: (panel: string) => void;
  onClose: () => void;
}

/** The + menu: entries from data, a stack for submenus and the model list, keyboard (arrows, Home/End, Esc, Backspace = back) and touch (44 px rows, a sheet at phone width). */
export default function ComposerMenu({ items, issues, ctx, flags, model, models, onAttach, onToggle, onModel, onPanel, onClose }: Props) {
  const t = useShellText();
  const lang = useShellLanguage();
  const [stack, setStack] = useState<string[]>([]);  // ids of the opened submenus; "@model" is the model list
  const ref = useRef<HTMLDivElement>(null);
  const word = (label: unknown, fallback: string): string => {
    if (typeof label === "string") { const said = t(label); return said === label ? fallback : said; }
    return dataLabel(label, lang, fallback);
  };
  let level: MenuItem[] | null = items;
  let title = t("cm_title");
  for (const id of stack) {
    if (id === "@model") { level = null; title = t("cm_model"); break; }
    const hit: MenuItem | undefined = level?.find(i => i.id === id);
    level = hit?.items ?? null;
    if (hit) title = word(hit.label, hit.id);
  }

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
    else if (item.kind === "attach" || item.kind === "camera") { onAttach(item.kind); onClose(); }
    else if (item.kind === "panel" && item.panel) { onPanel(item.panel); onClose(); }
    else if (item.kind === "toggle" && item.flag && item.flag in flags) onToggle(item.flag as keyof MenuFlags);
    else if (item.kind === "unavailable" && item.panel) { onPanel(item.panel); onClose(); }
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
        {issues.length > 0 && <p className="composer-menu-issue" role="status" data-testid="composer-menu-issue">{t("cm_issue", { issue: issues.join("; ") })}</p>}
        {level === null && <>
          <button type="button" role="menuitemradio" aria-checked={model === ""} data-cm-row className="composer-menu-row" data-testid="composer-model-default" onClick={() => { onModel(""); onClose(); }}>
            <span>{t("cm_model_default")}</span>{model === "" && <span aria-hidden>✓</span>}</button>
          {models.length === 0 && <p className="composer-menu-reason" data-testid="composer-model-empty">{t("cm_models_empty")}</p>}
          {models.map(m => <button type="button" key={m.id} role="menuitemradio" aria-checked={model === m.id} data-cm-row className="composer-menu-row"
            data-testid="composer-model-option" data-model={m.id} onClick={() => { onModel(m.id); onClose(); }}>
            <span>{m.name ?? m.id}</span>{model === m.id && <span aria-hidden>✓</span>}</button>)}
        </>}
        {level?.map(item => {
          const st = itemState(item, ctx);
          if (st.state === "hidden") return null;
          const label = word(item.label, item.id);
          const reason = st.state === "unavailable" ? word(st.reason, String(st.reason)) : null;
          const on = item.kind === "toggle" && item.flag ? flags[item.flag as keyof MenuFlags] : undefined;
          const role = item.kind === "toggle" ? "menuitemcheckbox" : "menuitem";
          const current = item.kind === "model" ? t("cm_model_current", { model: model ? (models.find(m => m.id === model)?.name ?? model) : t("cm_model_default") }) : null;
          return (
            <div key={item.id} className="composer-menu-item">
              <button type="button" role={role} data-cm-row className="composer-menu-row" data-testid={`composer-menu-${item.id}`}
                aria-checked={on === undefined ? undefined : on} aria-disabled={reason !== null || undefined}
                aria-haspopup={item.kind === "submenu" || item.kind === "model" ? "menu" : undefined}
                aria-describedby={reason !== null ? `cm-reason-${item.id}` : undefined}
                data-unavailable={reason !== null || undefined}
                onClick={() => { if (reason === null || item.panel) run(item); }}>
                <span>{label}</span>
                {on !== undefined && <span className="composer-menu-state">{on ? t("cm_on") : t("cm_off")}</span>}
                {reason !== null && <span className="composer-menu-state">{t("cm_unavailable")}</span>}
                {(item.kind === "submenu" || item.kind === "model") && <span aria-hidden>›</span>}
              </button>
              {current && <p className="composer-menu-reason">{current}</p>}
              {reason !== null && <p className="composer-menu-reason" id={`cm-reason-${item.id}`} data-testid={`composer-menu-reason-${item.id}`}>{reason}</p>}
            </div>
          );
        })}
      </div>
    </>
  );
}
