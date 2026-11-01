import { effectiveMapping, channelWords, measureWords, NAV_MAPPING, type ChannelReport } from "./navigator-mapping";
import { NAV_VIS, visText, localHome, type Axis, type LocalView } from "./navigator-visibility";

interface Props {
  value: LocalView;
  maxDepth: number | null;
  mapping: unknown;
  language: string;
  report?: ChannelReport[];
  onChange: (next: LocalView) => void;
}
export default function NavigatorMapControls({ value, maxDepth, mapping, language, report, onChange }: Props) {
  const update = (part: Partial<LocalView>) => onChange({ ...value, ...part });
  return <details className="gnav-visual-controls" open>
    <summary>{visText("title")}</summary>
    <label>{visText("profile")}<select value={NAV_VIS.profiles.find(p => p.system === value.system && p.x === value.x && p.y === value.y && p.z === value.z)?.id ?? ""}
      onChange={e => { const p = NAV_VIS.profiles.find(p => p.id === e.target.value); if (p) update({ system: p.system, x: p.x as Axis, y: p.y as Axis, z: p.z as Axis }); }}>
      <option value="" disabled>{visText("profileCustom")}</option>{NAV_VIS.profiles.map(p => <option key={p.id} value={p.id}>{p.label}</option>)}
    </select></label>
    {value.system === "cartesian" && (["x", "y", "z"] as const).map(axis => <label key={axis}>{visText("axis", { axis: axis.toUpperCase() })}
      <select value={value[axis]} onChange={e => update({ [axis]: e.target.value as Axis })}>
        {Object.entries(NAV_VIS.axes).filter(([id]) => axis !== "z" || NAV_VIS.z_axes.includes(id)).map(([id, a]) => <option key={id} value={id}>{a.label} ({a.unit})</option>)}
      </select></label>)}
    <label>{visText("reach")} <output>{maxDepth === null ? "—" : (value.reach ?? maxDepth).toFixed(2)}</output>
      <input data-testid="nav-local-reach" type="range" min={0} max={Math.max(maxDepth ?? 0, value.reach ?? 0, 1)} step={0.01} disabled={maxDepth === null}
        value={value.reach ?? maxDepth ?? 0} onChange={e => update({ reach: Number(e.target.value) })} /></label>
    {(["softness", "floor"] as const).map(key => <label key={key}>{visText(key)} <output>{value[key].toFixed(2)}</output>
      <input data-testid={`nav-local-${key}`} type="range" min={NAV_VIS.ranges[key][0]} max={NAV_VIS.ranges[key][1]} step={NAV_VIS.ranges[key][2]}
        value={value[key]} onChange={e => update({ [key]: Number(e.target.value) })} /></label>)}
    <button type="button" onClick={() => onChange(localHome())}>{visText("reset")}</button>
    <p className="gnav-note">{visText("local")}</p>
    <details><summary>{visText("legend")}</summary>
      <p>{value.system === "sector" ? visText("radius") : (["x", "y", "z"] as const).map(a => `${a.toUpperCase()} ← ${NAV_VIS.axes[value[a]].label} (${NAV_VIS.axes[value[a]].unit})`).join(" · ")}</p>
      <ul>{Object.entries(effectiveMapping(mapping)).map(([channel, c]) => {
        const r = report?.find(r => r.channel === channel);
        return <li key={channel}>{channelWords(channel, language)} ← {measureWords(c.measure, language)}
          {r && ` · ${visText("channelRange", { present: r.present, total: r.total, fallback: c.missing ?? NAV_MAPPING.channels[channel]?.missing ?? "—" })}`}</li>;
      })}</ul>
      {!effectiveMapping(mapping).alpha && <p>{visText("constantAlpha")}</p>}
      <p>{visText("alpha")}</p><p>{visText("missing")}</p><p>{visText("source")}</p><p>{visText("edgeMeaning")}</p>
    </details>
  </details>;
}
