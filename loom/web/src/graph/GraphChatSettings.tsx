import { nativeJsonFields, patchNativeJson } from "./native-json";
import { useEffect, useRef, useState } from "react";
import { graphChatPresets, graphChatInstallation, instantiateGraphChatPreset, selectGraphChatProfile } from "./chat-graph-presets";
import { methodAcceptance, methodCommand, methodObject, methodPretty, parseMethodObject, type MethodsTransport } from "../methods/graph-methods";

/** Native global settings, deliberately separate from one-call ChatRequest. */
export default function GraphChatSettings({ transport, disabled = false }: { transport: MethodsTransport; disabled?: boolean }) {
  const [snapshot, setSnapshot] = useState<Record<string, unknown> | null>(null);
  const [draft, setDraft] = useState("");
  const [preset, setPreset] = useState("");
  const [libraryTarget, setLibraryTarget] = useState(graphChatInstallation.target);
  const [libraryReceipt, setLibraryReceipt] = useState<Record<string, unknown> | null>(null);
  const [profileId, setProfileId] = useState("");
  const [open, setOpen] = useState(false);
  const [busy, setBusy] = useState(false);
  const [error, setError] = useState("");
  const [status, setStatus] = useState("");
  const epoch = useRef(0), alive = useRef(true);
  useEffect(() => { alive.current = true; return () => { alive.current = false; epoch.current++; }; }, []);
  const install = (result: Record<string, unknown>) => {
    const graph = result.graph_reply;
    if (!graph || typeof graph !== "object" || Array.isArray(graph)) throw new Error("Native chat settings did not return graph_reply.");
    setSnapshot(result); setDraft(typeof result.graph_reply_json === "string" ? result.graph_reply_json : methodPretty(graph));
  };
  const read = async () => {
    const current = ++epoch.current; setBusy(true); setError(""); setStatus("");
    try { const result = await methodCommand(transport, { action: "chat_settings" }); if (alive.current && epoch.current === current) install(result); }
    catch (failure) { if (alive.current && epoch.current === current) setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { if (alive.current && epoch.current === current) setBusy(false); }
  };
  useEffect(() => { if (open && transport.methods && !snapshot) void read(); }, [open, transport.methods]); // Read only after the owner opens these controls.
  let parsed: Record<string, unknown> | null = null;
  try { parsed = parseMethodObject(draft, "Graph reply settings"); } catch { /* Invalid drafts remain editable. */ }
  const update = (path: string[], value: unknown, exactValueJson?: string) => {
    try { setDraft(patchNativeJson(draft, path, value, exactValueJson)); setError(""); setStatus(""); }
    catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); }
  };
  const save = async () => {
    if (!snapshot) return;
    const current = ++epoch.current; setBusy(true); setError(""); setStatus("");
    try {
      const graph = parseMethodObject(draft, "Graph reply settings");
      if ((graph.mode ?? "off") !== "off" && (!Array.isArray(graph.receipt_ids) || !graph.receipt_ids.length)) throw new Error("Install the method definitions into the graph before activating this graph reply configuration.");
      const result = await methodCommand(transport, { action: "set_chat_settings", graph_reply_json: draft, expected_context_execution_sha256: snapshot.context_execution_sha256 });
      if (alive.current && epoch.current === current) { install(result); setStatus("Global graph reply settings saved. Other views and conversations use this same native setting."); }
    } catch (failure) { if (alive.current && epoch.current === current) setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { if (alive.current && epoch.current === current) setBusy(false); }
  };
  const installProfile = async () => {
    const current = ++epoch.current; setBusy(true); setError(""); setStatus("");
    try {
      const graph = parseMethodObject(draft, "Graph reply settings");
      const profile = methodObject(graph.profile);
      if (!Object.keys(profile).length || !libraryTarget.trim()) throw new Error("Method profile and a graph library target are required.");
      const profileJson = nativeJsonFields(draft).find(field => field.key === "profile")?.raw;
      if (!profileJson) throw new Error("Method profile JSON is unavailable.");
      const preview = await methodCommand(transport, { action: "profile_preview", profile_json: profileJson, receipt_ids: Array.isArray(graph.receipt_ids) ? graph.receipt_ids : [], target: libraryTarget, actor: graphChatInstallation.actor, known_at: new Date().toISOString() });
      if (!alive.current || epoch.current !== current) return;
      methodAcceptance(preview);
      const accepted = await methodCommand(transport, typeof preview.accept_request_json === "string"
        ? { action: "accept", request_json: preview.accept_request_json }
        : { action: "accept", request: methodAcceptance(preview) });
      if (!alive.current || epoch.current !== current) return;
      const receipt = methodObject(accepted.receipt);
      if (typeof receipt.id !== "string" || !receipt.id || receipt.acceptance_establishes_content_truth !== false) throw new Error("Native graph acceptance did not return a method library receipt.");
      setLibraryReceipt(accepted);
      setDraft(patchNativeJson(patchNativeJson(draft, ["profile"], preview.profile, typeof preview.profile_json === "string" ? preview.profile_json : undefined), ["receipt_ids"], [receipt.id]));
      setStatus("Method definitions installed in the graph. Global chat mode is unchanged; save the configuration to activate this exact receipt.");
    } catch (failure) { if (alive.current && epoch.current === current) setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { if (alive.current && epoch.current === current) setBusy(false); }
  };
  const applyPreset = async () => {
    const current = ++epoch.current; setBusy(true); setError(""); setStatus("");
    try { const options = await instantiateGraphChatPreset(preset); if (alive.current && epoch.current === current) { setDraft(methodPretty(options)); setStatus("Preset loaded into the draft. Review the data before saving. No native configuration has changed."); } }
    catch (failure) { if (alive.current && epoch.current === current) setError(failure instanceof Error ? failure.message : String(failure)); }
    finally { if (alive.current && epoch.current === current) setBusy(false); }
  };
  const profiles = Array.isArray(snapshot?.profiles) ? snapshot.profiles.map(methodObject) : [];
  const modes = Array.isArray(snapshot?.modes) ? snapshot.modes.filter(value => typeof value === "string") as string[] : [];
  const labels: Record<string, string> = { off: "Off", answer_as_graph: "Answer as graph", text_plus_JSONgraph: "Text plus JSON graph", separate_model_afterwards: "Separate model after the answer" };
  return <details className="chat-context-controls" data-testid="graph-chat-settings" onToggle={event => setOpen(event.currentTarget.open)}><summary>Graph reply mode · global native setting{!snapshot && " · not loaded"}</summary>
    <p>This native setting is shared by all views and conversations. Save before sending; changing the draft does not send a message or call a model.</p>
    {!transport.methods && <p role="status">Native graph chat settings are unavailable in this host.</p>}
    {transport.methods && !snapshot && <p role="status">{busy ? "Reading native global configuration…" : "Native graph reply configuration has not been read."}</p>}
    <button onClick={() => void read()} disabled={!transport.methods || busy || disabled}>{snapshot ? "Reload" : "Read"} global graph settings</button>
    {snapshot && <fieldset disabled={busy || disabled}>
      <label>Data preset<select aria-label="Graph reply data preset" value={preset} onChange={event => setPreset(event.target.value)}><option value="">Choose a data preset</option>{graphChatPresets.map(value => <option key={value.id} value={value.id}>{value.label}</option>)}</select></label>
      <button disabled={!preset} onClick={() => void applyPreset()}>Load graph preset into draft</button>
      <label>Saved method profile<select aria-label="Saved graph method profile" value={profileId} onChange={event => setProfileId(event.target.value)}><option value="">Choose a saved native profile</option>{profiles.map(value => <option key={String(value.id)} value={String(value.id)}>{String(value.label ?? value.id)}</option>)}</select></label>
      <button disabled={!profileId || !parsed} onClick={() => { try { const entry = profiles.find(value => value.id === profileId); if (!entry || !parsed) return; const chosen = selectGraphChatProfile(parsed, entry); const withProfile = patchNativeJson(draft, ["profile"], chosen.profile, typeof entry.profile_json === "string" ? entry.profile_json : undefined); setDraft(patchNativeJson(withProfile, ["receipt_ids"], chosen.receipt_ids ?? [])); setStatus(""); } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); } }}>Use saved method profile in draft</button>
      <label>Graph reply mode<select aria-label="Graph reply mode" value={typeof parsed?.mode === "string" ? parsed.mode : "off"} disabled={!parsed || !modes.length} onChange={event => update(["mode"], event.target.value)}>
        {typeof parsed?.mode === "string" && !modes.includes(parsed.mode) && <option value={parsed.mode}>{parsed.mode} · configured value</option>}
        {modes.map(mode => <option key={mode} value={mode}>{labels[mode] ?? mode}</option>)}
      </select></label>
      <label><input type="checkbox" aria-label="Authorize graph model calls when sending" checked={methodObject(parsed?.transport).calls_authorized === true} disabled={!parsed} onChange={event => (() => { try { let next = patchNativeJson(draft, ["transport", "calls_authorized"], event.target.checked); if (parsed?.mode === "separate_model_afterwards") next = patchNativeJson(next, ["postprocess_transport", "calls_authorized"], event.target.checked); setDraft(next); setStatus(""); setError(""); } catch (failure) { setError(failure instanceof Error ? failure.message : String(failure)); } })()} />Authorize graph model calls when sending (native usage guard still applies)</label>
      {parsed?.mode === "separate_model_afterwards" && <label>Postprocess provider ID<input aria-label="Graph postprocess provider ID" value={String(methodObject(parsed.postprocess_transport).provider_id ?? "")} onChange={event => update(["postprocess_transport", "provider_id"], event.target.value)} /></label>}
      <label>Admission mode<select aria-label="Graph admission mode" value={String(methodObject(parsed?.admission).mode ?? "")} disabled={!parsed} onChange={event => update(["admission", "mode"], event.target.value)}>
        <option value="">Unspecified · native capability check</option><option value="candidate">Keep as candidate</option><option value="automatic">Automatic native admission</option>
      </select></label>
      <label>Full graph reply settings JSON<textarea aria-label="Full graph reply settings JSON" value={draft} spellCheck={false} rows={12} onChange={event => { setDraft(event.target.value); setStatus(""); }} /></label>
      <p>Profile definitions, method/recipe selections, parameter layers, base graph context, provider/model bindings, resource estimates and store admission remain native JSON data. No browser preset replaces omitted fields. Automatic admission requires its explicit native store request.</p>
      <label>Graph method library target<input aria-label="Graph method library target" value={libraryTarget} onChange={event => setLibraryTarget(event.target.value)} /></label>
      <button disabled={!parsed || !Object.keys(methodObject(parsed.profile)).length || !libraryTarget.trim()} onClick={() => void installProfile()}>Install method definitions into graph</button>
      {libraryReceipt && <details><summary>Installed method graph receipt</summary><pre data-testid="graph-chat-library-receipt">{methodPretty(libraryReceipt)}</pre></details>}
      <button onClick={() => void save()} disabled={!parsed} data-testid="save-graph-chat-settings">Save global graph settings</button>
      <details><summary>Native settings receipt</summary><pre data-testid="graph-chat-settings-receipt">{methodPretty(snapshot)}</pre></details>
    </fieldset>}
    {status && <p role="status" data-testid="graph-chat-settings-status">{status}</p>}{error && <p role="alert" data-testid="graph-chat-settings-error">{error}</p>}
  </details>;
}
