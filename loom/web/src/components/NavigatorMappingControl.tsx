// G3d: the control of the setting navigator_mapping inside "Widok ▾": one row per channel the map draws (barwa, nasycenie, jasność, rozmiar, krycie
// ... the open list of data/navigator_mapping.json), each with the measures in words. A choice applies at once (the map redraws from the same setting)
// and is saved for the person (settings.set navigator_mapping, the same key Settings shows); "Domyślne" writes {} = the shipped mapping.
// A measure the person typed elsewhere (a path into the row) is kept and shown as "inne: …" — never dropped by opening this menu.
import { NAV_MAPPING, channelWords, choicesOnly, effectiveMapping, mappingText, measureWords, parseMapping, type Mapping } from "./navigator-mapping";

interface Props {
  /** The setting as the screen holds it (an object, its JSON text, or nothing). */
  value: unknown;
  language: string;
  /** The new setting (the person's choices only; {} = the default). */
  onChange: (next: Record<string, unknown>, said: string) => void;
}

export default function NavigatorMappingControl({ value, language, onChange }: Props) {
  const d = NAV_MAPPING;
  const chosen = parseMapping(value);
  const effective = effectiveMapping(value);
  const set = (channel: string, measure: string) => {
    const next: Mapping = { ...chosen };
    if (measure === "" || measure === d.default[channel]) delete next[channel]; else next[channel] = { measure };
    const said = mappingText("saved", language, { channel: channelWords(channel, language), measure: measure ? measureWords(measure, language) : mappingText("unmapped", language) });
    onChange(choicesOnly(next), said);
  };
  const isDefault = Object.keys(choicesOnly(chosen)).length === 0;
  return (
    <div className="gnav-mapping" data-testid="nav-mapping" data-default={isDefault ? "true" : "false"}>
      <p className="gnav-note gnav-view-title">{mappingText("title", language)}</p>
      <p className="gnav-why">{mappingText("help", language)}</p>
      {Object.keys(d.channels).map(channel => {
        const current = effective[channel]?.measure ?? "";
        const shipped = d.default[channel] ?? "";
        const names = [...(shipped ? [] : [""]), ...Object.keys(d.measures), ...(current && !d.measures[current] ? [current] : [])];
        const words = (m: string) => {
          const w = m === "" ? mappingText("unmapped", language) : measureWords(m, language);
          return m === shipped ? mappingText("default_marker", language, { measure: w }) : w;
        };
        return <label key={channel} className="gnav-mapping-row">
          <span className="gnav-mapping-channel">{channelWords(channel, language)}</span>
          <select className="gnav-mapping-select" data-testid={`nav-mapping-${channel}`} data-measure={current} value={current}
            onChange={e => set(channel, e.target.value)}>
            {names.map(m => <option key={m || "-"} value={m}>{words(m)}</option>)}
          </select>
        </label>;
      })}
      <button type="button" className="gnav-view-option" data-testid="nav-mapping-default" aria-pressed={isDefault} disabled={isDefault}
        onClick={() => onChange({}, mappingText("restored", language))}>{mappingText("default", language)}</button>
    </div>
  );
}
