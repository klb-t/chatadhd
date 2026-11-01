import { useEffect, useMemo, useState, type KeyboardEvent } from "react";
import { api } from "../api";
import type { ViewModel, ViewRendererInfo } from "../api/view-model";
import type { SceneCameraCommand, SceneViewState } from "../view-engine/scene3d";
import { projectSpatial, readSpatialConfig } from "../view-engine/spatial";
import ViewScene3D from "./ViewScene3D";
import { NAV_VIS, visText } from "./navigator-visibility";

interface Props {
  model: ViewModel;
  selected: string | null;
  language: string;
  onPick: (id: string) => void;
  onUnavailable: (reason: string) => void;
}

/** Reuses the installed view engine on the navigator's exact model; no evaluateView/getNodes or ID guessing. */
export default function NavigatorMap3D({ model, selected, language, onPick, onUnavailable }: Props) {
  const [entries, setEntries] = useState<ViewRendererInfo[] | null>(null);
  const [rendererId, setRendererId] = useState("");
  const [fov, setFov] = useState(NAV_VIS.defaults.fov);
  const [command, setCommand] = useState<SceneCameraCommand>();
  useEffect(() => {
    let live = true;
    api.listViewDeclarations().then(list => {
      if (!live) return;
      const found = list.renderers.filter(r => r.entry?.engine === "three" && r.entry.implemented !== false);
      if (!found.length) { onUnavailable(visText("noRenderer")); return; }
      setEntries(found); setRendererId(found[0].id);
    }, cause => { if (live) onUnavailable(cause instanceof Error ? cause.message : String(cause)); });
    return () => { live = false; };
  }, []); // eslint-disable-line react-hooks/exhaustive-deps
  const entry = useMemo(() => {
    const e = entries?.find(e => e.id === rendererId);
    // This embed offers screen navigation only. The general panel still owns XR bindings and sessions.
    return e ? { ...e, entry: { ...e.entry, default_mode: "orbit", modes: Object.fromEntries(Object.entries(e.entry?.modes ?? {}).filter(([id, m]) => m.kind === "screen" && id === "orbit")) } } : null;
  }, [entries, rendererId]);
  const spatial = useMemo(() => entry ? projectSpatial(model, readSpatialConfig(entry.entry)) : null, [model, entry]);
  const view = useMemo<SceneViewState>(() => ({ panX: 0, panY: 0, zoom: 1, angle: 0, labels: false, camera: { fovDeg: fov, command } }), [fov, command]);
  const send = (kind: SceneCameraCommand["kind"], x?: number, y?: number) => setCommand(c => ({ seq: (c?.seq ?? 0) + 1, kind, x, y }));
  const setField = (value: number) => setFov(Math.max(NAV_VIS.ranges.fov[0], Math.min(NAV_VIS.ranges.fov[1], value)));
  const onKeyDown = (e: KeyboardEvent) => {
    if (e.target instanceof HTMLElement && /^(INPUT|SELECT|BUTTON|TEXTAREA)$/.test(e.target.tagName)) return;
    const binding = (NAV_VIS.keys as Record<string, { kind: string; x?: number; y?: number }>)[e.key];
    if (!binding || e.altKey || e.ctrlKey || e.metaKey) return;
    e.preventDefault(); e.stopPropagation();
    if (binding.kind === "fov") setField(fov + (binding.x ?? 0) * NAV_VIS.ranges.fov[2]);
    else if (binding.kind === "orbit") {
      const kind = e.shiftKey ? "pan" : "orbit", step = e.shiftKey ? NAV_VIS.motion.pan_step : NAV_VIS.motion.orbit_step;
      send(kind, (binding.x ?? 0) * step, (binding.y ?? 0) * step);
    } else if (binding.kind === "dolly") send("dolly", NAV_VIS.motion.dolly_step ** (binding.x ?? 0));
    else send(binding.kind as SceneCameraCommand["kind"]);
  };
  const positioned = selected !== null && spatial?.marks.some(m => m.id === selected);
  return <div className="gnav-spatial" data-testid="nav-spatial" data-selected-id={selected ?? ""} tabIndex={0} onKeyDown={onKeyDown}>
    {!entry && <p role="status">{visText("loading")}</p>}
    {entries && entries.length > 1 && <select aria-label={visText("renderer")} value={rendererId} onChange={e => setRendererId(e.target.value)}>{entries.map(e => <option key={e.id}>{e.id}</option>)}</select>}
    <div className="gnav-camera-controls">
      <label>{visText("fov")} <output>{fov}°</output><input data-testid="nav-fov" type="range" min={NAV_VIS.ranges.fov[0]} max={NAV_VIS.ranges.fov[1]} step={NAV_VIS.ranges.fov[2]} value={fov} onChange={e => setField(Number(e.target.value))} /></label>
      {NAV_VIS.camera_buttons.map(b => <button key={b.word} type="button" data-testid={`nav-3d-${b.word}`} onClick={() => {
        const step = b.kind === "pan" ? NAV_VIS.motion.pan_step : NAV_VIS.motion.orbit_step;
        send(b.kind as SceneCameraCommand["kind"], b.kind === "dolly" ? NAV_VIS.motion.dolly_step ** (b.x ?? 0) : (b.x ?? 0) * step, (b.y ?? 0) * step);
      }}>{visText(b.word as keyof typeof NAV_VIS.words)}</button>)}
      <button type="button" onClick={() => send("focus")} disabled={!positioned}>{visText("center")}</button>
      <button type="button" onClick={() => send("fit")}>{visText("fit")}</button>
      <button type="button" onClick={() => { setFov(NAV_VIS.defaults.fov); send("reset"); }}>{visText("home")}</button>
    </div>
    <p className="gnav-note">{visText("cameraHelp")}</p>
    {selected !== null && !positioned && spatial && <p role="status">{visText("noPosition")}</p>}
    {spatial && <p className="gnav-note" data-testid="nav-3d-unplaced">{visText("unplaced3d", { count: spatial.unplaced.length, total: model.elements.length })}</p>}
    {entry && <ViewScene3D entry={entry} model={model} view={view} selected={selected} language={language}
      hintLines={() => []} onPick={id => { if (id !== null) onPick(id); }} onXr={() => {}} onXrButtons={() => {}} onUnavailable={onUnavailable} />}
  </div>;
}
