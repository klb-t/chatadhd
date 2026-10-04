import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { ConfigMap, ModelInfo } from "../api/types";
import UsagePolicyPanel from "./UsagePolicyPanel";

function errorText(error: unknown) { return error instanceof Error ? error.message : String(error); }
const credentialName = /^(?:api_key|.*_api_key|anthropic_batch_key|github_token|access_token|refresh_token|auth_token|bearer_token|password|client_secret|secret)$/i;
function containsCredential(value: unknown): boolean {
  if (Array.isArray(value)) return value.some(containsCredential);
  if (!value || typeof value !== "object") return false;
  return Object.entries(value).some(([key, child]) => credentialName.test(key) || containsCredential(child));
}
function redactCredentials(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(redactCredentials);
  if (!value || typeof value !== "object") return value;
  return Object.fromEntries(Object.entries(value).map(([key, child]) => [key, credentialName.test(key) ? "[credential hidden]" : redactCredentials(child)]));
}

function SecretField({ name, label }: { name: string; label: string }) {
  const [has, setHas] = useState<boolean | null>(null);
  const [value, setValue] = useState("");
  const [saving, setSaving] = useState(false);
  const [error, setError] = useState("");

  const refresh = useCallback(() => {
    api
      .hasSecret(name)
      .then(value => { setHas(value); setError(""); })
      .catch(cause => { setHas(null); setError(errorText(cause)); });
  }, [name]);

  useEffect(refresh, [refresh]);

  const save = useCallback(async () => {
    if (!value) return;
    setSaving(true);
    setError("");
    try {
      await api.setSecret(name, value);
      setValue("");
      refresh();
    } catch (cause) {
      setError(errorText(cause));
    } finally {
      setSaving(false);
    }
  }, [name, value, refresh]);

  const clear = useCallback(async () => {
    setSaving(true); setError("");
    try { await api.deleteSecret(name); refresh(); }
    catch (cause) { setError(errorText(cause)); }
    finally { setSaving(false); }
  }, [name, refresh]);

  return (
    <div className="form-row">
      <label htmlFor={`secret-${name}`}>
        {label} {has ? <span className="pill ok">set</span> : <span className="pill">{has == null ? "unknown" : "not set"}</span>}
      </label>
      <div style={{ display: "flex", gap: 6 }}>
        <input
          id={`secret-${name}`}
          type="password"
          placeholder={has ? "•••••••• (leave blank to keep)" : "paste key…"}
          value={value}
          onChange={(e) => setValue(e.target.value)}
          data-testid={`secret-${name}`}
        />
        <button onClick={save} disabled={saving || !value} data-testid={`secret-${name}-save`}>
          Save
        </button>
        {has && (
          <button className="danger" onClick={clear} disabled={saving}>
            Clear
          </button>
        )}
      </div>
      {error && <p role="alert" data-testid={`secret-${name}-error`}>{error}</p>}
    </div>
  );
}

export default function SettingsPanel() {
  const [config, setConfig] = useState<ConfigMap | null>(null);
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [saving, setSaving] = useState(false);
  const [authToken, setAuthToken] = useState("");
  const [loadError, setLoadError] = useState("");
  const [saveError, setSaveError] = useState("");
  const [modelError, setModelError] = useState("");
  const [configDraft, setConfigDraft] = useState("{}");
  const [configNotice, setConfigNotice] = useState("");

  const loadConfig = useCallback(() => {
    setLoadError("");
    api.getConfig().then(setConfig).catch(cause => setLoadError(errorText(cause)));
  }, []);

  useEffect(loadConfig, [loadConfig]);
  useEffect(() => {
    api.getModels().then(setModels).catch(cause => setModelError(errorText(cause)));
  }, []);

  const patch = useCallback(
    async (key: string, value: unknown) => {
      setSaving(true);
      setSaveError("");
      try {
        const updated = await api.setConfigKey(key, value);
        setConfig(updated);
      } catch (cause) {
        setSaveError(errorText(cause));
      } finally {
        setSaving(false);
      }
    },
    [],
  );

  const refreshModels = useCallback(async () => {
    setModelError("");
    try { await api.refreshModels(); setModels(await api.getModels()); }
    catch (cause) { setModelError(errorText(cause)); }
  }, []);

  const saveConfigDraft = useCallback(async () => {
    setSaving(true); setSaveError(""); setConfigNotice("");
    try {
      const parsed: unknown = JSON.parse(configDraft);
      if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error("Configuration patch must be a JSON object.");
      if (containsCredential(parsed)) throw new Error("Store credentials in the API key fields below, rather than configuration JSON. Your draft has been preserved.");
      setConfig(await api.setConfig(parsed as ConfigMap));
      setConfigNotice("Configuration patch saved. Your draft is preserved for review.");
    } catch (cause) { setSaveError(errorText(cause)); }
    finally { setSaving(false); }
  }, [configDraft]);

  if (!config) return <div className="settings-errors" data-testid="settings-panel">
    {loadError ? <><p role="alert" data-testid="settings-load-error">Configuration could not be loaded: {loadError}</p><button onClick={loadConfig}>Retry configuration</button></> : <div className="empty-state">Loading…</div>}
    <div className="form-row"><label htmlFor="settings-auth-recovery">Server bearer token</label><input id="settings-auth-recovery" type="password" value={authToken} onChange={event => setAuthToken(event.target.value)} /><button onClick={() => { api.setAuthToken(authToken || null); setAuthToken(""); loadConfig(); }}>Apply server token and retry</button></div>
  </div>;

  return (
    <div data-testid="settings-panel">
      <div className="settings-errors">
        {loadError && <p role="alert" data-testid="settings-load-error">Configuration refresh failed: {loadError}</p>}
        {saveError && <p role="alert" data-testid="settings-save-error">{saveError}</p>}
      </div>
      <div className="section-title">Connection</div>
      <div className="form-row">
        <label htmlFor="cfg-base-url">Base URL</label>
        <input
          id="cfg-base-url"
          key={String(config.base_url ?? "")}
          type="text"
          defaultValue={String(config.base_url ?? "")}
          onBlur={(e) => patch("base_url", e.target.value)}
          data-testid="cfg-base-url"
        />
      </div>
      <div className="form-row">
        <label htmlFor="cfg-default-model">Default model</label>
        <input
          id="cfg-default-model"
          key={String(config.default_model ?? "")}
          type="text"
          defaultValue={String(config.default_model ?? "")}
          onBlur={(e) => patch("default_model", e.target.value)}
          data-testid="cfg-default-model"
        />
      </div>
      <div className="form-row">
        <label htmlFor="cfg-semantic-model">Semantic model (background analysis, empty = off)</label>
        <input
          id="cfg-semantic-model"
          key={String(config.semantic_model ?? "")}
          type="text"
          defaultValue={String(config.semantic_model ?? "")}
          onBlur={(e) => patch("semantic_model", e.target.value)}
          data-testid="cfg-semantic-model"
        />
      </div>
      <div className="form-row">
        <label>
          <input
            type="checkbox"
            key={String(config.stream ?? true)}
            defaultChecked={Boolean(config.stream ?? true)}
            onChange={(e) => patch("stream", e.target.checked)}
          />{" "}
          Stream responses
        </label>
      </div>
      <div className="form-row">
        <label>
          <input
            type="checkbox"
            key={String(config.semantic_analysis ?? true)}
            defaultChecked={Boolean(config.semantic_analysis ?? true)}
            onChange={(e) => patch("semantic_analysis", e.target.checked)}
          />{" "}
          Background semantic analysis
        </label>
      </div>
      <div className="form-row">
        <label htmlFor="cfg-temp">Temperature: {String(config.temperature ?? 0.7)}</label>
        <input
          id="cfg-temp"
          key={String(config.temperature ?? 0.7)}
          type="number"
          step="any"
          defaultValue={Number(config.temperature ?? 0.7)}
          onBlur={(e) => { if (e.target.value !== "") patch("temperature", Number(e.target.value)); }}
        />
      </div>
      {saving && <div className="empty-state">Saving…</div>}

      <section className="expert-config" data-testid="expert-config">
        <div className="section-title">Expert configuration</div>
        <p>Submit any configuration keys as a JSON object. Top-level values are replaced; nested objects are not recursively merge-patched. Null is a stored value. Keep credentials in the separate secret fields.</p>
        <details><summary>Current configuration (credential fields hidden)</summary><pre data-testid="expert-config-current">{JSON.stringify(redactCredentials(config), null, 2)}</pre></details>
        <div className="form-row"><label htmlFor="expert-config-json">Configuration patch</label><textarea id="expert-config-json" className="usage-json-editor" rows={8} value={configDraft} onChange={event => setConfigDraft(event.target.value)} data-testid="expert-config-json" /></div>
        <button onClick={saveConfigDraft} disabled={saving} data-testid="expert-config-save">Save configuration patch</button>
        {configNotice && <p role="status">{configNotice}</p>}
      </section>

      <UsagePolicyPanel onConfigSaved={setConfig} />

      <div className="section-title">API keys</div>
      <SecretField name="api_key" label="OpenRouter API key (chat)" />
      <SecretField name="anthropic_batch_key" label="Anthropic batch key (semantic)" />
      <SecretField name="github_token" label="GitHub token (sync)" />

      <div className="section-title">Models</div>
      {modelError && <p role="alert" data-testid="settings-model-error">{modelError}</p>}
      <button onClick={refreshModels} data-testid="refresh-models">
        Refresh model list
      </button>
      <div style={{ maxHeight: 200, overflowY: "auto", marginTop: 8 }}>
        {models.length === 0 && <div className="empty-state">No models cached yet.</div>}
        {models.map((m) => (
          <div key={m.id} className="log-line">
            {m.id}
          </div>
        ))}
      </div>

      <div className="section-title">Server access (web only)</div>
      <div className="form-row">
        <label htmlFor="auth-token">Bearer token (if LOOM_SERVER_TOKEN is set)</label>
        <div style={{ display: "flex", gap: 6 }}>
          <input
            id="auth-token"
            type="password"
            value={authToken}
            onChange={(e) => setAuthToken(e.target.value)}
            data-testid="auth-token"
          />
          <button
            onClick={() => {
              api.setAuthToken(authToken || null);
              setAuthToken("");
              loadConfig();
            }}
          >
            Apply
          </button>
        </div>
      </div>
    </div>
  );
}
