// Loads TypeScript sources as `data:` ES modules with RECURSIVE mapping of relative imports,
// so a script needs no hand-written `.replaceAll('from "./x"', ...)` list: an import added to a
// source file later just works. Handles `import ... from`, `export ... from`, bare `import "x"`
// and `import("x")`. Resolves ./x -> x.ts / x.tsx / x/index.ts(x) / x.mjs / x.js (existing JS files are
// mapped to their file: URL). Results are cached per absolute path (one module instance per file).
// A cyclic import cannot be expressed with data: URLs (a URL contains its dependencies' URLs) and is
// reported as an error naming the cycle instead of hanging or recursing forever.
import { existsSync, readFileSync, statSync } from "node:fs";
import path from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";
import ts from "typescript";

const SPEC = /(\bfrom\s*|\bimport\s*\(\s*|\bimport\s+)(["'])(\.{1,2}\/[^"'\n]*|\.{1,2})\2/g;
const EXT = [".ts", ".tsx"];
const isFile = file => existsSync(file) && statSync(file).isFile();

export function resolveRelative(fromFile, spec) {
  const base = path.resolve(path.dirname(fromFile), spec);
  if (isFile(base) && /\.(mjs|js|cjs|json)$/.test(base)) return { file: base, script: true };
  for (const ext of EXT) if (isFile(base + ext)) return { file: base + ext };
  for (const ext of EXT) if (isFile(path.join(base, "index" + ext))) return { file: path.join(base, "index" + ext) };
  for (const ext of [".mjs", ".js"]) if (isFile(base + ext)) return { file: base + ext, script: true };
  throw new Error(`ts-module: cannot resolve "${spec}" imported from ${fromFile}`);
}

const toPath = input => (input instanceof URL || String(input).startsWith("file:")) ? fileURLToPath(input) : path.resolve(String(input));

export function createTsModuleLoader({ target = ts.ScriptTarget.ES2022, module = ts.ModuleKind.ES2022 } = {}) {
  const cache = new Map();   // absolute path -> data: URL
  const stack = [];
  function urlOf(file) {
    file = path.resolve(file);
    if (cache.has(file)) return cache.get(file);
    if (stack.includes(file)) throw new Error(`ts-module: import cycle ${[...stack.slice(stack.indexOf(file)), file].join(" -> ")}`);
    stack.push(file);
    try {
      let js = ts.transpileModule(readFileSync(file, "utf8"), { fileName: file, compilerOptions: { target, module } }).outputText;
      js = js.replace(SPEC, (all, lead, quote, spec) => {
        const r = resolveRelative(file, spec);
        return `${lead}${quote}${r.script ? pathToFileURL(r.file).href : urlOf(r.file)}${quote}`;
      });
      const url = `data:text/javascript;base64,${Buffer.from(js).toString("base64")}`;
      cache.set(file, url);
      return url;
    } finally { stack.pop(); }
  }
  return { url: input => urlOf(toPath(input)), load: async input => import(urlOf(toPath(input))) };
}

const shared = createTsModuleLoader();
/** data: URL of a TypeScript file, all relative imports mapped recursively. */
export const tsModuleUrl = input => shared.url(input);
/** import() of a TypeScript file with all relative imports mapped recursively. */
export const loadTsModule = input => shared.load(input);
