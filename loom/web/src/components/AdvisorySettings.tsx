// Settings: the remembered answers to advisories (G3b) and how often they ask. Every row can be forgotten (the advisory asks again); the policy is the
// preset of the view engine (shown) under the person's own values (advisory.policy), and "reset" puts the preset back. All through catalogue actions.
import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import { useShellText } from "../shell/text";

interface Ack { code: string; fingerprint: string; choice: string; ack_at: string; context?: { view?: string; text?: string } }
interface Answers { acks: Ack[]; policy: { ask?: boolean; remember_days?: number | null }; preset: { ask?: boolean; remember_days?: number | null }; effective: { ask?: boolean; remember_days?: number | null } }

export default function AdvisorySettings() {
  const t = useShellText();
  const [data, setData] = useState<Answers | null>(null);
  const [error, setError] = useState("");
  const [days, setDays] = useState("");

  const run = useCallback(async (action: string, args: Record<string, unknown> = {}) => {
    const actions = api.actions;
    if (!actions) throw new Error("actions_unavailable");
    const r = await actions.run({ action, element: { class: "application" }, args });
    if (r.status !== "completed") throw new Error(r.error?.message ?? r.status);
    return r.result?.data as unknown;
  }, []);
  const load = useCallback(async () => {
    try { setData(await run("advisory.list") as Answers); setError(""); } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)); }
  }, [run]);
  useEffect(() => { void load(); }, [load]);
  const act = async (action: string, args: Record<string, unknown>) => {
    try { await run(action, args); await load(); } catch (cause) { setError(cause instanceof Error ? cause.message : String(cause)); }
  };

  const eff = data?.effective ?? {};
  const daysText = (v: number | null | undefined) => (typeof v === "number" ? String(v) : t("adv_settings_forever"));
  return (
    <section data-testid="advisory-settings" className="expert-config">
      <div className="section-title">{t("adv_settings_title")}</div>
      {error && <div data-testid="advisory-settings-error" style={{ color: "var(--err)" }}>{error}</div>}
      <div className="form-row">
        <label>
          <input type="checkbox" data-testid="advisory-policy-ask" checked={eff.ask !== false} onChange={e => void act("advisory.policy", { ask: e.target.checked })} />{" "}
          {t("adv_settings_ask")}
        </label>
      </div>
      <div className="form-row">
        <label htmlFor="adv-days">{t("adv_settings_days", { days: daysText(eff.remember_days), preset: daysText(data?.preset.remember_days) })}</label>
        <input id="adv-days" data-testid="advisory-policy-days" value={days} placeholder={daysText(eff.remember_days)} onChange={e => setDays(e.target.value)} />
        <button type="button" data-testid="advisory-policy-days-save" disabled={days.trim() === ""} onClick={() => { void act("advisory.policy", { remember_days: days.trim() }); setDays(""); }}>{t("adv_settings_save")}</button>
        <button type="button" data-testid="advisory-policy-reset" onClick={() => void act("advisory.policy", { reset: true })}>{t("adv_settings_reset")}</button>
      </div>
      {data && data.acks.length === 0 && <div className="empty-state" data-testid="advisory-acks-empty">{t("adv_settings_none")}</div>}
      <ul data-testid="advisory-acks" style={{ listStyle: "none", padding: 0 }}>
        {(data?.acks ?? []).map(a => (
          <li key={`${a.code}|${a.fingerprint}`} data-testid="advisory-ack-row" data-code={a.code} data-fingerprint={a.fingerprint} data-choice={a.choice}>
            <strong>{a.code}</strong> ({a.choice}{a.context?.view ? `, ${a.context.view}` : ""}, {a.ack_at}){a.context?.text ? `: ${a.context.text}` : ""}{" "}
            <button type="button" data-testid="advisory-forget" onClick={() => void act("advisory.answer", { code: a.code, fingerprint: a.fingerprint, answer: "forget" })}>{t("adv_settings_forget")}</button>
          </li>
        ))}
      </ul>
    </section>
  );
}
