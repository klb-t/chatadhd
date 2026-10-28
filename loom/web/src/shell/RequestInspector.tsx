// W-gaps (OKNA, W8 gap 1): the window kind `request_inspector` - the Expert view lifted out of the Workshop. Openable from the chat menu
// („Pokaż zapytanie”) and by the sentence „pokaż zapytanie” / „show the request” (window.open, target request_inspector).
// RI-2: two sections in words. „Co wysłała strona”: the request this page sent to the kernel (or prepared in the Expert view).
// „Co jądro wysłało / wyśle dostawcy”: the request the kernel put on the wire for the provider (the last one of the conversation) or would
// put for the text in the field now („Podgląd następnego”, nothing sent) - parts with sizes, tools, parameters; secrets are masked by the
// kernel before the answer leaves it. The raw JSON of both stays folded under „Szczegóły techniczne”.
import { useCallback, useEffect, useState } from "react";
import type { LoomApi } from "../api/loom-api";
import { currentDraft, kernelLines, lastRequest, onRequest, requestLines, settledConversation } from "./last-request";
import { useShellText } from "./text";

type Tab = "page" | "kernel";
type Which = "last" | "next";

export default function RequestInspector({ api, convId }: { api?: Pick<LoomApi, "providerRequest">; convId?: string | null }) {
  const t = useShellText();
  const [sent, setSent] = useState(lastRequest);
  const [tab, setTab] = useState<Tab>("page");
  const [which, setWhich] = useState<Which>("last");
  const [kernel, setKernel] = useState<{ state: "idle" | "loading" | "ok" | "error"; view?: Record<string, unknown>; error?: string }>({ state: "idle" });
  const [tick, setTick] = useState(0);
  useEffect(() => onRequest(() => { setSent(lastRequest()); setTick((n) => n + 1); }), []);

  const conv = convId || (typeof sent?.request.conv_id === "string" ? sent.request.conv_id : null) || settledConversation();
  const ask = useCallback(async () => {
    if (!api?.providerRequest) { setKernel({ state: "error", error: "providerRequest" }); return; }
    setKernel({ state: "loading" });
    try {
      let view: Record<string, unknown>;
      if (which === "next") {
        const d = currentDraft();
        const c = d.conv || conv;
        view = await api.providerRequest({ preview: true, message: d.text, ...(c && { conv_id: c }), ...(!c && { new_conversation: true }) });
      } else if (conv) {
        view = await api.providerRequest({ conv_id: conv });
      } else {
        view = { kind: "none" };
      }
      setKernel({ state: "ok", view });
    } catch (e) {
      setKernel({ state: "error", error: e instanceof Error ? e.message : String(e) });
    }
  }, [api, conv, which]);
  useEffect(() => { if (tab === "kernel") void ask(); }, [tab, ask, tick]);

  const tabs = (
    <div className="ri-tabs" role="tablist">
      <button role="tab" aria-selected={tab === "page"} data-testid="ri-tab-page" onClick={() => setTab("page")}>{t("ri_tab_page")}</button>
      <button role="tab" aria-selected={tab === "kernel"} data-testid="ri-tab-kernel" onClick={() => setTab("kernel")}>{t("ri_tab_kernel")}</button>
    </div>
  );

  const page = !sent ? <p>{t("ri_empty")}</p> : <>
    <p>{t("ri_intro", { when: new Date(sent.at).toLocaleTimeString() })}</p>
    {sent.prepared && <p data-testid="request-inspector-prepared">{t("ri_preview")}</p>}
    <ul data-testid="request-inspector-words">{requestLines(sent.request).map((l) => <li key={l.id} data-line={l.id}>{t(l.id, l.vars)}</li>)}</ul>
    <details data-testid="request-inspector-details"><summary>{t("ri_details")}</summary>
      <pre data-testid="request-inspector-json">{JSON.stringify(sent.request, null, 2)}</pre>
    </details>
  </>;

  const view = kernel.view;
  const kernelBody = (
    <div data-testid="request-inspector-kernel" data-state={kernel.state} data-kind={view ? String(view.kind ?? "") : ""}>
      <div className="ri-which">
        <button aria-pressed={which === "last"} data-testid="ri-kernel-last" onClick={() => setWhich("last")}>{t("ri_k_last")}</button>
        <button aria-pressed={which === "next"} data-testid="ri-kernel-next" onClick={() => setWhich("next")}>{t("ri_k_next")}</button>
        <button data-testid="ri-kernel-refresh" onClick={() => void ask()}>{t("ri_k_refresh")}</button>
      </div>
      {which === "next" && <p>{t("ri_k_next_hint")}</p>}
      {kernel.state === "loading" && <p>{t("ri_k_loading")}</p>}
      {kernel.state === "error" && <p role="alert">{t("ri_k_error", { error: kernel.error ?? "" })}</p>}
      {kernel.state === "ok" && view && view.kind === "none" && <p data-testid="ri-kernel-none">{t("ri_k_none")}</p>}
      {kernel.state === "ok" && view && view.kind !== "none" && <>
        <ul data-testid="ri-kernel-words">{kernelLines(view).map((l, i) => <li key={`${l.id}-${i}`} data-line={l.id}>{t(l.id, l.vars)}</li>)}</ul>
        <details data-testid="ri-kernel-details"><summary>{t("ri_details")}</summary>
          <pre data-testid="ri-kernel-json">{JSON.stringify(view, null, 2)}</pre>
        </details>
      </>}
    </div>
  );

  return (
    <section className="request-inspector" data-testid="request-inspector" data-state={!sent ? "empty" : sent.prepared ? "prepared" : "sent"} data-tab={tab}>
      <h3>{t("ri_title")}</h3>
      {tabs}
      {tab === "page" ? page : kernelBody}
    </section>
  );
}
