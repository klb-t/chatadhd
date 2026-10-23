// M1-G1b (4): "Co tu widzę?" - the help of a graph screen in a disclosure under the path row. See graph-help-state.ts.
import { useEffect, useState } from "react";
import { api } from "../api";
import Disclosure from "../shell/Disclosure";
import type { ShellT } from "../shell/text";
import { HELP_SCREENS, helpLines, helpOpenOf, type HelpScreen } from "./graph-help-state";

interface Props {
  screen: HelpScreen;
  /** The words of presentation.graph_screen in the language of the screen. */
  t: ShellT;
  /** The setting as loaded (config); changes of it after the first render are followed until the person toggles here. */
  setting: unknown;
  /** Tells the app the new value, so a screen opened later in the same session starts as the person left it. */
  onSetting?: (key: string, value: boolean) => void;
  testId: string;
}

export default function GraphHelp({ screen, t, setting, onSetting, testId }: Props) {
  const { setting: key, prefix } = HELP_SCREENS[screen];
  const [open, setOpen] = useState(() => helpOpenOf(setting));
  const [note, setNote] = useState<string | null>(null);
  useEffect(() => { setOpen(helpOpenOf(setting)); }, [setting]);
  const lines = helpLines(t, prefix);
  if (lines.length === 0) return null;   // no words in this catalogue: no empty disclosure
  const toggle = (next: boolean) => {
    setOpen(next); setNote(null); onSetting?.(key, next);
    api.setConfigKey(key, next).catch(failure => setNote(failure instanceof Error ? failure.message : String(failure)));   // shown below; the state on screen stays as the person set it
  };
  return (
    <Disclosure testId={testId} className="gnav-help" labels={{ show: t(`${prefix}.show`) === `${prefix}.show` ? t("help.show") : t(`${prefix}.show`), hide: t(`${prefix}.hide`) === `${prefix}.hide` ? t("help.hide") : t(`${prefix}.hide`) }}
      open={open} onToggle={toggle}>
      {lines.map((line, i) => <p key={i} className="gnav-help-line" data-testid={`${testId}-line-${i + 1}`}>{line}</p>)}
      {note && <p className="gnav-note gnav-error" data-testid={`${testId}-save-error`}>{t("view.save_error", { reason: note })}</p>}
    </Disclosure>
  );
}
