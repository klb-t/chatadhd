import { useEffect, useMemo, useRef, useState } from "react";
import type { ViewModel, ViewRendererInfo, ViewRendererMode } from "../api/view-model";
import { dataLabel, useShellText } from "../shell/text";
import { projectSpatial, readSpatialConfig } from "../view-engine/spatial";
import { detectXr } from "../view-engine/xr-support";
import type { XrSupport } from "../view-engine/xr-support";
import type { SceneHandle, SceneViewState, ScreenMode, XrMode, XrPayload } from "../view-engine/scene3d";

// The 3D renderer of the view model (P28 step 3): the same view model as the 2D panel, drawn by three.js (loaded lazily, so the normal bundle does not
// carry it). Which modes exist (orbit and look-around on a flat screen, immersive VR and AR sessions) is data of the chosen `view_renderers` entry; the
// browser's WebXR support is detected and said out loud - a missing navigator.xr or an unsupported session is a visible note, never a silent fallback.
// What the input MEANS is decided by the panel (bindings, the shared reducer); this component only draws and reports.
export interface HintContext { xr: boolean; mode: string; sector: string | null }
interface Props {
  entry: ViewRendererInfo;
  model: ViewModel;
  view: SceneViewState;
  selected: string | null;
  language: string;
  hintLines: (ctx: HintContext) => string[];
  onPick: (id: string | null) => void;
  onXr: (payload: XrPayload) => void;
  onXrButtons: (down: string[]) => void;
}

export default function ViewScene3D(props: Props) {
  const t = useShellText();
  const { entry, model, language } = props;
  const hostRef = useRef<HTMLDivElement | null>(null);
  const handle = useRef<SceneHandle | null>(null);
  const latest = useRef(props);
  latest.current = props;
  const [support, setSupport] = useState<XrSupport | null>(null);
  const [error, setError] = useState("");
  const [loading, setLoading] = useState(true);
  const [ready, setReady] = useState(0);
  const [mode, setMode] = useState<ScreenMode>(((entry.entry?.default_mode === "look" ? "look" : "orbit")) as ScreenMode);
  const [xr, setXr] = useState<XrMode | null>(null);
  const [xrError, setXrError] = useState("");
  const [sector, setSector] = useState<string | null>(null);

  const cfg = useMemo(() => readSpatialConfig(entry.entry), [entry]);
  const spatial = useMemo(() => projectSpatial(model, cfg), [model, cfg]);
  const modes = useMemo(() => Object.entries(entry.entry?.modes ?? {}) as Array<[string, ViewRendererMode]>, [entry]);
  const screenModes = modes.filter(([, m]) => m.kind === "screen");
  const xrModes = modes.filter(([, m]) => m.kind === "xr");

  useEffect(() => {
    let live = true;
    detectXr(xrModes.map(([, m]) => m.session ?? "").filter(Boolean)).then(found => { if (live) setSupport(found); });
    return () => { live = false; };
  }, [entry]); // eslint-disable-line react-hooks/exhaustive-deps

  // the scene: created once per renderer entry, in a lazily loaded chunk
  useEffect(() => {
    const host = hostRef.current;
    if (!host) return;
    let cancelled = false, created: SceneHandle | null = null;
    setLoading(true); setError("");
    import("../view-engine/scene3d")
      .then(mod => {
        if (cancelled) return;
        created = mod.createScene(host, cfg, (entry.entry ?? {}) as Record<string, never>, {
          onPick: id => latest.current.onPick(id),
          onXr: payload => latest.current.onXr(payload),
          onXrButtons: down => latest.current.onXrButtons(down),
          onGaze: (s: string | null) => setSector(s),
          onSession: (state, m) => { setXr(state === "start" ? m : null); },
        }, (entry.entry as { xr?: unknown } | undefined)?.xr);
        handle.current = created;
        setLoading(false);
        setReady(n => n + 1);
      })
      .catch(cause => { if (!cancelled) { setLoading(false); setError(cause instanceof Error ? cause.message : String(cause)); } });
    return () => { cancelled = true; created?.dispose(); handle.current = null; };
  }, [entry, cfg]);

  useEffect(() => { handle.current?.setSpatial(spatial, props.selected); }, [spatial, ready]); // eslint-disable-line react-hooks/exhaustive-deps
  useEffect(() => { handle.current?.setSelected(props.selected); }, [props.selected, ready]);
  useEffect(() => { handle.current?.setViewState(props.view); }, [props.view, ready]);
  useEffect(() => { handle.current?.setMode(mode); }, [mode, ready]);
  const lines = props.hintLines({ xr: xr !== null, mode: xr ?? mode, sector: sector ? dataLabel(model.groups.find(g => g.id === sector)?.label, language, sector) : null });
  const hintKey = lines.join("\n");
  useEffect(() => { handle.current?.setHint(lines); }, [hintKey, ready]); // eslint-disable-line react-hooks/exhaustive-deps

  const enter = async (id: string, m: ViewRendererMode) => {
    setXrError("");
    try {
      await handle.current?.enter((m.session ?? id) as XrMode, m.features ?? {}, m.transparent_background === true);
    } catch (cause) {
      setXrError(t("ve3d_xr_start_failed", { mode: m.session ?? id, error: cause instanceof Error ? cause.message : String(cause) }));
    }
  };
  const supportedText = (v: boolean | undefined) => (v ? t("ve3d_supported") : t("ve3d_unsupported"));

  return (
    <div data-testid="ve3d" data-renderer={entry.id} data-xr-available={support ? String(support.available) : "checking"}>
      {cfg.notes.length > 0 && <p className="pill" style={{ color: "var(--warn)" }} data-testid="ve3d-config-notes">{t("ve3d_config_notes", { notes: cfg.notes.join("; ") })}</p>}
      <div className="form-row" style={{ flexWrap: "wrap", gap: "0.4em" }}>
        <span>{t("ve3d_screen_mode")}:</span>
        {screenModes.map(([id, m]) => (
          <button key={id} type="button" data-testid={`ve3d-mode-${id}`} aria-pressed={mode === id && xr === null} title={dataLabel(m.meaning, language, "")}
            onClick={() => setMode(id as ScreenMode)} style={{ fontWeight: mode === id ? 700 : 400 }}>{dataLabel(m.label, language, id)}</button>
        ))}
        {xrModes.map(([id, m]) => {
          const ok = support?.sessions[m.session ?? id] === true;
          return (
            <button key={id} type="button" data-testid={`ve3d-enter-${id}`} data-supported={String(ok)} disabled={!ok || xr !== null} onClick={() => void enter(id, m)}
              title={ok ? dataLabel(m.meaning, language, "") : t("ve3d_unsupported")}>{dataLabel(m.label, language, id)}</button>
          );
        })}
        {xr !== null && <button type="button" data-testid="ve3d-end-xr" onClick={() => void handle.current?.end()}>{t("ve3d_end_xr")}</button>}
      </div>
      <p className="pill" data-testid="ve3d-xr-note" role="status" data-kind={support === null ? "checking" : !support.available ? "unavailable" : support.error ? "check_failed" : "status"}>
        {support === null ? "..." : !support.available ? t("ve3d_xr_unavailable")
          : support.error ? t("ve3d_xr_check_failed", { error: support.error })
          : t("ve3d_xr_status", { vr: supportedText(support.sessions["immersive-vr"]), ar: supportedText(support.sessions["immersive-ar"]) })}
        {xr !== null && ` ${t("ve3d_xr_active", { mode: xr })}`}
      </p>
      {xrError && <p className="pill" style={{ color: "var(--err)" }} data-testid="ve3d-xr-error">{xrError}</p>}
      {error && <p className="pill" style={{ color: "var(--err)" }} data-testid="ve3d-error">{t("ve3d_webgl_failed", { error })}</p>}
      {loading && !error && <p className="pill" data-testid="ve3d-loading">{t("ve3d_loading")}</p>}
      <div ref={hostRef} data-testid="ve3d-host" style={{ position: "relative", width: "100%", height: "min(62vh, 520px)", minHeight: 280, borderRadius: "var(--radius)", overflow: "hidden", background: "#0f1218" }} />
    </div>
  );
}
