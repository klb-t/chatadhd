/** Keep untouched native JSON token bytes, including 1.0 versus 1. */
export function nativeJsonFields(raw: string): { key: string; raw: string }[] {
  const parsed: unknown = JSON.parse(raw);
  if (!parsed || typeof parsed !== "object" || Array.isArray(parsed)) throw new Error("Native JSON must be an object.");
  const fields: { key: string; raw: string }[] = [], seen = new Set<string>();
  let offset = 0;
  const space = () => { while (/\s/.test(raw[offset] ?? "") && offset < raw.length) offset++; };
  space(); offset++; space();
  while (raw[offset] !== "}") {
    const keyStart = offset++; let escaped = false;
    while (offset < raw.length) { const char = raw[offset++]; if (!escaped && char === '"') break; if (!escaped && char === "\\") escaped = true; else escaped = false; }
    const key: string = JSON.parse(raw.slice(keyStart, offset));
    if (seen.has(key)) throw new Error("Native JSON contains a duplicate object key.");
    seen.add(key); space(); offset++; space();
    const start = offset; let depth = 0, quoted = false; escaped = false;
    while (offset < raw.length) {
      const char = raw[offset];
      if (quoted) { if (!escaped && char === '"') quoted = false; if (!escaped && char === "\\") escaped = true; else escaped = false; }
      else if (char === '"') quoted = true;
      else if (depth === 0 && (char === "," || char === "}")) break;
      else if (char === "{" || char === "[") depth++;
      else if (char === "}" || char === "]") depth--;
      offset++;
    }
    fields.push({ key, raw: raw.slice(start, offset).trim() });
    if (raw[offset] === ",") { offset++; space(); }
  }
  return fields;
}
export function patchNativeJson(raw: string, path: string[], value: unknown, exactValueJson?: string): string {
  if (!path.length) throw new Error("A JSON field path is required.");
  const fields = nativeJsonFields(raw), [name, ...rest] = path;
  const current = fields.find(field => field.key === name);
  const replacement = rest.length ? patchNativeJson(current?.raw ?? "{}", rest, value, exactValueJson) : exactValueJson ?? JSON.stringify(value);
  JSON.parse(replacement);
  if (current) current.raw = replacement; else fields.push({ key: name, raw: replacement });
  return `{${fields.map(field => `${JSON.stringify(field.key)}:${field.raw}`).join(",\n")}}`;
}
