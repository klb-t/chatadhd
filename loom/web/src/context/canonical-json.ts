// PH29 P1 (D4): the browser rebuilds a loom.guest_bundle/2 from its own database, and the server checks
// `integrity.content_sha256` = sha256(compact nlohmann::ordered_json dump of `content`) (working_copy.cpp). So the page
// must write JSON exactly as the kernel re-dumps it after parsing: keys in insertion order, no spaces, strings escaped
// like nlohmann, and - the one place JSON.parse/JSON.stringify differ - a REAL stays a REAL ("1.0", not "1") and a
// double is printed in nlohmann's format (shortest digits, fixed between 1e-4 and 1e15, else "1e+20" / "1e-05").
// A parse keeps the integer/real distinction by marking reals (`F64`); when the browser cannot see the source text of a
// number (no JSON.parse source access) every number is marked `inexact`, and the caller falls back to keeping the
// server's original text (D4 fallback, recorded in the store's traits).
// Observation 2026-10-29 (fixture from the vendored nlohmann 3.x, 6027 doubles): its Grisu2 is not always the shortest
// round-trip form that JS prints - 19 of 6027 (0.3 %) differ in the last digit. Since nlohmann's text is a pure
// function of the double, the parse LEARNS those exceptions from the server's own text (`Lexemes`: double -> text) and
// the dump uses them; a double the server never printed is printed the JS way (and checked by the hash, D4).

/** Doubles whose nlohmann text differs from the JS shortest form, learned from the server's texts. */
export type Lexemes = Map<number, string>;

/** A JSON number that was written as a real ("1.0", "2.5e-7"); plain JS numbers are integers. */
export class F64 {
  constructor(public readonly v: number) {}
}

export type CJson = null | boolean | number | bigint | string | F64 | CJson[] | { [k: string]: CJson };

export interface ParseResult { value: CJson; exact: boolean }

let sourceAccess: boolean | null = null;
/** Whether this engine passes the number's source text to a JSON.parse reviver (TC39 "JSON.parse source text access"). */
export function hasSourceAccess(): boolean {
  if (sourceAccess === null) {
    try {
      let seen = "";
      JSON.parse("[1.0]", function (this: unknown, _k: string, v: unknown, ctx?: { source?: string }) { if (typeof v === "number") seen = String(ctx?.source ?? ""); return v; } as never);
      sourceAccess = seen === "1.0";
    } catch { sourceAccess = false; }
  }
  return sourceAccess;
}

/** Parses JSON keeping reals as F64 and integers beyond 2^53 as bigint. `exact` = the distinction could be kept. */
export function parseExact(text: string, learn?: Lexemes): ParseResult {
  const exact = hasSourceAccess();
  const value = JSON.parse(text, function (this: unknown, _k: string, v: unknown, ctx?: { source?: string }) {
    if (typeof v !== "number") return v;
    const src = ctx?.source;
    if (typeof src !== "string") return v;
    if (/[.eE]/.test(src)) { if (learn && formatDouble(v) !== src) learn.set(v, src); return new F64(v); }
    if (!Number.isSafeInteger(v)) { try { return BigInt(src); } catch { return v; } }
    return v;
  } as never) as CJson;
  return { value, exact };
}

/** nlohmann::json's double format (to_chars + format_buffer, min_exp -4, max_exp 15). */
export function formatDouble(x: number, known?: Lexemes): string {
  if (!Number.isFinite(x)) return "null";
  const k0 = known?.get(x);
  if (k0 !== undefined) return k0;
  if (x === 0) return Object.is(x, -0) ? "-0.0" : "0.0";
  const neg = x < 0;
  const e = Math.abs(x).toExponential();  // shortest round-trip digits: d[.ddd]e±n
  const m = /^(\d)(?:\.(\d+))?e([+-]\d+)$/.exec(e);
  if (!m) return String(x);
  const digits = m[1] + (m[2] ?? "");
  const k = digits.length;
  const n = Number(m[3]) + 1;  // position of the decimal point
  let out: string;
  if (k <= n && n <= 15) out = digits + "0".repeat(n - k) + ".0";
  else if (0 < n && n <= 15) out = digits.slice(0, n) + "." + digits.slice(n);
  else if (-4 < n && n <= 0) out = "0." + "0".repeat(-n) + digits;
  else {
    const ex = n - 1;
    const abs = Math.abs(ex);
    out = (k === 1 ? digits : digits[0] + "." + digits.slice(1)) + "e" + (ex < 0 ? "-" : "+") + (abs < 10 ? "0" + abs : String(abs));
  }
  return neg ? "-" + out : out;
}

/** A string as nlohmann writes it (ensure_ascii off): \" \\ \b \f \n \r \t, other controls as \u00xx. */
export function dumpString(s: string): string {
  // JSON.stringify escapes the same set the same way for well-formed strings (lone surrogates cannot reach the kernel).
  return JSON.stringify(s);
}

/** Compact dump in the kernel's canonical form. */
export function canonicalDump(v: CJson, known?: Lexemes): string {
  if (v === null) return "null";
  if (typeof v === "boolean") return v ? "true" : "false";
  if (typeof v === "number") return Number.isInteger(v) ? String(v) : formatDouble(v, known);
  if (typeof v === "bigint") return v.toString();
  if (typeof v === "string") return dumpString(v);
  if (v instanceof F64) return formatDouble(v.v, known);
  if (Array.isArray(v)) {
    let out = "[";
    for (let i = 0; i < v.length; i++) out += (i ? "," : "") + canonicalDump(v[i], known);
    return out + "]";
  }
  let out = "{", first = true;
  for (const k of Object.keys(v)) { out += (first ? "" : ",") + dumpString(k) + ":" + canonicalDump(v[k], known); first = false; }
  return out + "}";
}

/** Whether an object's key order survives a JS object (integer-like keys are reordered by the engine). */
export function keyOrderSafe(v: CJson): boolean {
  if (v === null || typeof v !== "object" || v instanceof F64) return true;
  if (Array.isArray(v)) return v.every(keyOrderSafe);
  for (const k of Object.keys(v)) { if (/^(0|[1-9]\d*)$/.test(k)) return false; if (!keyOrderSafe(v[k])) return false; }
  return true;
}

export async function sha256Hex(text: string): Promise<string> {
  const d = await crypto.subtle.digest("SHA-256", new TextEncoder().encode(text));
  return Array.from(new Uint8Array(d), (b) => b.toString(16).padStart(2, "0")).join("");
}

/** A fast non-cryptographic 53-bit hash of a row's canonical text (cyrb53), used only to see which rows changed. */
export function rowHash(s: string, seed = 0): string {
  let h1 = 0xdeadbeef ^ seed, h2 = 0x41c6ce57 ^ seed;
  for (let i = 0; i < s.length; i++) {
    const ch = s.charCodeAt(i);
    h1 = Math.imul(h1 ^ ch, 2654435761);
    h2 = Math.imul(h2 ^ ch, 1597334677);
  }
  h1 = Math.imul(h1 ^ (h1 >>> 16), 2246822507) ^ Math.imul(h2 ^ (h2 >>> 13), 3266489909);
  h2 = Math.imul(h2 ^ (h2 >>> 16), 2246822507) ^ Math.imul(h1 ^ (h1 >>> 13), 3266489909);
  return (4294967296 * (2097151 & h2) + (h1 >>> 0)).toString(36) + ":" + s.length.toString(36);
}
