#!/usr/bin/env node
/** Browser acceptance against the production perspective module and B's exact hook. */
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import net from 'node:net';
import { chromium } from 'playwright';
import { build } from 'esbuild';
import { transformRenderer } from './renderer-transform.mjs';
import { runBenchmark } from './benchmark.mjs';
import { stopChild, suiteCompletionGuard } from '../../e2e/harness-lifecycle.mjs';
import { createModuleLoader, webRoot, repoRoot, reportRoot } from './test-loader.mjs';

const groups = [], expectedGroups = 13;
const complete = suiteCompletionGuard('graph-perspectives-browser', expectedGroups, groups);
const screenshots = path.join(reportRoot, 'screenshots');
mkdirSync(screenshots, { recursive: true });
const pageErrors = [], requests = [], timings = [], failures = [];
const catalog = JSON.parse(readFileSync(path.join(repoRoot, 'loom/data/graph_perspectives/catalog.json'), 'utf8'));
const labels = catalog.ui;
let server, browser;
let serverOutput = '';
async function check(name, fn) {
  const started = performance.now();
  try {
    await fn();
    timings.push({ name, wallMs: performance.now() - started });
    groups.push(name);
    console.log(`[graph-perspectives-browser] PASS ${name}`);
  } catch (error) {
    failures.push({ name, error: String(error?.stack ?? error) });
    console.error(`[graph-perspectives-browser] FAIL ${name}: ${error?.message ?? error}`);
  }
}
async function freePort() {
  const socket = net.createServer();
  await new Promise((resolve, reject) => { socket.once('error', reject); socket.listen(0, '127.0.0.1', resolve); });
  const port = socket.address().port;
  await new Promise(resolve => socket.close(resolve));
  return port;
}
async function waitForServer(url) {
  const deadline = Date.now() + 30_000;
  while (Date.now() < deadline) {
    if (server?.exitCode !== null && server?.exitCode !== undefined) throw new Error(`Harness exited: ${serverOutput}`);
    try { if ((await fetch(url)).ok) return; } catch {}
    await new Promise(resolve => setTimeout(resolve, 100));
  }
  throw new Error(`Harness did not start: ${serverOutput}`);
}
async function diagnostics(page) {
  return page.evaluate(async () => {
    const value = window.__graphPerspectives;
    if (!value?.selection) throw new Error('Perspective selection not available');
    const bytes = new TextEncoder().encode(value.packetAfter());
    const hash = await crypto.subtle.digest('SHA-256', bytes);
    return { selection: value.selection, perspective: value.perspective, canonicalPacketHash: Array.from(new Uint8Array(hash), byte => byte.toString(16).padStart(2, '0')).join(''), timings: { renderMs: value.renderMs, layoutMs: value.layoutMs }, packetUnchanged: value.packetBefore === value.packetAfter() };
  });
}
async function waitSelection(page, predicate, arg) {
  await page.waitForFunction(({ source, arg }) => {
    const value = window.__graphPerspectives;
    return value?.selection && new Function('value', 'arg', `return (${source})(value, arg)`)(value, arg);
  }, { source: predicate.toString(), arg });
  return diagnostics(page);
}
async function snapshot(page, name, fullPage = true) {
  await page.screenshot({ path: path.join(screenshots, name), fullPage });
}

try {
  const port = await freePort();
  const base = `http://127.0.0.1:${port}`;
  const url = `${base}/src/graph-perspectives/harness.html`;
  server = spawn(process.execPath, [path.join(webRoot, 'tests/graph-perspectives/harness-server.mjs'), '--port', String(port)], {
    cwd: repoRoot, stdio: ['ignore', 'pipe', 'pipe'], env: process.env,
  });
  for (const stream of [server.stdout, server.stderr]) stream.on('data', chunk => { serverOutput += chunk.toString(); if (serverOutput.length > 16000) serverOutput = serverOutput.slice(-16000); });
  await waitForServer(url);
  const fallback = '/root/.cache/ms-playwright/chromium_headless_shell-1194/chrome-linux/headless_shell';
  const executablePath = process.env.PLAYWRIGHT_CHROMIUM_EXECUTABLE || (existsSync(fallback) ? fallback : undefined);
  browser = await chromium.launch({ headless: true, args: ['--no-sandbox', '--disable-dev-shm-usage'], ...(executablePath ? { executablePath } : {}) });
  const desktop = await browser.newContext({ viewport: { width: 1440, height: 1000 } });
  const page = await desktop.newPage();
  page.on('pageerror', error => pageErrors.push(error.message));
  page.on('request', request => requests.push({ method: request.method(), url: request.url() }));
  await page.goto(url, { waitUntil: 'domcontentloaded' });
  await page.waitForFunction(() => !!window.__graphPerspectives?.selection);

  const initial = await diagnostics(page);
  const baseline = structuredClone(initial.perspective);
  const objectIds = value => value.selection.objects.map(item => item.ref.selector);
  async function level(value) {
    await page.getByRole('combobox', { name: labels.interfaceLevel, exact: true }).selectOption(value);
    await page.evaluate(() => new Promise(resolve => requestAnimationFrame(() => requestAnimationFrame(resolve))));
  }
  async function document(value) {
    await level('expert');
    if (!(await page.getByTestId('perspective-json').isVisible())) await page.locator('.gp-transfer > summary').click();
    await page.getByTestId('perspective-json').fill(JSON.stringify(value));
    await page.getByRole('button', { name: labels.applyJson, exact: true }).click();
    await page.waitForFunction(() => document.querySelector('[data-testid="perspective-view"]')?.getAttribute('aria-busy') === 'false');
    await waitSelection(page, (current, expected) => JSON.stringify(current.perspective) === JSON.stringify(expected), value);
  }
  async function reset() { await document(baseline); }
  async function preset(id) {
    await page.getByTestId('perspective-preset').selectOption(id);
    return waitSelection(page, (value, id) => value.selection.plan.structure === id, id);
  }
  async function focus(ref) {
    await level('advanced');
    if (!(await page.getByTestId('focus-selector').isVisible())) await page.locator('.gp-address > summary').click();
    await page.getByTestId('focus-selector').fill(JSON.stringify(ref));
    await page.getByRole('button', { name: labels.focusAction, exact: true }).click();
    return waitSelection(page, (value, ref) => value.perspective.focus.source === ref.source && value.perspective.focus.selector === ref.selector && value.selection.plan.focus.selector === ref.selector, ref);
  }
  function target(id) { return structuredClone(catalog.targets.find(item => item.id === id).ref); }
  async function clickNode(id, input = 'mouse') {
    const current = await diagnostics(page);
    const item = current.selection.objects.find(row => row.ref.selector === `/objects/${id}`);
    assert.ok(item, `Object ${id} must be selected before interaction`);
    // CSS escaping handles structured selector IDs without reinterpreting quotes.
    const node = page.locator(`svg [data-object-key=${JSON.stringify(item.key)}]`);
    if (input === 'keyboard') { await node.focus(); await page.keyboard.press('Enter'); }
    else await node.click();
    return waitSelection(page, (value, selector) => value.perspective.focus.selector === selector, item.ref.selector);
  }
  async function assertParity() {
    const value = await diagnostics(page);
    const repeated = await page.evaluate(async () => window.__graphPerspectives.reselect());
    assert.deepEqual(repeated.objects.map(item => item.key), value.selection.objects.map(item => item.key), 'headless and UI select identical representations');
    const drawn = await page.locator('svg [data-object-key]').evaluateAll(nodes => nodes.map(node => node.dataset.objectKey));
    assert.deepEqual(drawn.sort(), value.selection.objects.map(item => item.key).sort(), 'existing renderer receives all and only projected objects');
    assert.deepEqual(repeated.analysis, value.selection.analysis, 'headless/UI analysis membership matches');
  }

  await check('native resolved data and identical headless/UI projection', async () => {
    assert.equal(initial.selection.plan.errors.length, 0);
    assert.equal(initial.selection.plan.structure, catalog.controls.defaultPreset);
    assert.equal(await page.getByTestId('perspective-preset').inputValue(), catalog.controls.defaultPreset);
    assert.ok(initial.selection.objects.length > 1);
    assert.ok(initial.selection.plan.explanation.some(row => row.origin), 'native resolution carries origin');
    await assertParity();
    await snapshot(page, '01-computational-desktop.png');
  });
  await check('mouse keyboard and reversible object navigation', async () => {
    await clickNode('e_formula');
    await clickNode('e_input_a', 'keyboard');
    await page.getByRole('button', { name: labels.back, exact: true }).click();
    await waitSelection(page, value => value.perspective.focus.selector === '/objects/e_formula');
    await page.getByRole('button', { name: labels.forward, exact: true }).click();
    await waitSelection(page, value => value.perspective.focus.selector === '/objects/e_input_a');
    await assertParity();
  });
  await check('physical and logical projection preserve focus identity and placement', async () => {
    await reset();
    const physical = await preset('physical');
    const cell = physical.selection.objects.find(item => item.ref.selector === '/objects/e_cell');
    const before = await page.locator(`svg [data-object-key=${JSON.stringify(cell.key)}]`).getAttribute('transform');
    const logical = await preset('logical');
    const same = logical.selection.objects.find(item => item.ref.selector === '/objects/e_cell');
    assert.equal(logical.perspective.focus.selector, '/objects/e_cell');
    assert.equal(same.canonicalKey, cell.canonicalKey);
    assert.notDeepEqual(objectIds(physical).sort(), objectIds(logical).sort());
    assert.equal(await page.locator(`svg [data-object-key=${JSON.stringify(same.key)}]`).getAttribute('transform'), before);
    assert.ok(logical.selection.objects.some(item => item.ref.selector === '/objects/e_cell_logic' && item.canonicalKey === same.canonicalKey), 'alternate representation explicitly preserves identity');
    await assertParity();
    await page.getByRole('button', { name: labels.back, exact: true }).click();
    const back = await waitSelection(page, value => value.selection.plan.structure === 'physical');
    assert.deepEqual(back.selection.plan.relations, physical.selection.plan.relations, 'Back restores relation selection with structure');
    await page.getByRole('button', { name: labels.forward, exact: true }).click();
    const forward = await waitSelection(page, value => value.selection.plan.structure === 'logical');
    assert.deepEqual(forward.selection.plan.relations, logical.selection.plan.relations);
  });
  await check('equal resolution selects distinct computation and historical structures', async () => {
    await reset();
    const computation = await preset('computational');
    assert.ok(objectIds(computation).includes('/objects/e_formula'));
    assert.ok(objectIds(computation).includes('/objects/e_justification'));
    const decision = computation.selection.relations.find(item => item.kind === 'informs');
    assert.ok(decision.evidence === 'inferred' || decision.basis?.support?.length > 0, 'decision relation requires interpretation status or explicit evidence');
    const history = await preset('history');
    assert.equal(history.selection.plan.resolution, computation.selection.plan.resolution);
    assert.ok(objectIds(history).includes('/objects/e_comment'));
    assert.ok(!objectIds(history).includes('/objects/e_formula'));
    assert.equal(history.perspective.focus.selector, computation.perspective.focus.selector);
    await snapshot(page, '02-history-desktop.png');
  });
  await check('two snapshots retain canonical object and historical center', async () => {
    await reset();
    await page.getByRole('combobox', { name: labels.snapshot, exact: true }).selectOption('v1');
    const old = await waitSelection(page, value => value.selection.plan.snapshot === 'v1');
    const oldFocus = old.selection.objects.find(item => item.ref.selector === '/objects/e_cell' && item.ref.snapshot === 'v1');
    assert.ok(oldFocus, 'historical focus remains addressable');
    assert.equal(oldFocus.status, 'available', 'snapshot switch resolves the historical representation');
    assert.equal(oldFocus.properties.raw.attrs.perspective.value, 5);
    await page.getByRole('combobox', { name: labels.compareSnapshot, exact: true }).selectOption('v2');
    const comparison = await waitSelection(page, value => value.selection.plan.compareSnapshots.includes('v2'));
    const representations = comparison.selection.objects.filter(item => item.ref.selector === '/objects/e_cell');
    assert.equal(new Set(representations.map(item => item.canonicalKey)).size, 1);
    assert.deepEqual(representations.map(item => item.ref.snapshot).sort(), ['v1', 'v2'], JSON.stringify(representations.map(item => ({key:item.key, ref:item.ref})), null, 2));
    assert.ok(representations.every(item => item.status === 'available'));
    assert.deepEqual(representations.map(item => item.properties.raw.attrs.perspective.value).sort((a,b) => a-b), [5,7]);
    assert.equal(comparison.perspective.focus.snapshot, 'v1');
    const focusedKeys = await page.locator('svg .kb-node.focused').evaluateAll(nodes => nodes.map(node => node.dataset.objectKey));
    assert.deepEqual(focusedKeys, [oldFocus.key], 'historical representation is the rendered focus');
    const versionDiff = page.getByTestId('version-differences');
    assert.ok(await versionDiff.isVisible());
    const versionText = await versionDiff.innerText();
    assert.ok(versionText.includes('5') && versionText.includes('7'), `actual historical value difference is displayed: ${versionText}`);
    await snapshot(page, '03-snapshot-comparison.png');
    await page.getByRole('combobox', { name: labels.snapshot, exact: true }).selectOption('');
    const current = await waitSelection(page, value => value.selection.plan.snapshot === undefined && value.perspective.focus.snapshot === undefined);
    assert.equal(Object.hasOwn(current.selection.plan, 'snapshot'), false, 'no snapshot means an absent constraint, never an empty snapshot ID');
    assert.equal(Object.hasOwn(current.perspective.focus, 'snapshot'), false);
    assert.equal(current.perspective.focus.canonicalId, comparison.perspective.focus.canonicalId);
    const currentObject = current.selection.objects.find(item => item.canonicalKey === oldFocus.canonicalKey && item.status === 'available');
    assert.ok(currentObject, 'clearing the snapshot resolves the current canonical object');
    assert.equal(currentObject.properties.raw.attrs.perspective.value, 7);
    assert.deepEqual(await page.locator('svg .kb-node.focused').evaluateAll(nodes => nodes.map(node => node.dataset.objectKey)), [currentObject.key]);
  });
  await check('external profile field effective value and override provenance', async () => {
    await reset();
    await page.getByTestId('scenario-target').selectOption('provenance');
    const value = await waitSelection(page, value => value.selection.plan.structure === 'provenance' && value.perspective.focus.selector === '/objects/e_effective');
    for (const id of ['e_profile', 'e_profile_field', 'e_effective', 'e_override']) assert.ok(objectIds(value).includes(`/objects/${id}`), id);
    assert.ok(value.selection.relations.some(item => item.kind === 'overridden_by'));
    await clickNode('e_override', 'keyboard');
  });
  await check('live native effective values and override provenance change through the UI', async () => {
    await reset();
    await page.getByTestId('scenario-target').selectOption('live-provenance');
    const live = await waitSelection(page, value => value.selection.plan.structure === 'native-provenance' && value.perspective.focus.source === 'native-defaults');
    const selector = '/components/graph.perspective.budget/value';
    const sourceSelector = '/components/graph.perspective.budget/source';
    const effective = live.selection.objects.find(item => item.ref.selector === selector);
    assert.ok(effective, 'real native value is addressable in projected graph');
    assert.equal(effective.properties.value.render, 80);
    assert.equal(effective.properties.projection, 'read_only_native_response');
    assert.ok(effective.properties.sourcePointer.startsWith('/effectiveDefaults/'));
    assert.ok(live.selection.relations.some(item => item.kind === 'resolved_from' && item.from.selector === selector && item.to.selector === sourceSelector));
    assert.ok(live.selection.relations.some(item => item.kind === 'recorded_in' && item.from.selector === sourceSelector && item.to.selector === '/profile/overrides/graph.perspective.budget'));
    await level('advanced');
    const descriptor = catalog.capabilities.find(item => item.target === 'budget');
    const editor = page.locator('.gp-component').filter({ has: page.locator('summary', { hasText: descriptor.label }) });
    await editor.locator('summary').click();
    const next = { ...effective.properties.value, render: 96 };
    await editor.getByRole('textbox', { name: descriptor.label, exact: true }).fill(JSON.stringify(next));
    await editor.getByRole('button', { name: labels.applyComponent, exact: true }).click();
    const changed = await waitSelection(page, value => value.selection.plan.renderBudget === 96);
    const updated = changed.selection.objects.find(item => item.ref.selector === selector);
    assert.equal(updated.properties.value.render, 96);
    assert.equal(updated.canonicalKey, effective.canonicalKey);
    assert.equal(updated.properties.confidenceStatus, 'not_calibrated');
    assert.equal(updated.confidence, undefined);
    await assertParity();
    await level('basic');
    await page.evaluate(() => window.scrollTo(0, 0));
    await snapshot(page, '06-live-native-provenance.png', false);
  });
  await check('conversation source correction and current instruction', async () => {
    await reset();
    await page.getByTestId('scenario-target').selectOption('conversation');
    const value = await waitSelection(page, value => value.selection.plan.structure === 'conversation' && value.perspective.focus.selector === '/objects/e_conversation');
    for (const id of ['e_message', 'e_correction', 'e_instruction']) assert.ok(objectIds(value).includes(`/objects/${id}`), id);
    await clickNode('e_message');
    await clickNode('e_correction', 'keyboard');
    await clickNode('e_instruction');
  });
  await check('unrendered unloaded unavailable and unknown references remain addressable', async () => {
    await reset();
    for (const [id, status] of [['e_unloaded', 'unloaded'], ['e_unavailable', 'unavailable'], ['e_unknown', 'unknown']]) {
      const value = await focus(target(id));
      assert.equal(value.selection.objects.find(item => item.ref.selector === `/objects/${id}`).status, status);
      assert.ok((await page.getByTestId('source-status').innerText()).includes(status));
      assert.equal(value.selection.complete, false);
      await assertParity();
    }
    await snapshot(page, '04-unknown-source.png');
  });
  await check('unknown fields mode switches and save restore preserve perspective intent', async () => {
    await reset();
    const future = structuredClone(baseline);
    future.components['test.future.parameter'] = { extension: ['kept', 7] };
    future.futureEnvelope = { original: true };
    await document(future);
    let imported = await diagnostics(page);
    assert.ok(imported.selection.plan.unsupported.some(item => item.id === 'test.future.parameter'));
    for (const [targetName, action, status] of [['visual', 'disable', 'disabled'], ['goal', 'exclude', 'excluded']]) {
      const descriptor = catalog.capabilities.find(item => item.target === targetName);
      const editor = page.locator('.gp-component').filter({ has: page.locator('summary', { hasText: descriptor.label }) });
      await editor.locator('summary').click();
      await editor.getByRole('button', { name: labels[action], exact: true }).click();
      await waitSelection(page, (value, expected) => value.selection.plan.explanation.some(row => row.id === expected.id && row.status === expected.status), { id: descriptor.id, status });
    }
    imported = await diagnostics(page);
    Object.assign(future, imported.perspective);
    for (const mode of ['basic', 'advanced', 'expert']) { await level(mode); assert.deepEqual((await diagnostics(page)).perspective, future); }
    await page.getByRole('button', { name: labels.save, exact: true }).click();
    if (!(await page.getByTestId('perspective-json').isVisible())) await page.locator('.gp-transfer > summary').click();
    const downloadPromise = page.waitForEvent('download');
    await page.getByRole('button', { name: labels.download, exact: true }).click();
    const download = await downloadPromise;
    const downloaded = JSON.parse(readFileSync(await download.path(), 'utf8'));
    assert.deepEqual(downloaded, future, 'file export preserves unknown fields');
    await preset('history');
    await page.getByRole('button', { name: labels.restore, exact: true }).click();
    await waitSelection(page, value => value.perspective.futureEnvelope?.original === true && value.selection.plan.structure === 'computational');
    assert.deepEqual((await diagnostics(page)).perspective, future);
    await page.reload({ waitUntil: 'domcontentloaded' });
    await page.waitForFunction(() => !!window.__graphPerspectives?.selection);
    await page.getByRole('button', { name: labels.restore, exact: true }).click();
    await waitSelection(page, value => value.perspective.futureEnvelope?.original === true);
    const restored = await diagnostics(page);
    assert.deepEqual(restored.perspective, future);
    assert.ok(restored.selection.plan.explanation.some(row => row.status === 'disabled'));
    assert.ok(restored.selection.plan.explanation.some(row => row.status === 'excluded'));
  });
  await check('explicit render omissions analysis separation and canonical immutability', async () => {
    await reset();
    await focus(target('e_unknown'));
    await page.getByTestId('gp-analysis-add').click();
    const selectedForAnalysis = await waitSelection(page, value => value.selection.analysis.length === 1);
    const limited = structuredClone(selectedForAnalysis.perspective);
    limited.focus = structuredClone(baseline.focus);
    const budget = catalog.capabilities.find(item => item.target === 'budget').id;
    limited.components[budget] = { query: 128, render: 2, page: 16 };
    limited.layerActions = [...(limited.layerActions ?? []), { op: 'override', key: budget, value: limited.components[budget] }];
    assert.equal(limited.analysis.selected[0].selector, '/objects/e_unknown');
    await document(limited);
    const value = await diagnostics(page);
    assert.equal(value.selection.objects.length, 2);
    assert.ok(value.selection.omissions.some(item => item.reason === 'render_budget'));
    assert.equal(value.selection.complete, false);
    assert.deepEqual(value.selection.analysis, limited.analysis.selected);
    assert.equal(value.selection.permissionId, initial.selection.permissionId);
    assert.equal(value.canonicalPacketHash, initial.canonicalPacketHash);
    assert.equal(value.packetUnchanged, true);
    assert.ok(await page.getByTestId('omissions').isVisible());
    await assertParity();
  });
  await check('comparison visibility query plan export and workflow preparation are explicit', async () => {
    const before = await diagnostics(page);
    await page.getByTestId('gp-compare-saved').click();
    const comparison = JSON.parse(await page.getByTestId('perspective-comparison').locator('pre').innerText());
    assert.ok(comparison.length > 0 && comparison.some(item => item.path.includes('components')));
    await level('advanced');
    if (!(await page.getByTestId('focus-selector').isVisible())) await page.locator('.gp-address > summary').click();
    await page.getByTestId('focus-selector').fill(JSON.stringify(target('e_result')));
    await page.getByTestId('gp-visibility-query').click();
    const visibility = JSON.parse(await page.getByTestId('visibility-explanation').innerText());
    assert.equal(visibility.state, 'omitted');
    assert.ok(visibility.omissions.some(item => item.reason === 'render_budget'));
    assert.equal((await diagnostics(page)).perspective.focus.selector, before.perspective.focus.selector, 'inspector query does not navigate');
    const downloadPromise = page.waitForEvent('download');
    await page.getByTestId('gp-plan-export').click();
    const downloaded = JSON.parse(readFileSync(await (await downloadPromise).path(), 'utf8'));
    assert.deepEqual(downloaded, JSON.parse(JSON.stringify(before.selection.plan)), 'JSON export preserves every serializable plan field');
    const requestCount = requests.length;
    await page.getByTestId('gp-analysis-mode').selectOption('exact');
    await page.getByTestId('gp-workflow-prepare').click();
    const exact = JSON.parse(await page.getByTestId('analysis-export').innerText());
    assert.equal(exact.status, 'blocked');
    assert.equal(exact.blockedReason, 'exact_membership_not_supported_by_native_plan');
    assert.equal(exact.executed, false); assert.equal(exact.plan, null);
    await page.getByTestId('gp-analysis-mode').selectOption('anchors');
    await page.getByTestId('gp-workflow-prepare').click();
    await page.waitForFunction(() => JSON.parse(document.querySelector('[data-testid="analysis-export"]').textContent).selectionMode === 'anchors');
    const anchors = JSON.parse(await page.getByTestId('analysis-export').innerText());
    assert.equal(anchors.status, 'blocked');
    assert.ok(anchors.unsupported.some(item => item.reason === 'native_anchor_unavailable'));
    assert.equal(anchors.executed, false); assert.equal(anchors.plan, null);
    assert.deepEqual(requests.slice(requestCount).filter(request => request.url.includes('/api/')), [], 'preparing an analysis plan never executes or sends source data');
    assert.deepEqual((await diagnostics(page)).selection.analysis, before.selection.analysis);
  });
  await check('touch interaction and accessible object details work without hover', async () => {
    const mobile = await browser.newContext({ viewport: { width: 430, height: 932 }, isMobile: true, hasTouch: true });
    const touch = await mobile.newPage();
    touch.on('pageerror', error => pageErrors.push(error.message));
    await touch.goto(url, { waitUntil: 'domcontentloaded' });
    await touch.waitForFunction(() => !!window.__graphPerspectives?.selection);
    const value = await diagnostics(touch);
    const operation = value.selection.objects.find(item => item.ref.selector === '/objects/e_formula');
    await touch.locator(`[data-testid="object-entry"][data-object-key=${JSON.stringify(operation.key)}]`).tap();
    await waitSelection(touch, value => value.perspective.focus.selector === '/objects/e_formula');
    assert.ok(await touch.getByTestId('canonical-identity').isVisible());
    await touch.getByRole('button', { name: labels.back, exact: true }).tap();
    await waitSelection(touch, value => value.perspective.focus.selector === '/objects/e_cell');
    await snapshot(touch, '05-touch-mobile.png');
    await mobile.close();
  });
  // Measure real renderer updates using the same transformed KnowledgeGraph,
  // bridge and layout, with results from the production lazy headless selector.
  const rendererBundle = await build({
    stdin: { contents: `
      import React, { Profiler } from 'react';
      import { createRoot } from 'react-dom/client';
      import { KnowledgeGraph } from './src/components/KnowledgeWorkbench';
      import { createKnowledgeGraphBridge } from './src/graph-perspectives/renderer-bridge';
      const host = document.createElement('div'); host.id = 'benchmark-renderer'; document.body.append(host);
      const root = createRoot(host);
      let layoutMs = 0;
      const Renderer = createKnowledgeGraphBridge(KnowledgeGraph, { onLayoutMeasured: duration => { layoutMs = duration; } });
      window.__measurePerspectiveRenderer = (selection) => new Promise(resolve => {
        const started = performance.now(); layoutMs = 0;
        root.render(<Profiler id="benchmark" onRender={(_id, phase, duration) => {
          const measuredLayout = layoutMs;
          requestAnimationFrame(() => requestAnimationFrame(() => resolve({
            phase, layoutMs: measuredLayout, reactRenderMs: duration,
            renderExcludingMeasuredLayoutMs: Math.max(0, duration - measuredLayout),
            updateThroughTwoAnimationFramesMs: performance.now() - started,
            renderedObjectCount: host.querySelectorAll('svg [data-object-key]').length,
          })));
        }}><Renderer selection={selection} onSelect={() => {}} onInspectRelation={() => {}}
          labels={{graph:'Benchmark graph',focus:'Focus',inspectRelation:'Inspect relation'}} /></Profiler>);
      });
    `, resolveDir: webRoot, loader: 'tsx' },
    bundle: true, write: false, format: 'iife', platform: 'browser', jsx: 'automatic', outfile: '/tmp/perspectives-renderer-test.js',
    plugins: [{ name: 'exact-b-renderer-hook', setup(plugin) {
      plugin.onLoad({ filter: /KnowledgeWorkbench\.tsx$/ }, args => ({ contents: transformRenderer(readFileSync(args.path, 'utf8')), loader: 'tsx' }));
    } }],
  });
  await page.addScriptTag({ content: rendererBundle.outputFiles.find(file => file.path.endsWith('.js')).text });
  const moduleLoader = await createModuleLoader();
  let sparsePresentationBenchmark;
  try {
    const { selectPerspective } = await moduleLoader.load('src/graph-perspectives/select.ts');
    sparsePresentationBenchmark = await runBenchmark(selectPerspective, { onSelection: async result => {
      const value = await page.evaluate(result => window.__measurePerspectiveRenderer(result), result);
      assert.equal(value.renderedObjectCount, result.objects.length, 'renderer must consume sparse projection exactly');
      return value;
    } });
  } finally { await moduleLoader.close(); }
  assert.deepEqual(pageErrors, [], 'no browser exceptions');
  assert.ok(requests.every(request => request.url.startsWith(base) || request.url.startsWith('blob:') || request.url.startsWith('data:')), 'no external data requests');
  const final = await diagnostics(page);
  const evidence = {
    schema: 'loom.graph_perspectives_browser_evidence/1', generatedAt: new Date().toISOString(),
    browser: await browser.version(), expectedGroups, groups, timings, failures,
    presentationTimings: final.timings ?? null,
    sparsePresentationBenchmark,
    presentationMeasurement: 'Existing KnowledgeGraph layout timer; React Profiler actualDuration includes that layout, so the exclusive estimate subtracts it. Update wall time includes two animation frames and scheduler/paint opportunity; no claim that this isolates GPU paint.',
    pageErrors, requests: requests.filter(request => request.url.includes('/api/')),
    canonicalGraphUnchanged: final.canonicalPacketHash === initial.canonicalPacketHash,
    integration: 'Production module, native R40 resolver, local fixtures and existing KnowledgeGraph renderer transformed in memory by the exact B hook; application integration requires B applying that patch.',
    screenshots: ['01-computational-desktop.png', '02-history-desktop.png', '03-snapshot-comparison.png', '04-unknown-source.png', '05-touch-mobile.png', '06-live-native-provenance.png'],
  };
  writeFileSync(path.join(reportRoot, 'browser-evidence.json'), JSON.stringify(evidence, null, 2) + '\n');
  assert.deepEqual(failures, [], 'all declared browser scenarios must pass');
  complete.complete();
  console.log(`[graph-perspectives-browser] ${groups.length}/${expectedGroups} groups passed`);
} finally {
  await browser?.close();
  await stopChild(server);
}
