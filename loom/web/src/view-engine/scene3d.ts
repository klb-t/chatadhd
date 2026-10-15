// The 3D renderer of the visualization engine (P28 step 3), the part that needs three.js - loaded lazily, so the normal bundle does not carry it.
// It draws a `Spatial3D` (spatial.ts: sector angles -> real directions, depth -> distance) on a flat screen (orbit around the scene, or look around from
// the user's place) and in WebXR (immersive-vr / immersive-ar). The world stands still; the user turns. The sector blend by gaze angle, label policy,
// distances and sizes are the settings of the `view_renderers` entry. Input is reported as events; what they MEAN (bindings) is decided by the panel
// with interaction.ts / xr-input.ts, the same reducer as the keyboard.
import {
  AmbientLight, BufferGeometry, CanvasTexture, CircleGeometry, Color, DirectionalLight, DoubleSide, Float32BufferAttribute, Group, LineBasicMaterial, LineLoop, LineSegments,
  Mesh, MeshBasicMaterial, MeshLambertMaterial, Object3D, PerspectiveCamera, PlaneGeometry, Quaternion, Raycaster, RingGeometry, Scene, SphereGeometry, Sprite, SpriteMaterial, SRGBColorSpace, Vector2, Vector3, WebGLRenderer,
} from "three";
import type { Spatial3D, SpatialConfig, Mark3 } from "./spatial";
import { azimuthOf, blendFor, labelVisible, sectorWeight } from "./spatial";
import { SwipeDetector, TwoHandRotateDetector, pickByRay, readGestureSettings, stickGesture } from "./xr-input";
import type { PickTarget, XrEvent } from "./xr-input";

export type ScreenMode = "orbit" | "look";
export type XrMode = "immersive-vr" | "immersive-ar";
export interface SceneViewState { panX: number; panY: number; zoom: number; angle: number; labels: boolean }
export interface XrPayload { event: XrEvent; picked: string | null }
export interface SceneCallbacks {
  /** A pointer click on the flat screen picked an element (or nothing). */
  onPick(id: string | null): void;
  /** An XR input (pinch with the gaze, trigger, thumbstick, large gesture) with the element it points at. */
  onXr(payload: XrPayload): void;
  /** The controller buttons that are down now (held modifiers are derived from them by the bindings). */
  onXrButtons(down: string[]): void;
  /** The sector the user faces changed (id or null). */
  onGaze(sector: string | null, azimuth: number): void;
  onSession(state: "start" | "end", mode: XrMode | null): void;
}
export interface SceneStats { nodes: number; edges: number; wedges: number; labels: number; frames: number; mode: string; xr: string; azimuth: number; sector: string | null; hint: string; alpha: Record<string, number>; scale: Record<string, number> }
export interface SceneHandle {
  setSpatial(spatial: Spatial3D, selected: string | null): void;
  setViewState(v: SceneViewState): void;
  setHint(lines: string[]): void;
  setMode(mode: ScreenMode): void;
  setSelected(id: string | null): void;
  enter(mode: XrMode, features: { required?: string[]; optional?: string[] }, transparent: boolean): Promise<void>;
  end(): Promise<void>;
  stats(): SceneStats;
  /** Screen position (CSS px relative to the host) of every node, for pointing and for tests. */
  screenPositions(): Array<{ id: string; x: number; y: number; visible: boolean }>;
  dispose(): void;
}

const BG = 0x0f1218;
const rad = (d: number) => (d * Math.PI) / 180;
const clamp = (v: number, lo: number, hi: number) => Math.max(lo, Math.min(hi, v));

function textTexture(lines: string[], opts: { px: number; color: string; background?: string; pad?: number; width?: number; align?: "left" | "center" }): { texture: CanvasTexture; aspect: number } {
  const pad = opts.pad ?? 8, font = `${opts.px}px system-ui, sans-serif`;
  const probe = document.createElement("canvas").getContext("2d");
  if (!probe) throw new Error("canvas_2d_unavailable");
  probe.font = font;
  const width = Math.ceil(opts.width ?? Math.max(...lines.map(l => probe.measureText(l).width), 1) + pad * 2);
  const lineH = Math.round(opts.px * 1.3), height = lines.length * lineH + pad * 2;
  const canvas = document.createElement("canvas");
  canvas.width = width; canvas.height = height;
  const ctx = canvas.getContext("2d");
  if (!ctx) throw new Error("canvas_2d_unavailable");
  if (opts.background) { ctx.fillStyle = opts.background; ctx.fillRect(0, 0, width, height); }
  ctx.font = font; ctx.textBaseline = "top";
  lines.forEach((l, i) => {
    if (!opts.background) { ctx.lineWidth = 4; ctx.strokeStyle = "rgba(0,0,0,0.85)"; ctx.strokeText(l, pad, pad + i * lineH); }
    ctx.fillStyle = opts.color; ctx.fillText(l, pad, pad + i * lineH);
  });
  const texture = new CanvasTexture(canvas);
  texture.colorSpace = SRGBColorSpace;
  return { texture, aspect: width / height };
}
function haloTexture(): CanvasTexture {
  const c = document.createElement("canvas"); c.width = c.height = 64;
  const ctx = c.getContext("2d");
  if (!ctx) throw new Error("canvas_2d_unavailable");
  const g = ctx.createRadialGradient(32, 32, 0, 32, 32, 32);
  g.addColorStop(0, "rgba(255,255,255,1)"); g.addColorStop(1, "rgba(255,255,255,0)");
  ctx.fillStyle = g; ctx.fillRect(0, 0, 64, 64);
  return new CanvasTexture(c);
}

interface NodeObj { mark: Mark3; mesh: Mesh; halo: Sprite | null; focusRing: Mesh | null; label: Sprite | null; badge: Sprite | null; material: MeshLambertMaterial }

export function createScene(host: HTMLElement, cfg: SpatialConfig, renderer: { xr?: unknown; screen?: Record<string, unknown> }, cb: SceneCallbacks, gestureSettings?: unknown): SceneHandle {
  const gl = new WebGLRenderer({ antialias: true, alpha: true });
  gl.setPixelRatio(Math.min(window.devicePixelRatio || 1, 2));
  gl.xr.enabled = true;
  gl.domElement.dataset.testid = "ve3d-canvas";
  gl.domElement.style.cssText = "display:block;width:100%;height:100%;touch-action:none;outline:none";
  gl.domElement.tabIndex = 0;
  host.appendChild(gl.domElement);
  const screenCfg = (renderer.screen ?? {}) as { orbit?: Record<string, number>; look?: Record<string, number>; fov_deg?: number };
  const orbitCfg = { distance_m: 9, min_m: 1.5, max_m: 30, yaw_deg: 25, pitch_deg: 18, fit: true, ...(screenCfg.orbit ?? {}) } as { distance_m: number; min_m: number; max_m: number; yaw_deg: number; pitch_deg: number; fit: boolean | number };
  const lookFov = screenCfg.look?.fov_deg ?? 70, orbitFov = screenCfg.fov_deg ?? 55;

  const scene = new Scene();
  scene.background = new Color(BG);
  const camera = new PerspectiveCamera(orbitFov, 1, 0.05, 200);
  scene.add(camera);
  scene.add(new AmbientLight(0xffffff, 1.4));
  const sun = new DirectionalLight(0xffffff, 1.6); sun.position.set(2, 5, 3); scene.add(sun);
  const root = new Group(); scene.add(root);          // the scene content: the coarse scene move / rotate / zoom is applied here, never to the user
  const world = new Group(); root.add(world);
  const userMarker = new Mesh(new RingGeometry(0.12, 0.16, 24), new MeshBasicMaterial({ color: 0xffffff, side: DoubleSide, transparent: true, opacity: 0.8 }));
  userMarker.rotation.x = -Math.PI / 2; userMarker.position.y = cfg.groundYM + 0.01; scene.add(userMarker);

  let spatial: Spatial3D | null = null, nodes: NodeObj[] = [], selected: string | null = null;
  let view: SceneViewState = { panX: 0, panY: 0, zoom: 1, angle: 0, labels: false };
  let mode: ScreenMode = "orbit";
  let yaw = rad(orbitCfg.yaw_deg), pitch = rad(orbitCfg.pitch_deg), dist = orbitCfg.distance_m;
  let lookYaw = 0, lookPitch = 0, zoomedByUser = false;
  let frames = 0, xrMode: XrMode | null = null, session: XRSession | null = null;
  let azimuth = 0, sector: string | null = null, lastBlendAz = Number.NaN, dirty = true;
  let hintLines: string[] = [], hintMesh: Mesh | null = null;
  let blendSnapshot: Record<string, number> = {}, scaleSnapshot: Record<string, number> = {};
  const disposables: Array<{ dispose(): void }> = [];
  const halo = haloTexture(); disposables.push(halo);
  const labelCache = new Map<string, { texture: CanvasTexture; aspect: number }>();
  const labelTex = (text: string, key: string, px: number, color: string) => {
    const k = `${key}|${text}`;
    let t = labelCache.get(k);
    if (!t) { t = textTexture([text], { px, color }); labelCache.set(k, t); }
    return t;
  };
  const fit = (s: string, max: number) => (s.length > max ? `${s.slice(0, Math.max(1, max - 1))}…` : s);

  // ── content ──
  function clearWorld() {
    for (const o of [...world.children]) {
      world.remove(o);
      o.traverse(x => {
        const m = x as Mesh;
        m.geometry?.dispose?.();
        const mat = (m as unknown as { material?: { dispose(): void } | Array<{ dispose(): void }> }).material;
        if (Array.isArray(mat)) mat.forEach(y => y.dispose()); else mat?.dispose();
      });
    }
    for (const t of labelCache.values()) t.texture.dispose();
    labelCache.clear();
    nodes = [];
  }
  function build(s: Spatial3D) {
    clearWorld();
    spatial = s;
    const far = Math.max(cfg.farM, s.maxDistance, 1);
    if (cfg.groundShow) {
      const ringMat = new LineBasicMaterial({ color: 0x8899aa, transparent: true, opacity: Math.min(1, cfg.groundOpacity * 4) });
      for (const r of [cfg.nearM, (cfg.nearM + far) / 2, far]) {
        const pts: number[] = [];
        for (let i = 0; i < 64; i++) { const a = (i / 64) * Math.PI * 2; pts.push(Math.cos(a) * r, cfg.groundYM, Math.sin(a) * r); }
        const g = new BufferGeometry(); g.setAttribute("position", new Float32BufferAttribute(pts, 3));
        world.add(new LineLoop(g, ringMat));
      }
      s.wedges.forEach((w, i) => {
        const theta = rad(90 - w.azimuth);
        const geo = new CircleGeometry(far, 48, theta - rad(w.widthDeg) / 2, rad(w.widthDeg));
        const m = new Mesh(geo, new MeshBasicMaterial({ color: new Color().setHSL((i * 0.19) % 1, 0.5, 0.55), transparent: true, opacity: cfg.groundOpacity * 1.5, side: DoubleSide, depthWrite: false }));
        m.rotation.x = -Math.PI / 2; m.position.y = cfg.groundYM; m.userData.wedge = w.id;
        world.add(m);
        const text = typeof w.group.label === "string" ? w.group.label : (w.group.label?.pl ?? w.group.label?.en ?? w.id);
        const tex = labelTex(fit(text, cfg.labelMaxChars), "wedge", 40, "#cfd8e3");
        const sp = new Sprite(new SpriteMaterial({ map: tex.texture, transparent: true, depthWrite: false, opacity: 0.8 }));
        sp.scale.set(cfg.labelWorldHeightM * 2 * tex.aspect, cfg.labelWorldHeightM * 2, 1);
        const dir = new Vector3(Math.sin(rad(w.azimuth)), 0, -Math.cos(rad(w.azimuth)));
        sp.position.copy(dir.multiplyScalar(far * 0.96)).setY(cfg.groundYM + 0.3);
        sp.userData.wedgeLabel = w.id;
        world.add(sp);
      });
    }
    for (const m of s.marks) {
      const material = new MeshLambertMaterial({ color: new Color().setHSL(m.hue / 360, m.saturation, m.lightness), transparent: true, opacity: m.alpha });
      const mesh = new Mesh(new SphereGeometry(1, 20, 14), material);
      mesh.position.set(m.x, m.y, m.z); mesh.scale.setScalar(m.radiusM); mesh.userData.id = m.id;
      world.add(mesh);
      let haloSprite: Sprite | null = null, ring: Mesh | null = null, label: Sprite | null = null, badge: Sprite | null = null;
      if (m.blur > 0) {
        haloSprite = new Sprite(new SpriteMaterial({ map: halo, color: material.color, transparent: true, opacity: 0.5 * m.alpha, depthWrite: false }));
        haloSprite.position.copy(mesh.position); haloSprite.scale.setScalar(m.radiusM * (2 + m.blur * 0.8)); world.add(haloSprite);
      }
      if (m.focus) {
        ring = new Mesh(new SphereGeometry(1, 16, 10), new MeshBasicMaterial({ color: 0xffffff, wireframe: true, transparent: true, opacity: 0.55 }));
        ring.position.copy(mesh.position); ring.scale.setScalar(m.radiusM * 1.45); world.add(ring);
      }
      const lt = labelTex(fit(m.label, cfg.labelMaxChars), "label", 48, m.focus ? "#ffffff" : "#e6edf5");
      label = new Sprite(new SpriteMaterial({ map: lt.texture, transparent: true, depthWrite: false, depthTest: false }));
      label.scale.set(cfg.labelWorldHeightM * lt.aspect, cfg.labelWorldHeightM, 1);
      label.position.set(m.x, m.y + m.radiusM + cfg.labelWorldHeightM * 0.8, m.z); label.renderOrder = 10; label.visible = false;
      label.userData.id = m.id; world.add(label);
      if (cfg.badges && m.badge) {
        const bt = labelTex(fit(m.badge, 14), "badge", 36, "#ffd479");
        badge = new Sprite(new SpriteMaterial({ map: bt.texture, transparent: true, depthWrite: false, depthTest: false }));
        badge.scale.set(cfg.labelWorldHeightM * 0.7 * bt.aspect, cfg.labelWorldHeightM * 0.7, 1);
        badge.position.set(m.x + m.radiusM * 1.1, m.y + m.radiusM * 1.1, m.z); badge.renderOrder = 11; world.add(badge);
      }
      nodes.push({ mark: m, mesh, halo: haloSprite, focusRing: ring, label, badge, material });
    }
    const at = new Map(s.marks.map(m => [m.id, m]));
    const pos: number[] = [];
    for (const e of s.edges) {
      const a = at.get(e.from), b = at.get(e.to);
      if (a && b) pos.push(a.x, a.y, a.z, b.x, b.y, b.z);
    }
    if (pos.length) {
      const g = new BufferGeometry(); g.setAttribute("position", new Float32BufferAttribute(pos, 3));
      world.add(new LineSegments(g, new LineBasicMaterial({ color: 0xaab6c5, transparent: true, opacity: cfg.edgeOpacity })));
    }
    lastBlendAz = Number.NaN; dirty = true;
    fitOrbit();
  }

  // ── view state, camera ──
  /** The orbit distance that frames the whole scene (its far radius) in the narrower of the two fields of view, until the user zooms by hand. */
  function fitOrbit() {
    if (zoomedByUser || !orbitCfg.fit || !spatial) return;
    const vfov = rad(orbitFov), hfov = 2 * Math.atan(Math.tan(vfov / 2) * Math.max(0.2, camera.aspect));
    const radius = Math.max(cfg.farM, spatial.maxDistance, 1) * 1.1;
    dist = clamp(radius / Math.sin(Math.min(vfov, hfov) / 2), orbitCfg.min_m, orbitCfg.max_m);
  }
  /** The hint panel is at most as wide as the screen shows at its distance (a narrow phone screen); in XR it keeps its own width. */
  function fitHint() {
    if (!hintMesh) return;
    const hc = (renderer as { hint?: Record<string, number> }).hint ?? {};
    const distance = hc.distance_m ?? 1.25, widthM = hc.width_m ?? 1;
    const visibleW = 2 * distance * Math.tan(rad(camera.fov) / 2) * camera.aspect;
    hintMesh.scale.setScalar(xrMode ? 1 : Math.min(1, (0.96 * visibleW) / widthM));
  }
  function applyView() {
    root.position.set(view.panX * 0.0025, -view.panY * 0.0025, 0);
    root.rotation.y = -rad(view.angle);
    root.scale.setScalar(view.zoom);
    dirty = true;
  }
  const target = new Vector3(0, 0.6, 0);
  function placeCamera() {
    if (xrMode) return;
    if (mode === "orbit") {
      camera.fov = orbitFov;
      camera.position.set(target.x + dist * Math.sin(yaw) * Math.cos(pitch), target.y + dist * Math.sin(pitch), target.z + dist * Math.cos(yaw) * Math.cos(pitch));
      camera.lookAt(target);
    } else {
      camera.fov = lookFov;
      camera.position.set(0, 0, 0);
      camera.rotation.set(lookPitch, -lookYaw, 0, "YXZ");
    }
    camera.updateProjectionMatrix();
  }
  const headCam = () => (xrMode ? gl.xr.getCamera() : camera);
  const tmp = new Vector3(), tmpQ = new Quaternion();
  function gazeLocal(): Vector3 {
    headCam().getWorldDirection(tmp);
    root.getWorldQuaternion(tmpQ).invert();
    return tmp.applyQuaternion(tmpQ);
  }

  // ── the sector blend by gaze angle ──
  function applyBlend(force: boolean) {
    if (!spatial) return;
    const g = gazeLocal();
    azimuth = azimuthOf(g.x, g.z);
    const groups = spatial.wedges.map(w => w.group);
    if (!force && Math.abs(azimuth - lastBlendAz) < 0.2) return;
    lastBlendAz = azimuth;
    const blend = blendFor(spatial.marks, groups, azimuth, cfg);
    let best: { id: string; w: number } | null = null;
    for (const gp of groups) { const w = sectorWeight(azimuth, gp, cfg); if (!best || w > best.w) best = { id: gp.id, w }; }
    const now = best && best.w >= 0.5 ? best.id : null;
    if (now !== sector) { sector = now; cb.onGaze(sector, azimuth); }
    blendSnapshot = {}; scaleSnapshot = {};
    let labelsOn = 0;
    for (const n of nodes) {
      const b = blend.get(n.mark.id) ?? { alpha: 1, scale: 1, weight: 1 };
      n.material.opacity = n.mark.alpha * b.alpha;
      const sel = n.mark.id === selected;
      n.mesh.scale.setScalar(n.mark.radiusM * b.scale * (sel ? 1.3 : 1));
      if (n.halo) (n.halo.material as SpriteMaterial).opacity = 0.5 * n.mark.alpha * b.alpha;
      if (n.focusRing) n.focusRing.scale.setScalar(n.mark.radiusM * b.scale * 1.45);
      if (n.label) { n.label.visible = labelVisible(n.mark, b.weight, cfg, view.labels); if (n.label.visible) labelsOn++; }
      if (n.badge) n.badge.visible = b.weight >= 0.3;
      blendSnapshot[n.mark.id] = Math.round(n.material.opacity * 1000) / 1000;
      scaleSnapshot[n.mark.id] = Math.round(b.scale * 1000) / 1000;
    }
    labelCount = labelsOn;
    publish();
  }
  let labelCount = 0;

  // ── the in-scene hint panel (head-locked, always visible) ──
  function drawHint() {
    if (hintMesh) { camera.remove(hintMesh); hintMesh.geometry.dispose(); (hintMesh.material as MeshBasicMaterial).map?.dispose(); (hintMesh.material as MeshBasicMaterial).dispose(); hintMesh = null; }
    if (!hintLines.length) return;
    const hc = (renderer as { hint?: Record<string, number> }).hint ?? {};
    const distance = hc.distance_m ?? 1.25, down = hc.down_deg ?? 24, widthM = hc.width_m ?? 1, opacity = hc.opacity ?? 0.88, maxLines = hc.max_lines ?? 14;
    const lines = hintLines.slice(0, maxLines);
    const t = textTexture(lines, { px: 30, color: "#f1f5f9", background: "rgba(10,14,22,0.88)", pad: 14, width: 1100 });
    const mesh = new Mesh(new PlaneGeometry(widthM, widthM / t.aspect), new MeshBasicMaterial({ map: t.texture, transparent: true, opacity, depthTest: false, depthWrite: false }));
    mesh.renderOrder = 100;
    mesh.position.set(0, -Math.tan(rad(down)) * distance, -distance);
    mesh.userData.hint = true;
    camera.add(mesh); hintMesh = mesh; fitHint();
  }

  // ── published state (tests, the panel's status line) ──
  const stateEl = document.createElement("div");
  stateEl.dataset.testid = "ve3d-state"; stateEl.hidden = true;
  host.appendChild(stateEl);
  function stats(): SceneStats {
    return { nodes: nodes.length, edges: spatial?.edges.length ?? 0, wedges: spatial?.wedges.length ?? 0, labels: labelCount, frames, mode: xrMode ?? mode, xr: xrMode ?? "none", azimuth: Math.round(azimuth * 10) / 10, sector, hint: hintLines.join("\n"), alpha: blendSnapshot, scale: scaleSnapshot };
  }
  function publish() {
    const s = stats();
    stateEl.dataset.nodes = String(s.nodes); stateEl.dataset.edges = String(s.edges); stateEl.dataset.wedges = String(s.wedges); stateEl.dataset.labels = String(s.labels);
    stateEl.dataset.mode = s.mode; stateEl.dataset.xr = s.xr; stateEl.dataset.azimuth = String(s.azimuth); stateEl.dataset.sector = s.sector ?? "";
    stateEl.dataset.hint = s.hint; stateEl.dataset.alpha = JSON.stringify(s.alpha); stateEl.dataset.scale = JSON.stringify(s.scale);
    stateEl.dataset.frames = String(frames);
    stateEl.dataset.positions = JSON.stringify(positions());
    stateEl.dataset.xrEvents = xrLog.join(" ");
  }
  /** Screen position (CSS px in the canvas) and world position (metres, after the scene move) of every node: for pointing and for tests. */
  function positions() {
    const r = gl.domElement.getBoundingClientRect(), v = new Vector3();
    scene.updateMatrixWorld(); camera.updateMatrixWorld();
    return nodes.map(n => {
      n.mesh.getWorldPosition(v);
      const w = { wx: Math.round(v.x * 1000) / 1000, wy: Math.round(v.y * 1000) / 1000, wz: Math.round(v.z * 1000) / 1000 };
      v.project(camera);
      return { id: n.mark.id, x: Math.round(((v.x + 1) / 2) * r.width), y: Math.round(((1 - v.y) / 2) * r.height), visible: v.z > -1 && v.z < 1 && Math.abs(v.x) <= 1 && Math.abs(v.y) <= 1, ...w };
    });
  }

  // ── flat-screen pointer: drag turns, wheel moves, a short click picks ──
  const raycaster = new Raycaster(), ndc = new Vector2();
  function pickPointer(clientX: number, clientY: number): string | null {
    const r = gl.domElement.getBoundingClientRect();
    ndc.set(((clientX - r.left) / r.width) * 2 - 1, -(((clientY - r.top) / r.height) * 2 - 1));
    raycaster.setFromCamera(ndc, camera);
    const hits = raycaster.intersectObjects(nodes.map(n => n.mesh), false);
    return hits.length ? (hits[0].object.userData.id as string) : null;
  }
  let drag: { x: number; y: number; t: number; moved: boolean; id: number } | null = null;
  const pointers = new Map<number, { x: number; y: number }>();
  let pinchBase = 0;
  const onDown = (e: PointerEvent) => {
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size === 2) { const [a, b] = [...pointers.values()]; pinchBase = Math.hypot(a.x - b.x, a.y - b.y); }
    drag = { x: e.clientX, y: e.clientY, t: performance.now(), moved: false, id: e.pointerId };
    try { gl.domElement.setPointerCapture(e.pointerId); } catch (cause) { console.debug("setPointerCapture failed", cause); }
  };
  const onMove = (e: PointerEvent) => {
    if (!pointers.has(e.pointerId)) return;
    const prev = pointers.get(e.pointerId)!;
    pointers.set(e.pointerId, { x: e.clientX, y: e.clientY });
    if (pointers.size === 2) {
      const [a, b] = [...pointers.values()], d = Math.hypot(a.x - b.x, a.y - b.y);
      if (pinchBase > 0 && mode === "orbit") zoomedByUser = true;
      if (pinchBase > 0 && mode === "orbit") dist = clamp(dist * (pinchBase / d), orbitCfg.min_m, orbitCfg.max_m);
      pinchBase = d; if (drag) drag.moved = true; return;
    }
    if (!drag || drag.id !== e.pointerId) return;
    const dx = e.clientX - prev.x, dy = e.clientY - prev.y;
    if (Math.hypot(e.clientX - drag.x, e.clientY - drag.y) > 4) drag.moved = true;
    if (!drag.moved) return;
    if (mode === "orbit") { yaw -= dx * 0.008; pitch = clamp(pitch + dy * 0.006, -1.4, 1.4); }
    else { lookYaw -= dx * 0.006; lookPitch = clamp(lookPitch - dy * 0.006, -1.5, 1.5); }
  };
  const onUp = (e: PointerEvent) => {
    pointers.delete(e.pointerId);
    const d = drag; drag = null;
    if (d && !d.moved && performance.now() - d.t < 500 && !xrMode) cb.onPick(pickPointer(e.clientX, e.clientY));
  };
  const onWheel = (e: WheelEvent) => {
    if (mode !== "orbit") return;
    e.preventDefault();
    zoomedByUser = true;
    dist = clamp(dist * Math.exp(e.deltaY * 0.0012), orbitCfg.min_m, orbitCfg.max_m);
  };
  gl.domElement.addEventListener("pointerdown", onDown);
  gl.domElement.addEventListener("pointermove", onMove);
  gl.domElement.addEventListener("pointerup", onUp);
  gl.domElement.addEventListener("pointercancel", onUp);
  gl.domElement.addEventListener("wheel", onWheel, { passive: false });

  // ── XR input: pinch with the gaze, controller buttons, the thumbstick, large gestures ──
  const gestures = readGestureSettings(gestureSettings);
  const swipes = [new SwipeDetector(gestures.swipe), new SwipeDetector(gestures.swipe)];
  const rotate = new TwoHandRotateDetector(gestures.rotate);
  const stickState = new Map<number, { gesture: string | null; at: number }>();
  const down = new Set<string>();
  const stickCfg = ((renderer.xr ?? {}) as { stick?: { threshold?: number; repeat_ms?: number } }).stick ?? {};
  /** The XR events reported so far (the last ones), exposed in the published state: what the scene saw, whatever the panel did with it. */
  const xrLog: string[] = [];
  const emitXr = (p: XrPayload) => {
    xrLog.push(`${p.event.button ?? p.event.gesture}${p.picked ? `>${p.picked}` : ""}`);
    if (xrLog.length > 40) xrLog.shift();
    cb.onXr(p);
  };
  const targets = (): PickTarget[] => nodes.map(n => { const p = n.mesh.getWorldPosition(new Vector3()); return { id: n.mark.id, x: p.x, y: p.y, z: p.z, radius: n.mark.radiusM * root.scale.x }; });
  function pick(origin: Vector3, dir: Vector3): string | null {
    return pickByRay([origin.x, origin.y, origin.z], [dir.x, dir.y, dir.z], targets(), cfg.pickToleranceDeg);
  }
  function headRay(): string | null {
    const c = headCam(); const o = c.getWorldPosition(new Vector3()); const d = c.getWorldDirection(new Vector3());
    return pick(o, d);
  }
  function controllerRay(obj: Object3D): string | null {
    const o = obj.getWorldPosition(new Vector3());
    const d = new Vector3(0, 0, -1).applyQuaternion(obj.getWorldQuaternion(new Quaternion()));
    return pick(o, d);
  }
  const buttonName = (hand: string | undefined, kind: string) => `${hand && hand !== "none" ? hand : "right"}_${kind}`;
  const controllers = [0, 1].map(i => {
    const ctl = gl.xr.getController(i);
    const info: { hand: string | undefined; isHand: boolean } = { hand: undefined, isHand: false };
    ctl.addEventListener("connected", (ev: unknown) => {
      const src = (ev as { data?: XRInputSource }).data;
      info.hand = src?.handedness; info.isHand = !!src?.hand;
    });
    ctl.addEventListener("disconnected", () => {
      for (const kind of ["trigger", "squeeze", "primary"]) down.delete(buttonName(info.hand, kind));
      cb.onXrButtons([...down]);
      info.hand = undefined; info.isHand = false;
    });
    ctl.addEventListener("selectstart", () => {
      if (info.isHand) { emitXr({ event: { gesture: "pinch", gaze: true }, picked: headRay() }); return; }
      const b = buttonName(info.hand, "trigger"); down.add(b); cb.onXrButtons([...down]);
      emitXr({ event: { button: b }, picked: controllerRay(ctl) });
    });
    ctl.addEventListener("selectend", () => { if (!info.isHand) { down.delete(buttonName(info.hand, "trigger")); cb.onXrButtons([...down]); } });
    ctl.addEventListener("squeezestart", () => { const b = buttonName(info.hand, "squeeze"); down.add(b); cb.onXrButtons([...down]); emitXr({ event: { button: b }, picked: controllerRay(ctl) }); });
    ctl.addEventListener("squeezeend", () => { down.delete(buttonName(info.hand, "squeeze")); cb.onXrButtons([...down]); });
    scene.add(ctl);
    return { ctl, info };
  });
  function pollXr(now: number) {
    const src = session?.inputSources ?? [];
    let k = 0;
    for (const s of src) {
      const gp = s.gamepad;
      if (gp) {
        const g = stickGesture(gp.axes[2] ?? 0, gp.axes[3] ?? 0, stickCfg.threshold ?? 0.7);
        const st = stickState.get(k) ?? { gesture: null, at: 0 };
        if (g && (g !== st.gesture || now - st.at >= (stickCfg.repeat_ms ?? 350))) { emitXr({ event: { gesture: g }, picked: null }); stickState.set(k, { gesture: g, at: now }); }
        else if (!g) stickState.set(k, { gesture: null, at: 0 });
        else stickState.set(k, { gesture: g, at: st.at });
        const primary = gp.buttons[4]?.pressed ?? false, name = buttonName(s.handedness, "primary");
        if (primary && !down.has(name)) { down.add(name); cb.onXrButtons([...down]); emitXr({ event: { button: "primary" }, picked: null }); }
        else if (!primary && down.has(name)) { down.delete(name); cb.onXrButtons([...down]); }
      }
      k++;
    }
    controllers.forEach((c, i) => {
      if (!c.ctl.visible) return;
      const p = c.ctl.getWorldPosition(new Vector3());
      const g = swipes[i].push(now, [p.x, p.y, p.z]);
      if (g) emitXr({ event: { gesture: g }, picked: null });
    });
    if (controllers[0].ctl.visible && controllers[1].ctl.visible) {
      const l = controllers.find(c => c.info.hand === "left") ?? controllers[0], r = controllers.find(c => c.info.hand === "right") ?? controllers[1];
      const lp = l.ctl.getWorldPosition(new Vector3()), rp = r.ctl.getWorldPosition(new Vector3());
      const res = rotate.push(now, [lp.x, lp.y, lp.z], [rp.x, rp.y, rp.z]);
      if (res) emitXr({ event: { gesture: res.gesture }, picked: null });
    } else rotate.reset();
  }

  // ── loop ──
  const resize = () => {
    const w = Math.max(1, host.clientWidth), h = Math.max(1, host.clientHeight || Math.round(w * 0.62));
    if (!xrMode) { gl.setSize(w, h, false); camera.aspect = w / h; camera.updateProjectionMatrix(); fitOrbit(); fitHint(); }
    dirty = true;
  };
  const observer = typeof ResizeObserver !== "undefined" ? new ResizeObserver(resize) : null;
  observer?.observe(host);
  resize();
  gl.setAnimationLoop((time: number) => {
    frames++;
    if (!xrMode) { placeCamera(); fitHint(); }
    userMarker.visible = !xrMode && mode === "orbit";
    applyBlend(dirty); dirty = false;
    if (xrMode) pollXr(time);
    gl.render(scene, camera);
    if (frames % 20 === 1) publish();
  });

  async function enter(m: XrMode, features: { required?: string[]; optional?: string[] }, transparent: boolean) {
    const xr = (navigator as Navigator & { xr?: XRSystem }).xr;
    if (!xr) throw new Error("webxr_unavailable");
    const s = await xr.requestSession(m, { requiredFeatures: features.required ?? [], optionalFeatures: features.optional ?? [] });
    gl.xr.setReferenceSpaceType(((renderer.xr ?? {}) as { reference_space?: XRReferenceSpaceType }).reference_space ?? "local");
    await gl.xr.setSession(s);
    session = s; xrMode = m; fitHint();
    scene.background = transparent && m === "immersive-ar" ? null : new Color(BG);
    gl.setClearAlpha(transparent && m === "immersive-ar" ? 0 : 1);
    s.addEventListener("end", () => {
      session = null; xrMode = null; down.clear(); cb.onXrButtons([]);
      scene.background = new Color(BG); gl.setClearAlpha(1); resize(); fitHint(); dirty = true; cb.onSession("end", m); publish();
    });
    dirty = true; cb.onSession("start", m); publish();
  }

  return {
    setSpatial(s, sel) { selected = sel; build(s); applyBlend(true); },
    setViewState(v) { view = v; applyView(); },
    setHint(lines) { hintLines = lines; drawHint(); publish(); },
    setMode(m) { mode = m; dirty = true; },
    setSelected(id) { selected = id; dirty = true; lastBlendAz = Number.NaN; },
    enter,
    async end() { if (session) await session.end(); },
    stats,
    screenPositions() { return positions().map(({ id, x, y, visible }) => ({ id, x, y, visible })); },
    dispose() {
      gl.setAnimationLoop(null); observer?.disconnect();
      gl.domElement.removeEventListener("pointerdown", onDown); gl.domElement.removeEventListener("pointermove", onMove);
      gl.domElement.removeEventListener("pointerup", onUp); gl.domElement.removeEventListener("pointercancel", onUp); gl.domElement.removeEventListener("wheel", onWheel);
      clearWorld(); if (hintMesh) { hintMesh.geometry.dispose(); (hintMesh.material as MeshBasicMaterial).map?.dispose(); (hintMesh.material as MeshBasicMaterial).dispose(); }
      for (const d of disposables) d.dispose();
      gl.dispose(); gl.domElement.remove(); stateEl.remove();
    },
  };
}
