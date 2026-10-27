// W5 (OKNA §2.5, §6.1): the sheet „Układ ▾” of the ⋮ menu - save the screen as a named layout, apply one, rename, delete (with a
// question first), undo / redo a change of the layout, go back to the shipped layout. The same operations are the catalogue actions
// layout.save / apply / undo / redo / reset, reached by a sentence; App.tsx carries both out with one set of functions.
import { useState } from "react";
import { useShellText } from "./text";

export interface ShellLayouts {
  own: { id: string; label: string; active: boolean }[];
  shipped: { id: string; label: string; current: boolean }[];
  deviceClass: string;
  canUndo: boolean;
  canRedo: boolean;
  save(label: string): void;
  apply(id: string): void;
  /** Null = renamed; a text = the label of the layout that already has that name (nothing changed). */
  rename(id: string, label: string): string | null;
  remove(id: string): void;
  undo(): void;
  redo(): void;
  reset(): void;
}

export default function LayoutsSheet({ layouts, done }: { layouts: ShellLayouts; done(): void }) {
  const t = useShellText();
  const [name, setName] = useState("");
  const [renaming, setRenaming] = useState<string | null>(null);
  const [newLabel, setNewLabel] = useState("");
  const [asking, setAsking] = useState<string | null>(null);
  const [note, setNote] = useState("");
  // the device class in words of the shell (data: device_class_<id>); a class without words (an overlay's own) is shown by its id
  const classKey = `device_class_${layouts.deviceClass}`;
  const classWords = t(classKey) === classKey ? layouts.deviceClass : t(classKey);
  return (
    <div data-testid="layouts-sheet" data-device-class={layouts.deviceClass}>
      <form className="shell-row" style={{ display: "flex", gap: 8 }} onSubmit={(e) => {
        e.preventDefault();
        const label = name.trim();
        if (!label) return;
        layouts.save(label);
        setNote(t("layout_receipt_saved", { name: label }));
        setName("");
      }}>
        <input data-testid="layouts-save-name" aria-label={t("layouts_save_name")} placeholder={t("layouts_save_name")} value={name}
          onChange={(e) => setName(e.target.value)} style={{ flex: 1, minWidth: 0 }} />
        <button type="submit" data-testid="layouts-save" disabled={!name.trim()}>{t("layouts_save")}</button>
      </form>
      <div className="shell-row" role="toolbar" aria-label={t("layouts_title")} style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
        <button data-testid="layouts-undo" disabled={!layouts.canUndo} onClick={() => { layouts.undo(); done(); }}>{t("layouts_undo")}</button>
        <button data-testid="layouts-redo" disabled={!layouts.canRedo} onClick={() => { layouts.redo(); done(); }}>{t("layouts_redo")}</button>
        <button data-testid="layouts-reset" onClick={() => { layouts.reset(); done(); }}>{t("layouts_reset")}</button>
      </div>
      <h3 className="shell-row">{t("layouts_own")}</h3>
      {layouts.own.length === 0 && <p className="shell-row" data-testid="layouts-empty">{t("layouts_empty")}</p>}
      <div role="list">
        {layouts.own.map((l) => (
          <div key={l.id} role="listitem" className="shell-row" data-testid="layouts-entry" data-layout={l.id} data-active={l.active ? "true" : "false"}
            style={{ display: "flex", alignItems: "center", gap: 8, flexWrap: "wrap" }}>
            {renaming === l.id ? (
              <form style={{ display: "flex", gap: 8, flex: 1 }} onSubmit={(e) => {
                e.preventDefault();
                const clash = layouts.rename(l.id, newLabel);
                if (clash) setNote(t("layouts_rename_clash", { name: clash }));
                else { setRenaming(null); setNote(""); }
              }}>
                <input data-testid="layouts-rename-input" aria-label={t("layouts_rename")} value={newLabel} onChange={(e) => setNewLabel(e.target.value)} style={{ flex: 1, minWidth: 0 }} />
                <button type="submit" data-testid="layouts-rename-ok" disabled={!newLabel.trim()}>{t("layouts_rename_ok")}</button>
              </form>
            ) : (
              <button className="shell-row" style={{ flex: 1, textAlign: "start", border: 0, background: "transparent" }} data-testid="layouts-apply"
                aria-current={l.active ? "true" : undefined} onClick={() => { layouts.apply(l.id); done(); }}>
                {l.label}{l.active ? ` · ${t("layouts_active", { cls: classWords })}` : ""}
              </button>
            )}
            {renaming !== l.id && <button data-testid="layouts-rename" onClick={() => { setRenaming(l.id); setNewLabel(l.label); }}>{t("layouts_rename")}</button>}
            {asking === l.id ? (
              <span role="group" aria-label={t("layouts_delete_ask", { name: l.label })} data-testid="layouts-delete-ask">
                {t("layouts_delete_ask", { name: l.label })}{" "}
                <button data-testid="layouts-delete-yes" onClick={() => { layouts.remove(l.id); setAsking(null); }}>{t("layouts_delete_yes")}</button>{" "}
                <button data-testid="layouts-delete-no" onClick={() => setAsking(null)}>{t("layouts_delete_no")}</button>
              </span>
            ) : <button data-testid="layouts-delete" aria-label={t("layouts_delete_ask", { name: l.label })} onClick={() => setAsking(l.id)}>{t("layouts_delete")}</button>}
          </div>
        ))}
      </div>
      <h3 className="shell-row">{t("layouts_shipped")}</h3>
      <div role="list">
        {layouts.shipped.map((l) => (
          <button key={l.id} role="listitem" className="shell-row" data-testid="layouts-shipped" data-layout={l.id} aria-current={l.current ? "true" : undefined}
            onClick={() => { layouts.apply(l.id); done(); }}>{l.label}</button>
        ))}
      </div>
      {note && <p className="shell-row" role="status" data-testid="layouts-note">{note}</p>}
    </div>
  );
}
