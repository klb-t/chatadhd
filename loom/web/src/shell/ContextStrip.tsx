// M1-W1b: the thin strip above the composer (setting composer_context_strip: chips | hidden | full). It says in words what the next request
// will carry: the profile and the changes that wait for it (szerzej, głębiej ...). Read from the scope chain / the last receipt and from the
// kernel's last answer to context.modify given to THIS window - never invented. Display only: the request is the same whatever it shows.
import { useEffect, useMemo, useState } from "react";
import { api } from "../api";
import { stripMode, stripView, type StripMode, type StripProfile } from "./context-strip";
import { useNextRequest } from "./use-next-request";
import type { ShellT } from "./text";

interface Props {
  convId: string | null;
  language: string;
  /** The words of presentation.context_receipt. */
  t: ShellT;
  /** The profile of the last request of this conversation (from its receipt), used when the chain cannot be read. */
  lastProfile: StripProfile | null;
  /** Changes with every message of the conversation: the profile is read again. */
  messageCount: number;
}

export default function ContextStrip({ convId, language, t, lastProfile, messageCount }: Props) {
  const [mode, setMode] = useState<StripMode>("chips");
  useEffect(() => {
    let live = true;
    api.getConfig().then(cfg => { if (live) setMode(stripMode(cfg.composer_context_strip)); }, () => undefined);
    return () => { live = false; };
  }, []);
  const { profile, profiles, waiting } = useNextRequest(convId, language, lastProfile, messageCount);
  const view = useMemo(() => stripView({ mode, waiting, profile, profiles }, t, language), [mode, waiting, profile, profiles, t, language]);
  if (mode === "hidden") return null;
  return (
    <div className="context-strip" role="status" aria-label={t("strip.aria")} data-testid="context-strip" data-mode={mode} data-pending={view.pending.length}>
      {mode === "full"
        ? <p data-testid="context-strip-full">{view.fullText}</p>
        : <>
          <span className="context-strip-chip" data-testid="context-strip-profile" data-known={view.profileKnown}>{view.profileText}</span>
          {view.nextText && <span className="context-strip-chip" data-testid="context-strip-next">{view.nextText}</span>}
        </>}
    </div>
  );
}
