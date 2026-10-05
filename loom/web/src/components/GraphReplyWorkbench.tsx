import { useEffect, useMemo, useRef, useState } from "react";
import { inspectReply, packetFailure, packetResult, parseObject, record, type GraphReplyAction, type GraphReplyFragmentAddress, type PacketCommandApi } from "../graph/reply-inspection";
import "./graph-reply-workbench.css";

export interface GraphReplyWorkbenchProps {
  packet?: PacketCommandApi;
  usagePolicy?: PacketCommandApi;
  responseText?: string;
  conversationId?: string;
  turnId?: string;
  requestId?: string;
  model?: string;
  onAddressFragment?: (action: GraphReplyAction, fragment: GraphReplyFragmentAddress) => void;
}
type Operation = "make" | "compile_reply" | "validate_compilation" | "apply_compiled_reply";
type Pending = { command: Record<string, unknown>; decision: Record<string, unknown>; operation: Operation };
const json = (value: unknown) => JSON.stringify(value, null, 2);
const errorText = (failure: unknown) => failure instanceof Error ? failure.message : String(failure);
const initialHost = (props: GraphReplyWorkbenchProps) => json({ request_id: props.requestId ?? "", turn_id: props.turnId ?? "", model: props.model ?? "", parent_turn_id: null, recipe_sha256: null });

export default function GraphReplyWorkbench(props: GraphReplyWorkbenchProps) {
  const { packet, usagePolicy, onAddressFragment } = props;
  const [raw, setRaw] = useState(props.responseText ?? "");
  const [packetJson, setPacketJson] = useState("");
  const [hostJson, setHostJson] = useState(() => initialHost(props));
  const [acceptance, setAcceptance] = useState<"preview" | "auto">("preview");
  const [guard, setGuard] = useState(false);
  const [estimateJson, setEstimateJson] = useState(json({ baseline_key: "packet:graph-reply", resources: { input_bytes: null, calls: 1, money_usd: 0 } }));
  const [compiled, setCompiled] = useState<Record<string, unknown> | null>(null);
  const [result, setResult] = useState<Record<string, unknown> | null>(null);
  const [pending, setPending] = useState<Pending | null>(null);
  const [busy, setBusy] = useState(false);
  const [status, setStatus] = useState("");
  const [error, setError] = useState("");
  const [responseEnvelope, setResponseEnvelope] = useState<Record<string, unknown> | null>(null);
  const epoch = useRef(0), mounted = useRef(true);
  useEffect(() => { mounted.current = true; return () => { mounted.current = false; epoch.current++; }; }, []);
  useEffect(() => {
    epoch.current++; setBusy(false); setRaw(props.responseText ?? ""); setHostJson(initialHost(props)); setCompiled(null); setResult(null); setPending(null); setResponseEnvelope(null); setStatus(""); setError("");
  }, [props.responseText, props.conversationId, props.turnId, props.requestId, props.model]);
  const fragments = useMemo(() => compiled ? inspectReply(compiled) : [], [compiled]);
  const invalidate = () => { epoch.current++; setCompiled(null); setResult(null); setPending(null); setResponseEnvelope(null); setStatus(""); setError(""); };

  const handleResponse = (operation: Operation, command: Record<string, unknown>, response: Record<string, unknown>) => {
    setResponseEnvelope(response);
    const failure = packetFailure(response);
    if (failure) throw new Error(failure);
    if (response.executed === false) {
      const decision = record(response.usage_decision);
      if (decision.status === "requires_confirmation" && typeof decision.receipt_id === "string" && typeof decision.operation_id === "string") {
        setPending({ command, decision, operation }); setStatus("Usage increased by the policy threshold. Review the exact receipt before confirming.");
      } else { setPending(null); setError(`Operation was not executed: ${typeof decision.status === "string" ? decision.status : "usage decision unavailable"}.`); }
      return;
    }
    const next = packetResult(response); setPending(null);
    if (operation === "make") {
      if (next.schema !== "loom.graph_packet/1") throw new Error("Server did not return a GraphPacket.");
      setPacketJson(json(next)); setCompiled(null); setResult(null); setStatus("Empty packet created. Match the reply's base packet before compiling.");
    } else if (operation === "compile_reply" || operation === "validate_compilation") {
      inspectReply(next); setCompiled(next); setResult(null);
      setStatus(operation === "compile_reply" ? "Compiled as a candidate. No graph has been applied." : "Compilation replay validated against the exact captured response.");
    } else {
      if (!record(next.packet).packet_id || !next.receipt) throw new Error("Server did not return an applied packet and receipt.");
      setResult(next); setStatus("Reply applied to a returned packet projection. Review the receipt; this operation does not store it in the knowledge database.");
    }
  };
  const dispatch = async (operation: Operation, originalCommand?: Record<string, unknown>) => {
    if (!packet) { setError("Native packet API is unavailable on this transport."); return; }
    const currentEpoch = ++epoch.current;
    setBusy(true); setError(""); setStatus("");
    try {
      let command = originalCommand;
      if (!command) {
        if (operation === "make") command = { operation, origin: { kind: "user", actor: "graph_reply_workbench", model: null, recipe_sha256: null, response_sha256: null } };
        else {
          const base = parseObject(packetJson, "Base packet");
          if (operation === "compile_reply") command = { operation, packet: base, raw, host: parseObject(hostJson, "Host metadata") };
          else {
            if (!compiled) throw new Error("Compile this response first.");
            command = { operation, packet: base, compilation: compiled };
            if (operation === "apply_compiled_reply") { command.policy = { schema: "loom.graph_packet_apply_policy/1", acceptance, allow_source_tombstones: false }; command.explicitly_accepted = true; }
          }
        }
        if (guard) {
          const estimate = parseObject(estimateJson, "Usage estimate");
          command.usage_estimate = { ...estimate, operation_id: `graph-reply:${crypto.randomUUID()}` };
        }
      }
      const response = await packet(command);
      if (!mounted.current || epoch.current !== currentEpoch) return;
      handleResponse(operation, command, response);
    } catch (failure) {
      if (mounted.current && epoch.current === currentEpoch) { setError(`${errorText(failure)} The raw response is retained.`); if (operation === "compile_reply") setCompiled(null); }
    } finally { if (mounted.current && epoch.current === currentEpoch) setBusy(false); }
  };
  const confirm = async (approved: boolean) => {
    if (!pending || !usagePolicy) return;
    const held = pending, currentEpoch = ++epoch.current;
    setBusy(true); setError("");
    try {
      const response = packetResult(await usagePolicy({ action: "confirm", operation_id: held.decision.operation_id, receipt_id: held.decision.receipt_id, approved, confirmation_ref: "graph-reply-workbench:explicit-user-action" }));
      if (!mounted.current || epoch.current !== currentEpoch) return;
      if (!approved) { setPending(null); setStatus("Usage increase declined. The operation was not executed."); return; }
      if (response.authorized !== true) throw new Error("Confirmation did not authorize the held operation.");
      await dispatch(held.operation, held.command);
    } catch (failure) { if (mounted.current && epoch.current === currentEpoch) setError(errorText(failure)); }
    finally { if (mounted.current && epoch.current === currentEpoch) setBusy(false); }
  };
  return <section className="graph-reply-workbench" aria-label="Graph reply workbench" data-testid="graph-reply-workbench">
    <h3>Graph reply workbench</h3>
    <p>Compile an existing model response, inspect its logical parts, and apply it to a packet. Model content remains unverified.</p>
    {!packet && <p role="status">Native packet API is unavailable on this transport. The response remains available below.</p>}
    <details className="gr-original"><summary>Original response</summary><pre data-testid="gr-original-response">{props.responseText ?? ""}</pre></details>
    <label>Raw model response<textarea aria-label="Raw model response" value={raw} spellCheck={false} disabled={busy} onChange={event => { invalidate(); setRaw(event.target.value); }} /></label>
    <details><summary>Base packet and host metadata</summary>
      <label>Base GraphPacket JSON<textarea aria-label="Base GraphPacket JSON" value={packetJson} spellCheck={false} disabled={busy} onChange={event => { invalidate(); setPacketJson(event.target.value); }} /></label>
      <button disabled={!packet || busy} onClick={() => void dispatch("make")}>Create empty packet</button>
      <label>Host metadata JSON<textarea aria-label="Host metadata JSON" value={hostJson} spellCheck={false} disabled={busy} onChange={event => { invalidate(); setHostJson(event.target.value); }} /></label>
      <p>Request ID, turn ID and model identify the actual response. Use a new turn ID for a retry. Parent turns must already exist in the packet.</p>
    </details>
    <details><summary>Usage estimate</summary>
      <label className="gr-check"><input type="checkbox" checked={guard} disabled={busy} onChange={event => { invalidate(); setGuard(event.target.checked); }} />Request the shared usage policy</label>
      <label>Usage estimate JSON<textarea aria-label="Usage estimate JSON" value={estimateJson} disabled={busy} spellCheck={false} onChange={event => { invalidate(); setEstimateJson(event.target.value); }} /></label>
      <p>The estimate is caller supplied; unknown dimensions remain null. An operation ID is generated for each new command. Missing policy support produces a visible error.</p>
    </details>
    <div className="gr-actions"><button disabled={!packet || busy} onClick={() => void dispatch("compile_reply")}>Compile candidate</button>
      <button disabled={!packet || !compiled || busy} onClick={() => void dispatch("validate_compilation")}>Validate compilation</button>
      <label>Apply policy<select aria-label="Graph reply apply policy" value={acceptance} disabled={busy} onChange={event => { setPending(null); setResult(null); setAcceptance(event.target.value as "preview" | "auto"); }}><option value="preview">Candidate / explicit acceptance</option><option value="auto">Automatic acceptance policy</option></select></label>
      <button disabled={!packet || !compiled || busy || !!result} onClick={() => void dispatch("apply_compiled_reply")}>Apply compiled reply</button></div>
    {status && <p role="status" data-testid="gr-status">{status}</p>}
    {error && <p role="alert" data-testid="gr-error">{error}</p>}
    {pending && <section className="gr-confirm" aria-label="Usage confirmation"><h4>Confirm usage increase</h4><pre data-testid="gr-usage-receipt">{json(pending.decision)}</pre>
      {!usagePolicy && <p role="alert">Usage confirmation API is unavailable. This operation remains held.</p>}
      <button disabled={busy || !usagePolicy} onClick={() => void confirm(true)}>Confirm increase and retry exact operation</button><button disabled={busy || !usagePolicy} onClick={() => void confirm(false)}>Decline increase</button></section>}
    {compiled && <><h4>Compiled response</h4><pre data-testid="gr-rendered-response">{String(compiled.response_text)}</pre><p>Origin: model · content unverified · structural validation does not establish truth.</p>
      <div className="gr-fragments">{fragments.map(fragment => <article key={fragment.entity_id} data-testid="gr-fragment"><header><strong>{fragment.local_id}</strong><span>{fragment.role}</span></header><pre>{fragment.text}</pre>
        <p>Code points: {String(fragment.span.char_start)} + {String(fragment.span.char_len)} · UTF-8 bytes: {String(fragment.span.byte_start)} + {String(fragment.span.byte_len)}</p>
        <details><summary>Fragment address and provenance</summary><pre>{json(fragment)}</pre></details>
        <div className="gr-actions"><button disabled={!onAddressFragment || busy} onClick={() => onAddressFragment?.("expand", fragment)}>Expand this node</button><button disabled={!onAddressFragment || busy} onClick={() => onAddressFragment?.("correct", fragment)}>Correct this node</button></div>
      </article>)}</div>
      {!onAddressFragment && <p>Fragment prompting is unavailable here. Addresses remain available for inspection.</p>}
      <details><summary>Compilation JSON</summary><pre data-testid="gr-compilation-json">{json(compiled)}</pre></details></>}
    {result && <details open><summary>Applied packet and receipt</summary><pre data-testid="gr-applied-result">{json(result)}</pre></details>}
    {responseEnvelope && <details><summary>Native response and accounting</summary><pre data-testid="gr-native-response">{json(responseEnvelope)}</pre></details>}
  </section>;
}
