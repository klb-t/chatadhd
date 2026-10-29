// PH29 P1 §3: the working copy in the browser as a real SQLite database in OPFS (VFS opfs-sahpool: no COOP/COEP
// headers needed; observation 2026-10-30: the server sends none - D7). Tables: the kernel's own DDL (generated into
// kernel-schema.json by loom/tools/gen_web_kernel_schema.py), columns added when a bundle brings more (a newer kernel),
// unknown tables created from the bundle's column list. A save writes only the rows whose hash changed and deletes the
// rows that are gone (§3.3 P1: the transfer is still the whole bundle; P3 makes the server send changes). An export
// rebuilds loom.guest_bundle/2 with the kernel's canonical dump and sha256; when the browser cannot reproduce the
// server's hash byte for byte the server's original text is kept and served instead (D4 fallback, reported).
import sqlite3InitModule from "@sqlite.org/sqlite-wasm";
import { F64, canonicalDump, keyOrderSafe, parseExact, rowHash, sha256Hex, type CJson, type Lexemes } from "../context/canonical-json";
import kernelSchema from "../context/kernel-schema.json";

type Db = {
  exec(o: unknown): unknown; selectValue(sql: string, bind?: unknown[]): unknown; selectArrays(sql: string, bind?: unknown[]): unknown[][];
  prepare(sql: string): Stmt; transaction(cb: () => void): void; close(): void;
};
type Stmt = { bind(v: unknown[]): Stmt; stepReset(): Stmt; step(): boolean; get(i: number): unknown; columnCount: number; pointer: number; finalize(): void; reset(): Stmt };

interface OpenArgs { vfsName?: string; directory?: string; dbName?: string; initialCapacity?: number }
interface Bundle { [k: string]: CJson }
interface TableJson { columns: string[]; rows: CJson[][] }

let sqlite3: any = null;
let pool: any = null;
let db: Db | null = null;
let opened: OpenArgs = {};
const hashes = new Map<string, Map<string, string>>();  // table -> key -> row hash (cache of _store_rows)
const sch = kernelSchema as { tables: Record<string, string>; indexes: Record<string, string[]> };

// nlohmann's text for doubles whose JS shortest form differs (learned from the server's bundles, kept in the db).
let lexemes: Lexemes | null = null;
function known(): Lexemes {
  if (!lexemes) {
    lexemes = new Map();
    try { for (const t of JSON.parse(meta("double_lexemes") ?? "[]") as string[]) lexemes.set(Number(t), t); } catch { /* none */ }
  }
  return lexemes;
}

const q = (s: string) => `"${s.replace(/"/g, '""')}"`;

async function open(a: OpenArgs) {
  if (db) return observations();
  opened = { vfsName: "loom-opfs-sahpool", directory: ".loom-store", dbName: "/working-copy.sqlite3", initialCapacity: 6, ...a };
  sqlite3 ??= await (sqlite3InitModule as unknown as (o?: unknown) => Promise<any>)({ print: () => {}, printErr: () => {} });
  pool = await sqlite3.installOpfsSAHPoolVfs({ name: opened.vfsName, directory: opened.directory, initialCapacity: opened.initialCapacity });
  db = new pool.OpfsSAHPoolDb(opened.dbName) as Db;
  db.exec("PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL;");
  db.exec("CREATE TABLE IF NOT EXISTS _store_meta (key TEXT PRIMARY KEY, value TEXT)");
  db.exec("CREATE TABLE IF NOT EXISTS _store_rows (tbl TEXT NOT NULL, key TEXT NOT NULL, hash TEXT NOT NULL, PRIMARY KEY (tbl, key)) WITHOUT ROWID");
  return observations();
}

function observations() {
  const d = db!;
  const opt = (name: string) => Number(d.selectValue("SELECT sqlite_compileoption_used(?)", [name])) === 1;
  let fts5 = opt("ENABLE_FTS5");
  if (fts5) {
    try { d.exec("CREATE VIRTUAL TABLE temp._fts5_probe USING fts5(x); DROP TABLE temp._fts5_probe;"); } catch { fts5 = false; }
  }
  return { sqlite_version: String(d.selectValue("SELECT sqlite_version()")), vfs: "opfs-sahpool", vfs_name: opened.vfsName,
           fts5, file_count: pool?.getFileCount?.() ?? null, capacity: pool?.getCapacity?.() ?? null,
           source_access: parseExact("[1.0]").exact };
}

function meta(key: string): string | null { const v = db!.selectValue("SELECT value FROM _store_meta WHERE key=?", [key]); return v == null ? null : String(v); }
function setMeta(key: string, value: string | null) {
  if (value === null) db!.exec({ sql: "DELETE FROM _store_meta WHERE key=?", bind: [key] });
  else if (meta(key) !== value) db!.exec({ sql: "INSERT INTO _store_meta(key,value) VALUES(?,?) ON CONFLICT(key) DO UPDATE SET value=excluded.value", bind: [key, value] });
}

function tableColumns(t: string): { name: string; pk: number }[] {
  return db!.selectArrays(`PRAGMA table_info(${q(t)})`).map((r) => ({ name: String(r[1]), pk: Number(r[5]) }));
}

function ensureTable(t: string, columns: string[]) {
  let have = tableColumns(t);
  if (!have.length) {
    db!.exec(sch.tables[t] ?? `CREATE TABLE ${q(t)} (${columns.map(q).join(", ")})`);
    for (const ix of sch.indexes[t] ?? []) { try { db!.exec(ix); } catch { /* an index over a column the bundle lacks */ } }
    have = tableColumns(t);
  }
  for (const c of columns) if (!have.some((h) => h.name === c)) db!.exec(`ALTER TABLE ${q(t)} ADD COLUMN ${q(c)}`);
  return tableColumns(t);
}

function bindable(v: CJson): unknown {
  if (v instanceof F64) return v.v;
  if (v === null || typeof v === "number" || typeof v === "string" || typeof v === "bigint") return v;
  if (typeof v === "boolean") return v ? 1 : 0;
  if (!Array.isArray(v) && typeof v === "object" && typeof (v as Record<string, CJson>).$blob === "string")
    return Uint8Array.from(atob((v as Record<string, string>).$blob), (c) => c.charCodeAt(0));
  return canonicalDump(v);  // like the kernel's bind_cell: other JSON as its dump
}

function hashesOf(t: string): Map<string, string> {
  let m = hashes.get(t);
  if (!m) {
    m = new Map();
    for (const r of db!.selectArrays("SELECT key, hash FROM _store_rows WHERE tbl=?", [t])) m.set(String(r[0]), String(r[1]));
    hashes.set(t, m);
  }
  return m;
}

async function save(text: string) {
  const t0 = performance.now();
  const lex = known();
  const before = lex.size;
  const parsed = parseExact(text, lex);
  const b = parsed.value as Bundle;
  if (!b || typeof b !== "object" || b.schema !== "loom.guest_bundle/2") {
    db!.transaction(() => { setMeta("original_text", text); setMeta("fallback", "not_v2"); });
    return { rows_written: 0, rows_deleted: 0, bytes_written: text.length, fallback: "not_v2", ms: performance.now() - t0 };
  }
  const content = (b.content ?? {}) as Record<string, CJson>;
  const tables = (content.tables ?? {}) as Record<string, TableJson>;
  // D4: can this browser reproduce the server's hash? (number source access + canonical dump + key order)
  const want = String(((b.integrity ?? {}) as Record<string, CJson>).content_sha256 ?? "");
  const canonical = parsed.exact && keyOrderSafe(content) && (await sha256Hex(canonicalDump(content, lex))) === want;
  let written = 0, deleted = 0, bytes = 0;
  db!.transaction(() => {
    for (const [t, tab] of Object.entries(tables)) {
      const cols = ensureTable(t, tab.columns);
      const pk = cols.filter((c) => c.pk > 0).sort((a, z) => a.pk - z.pk).map((c) => tab.columns.indexOf(c.name));
      const usePk = pk.length > 0 && pk.every((i) => i >= 0);
      const known = hashesOf(t);
      const seen = new Set<string>();
      const ins = db!.prepare(`INSERT INTO ${q(t)} (${tab.columns.map(q).join(",")}) VALUES (${tab.columns.map(() => "?").join(",")})`);
      const upd = usePk ? db!.prepare(`UPDATE ${q(t)} SET ${tab.columns.map((c) => `${q(c)}=?`).join(",")} WHERE ${pk.map((i) => `${q(tab.columns[i])}=?`).join(" AND ")}`) : null;
      const del = usePk ? db!.prepare(`DELETE FROM ${q(t)} WHERE ${pk.map((i) => `${q(tab.columns[i])}=?`).join(" AND ")}`) : null;
      const putHash = db!.prepare("INSERT INTO _store_rows(tbl,key,hash) VALUES(?,?,?) ON CONFLICT(tbl,key) DO UPDATE SET hash=excluded.hash");
      const dropHash = db!.prepare("DELETE FROM _store_rows WHERE tbl=? AND key=?");
      try {
        // Without a primary key the row itself is the key (a multiset: a repeated row gets an occurrence number).
        const occurrences = new Map<string, number>();
        for (const row of tab.rows) {
          const rowText = canonicalDump(row, lex);
          let key = usePk ? canonicalDump(pk.map((i) => row[i]), lex) : rowText;
          if (!usePk) { const n = (occurrences.get(key) ?? 0) + 1; occurrences.set(key, n); if (n > 1) key += `#${n}`; }
          seen.add(key);
          const h = rowHash(rowText);
          const old = known.get(key);
          if (old === h) continue;
          const vals = row.map(bindable);
          if (old !== undefined && upd) upd.bind([...vals, ...pk.map((i) => bindable(row[i]))]).stepReset();
          else ins.bind(vals).stepReset();
          putHash.bind([t, key, h]).stepReset();
          known.set(key, h);
          written += 1; bytes += rowText.length;
        }
        for (const key of [...known.keys()]) {
          if (seen.has(key)) continue;
          if (usePk && del) del.bind((parseExact(key).value as CJson[]).map(bindable)).stepReset();
          dropHash.bind([t, key]).stepReset();
          known.delete(key);
          deleted += 1;
        }
        if (!usePk && deleted) {
          // A table without a key: rewrite it whole when something left (rare: every kernel bundle table has a key).
          db!.exec(`DELETE FROM ${q(t)}`);
          for (const row of tab.rows) ins.bind(row.map(bindable)).stepReset();
        }
      } finally { ins.finalize(); upd?.finalize(); del?.finalize(); putHash.finalize(); dropHash.finalize(); }
    }
    // Everything except the rows: header, memory, config, column order (small; written only when it changed).
    const skeleton: Bundle = { ...b, content: { ...content, tables: Object.fromEntries(Object.entries(tables).map(([t, tab]) => [t, { columns: tab.columns, rows: [] }])) } };
    const skText = canonicalDump(skeleton as CJson, lex);
    if (lex.size !== before) setMeta("double_lexemes", JSON.stringify([...lex.values()]));
    if (meta("skeleton") !== skText) { setMeta("skeleton", skText); bytes += skText.length; }
    if (canonical) { setMeta("original_text", null); setMeta("fallback", null); }
    else { setMeta("original_text", text); setMeta("fallback", parsed.exact ? "hash_mismatch" : "no_number_source"); bytes += text.length; }
  });
  return { rows_written: written, rows_deleted: deleted, bytes_written: bytes, canonical, fallback: canonical ? null : (parsed.exact ? "hash_mismatch" : "no_number_source"),
           total_bytes: text.length, ms: Math.round(performance.now() - t0) };
}

function cellOf(st: Stmt, i: number): CJson {
  switch (sqlite3.capi.sqlite3_column_type(st.pointer, i)) {
    case 5: return null;
    case 1: { const v = st.get(i); return typeof v === "bigint" ? (v >= BigInt(Number.MIN_SAFE_INTEGER) && v <= BigInt(Number.MAX_SAFE_INTEGER) ? Number(v) : v) : (v as number); }
    case 2: return new F64(Number(st.get(i)));
    case 4: { const u = st.get(i) as Uint8Array; let s = ""; for (const c of u) s += String.fromCharCode(c); return { $blob: btoa(s) }; }
    default: return String(st.get(i));
  }
}

async function exportText(): Promise<string | null> {
  const original = meta("original_text");
  if (original !== null) return original;
  const sk = meta("skeleton");
  if (sk === null) return null;
  const b = parseExact(sk, known()).value as Bundle;
  const content = b.content as Record<string, CJson>;
  const tables = content.tables as Record<string, TableJson>;
  for (const [t, tab] of Object.entries(tables)) {
    const sel = `SELECT ${tab.columns.map(q).join(",")} FROM ${q(t)}`;
    let st: Stmt;
    try { st = db!.prepare(`${sel} ORDER BY rowid`); } catch { st = db!.prepare(sel); }
    try { while (st.step()) { const r: CJson[] = []; for (let i = 0; i < tab.columns.length; i++) r.push(cellOf(st, i)); tab.rows.push(r); } }
    finally { st.finalize(); }
  }
  const integ = (b.integrity ?? {}) as Record<string, CJson>;
  const lex = known();
  b.integrity = { ...integ, content_sha256: await sha256Hex(canonicalDump(content, lex)) };
  return canonicalDump(b as CJson, lex);
}

function info() {
  const d = db!;
  const counts: Record<string, number> = {};
  for (const r of d.selectArrays("SELECT name FROM sqlite_master WHERE type='table' AND name NOT LIKE 'sqlite_%' AND name NOT LIKE '_store_%'"))
    counts[String(r[0])] = Number(d.selectValue(`SELECT count(*) FROM ${q(String(r[0]))}`));
  return { ...observations(), integrity: String(d.selectValue("PRAGMA integrity_check")), tables: counts,
           fallback: meta("fallback"), stored: meta("skeleton") !== null || meta("original_text") !== null,
           page_count: Number(d.selectValue("PRAGMA page_count")), page_size: Number(d.selectValue("PRAGMA page_size")) };
}

async function wipe() {
  try { db?.close(); } catch { /* already closed */ }
  db = null; hashes.clear(); lexemes = null;
  let removed = false;
  try { removed = Boolean(await pool?.removeVfs?.()); } catch { /* the directory is removed by the page's wipe too */ }
  pool = null;
  return { removed };
}

async function handle(op: string, args: any): Promise<unknown> {
  if (op === "open") return open(args ?? {});
  if (!db && op !== "wipe" && op !== "close") await open(opened);
  switch (op) {
    case "save": return save(String(args?.text ?? ""));
    case "load": return { text: await exportText() };
    case "list": return meta("skeleton") !== null || meta("original_text") !== null ? ["current"] : [];
    case "info": return info();
    case "wipe": return wipe();
    case "close": { try { db?.close(); } catch { /* closed */ } db = null; hashes.clear(); lexemes = null; try { pool?.pauseVfs?.(); } catch { /* old library */ } return true; }
    default: throw new Error(`unknown op ${op}`);
  }
}

const scope = self as unknown as { postMessage(m: unknown): void; onmessage: ((e: MessageEvent<{ id: number; op: string; args: unknown }>) => void) | null };
scope.onmessage = (e) => {
  const { id, op, args } = e.data;
  handle(op, args).then((value) => scope.postMessage({ id, ok: true, value }),
    (err: unknown) => scope.postMessage({ id, ok: false, error: String((err as Error)?.message ?? err), name: (err as Error)?.name }));
};
