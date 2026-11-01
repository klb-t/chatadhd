// Focused syntax + transport mechanism checks; never a web build/typecheck or browser claim.
const fs = require("node:fs");
const os = require("node:os");
const path = require("node:path");
const { createRequire } = require("node:module");
const { pathToFileURL } = require("node:url");
const { spawnSync } = require("node:child_process");
let req = require;
try { req.resolve("@babel/core"); } catch { req = createRequire("/usr/share/nodejs/face-sanity.cjs"); }
const babel = req("@babel/core");
const ts = req.resolve("@babel/plugin-transform-typescript");
const jsx = req.resolve("@babel/plugin-transform-react-jsx");
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), "face-client-sanity-"));
try {
  for (const file of ["port.ts", "use-face-client.ts", "../components/FaceParameterPreview.tsx"]) {
    const filename = path.resolve(__dirname, file);
    const result = babel.transformSync(fs.readFileSync(filename, "utf8"), {
      filename, configFile: false, babelrc: false, plugins: [[ts, { isTSX: file.endsWith("tsx") }], [jsx, { runtime: "automatic" }]],
    });
    if (file === "port.ts") fs.writeFileSync(path.join(scratch, "port.mjs"), result.code);
  }
  const words = JSON.parse(fs.readFileSync(path.resolve(__dirname, "../content/face-preview.words.json"), "utf8"));
  if (words.coordinates.length !== 9 || words.library.length !== 0) throw Error("Unexpected shipped library/domain labels");
  function labels(value) {
    if (!value || typeof value !== "object") return;
    if ("pl" in value || "en" in value) {
      if (typeof value.pl !== "string" || typeof value.en !== "string" || !value.pl || !value.en) throw Error("Missing PL/EN label");
    } else Object.values(value).forEach(labels);
  }
  labels(words);
  console.log("Syntax: 3/3 TS/TSX files; JSON and PL/EN labels valid. Typecheck/browser/backend: NOTRUN.");
  const run = spawnSync(process.execPath, ["--test", path.join(__dirname, "port.test.mjs")], {
    env: { ...process.env, FACE_PORT_MODULE: pathToFileURL(path.join(scratch, "port.mjs")).href }, encoding: "utf8",
  });
  process.stdout.write(run.stdout ?? "");
  process.stderr.write(run.stderr ?? "");
  if (run.error) throw run.error;
  process.exitCode = run.status ?? 1;
} finally { fs.rmSync(scratch, { recursive: true, force: true }); }
