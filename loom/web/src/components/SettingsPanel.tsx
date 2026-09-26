import { useCallback, useEffect, useState } from "react";
import { api } from "../api";
import type { ConfigMap, ModelInfo } from "../api/types";

function SecretField({ name, label }: { name: string; label: string }) {
  const [has, setHas] = useState<boolean | null>(null);
  const [value, setValue] = useState("");
  const [saving, setSaving] = useState(false);

  const refresh = useCallback(() => {
    api
      .hasSecret(name)
      .then(setHas)
      .catch(() => setHas(null));
  }, [name]);

  useEffect(refresh, [refresh]);

  const save = useCallback(async () => {
    if (!value) return;
    setSaving(true);
    try {
      await api.setSecret(name, value);
      setValue("");
      refresh();
    } finally {
      setSaving(false);
    }
  }, [name, value, refresh]);

  const clear = useCallback(async () => {
    await api.deleteSecret(name);
    refresh();
  }, [name, refresh]);

  return (
    <div className="form-row">
      <label htmlFor={`secret-${name}`}>
        {label} {has ? <span className="pill ok">set</span> : <span className="pill">not set</span>}
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
          <button className="danger" onClick={clear}>
            Clear
          </button>
        )}
      </div>
    </div>
  );
}

export default function SettingsPanel() {
  const [config, setConfig] = useState<ConfigMap | null>(null);
  const [models, setModels] = useState<ModelInfo[]>([]);
  const [saving, setSaving] = useState(false);
  const [authToken, setAuthToken] = useState("");

  const loadConfig = useCallback(() => {
    api.getConfig().then(setConfig).catch(() => {});
  }, []);

  useEffect(loadConfig, [loadConfig]);
  useEffect(() => {
    api.getModels().then(setModels).catch(() => {});
  }, []);

  const patch = useCallback(
    async (key: string, value: unknown) => {
      setSaving(true);
      try {
        const updated = await api.setConfigKey(key, value);
        setConfig(updated);
      } finally {
        setSaving(false);
      }
    },
    [],
  );

  const refreshModels = useCallback(async () => {
    await api.refreshModels();
    setModels(await api.getModels());
  }, []);

  if (!config) return <div className="empty-state">Loading…</div>;

  return (
    <div data-testid="settings-panel">
      <div className="section-title">Connection</div>
      <div className="form-row">
        <label htmlFor="cfg-base-url">Base URL</label>
        <input
          id="cfg-base-url"
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
          type="range"
          min={0}
          max={2}
          step={0.1}
          defaultValue={Number(config.temperature ?? 0.7)}
          onChange={(e) => patch("temperature", Number(e.target.value))}
        />
      </div>
      {saving && <div className="empty-state">Saving…</div>}

      <div className="section-title">API keys</div>
      <SecretField name="api_key" label="OpenRouter API key (chat)" />
      <SecretField name="anthropic_batch_key" label="Anthropic batch key (semantic)" />
      <SecretField name="github_token" label="GitHub token (sync)" />

      <div className="section-title">Models</div>
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
