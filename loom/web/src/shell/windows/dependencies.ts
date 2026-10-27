// W6 of the window system (OKNA §5.1): the dependencies of a linked parameter as pure functions registered by id. A dependency is DATA
// (`{ kind, ...args }`); a kind without a function is not refused and not silent: the value stays and an advisory says "unknown".
// The kinds below are examples (CLAUDE.md "lists are examples"): same, offset, scale, parent_of, children_of, perspective_of_node,
// expression. Numeric dependencies act on numbers, on ISO dates (offset in days) and element-wise on objects/arrays of them (a pan {x,y},
// a time range {from,to}), so every value shape that has numbers in it is handled; a shape without numbers comes back unchanged with
// an advisory, never an exception.
// `expression` is a small whitelisted grammar evaluated without eval/Function: numbers, strings, true/false/null, names
// (`value`, `self.<param>`, `other.<param>`, `<channel>`), + - * / % **, comparisons, && || !, `c ? a : b`, parentheses and the functions
// of FUNCS. Nothing else is reachable: no member access beyond one dotted name, no calls of values, no assignment.

export type Json = unknown;
export interface Dependency { kind: string; [arg: string]: unknown }
/** What a dependency may consult besides the source value (all optional: absent = the dependency reports "unavailable"). */
export interface DepEnv {
  /** The parameters of the source window (`other.<param>` of an expression). */
  other?: Record<string, unknown>;
  /** The parameters of the bound window itself (`self.<param>`). */
  self?: Record<string, unknown>;
  /** The values of the channels of the layout (a bare name of an expression). */
  channels?: Record<string, unknown>;
  parentOf?: (node: string) => string | null | undefined;
  childrenOf?: (node: string) => string[] | null | undefined;
  perspectiveOfNode?: (node: string) => string | null | undefined;
}
export type DepResult = { ok: true; value: Json } | { ok: false; code: string; evidence: Record<string, unknown> };
export type DepFn = (value: Json, dep: Dependency, env: DepEnv) => DepResult;

const ok = (value: Json): DepResult => ({ ok: true, value });
const fail = (code: string, evidence: Record<string, unknown> = {}): DepResult => ({ ok: false, code, evidence });
const isNum = (v: unknown): v is number => typeof v === "number" && Number.isFinite(v);
const ISO_DAY = /^\d{4}-\d{2}-\d{2}(T[\d:.]+Z?)?$/;

/** Applies a numeric map to every number in a value (numbers, ISO dates through `onDate`, objects and arrays element-wise). */
function mapNumbers(v: Json, f: (n: number) => number, onDate?: (iso: string) => string): { value: Json; touched: number } {
  let touched = 0;
  const walk = (x: Json): Json => {
    if (isNum(x)) { touched++; return f(x); }
    if (typeof x === "string" && onDate && ISO_DAY.test(x) && !Number.isNaN(Date.parse(x))) { touched++; return onDate(x); }
    if (Array.isArray(x)) return x.map(walk);
    if (x && typeof x === "object") return Object.fromEntries(Object.entries(x as Record<string, unknown>).map(([k, y]) => [k, walk(y)]));
    return x;
  };
  return { value: walk(v), touched };
}
const shiftDays = (iso: string, days: number): string => {
  const d = new Date(Date.parse(iso) + days * 86400000).toISOString();
  return iso.length === 10 ? d.slice(0, 10) : d;
};
const numArg = (dep: Dependency, name: string, dflt: number): number => (isNum(dep[name]) ? dep[name] as number : dflt);

export const DEPENDENCIES: Record<string, DepFn> = {
  same: (v) => ok(v),
  offset: (v, dep) => {
    const by = numArg(dep, "by", 0), days = numArg(dep, "days", by);
    const r = mapNumbers(v, (n) => n + by, (iso) => shiftDays(iso, days));
    return r.touched || v === null || v === undefined ? ok(r.value) : fail("dependency_not_numeric", { kind: "offset", value: v });
  },
  scale: (v, dep) => {
    const factor = numArg(dep, "factor", 1);
    const r = mapNumbers(v, (n) => n * factor);
    return r.touched || v === null || v === undefined ? ok(r.value) : fail("dependency_not_numeric", { kind: "scale", value: v });
  },
  parent_of: (v, _dep, env) => {
    if (typeof v !== "string") return v === null || v === undefined ? ok(v) : fail("dependency_not_node", { kind: "parent_of", value: v });
    if (!env.parentOf) return fail("dependency_unavailable", { kind: "parent_of" });
    return ok(env.parentOf(v) ?? null);
  },
  children_of: (v, _dep, env) => {
    if (typeof v !== "string") return v === null || v === undefined ? ok(v) : fail("dependency_not_node", { kind: "children_of", value: v });
    if (!env.childrenOf) return fail("dependency_unavailable", { kind: "children_of" });
    return ok(env.childrenOf(v) ?? []);
  },
  perspective_of_node: (v, _dep, env) => {
    if (typeof v !== "string") return v === null || v === undefined ? ok(v) : fail("dependency_not_node", { kind: "perspective_of_node", value: v });
    if (!env.perspectiveOfNode) return fail("dependency_unavailable", { kind: "perspective_of_node" });
    return ok(env.perspectiveOfNode(v) ?? null);
  },
  expression: (v, dep, env) => {
    const src = typeof dep.expr === "string" ? dep.expr : "";
    const c = compileExpression(src);
    if (!c.ok) return fail("expression_invalid", { expr: src, error: c.error, at: c.at });
    const r = evalExpression(c.ast, { value: v, self: env.self ?? {}, other: env.other ?? {}, channels: env.channels ?? {} });
    return r.ok ? ok(r.value) : fail("expression_failed", { expr: src, error: r.error });
  },
};

/** Runs a dependency by its id; an unknown id leaves the value as it is and says so. Never throws. */
export function applyDependency(value: Json, dep: Dependency | undefined, env: DepEnv = {}, registry: Record<string, DepFn> = DEPENDENCIES): DepResult {
  const kind = dep && typeof dep.kind === "string" ? dep.kind : "same";
  const f = Object.prototype.hasOwnProperty.call(registry, kind) ? registry[kind] : undefined;
  if (!f) return fail("unknown_dependency", { kind });
  try { return f(value, dep ?? { kind }, env); } catch (e) { return fail("dependency_failed", { kind, error: String(e) }); }
}

/** The names of the source window's parameters an expression reads (`other.x`), so the coupling graph knows its edges. */
export function expressionRefs(src: string): { other: string[]; self: string[]; channels: string[] } {
  const c = compileExpression(src);
  const out = { other: new Set<string>(), self: new Set<string>(), channels: new Set<string>() };
  const walk = (n: Ast) => {
    if (n.t === "name") { if (n.scope === "other") out.other.add(n.name); else if (n.scope === "self") out.self.add(n.name); else if (n.name !== "value") out.channels.add(n.name); }
    for (const k of n.t === "bin" ? [n.a, n.b] : n.t === "un" ? [n.a] : n.t === "cond" ? [n.c, n.a, n.b] : n.t === "call" ? n.args : []) walk(k);
  };
  if (c.ok) walk(c.ast);
  return { other: [...out.other], self: [...out.self], channels: [...out.channels] };
}

// ---------- the expression grammar ----------
export type Ast =
  | { t: "lit"; v: Json }
  | { t: "name"; scope: "other" | "self" | ""; name: string }
  | { t: "un"; op: string; a: Ast }
  | { t: "bin"; op: string; a: Ast; b: Ast }
  | { t: "cond"; c: Ast; a: Ast; b: Ast }
  | { t: "call"; fn: string; args: Ast[] };
const num = (x: Json): number => (typeof x === "number" ? x : typeof x === "boolean" ? Number(x) : NaN);
/** The whitelisted functions (an example set; adding one is a line here). */
export const FUNCS: Record<string, (...a: Json[]) => Json> = {
  min: (...a) => Math.min(...a.map(num)), max: (...a) => Math.max(...a.map(num)), abs: (a) => Math.abs(num(a)),
  round: (a, d) => { const k = 10 ** (isNum(d) ? d : 0); return Math.round(num(a) * k) / k; }, floor: (a) => Math.floor(num(a)), ceil: (a) => Math.ceil(num(a)),
  clamp: (a, lo, hi) => Math.min(num(hi), Math.max(num(lo), num(a))), sqrt: (a) => Math.sqrt(num(a)), log: (a) => Math.log(num(a)), exp: (a) => Math.exp(num(a)),
  sign: (a) => Math.sign(num(a)), if: (c, a, b) => (c ? a : b),
};
const MAX_LEN = 2000, MAX_DEPTH = 64;

type Tok = { k: "num" | "str" | "id" | "op"; v: string; at: number };
function lex(src: string): Tok[] | { error: string; at: number } {
  const out: Tok[] = []; let i = 0;
  while (i < src.length) {
    const c = src[i];
    if (/\s/.test(c)) { i++; continue; }
    const m3 = src.slice(i, i + 3), m2 = src.slice(i, i + 2);
    if (/[0-9]/.test(c) || (c === "." && /[0-9]/.test(src[i + 1] ?? ""))) { const m = /^(\d+\.?\d*|\.\d+)([eE][+-]?\d+)?/.exec(src.slice(i)); if (!m) return { error: "number", at: i }; out.push({ k: "num", v: m[0], at: i }); i += m[0].length; continue; }
    if (/[A-Za-z_]/.test(c)) { const m = /^[A-Za-z_][A-Za-z0-9_]*/.exec(src.slice(i))!; out.push({ k: "id", v: m[0], at: i }); i += m[0].length; continue; }
    if (c === '"' || c === "'") { const end = src.indexOf(c, i + 1); if (end < 0) return { error: "string", at: i }; out.push({ k: "str", v: src.slice(i + 1, end), at: i }); i = end + 1; continue; }
    const op = ["===", "!=="].includes(m3) ? m3 : ["**", "==", "!=", "<=", ">=", "&&", "||"].includes(m2) ? m2 : "+-*/%<>!?:(),.".includes(c) ? c : "";
    if (!op) return { error: `character ${c}`, at: i };
    out.push({ k: "op", v: op, at: i }); i += op.length;
  }
  return out;
}

/** Parses an expression into an AST, or says where it stopped. Never throws. */
export function compileExpression(src: string): { ok: true; ast: Ast } | { ok: false; error: string; at: number } {
  if (typeof src !== "string" || !src.trim()) return { ok: false, error: "empty", at: 0 };
  if (src.length > MAX_LEN) return { ok: false, error: "too_long", at: MAX_LEN };
  const toks = lex(src);
  if (!Array.isArray(toks)) return { ok: false, error: toks.error, at: toks.at };
  let p = 0, depth = 0;
  class Stop { error: string; at: number; constructor(error: string, at: number) { this.error = error; this.at = at; } }
  const peek = (v?: string) => (toks[p] && (v === undefined || (toks[p].k === "op" && toks[p].v === v)) ? toks[p] : undefined);
  const eat = (v: string) => { if (!peek(v)) throw new Stop(`expected ${v}`, toks[p]?.at ?? src.length); p++; };
  const deeper = <T,>(f: () => T): T => { if (++depth > MAX_DEPTH) throw new Stop("too_deep", toks[p]?.at ?? 0); try { return f(); } finally { depth--; } };
  const LEVELS: string[][] = [["||"], ["&&"], ["==", "!=", "===", "!=="], ["<", ">", "<=", ">="], ["+", "-"], ["*", "/", "%"]];
  const cond = (): Ast => deeper(() => {
    const c = bin(0);
    if (peek("?")) { p++; const a = cond(); eat(":"); const b = cond(); return { t: "cond", c, a, b }; }
    return c;
  });
  const bin = (lvl: number): Ast => {
    if (lvl >= LEVELS.length) return unary();
    let a = bin(lvl + 1);
    while (toks[p]?.k === "op" && LEVELS[lvl].includes(toks[p].v)) { const op = toks[p++].v; a = { t: "bin", op, a, b: bin(lvl + 1) }; }
    return a;
  };
  const unary = (): Ast => deeper(() => {
    if (peek("-") || peek("+") || peek("!")) { const op = toks[p++].v; return { t: "un", op, a: unary() }; }
    const a = atom();
    if (peek("**")) { p++; return { t: "bin", op: "**", a, b: unary() }; }
    return a;
  });
  const atom = (): Ast => {
    const tk = toks[p];
    if (!tk) throw new Stop("unexpected end", src.length);
    if (tk.k === "num") { p++; return { t: "lit", v: Number(tk.v) }; }
    if (tk.k === "str") { p++; return { t: "lit", v: tk.v }; }
    if (tk.k === "op" && tk.v === "(") { p++; const e = cond(); eat(")"); return e; }
    if (tk.k === "id") {
      p++;
      if (tk.v === "true" || tk.v === "false") return { t: "lit", v: tk.v === "true" };
      if (tk.v === "null") return { t: "lit", v: null };
      if (peek("(")) {
        if (!Object.prototype.hasOwnProperty.call(FUNCS, tk.v)) throw new Stop(`unknown function ${tk.v}`, tk.at);
        p++; const args: Ast[] = [];
        if (!peek(")")) { args.push(cond()); while (peek(",")) { p++; args.push(cond()); } }
        eat(")"); return { t: "call", fn: tk.v, args };
      }
      if ((tk.v === "other" || tk.v === "self") && peek(".")) {
        p++; const n = toks[p];
        if (!n || n.k !== "id") throw new Stop("expected a name", n?.at ?? src.length);
        p++; return { t: "name", scope: tk.v, name: n.v };
      }
      return { t: "name", scope: "", name: tk.v };
    }
    throw new Stop(`unexpected ${tk.v}`, tk.at);
  };
  try {
    const ast = cond();
    if (p < toks.length) return { ok: false, error: `unexpected ${toks[p].v}`, at: toks[p].at };
    return { ok: true, ast };
  } catch (e) {
    if (e instanceof Stop) return { ok: false, error: e.error, at: e.at };
    return { ok: false, error: String(e), at: 0 };
  }
}

export interface Scope { value: Json; self: Record<string, unknown>; other: Record<string, unknown>; channels: Record<string, unknown> }
const own = (o: Record<string, unknown>, k: string) => (Object.prototype.hasOwnProperty.call(o, k) ? o[k] : undefined);
/** Evaluates an AST over plain values only. Never throws; a non-finite number is a failure (said, not silently passed on). */
export function evalExpression(ast: Ast, scope: Scope): { ok: true; value: Json } | { ok: false; error: string } {
  const ev = (n: Ast): Json => {
    switch (n.t) {
      case "lit": return n.v;
      case "name": return n.scope === "other" ? own(scope.other, n.name) : n.scope === "self" ? own(scope.self, n.name) : n.name === "value" ? scope.value : own(scope.channels, n.name);
      case "un": { const a = ev(n.a); return n.op === "-" ? -num(a) : n.op === "+" ? num(a) : !a; }
      case "cond": return ev(n.c) ? ev(n.a) : ev(n.b);
      case "call": return FUNCS[n.fn](...n.args.map(ev));
      case "bin": {
        if (n.op === "&&") { const a = ev(n.a); return a ? ev(n.b) : a; }
        if (n.op === "||") { const a = ev(n.a); return a ? a : ev(n.b); }
        const a = ev(n.a), b = ev(n.b);
        switch (n.op) {
          case "+": return typeof a === "string" || typeof b === "string" ? String(a) + String(b) : num(a) + num(b);
          case "-": return num(a) - num(b); case "*": return num(a) * num(b); case "/": return num(a) / num(b); case "%": return num(a) % num(b);
          case "**": return num(a) ** num(b);
          case "==": case "===": return JSON.stringify(a) === JSON.stringify(b);
          case "!=": case "!==": return JSON.stringify(a) !== JSON.stringify(b);
          case "<": return num(a) < num(b); case ">": return num(a) > num(b); case "<=": return num(a) <= num(b); case ">=": return num(a) >= num(b);
        }
        return null;
      }
    }
  };
  try {
    const v = ev(ast);
    if (typeof v === "number" && !Number.isFinite(v)) return { ok: false, error: "not_finite" };
    return { ok: true, value: v };
  } catch (e) { return { ok: false, error: String(e) }; }
}
