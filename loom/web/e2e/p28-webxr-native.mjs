#!/usr/bin/env node
// Real production bundle -> native Loom server (no model, no network): P28 step 3, the 3D / WebXR renderer of the view model.
//   1. the kernel serves the renderers as data (effective view `view_renderers`) and the xr bindings (voice stays recorded);
//   2. WITHOUT navigator.xr the 3D renderer still works on a flat screen (canvas renders, node count, hint in Polish) and says so in a visible note;
//   3. the same view model in 3D: sector directions around the user, the blend by gaze angle (look-around: turning swaps the emphasis), labels, select + focus;
//   4. WebXR with a hand-written emulation stub (no network, no polyfill package): feature detection, entering immersive-vr, pinch with the gaze,
//      right trigger, controller buttons as held modifiers, the thumbstick as the arrows, a large gesture for a coarse scene move, the in-scene hint;
//   5. a renderer chosen as data: a user overlay changes the spatial preset (the direction of the past reversed) and the 3D scene follows.
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import { createServer } from "node:http";
import { existsSync, mkdtempSync, rmSync } from "node:fs";
import { DatabaseSync } from "node:sqlite";
import { tmpdir } from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";
import { chromium } from "playwright";
import { stopChild, suiteCompletionGuard } from "./harness-lifecycle.mjs";

const webRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), "..");
const loomRoot = path.resolve(webRoot, "..");
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, "build/dev/server/loom-server");
const dist = path.join(webRoot, "dist");
assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
assert.ok(existsSync(path.join(dist, "index.html")), "Run npm run build first");
const groups = [], pageErrors = [], dirs = [];
const complete = suiteCompletionGuard("p28-webxr-native", 8, groups);
let browser, child, serverLog = "", base = "";

async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, "127.0.0.1", resolve));
  const { port } = probe.address();
  await new Promise(resolve => probe.close(resolve));
  return port;
}
async function start(dataDir) {
  const port = await freePort();
  base = `http://127.0.0.1:${port}`;
  child = spawn(serverBin, ["--host", "127.0.0.1", "--port", String(port), "--data-dir", dataDir, "--static-dir", dist], { cwd: loomRoot, stdio: ["ignore", "pipe", "pipe"] });
  for (const output of [child.stdout, child.stderr]) output.on("data", chunk => { serverLog += chunk.toString(); });
  const deadline = Date.now() + 20000;
  for (;;) {
    try { if ((await fetch(`${base}/api/healthz`)).ok) return; } catch { /* startup */ }
    if (child.exitCode !== null) throw Error(`Native server exited: ${serverLog}`);
    if (Date.now() > deadline) throw Error(`Native server startup timeout: ${serverLog}`);
    await new Promise(resolve => setTimeout(resolve, 100));
  }
}
async function api(method, url, body) {
  const response = await fetch(`${base}${url}`, { method, ...(body === undefined ? {} : { headers: { "content-type": "application/json" }, body: JSON.stringify(body) }) });
  assert.ok(response.ok, `${method} ${url}: ${response.status} ${await response.clone().text()}`);
  return response.json();
}
async function check(name, run) { await run(); groups.push(name); console.log(`[p28-webxr-native] PASS ${name}`); }

function seed(dataDir) {
  const db = new DatabaseSync(path.join(dataDir, "chatadhd.db"));
  db.exec("PRAGMA busy_timeout = 5000");
  const ids = {};
  const day = n => new Date(Date.UTC(2026, 0, 1 + n)).toISOString().replace(".000Z", "Z");
  const node = (key, kind, n) => {
    ids[key] = `n_p28_${key}`;
    db.prepare("INSERT INTO nodes (id, kind, label, content, tags, metadata, created) VALUES (?, ?, ?, '', '[]', '{}', ?)").run(ids[key], kind, key, day(n));
  };
  let linkNo = 0;
  const link = (a, b, type) => db.prepare("INSERT INTO links (id, src, dst, link_type, weight, metadata, created) VALUES (?, ?, ?, ?, 1.0, '{}', ?)").run(`l_p28_${++linkNo}`, ids[a], ids[b], type, day(0));
  [["v1", "version"], ["v2", "version"], ["v3", "version"], ["v4", "version"], ["v5", "version"], ["fb1", "version"]].forEach(([k, kind], i) => node(k, kind, i * 10));
  node("caller_a", "function", 60); node("caller_b", "function", 61); node("libx", "module", 62); node("ship", "goal", 63);
  link("v2", "v1", "previous_version"); link("v3", "v2", "previous_version"); link("v4", "v3", "previous_version"); link("v5", "v4", "previous_version");
  link("fb1", "v2", "forked_from");
  link("caller_a", "v3", "calls"); link("caller_b", "v3", "calls");
  link("v3", "libx", "imports");
  link("v3", "ship", "serves_goal");
  db.close();
  return ids;
}

// A WebXR emulation stub (no network, no package): navigator.xr, a fake session with the parts three.js uses, controllers as input sources whose poses,
// buttons and thumbsticks the test sets from outside (window.__xr). It stands where a headset would.
const XR_STUB = `(() => {
  const identity = () => [1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1];
  const state = { viewer: identity(), poses: {}, sources: [], session: null, frame: null, supported: { 'immersive-vr': true, 'immersive-ar': false }, started: [] };
  window.XRRigidTransform = class { constructor(p, o) { this.position = p; this.orientation = o; this.matrix = identity(); } get inverse() { return this; } };
  window.XRWebGLLayer = class { constructor(session, gl) { this.framebuffer = null; this.framebufferWidth = gl.drawingBufferWidth; this.framebufferHeight = gl.drawingBufferHeight; this.antialias = true; }
    getViewport() { return { x: 0, y: 0, width: this.framebufferWidth, height: this.framebufferHeight }; } };
  for (const C of [window.WebGLRenderingContext, window.WebGL2RenderingContext]) if (C) C.prototype.makeXRCompatible = function () { return Promise.resolve(); };
  const proj = (aspect) => { const f = 1, n = 0.05, fa = 200; return [f / aspect,0,0,0, 0,f,0,0, 0,0,(fa+n)/(n-fa),-1, 0,0,2*fa*n/(n-fa),0]; };
  const tf = (m) => ({ matrix: m, position: { x: m[12], y: m[13], z: m[14], w: 1 }, orientation: { x: 0, y: 0, z: 0, w: 1 }, get inverse() { return tf(identity()); } });
  const space = (id) => ({ id, getOffsetReferenceSpace() { return this; } });
  const frame = {
    getViewerPose() { return { transform: tf(state.viewer), views: [{ eye: 'none', projectionMatrix: proj(1.5), transform: tf(state.viewer), requestViewportScale() {}, recommendedViewportScale: null }] }; },
    getPose(sp) { const m = state.poses[sp.id]; return m ? { transform: tf(m), emulatedPosition: false } : null; },
  };
  state.frame = frame;
  const makeSession = (mode) => {
    const s = new EventTarget();
    s.mode = mode; s.inputSources = []; s.renderState = { baseLayer: null, depthNear: 0.1, depthFar: 1000 }; s.visibilityState = 'visible'; s.environmentBlendMode = mode === 'immersive-ar' ? 'alpha-blend' : 'opaque';
    s.updateRenderState = (st) => { Object.assign(s.renderState, st); };
    s.requestReferenceSpace = async (type) => space('ref:' + type);
    s.requestAnimationFrame = (cb) => requestAnimationFrame((t) => cb(t, frame));
    s.cancelAnimationFrame = (h) => cancelAnimationFrame(h);
    s.end = async () => { s.dispatchEvent(new Event('end')); state.session = null; };
    s.addEventListener('end', () => {});
    frame.session = s; state.session = s; state.started.push(mode);
    return s;
  };
  Object.defineProperty(navigator, 'xr', { configurable: true, value: {
    isSessionSupported: async (m) => state.supported[m] === true,
    requestSession: async (m) => { if (!state.supported[m]) throw new Error('NotSupportedError: ' + m); return makeSession(m); },
    addEventListener() {}, removeEventListener() {},
  } });
  let n = 0;
  window.__xr = {
    state,
    addSource({ handedness = 'right', hand = false } = {}) {
      const i = n++;
      const src = { handedness, targetRayMode: 'tracked-pointer', targetRaySpace: space('ray' + i), gripSpace: space('grip' + i), profiles: [], hand: hand ? new Map() : null,
        gamepad: hand ? null : { axes: [0, 0, 0, 0], buttons: [{ pressed: false, value: 0 }, { pressed: false, value: 0 }, { pressed: false, value: 0 }, { pressed: false, value: 0 }, { pressed: false, value: 0 }] } };
      state.poses['ray' + i] = identity();
      state.session.inputSources.push(src);
      const ev = new Event('inputsourceschange'); ev.added = [src]; ev.removed = []; state.session.dispatchEvent(ev);
      return i;
    },
    removeSource(i) {
      const src = state.session.inputSources.splice(i, 1)[0];
      const ev = new Event('inputsourceschange'); ev.added = []; ev.removed = [src]; state.session.dispatchEvent(ev);
    },
    pose(i, m) { state.poses['ray' + i] = m; },
    viewer(m) { state.viewer = m; },
    fire(i, type) { const ev = new Event(type); ev.inputSource = state.session.inputSources[i]; ev.frame = frame; state.session.dispatchEvent(ev); },
    axes(i, x, y) { const g = state.session.inputSources[i].gamepad; g.axes[2] = x; g.axes[3] = y; },
    sweep(i, from, to, ms) { return new Promise((resolve) => { const t0 = performance.now(); const step = () => { const k = Math.min(1, (performance.now() - t0) / ms); const m = identity(); m[12] = from[0] + (to[0] - from[0]) * k; m[13] = from[1] + (to[1] - from[1]) * k; m[14] = from[2] + (to[2] - from[2]) * k; state.poses['ray' + i] = m; if (k < 1) requestAnimationFrame(step); else resolve(); }; step(); }); },
  };
})();`;

// a pose matrix (column-major) at p looking along dir
function lookAlong(p, dir) {
  const len = Math.hypot(...dir); const z = dir.map(v => -v / len);
  let x = [z[2], 0, -z[0]]; const xl = Math.hypot(...x) || 1; x = x.map(v => v / xl);
  const y = [z[1] * x[2] - z[2] * x[1], z[2] * x[0] - z[0] * x[2], z[0] * x[1] - z[1] * x[0]];
  return [...x, 0, ...y, 0, ...z, 0, ...p, 1];
}
const sub = (a, b) => a.map((v, i) => v - b[i]);

try {
  const dataDir = mkdtempSync(path.join(tmpdir(), "loom-p28x-")); dirs.push(dataDir);
  await start(dataDir);
  await api("PATCH", "/api/config", { semantic_analysis: false });
  await api("PUT", "/api/config/ui_language", { value: "pl" });
  const ids = seed(dataDir);

  await check("the kernel serves the renderers as data and the xr bindings; only the voice phrase stays recorded", async () => {
    const list = await api("GET", "/api/view-model");
    const byId = Object.fromEntries(list.renderers.entries.map(e => [e.id, e]));
    assert.ok(byId.flat_2d && byId.spatial_3d && byId.spatial_3d.origin === "rom");
    assert.equal(byId.spatial_3d.entry.engine, "three");
    assert.deepEqual(Object.keys(byId.spatial_3d.entry.modes), ["orbit", "look", "immersive-vr", "immersive-ar"]);
    assert.equal(byId.spatial_3d.entry.modes["immersive-vr"].session, "immersive-vr");
    assert.ok(Array.isArray(byId.spatial_3d.entry.blend.curve) && byId.spatial_3d.entry.blend.curve.length >= 3, "the blend curve is data");
    const xr = await api("POST", "/api/view-interaction", { device: "xr" });
    assert.deepEqual(xr.bindings.filter(b => b.status === "recorded").map(b => b.id), ["xr.voice.zoom.in"]);
    for (const id of ["xr.focus.gaze", "xr.focus.trigger", "xr.hold.shift", "xr.hold.ctrl", "xr.hold.alt", "xr.plane.left", "xr.depth_time.left", "xr.branches.left", "xr.coarse.left"]) assert.ok(xr.bindings.find(b => b.id === id && b.status === "active"), id);
    assert.deepEqual(xr.conflicts, []);
    const a = await api("POST", "/api/view-model", { declaration: "history_cone", focus: ids.v3 });
    assert.equal(a.elements.length, 10);
  });

  const cachedShell = "/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell";
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(cachedShell) ? cachedShell : undefined);
  browser = await chromium.launch({ headless: true, args: ["--use-angle=swiftshader", "--enable-unsafe-swiftshader", "--ignore-gpu-blocklist", "--enable-webgl"], ...(executablePath ? { executablePath } : {}) });

  const open = async (page, { renderer = "spatial_3d", focus = ids.v3 } = {}) => {
    await page.goto(base);
    await page.locator('[data-testid="shell-menu-toggle"], [data-testid="nav-view-engine"], [data-testid="view-engine-panel"]').first().waitFor();
    if (await page.getByTestId("view-engine-panel").count() === 0) {
      if (await page.getByTestId("nav-view-engine").count() === 0) {
        await page.getByTestId("shell-menu-toggle").click();
        await page.getByTestId("shell-menu-workshop").click();
      }
      await page.getByTestId("nav-view-engine").click();
    }
    await page.getByTestId("view-engine-panel").waitFor();
    await page.getByTestId("ve-renderer").waitFor();
    await page.getByTestId("ve-renderer").selectOption(renderer);
    await page.getByTestId("ve-declaration").selectOption("history_cone");
    await page.getByTestId("ve-focus").selectOption(focus);
    await page.getByTestId("ve-show").click();
    await page.getByTestId("ve-result").waitFor();
  };
  const state = async page => page.getByTestId("ve3d-state").evaluate(el => ({ ...el.dataset }));
  const waitState = async (page, pred, what) => {
    const deadline = Date.now() + 15000;
    for (;;) {
      const s = await state(page).catch(() => null);
      if (s && pred(s)) return s;
      if (Date.now() > deadline) throw Error(`timeout waiting for ${what}: ${JSON.stringify(s)}`);
      await page.waitForTimeout(100);
    }
  };
  // like waitForFunction, but a timeout reports what the page shows (the last action, the focus, the pan) instead of a bare timeout
  const until = async (page, fn, arg, what) => {
    try { await page.waitForFunction(fn, arg, { timeout: 20000 }); } catch (cause) {
      const seen = await page.evaluate(() => ({ last: document.querySelector('[data-testid="ve-last-action"]')?.outerHTML, view: document.querySelector('[data-testid="ve-view-state"]')?.outerHTML, events: document.querySelector('[data-testid="ve3d-state"]')?.dataset.xrEvents }));
      throw Error(`timeout waiting for ${what}: ${JSON.stringify(seen)} (${cause.message.split("\n")[0]})`);
    }
  };
  // The renderer samples the viewer pose, the controller poses and the thumbstick once per rendered frame, and three.js maps a new input source to a
  // controller when it handles the change: after a change made from outside, wait for frames that used it instead of a fixed pause (under a loaded
  // machine — the full gate runs ctest alongside — a fixed pause is not enough and the gesture lands on the stale pose).
  const rendered = async (page, n = 2) => { const f0 = Number((await state(page)).frames); await waitState(page, x => Number(x.frames) >= f0 + n, `${n} frames after a change made from outside`); };
  const positions = async page => JSON.parse((await state(page)).positions);
  const viewState = (page, key) => page.getByTestId("ve-view-state").getAttribute(key);

  // ── 2. no navigator.xr: the flat-screen 3D fallback, with a visible note ──
  const flat = await browser.newPage({ viewport: { width: 1400, height: 1100 } });
  flat.on("pageerror", error => pageErrors.push(error.message));
  await flat.addInitScript(() => { Object.defineProperty(navigator, "xr", { configurable: true, value: undefined }); });
  await check("without WebXR the 3D renderer runs on the flat screen: a visible note, a canvas that renders, the node count of the view model, the hint in Polish", async () => {
    await open(flat);
    const note = flat.getByTestId("ve3d-xr-note");
    await note.waitFor();
    await flat.waitForFunction(() => document.querySelector('[data-testid="ve3d-xr-note"]')?.getAttribute("data-kind") === "unavailable");
    assert.match(await note.innerText(), /^WebXR nie jest dostępne w tej przeglądarce \(brak navigator\.xr\)/);
    assert.equal(await flat.getByTestId("ve3d-error").count(), 0, await flat.getByTestId("ve3d-error").allInnerTexts().then(a => a.join()));
    for (const mode of ["immersive-vr", "immersive-ar"]) assert.equal(await flat.getByTestId(`ve3d-enter-${mode}`).isDisabled(), true, `${mode} is disabled, with the note saying why`);
    const model = await api("POST", "/api/view-model", { declaration: "history_cone", focus: ids.v3 });
    const s = await waitState(flat, x => Number(x.frames) > 3 && Number(x.nodes) > 0, "frames");
    assert.equal(Number(s.nodes), model.elements.length);
    assert.equal(Number(s.wedges), 4);
    assert.equal(s.mode, "orbit"); assert.equal(s.xr, "none");
    assert.match(s.hint, /^Sterowanie \(3D, orbit\)/);
    assert.match(s.hint, /Strzałki teraz: płaszczyzna/);
    assert.match(s.hint, /Przeciągnij: obrót/);
    assert.equal(await flat.locator('[data-testid="ve-canvas"]').count(), 0, "the 2D svg is not drawn when the 3D renderer is chosen");
    assert.match(await flat.getByTestId("ve-counts").innerText(), /^Elementów: 10,/);
    // the canvas really draws: a screenshot of it has many distinct colours (the clear colour alone would be one)
    const png = (await flat.getByTestId("ve3d-host").screenshot()).toString("base64");
    const colours = await flat.evaluate(async b64 => {
      const img = new Image(); img.src = `data:image/png;base64,${b64}`; await img.decode();
      const c = document.createElement("canvas"); c.width = img.width; c.height = img.height;
      const g = c.getContext("2d"); g.drawImage(img, 0, 0);
      const d = g.getImageData(0, 0, c.width, c.height).data, seen = new Set();
      for (let i = 0; i < d.length; i += 16) seen.add((d[i] >> 3) << 10 | (d[i + 1] >> 3) << 5 | d[i + 2] >> 3);
      return seen.size;
    }, png);
    assert.ok(colours > 20, `the 3D canvas shows nodes, rings and labels (distinct colours: ${colours})`);
  });

  await check("the same view model in 3D: look around - the sector you face is emphasised and turning swaps it (the world stands still, the user turns); labels follow the policy; select and focus", async () => {
    await flat.getByTestId("ve3d-mode-look").click();
    let s = await waitState(flat, x => x.mode === "look", "look mode");
    assert.equal(s.sector, "past", "facing ahead faces the history");
    const alpha0 = JSON.parse(s.alpha);
    await flat.getByTestId("ve3d-host").scrollIntoViewIfNeeded();
    const box = await flat.getByTestId("ve3d-host").boundingBox();
    const cx = box.x + box.width / 2, cy = box.y + box.height / 2;
    // dragging right by 262 px turns the view 90 degrees to the left (callers are at -90 degrees)
    await flat.mouse.move(cx, cy); await flat.mouse.down(); await flat.mouse.move(cx + 131, cy, { steps: 6 }); await flat.mouse.move(cx + 262, cy, { steps: 6 }); await flat.mouse.up();
    s = await waitState(flat, x => x.sector === "callers", "the callers sector after turning left");
    assert.ok(Math.abs(Number(s.azimuth) + 90) < 3, `azimuth ${s.azimuth}`);
    const alpha1 = JSON.parse(s.alpha);
    assert.ok(alpha1[ids.caller_a] > alpha0[ids.caller_a], "the callers become more opaque when faced");
    assert.ok(alpha1[ids.v4] < alpha0[ids.v4], "the history fades when you turn away");
    assert.ok(Number(s.labels) < 10, "labels are on a policy, not all at once");
    await flat.keyboard.press("l");
    s = await waitState(flat, x => Number(x.labels) === 10, "all labels after the toggle");
    await flat.keyboard.press("l");
    // back to orbit, click a node to select it, F focuses it
    await flat.getByTestId("ve3d-mode-orbit").click();
    s = await waitState(flat, x => x.mode === "orbit", "orbit mode");
    const seen = await waitState(flat, x => JSON.parse(x.positions).some(p => p.id === ids.libx && p.visible && x.mode === "orbit"), "the dependency node on screen in orbit mode");
    const target = JSON.parse(seen.positions).find(p => p.id === ids.libx);
    await flat.getByTestId("ve3d-host").scrollIntoViewIfNeeded();
    const host = await flat.getByTestId("ve3d-host").boundingBox();
    await flat.mouse.click(host.x + target.x, host.y + target.y);
    await flat.getByTestId("ve3d-selected").waitFor();
    assert.equal(await flat.getByTestId("ve3d-selected").getAttribute("data-id"), ids.libx);
    await flat.keyboard.press("f");
    await flat.waitForFunction(id => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") === id, ids.libx);
    // the keyboard drives the same reducer: a plain arrow moves the scene (panX)
    await flat.keyboard.press("ArrowRight");
    assert.equal(await viewState(flat, "data-pan-x"), "40");
  });

  await check("the renderer is data: an overlay reverses the direction of the past and the scene follows; unusable settings are reported, not hidden", async () => {
    await api("POST", "/api/views/write", { overlay: "p28x", op: { view: "view_renderers", op: "modify", id: "spatial_3d", patch: { spatial: { direction_offset_deg: 180 } } } });
    await open(flat);
    const before = await waitState(flat, x => Number(x.nodes) > 0, "the scene");
    const model = await api("POST", "/api/view-model", { declaration: "history_cone", focus: ids.v3 });
    const pos = Object.fromEntries((JSON.parse(before.positions)).map(p => [p.id, p]));
    assert.ok(pos[ids.v4].wz > 0, "with the past reversed the history is behind (+z)");
    assert.ok(pos[ids.ship].wz < 0, "and the future is ahead (-z)");
    assert.ok(model.elements.length === Object.keys(pos).length);
    await api("POST", "/api/views/write", { overlay: "p28x", op: { view: "view_renderers", op: "modify", id: "spatial_3d", patch: { blend: { curve: "not a curve" } } } });
    await open(flat);
    assert.match(await flat.getByTestId("ve3d-config-notes").innerText(), /blend\.curve/);
    await api("POST", "/api/views/write", { overlay: "p28x", remove: true, view: "view_renderers", id: "spatial_3d" });
  });
  await flat.close();

  // ── 4. WebXR with the emulation stub ──
  const xrPage = await browser.newPage({ viewport: { width: 1400, height: 1100 } });
  xrPage.on("pageerror", error => { pageErrors.push(error.message); if (process.env.P28_DEBUG) console.log("PAGEERROR", error.message); });
  if (process.env.P28_DEBUG) xrPage.on("console", m => console.log("PAGE", m.text()));
  await xrPage.addInitScript(XR_STUB);
  await check("WebXR emulation: feature detection says VR yes / AR no, entering immersive-vr works, the in-scene hint switches to the XR bindings", async () => {
    await open(xrPage);
    await xrPage.waitForFunction(() => document.querySelector('[data-testid="ve3d-xr-note"]')?.getAttribute("data-kind") === "status");
    assert.match(await xrPage.getByTestId("ve3d-xr-note").innerText(), /^WebXR: VR obsługiwane, AR nieobsługiwane\./);
    assert.equal(await xrPage.getByTestId("ve3d-enter-immersive-vr").isDisabled(), false);
    assert.equal(await xrPage.getByTestId("ve3d-enter-immersive-ar").isDisabled(), true);
    await waitState(xrPage, x => Number(x.nodes) > 0, "the scene");
    await xrPage.getByTestId("ve3d-enter-immersive-vr").click();
    const s = await waitState(xrPage, x => x.xr === "immersive-vr", "an XR session");
    assert.equal(await xrPage.getByTestId("ve3d-xr-error").count(), 0, await xrPage.getByTestId("ve3d-xr-error").allInnerTexts().then(a => a.join()));
    assert.match(await xrPage.getByTestId("ve3d-xr-note").innerText(), /Sesja XR aktywna: immersive-vr\./);
    await waitState(xrPage, x => /Wybór:/.test(x.hint), "the XR hint");
    const hint = (await state(xrPage)).hint;
    assert.match(hint, /^Sterowanie \(3D, immersive-vr\)/);
    assert.match(hint, /Trzymaj: .*L-trigger = Shift/);
    assert.match(hint, /Drążek teraz: płaszczyzna|Drążek teraz: .*Płaszczyzna/);
    assert.match(hint, /Duże gesty \(tylko zgrubne ruchy sceny\)/);
    assert.match(hint, /Zapisane tylko jako dane \(głos, bez obsługi\): 1/);
    assert.equal(await xrPage.evaluate(() => window.__xr.state.started.join()), "immersive-vr");
    const frames = Number(s.frames);
    await waitState(xrPage, x => Number(x.frames) > frames, "frames in the session");
  });

  await check("XR input through the same reducer: right trigger and pinch+gaze focus on the pointed / looked-at element, the thumbstick moves the scene, a held controller button is a modifier", async () => {
    const ctl = await xrPage.evaluate(() => window.__xr.addSource({ handedness: "right" }));
    const left = await xrPage.evaluate(() => window.__xr.addSource({ handedness: "left" }));
    await rendered(xrPage);
    if (process.env.P28_DEBUG) console.log("SOURCES", await xrPage.evaluate(() => window.__xr.state.session.inputSources.length));
    const at = id => positions(xrPage).then(ps => { const p = ps.find(x => x.id === id); return [p.wx, p.wy, p.wz]; });
    // point the right controller (held at the chest) at v4 and pull the trigger
    const v4 = await at(ids.v4), hand = [0.25, -0.4, -0.3];
    await xrPage.evaluate(([i, m]) => window.__xr.pose(i, m), [ctl, lookAlong(hand, sub(v4, hand))]);
    await rendered(xrPage);
    await xrPage.evaluate(i => window.__xr.fire(i, "selectstart"), ctl);
    await until(xrPage, id => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") === id, ids.v4, "the right trigger to focus the pointed element");
    assert.equal(await xrPage.getByTestId("ve-last-action").getAttribute("data-binding"), "xr.focus.trigger");
    await xrPage.evaluate(i => window.__xr.fire(i, "selectend"), ctl);
    // the thumbstick left = the arrows: plain = the plane (pan -40)
    const pan0 = Number(await viewState(xrPage, "data-pan-x"));
    await xrPage.evaluate(i => window.__xr.axes(i, -1, 0), ctl);
    await xrPage.waitForFunction(p => Number(document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-pan-x")) === p - 40, pan0);
    assert.equal(await xrPage.getByTestId("ve-last-action").getAttribute("data-binding"), "xr.plane.left");
    await xrPage.evaluate(i => window.__xr.axes(i, 0, 0), ctl);
    await rendered(xrPage);
    // hold the left trigger (a modifier: Shift), then the same stick direction is time, not the plane
    await xrPage.evaluate(i => window.__xr.fire(i, "selectstart"), left);
    await waitState(xrPage, x => /Drążek teraz: Shift: Czas: wstecz/.test(x.hint), "the hint with Shift held");
    assert.equal(await xrPage.getByTestId("ve-last-action").getAttribute("data-binding"), "xr.plane.left", "holding a modifier button is not a selection and does nothing by itself");
    const focus0 = await viewState(xrPage, "data-focus");
    await xrPage.evaluate(i => window.__xr.axes(i, -1, 0), ctl);
    await xrPage.waitForFunction(() => document.querySelector('[data-testid="ve-last-action"]')?.getAttribute("data-binding") === "xr.depth_time.left");
    await xrPage.evaluate(i => window.__xr.axes(i, 0, 0), ctl);
    await xrPage.waitForFunction(f => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") !== f, focus0);
    await xrPage.evaluate(i => window.__xr.fire(i, "selectend"), left);
    await waitState(xrPage, x => /Drążek teraz: płaszczyzna|Drążek teraz: .*Płaszczyzna/.test(x.hint), "the hint after the modifier is released");
  });

  await check("XR gestures: a pinch on a hand with the gaze ray focuses the looked-at element; a large swipe is only a coarse scene move; voice does nothing", async () => {
    // the left controller is put down and the left hand is tracked instead (a device has two input slots)
    await xrPage.evaluate(() => window.__xr.removeSource(1));
    await rendered(xrPage);
    await xrPage.evaluate(() => window.__xr.addSource({ handedness: "left", hand: true }));
    await rendered(xrPage);
    const hand = 1;
    const ps = await positions(xrPage);
    const v2 = ps.find(p => p.id === ids.v2);
    // turn the head to v2 (the viewer pose is the gaze)
    await xrPage.evaluate(m => window.__xr.viewer(m), lookAlong([0, 0, 0], [v2.wx, v2.wy, v2.wz]));
    await rendered(xrPage);
    await xrPage.evaluate(i => window.__xr.fire(i, "selectstart"), hand);
    await until(xrPage, id => document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-focus") === id, ids.v2, "the pinch with the gaze to focus the looked-at element");
    assert.equal(await xrPage.getByTestId("ve-last-action").getAttribute("data-binding"), "xr.focus.gaze");
    await xrPage.evaluate(m => window.__xr.viewer(m), lookAlong([0, 0, 0], [0, 0, -1]));
    // a large swipe of the right controller from right to left within 300 ms
    const pan0 = Number(await viewState(xrPage, "data-pan-x"));
    await xrPage.evaluate(() => window.__xr.sweep(0, [0.35, -0.3, -0.3], [-0.35, -0.3, -0.3], 300));
    await until(xrPage, p => Number(document.querySelector('[data-testid="ve-view-state"]')?.getAttribute("data-pan-x")) === p - 200, pan0, "a large swipe to move the scene coarsely");
    assert.equal(await xrPage.getByTestId("ve-last-action").getAttribute("data-binding"), "xr.coarse.left");
    // the voice binding is recorded: the kernel says so, and the hint counts it; nothing in the scene reacts to a phrase
    const xr = await api("POST", "/api/view-interaction", { device: "xr" });
    assert.equal(xr.bindings.find(b => b.id === "xr.voice.zoom.in").status, "recorded");
    assert.equal(await viewState(xrPage, "data-zoom"), "1");
  });

  await check("leaving XR returns to the flat screen with the note updated", async () => {
    await xrPage.getByTestId("ve3d-end-xr").click();
    await waitState(xrPage, x => x.xr === "none", "the session to end");
    assert.doesNotMatch(await xrPage.getByTestId("ve3d-xr-note").innerText(), /Sesja XR aktywna/);
    assert.equal(await xrPage.getByTestId("ve3d-end-xr").count(), 0);
    const s = await state(xrPage);
    assert.match(s.hint, /^Sterowanie \(3D, (orbit|look)\)/);
  });

  assert.deepEqual(pageErrors, []);
  complete.complete();
  console.log(`[p28-webxr-native] ${groups.length}/8 groups passed`);
} catch (error) {
  console.error(serverLog.slice(-2000));
  throw error;
} finally {
  await browser?.close();
  await stopChild(child).catch(() => {});
  for (const dir of dirs) rmSync(dir, { recursive: true, force: true });
}
