import assert from 'node:assert/strict';
import { test, before, after } from 'node:test';
import { readFileSync } from 'node:fs';
import { createModuleLoader } from './test-loader.mjs';
import { invokeNativeLayers } from './native-layers-runner.mjs';

let loader, workspace, bindings, workflow, comparisons;
before(async () => {
  loader = await createModuleLoader();
  [workspace, bindings, workflow, comparisons] = await Promise.all([
    loader.load('src/workspace/state.ts'), loader.load('src/graph-perspectives/workspace.ts'),
    loader.load('src/graph-perspectives/workflow.ts'), loader.load('src/graph-perspectives/comparison.ts'),
  ]);
});
after(async () => { await loader?.close(); });
const focus = { source: 'synthetic-source', selector: '/unopened/object', canonicalId: 'entity-object', future_selector: { retained: true } };
const depth = { id: 'graph.hops', component: 'traversal.hops', parameter: 'depth', target: 'component' };
function perspective(id, hops = 1) {
  return { schema: 'loom.graph_perspective/1', id, components: { 'traversal.hops': hops, structure: id, future: { option: 9 } }, focus: structuredClone(focus), future_root: { retained: true } };
}
function envelope() {
  const state = workspace.initialWorkspace(['graph', 'graph', 'graph']);
  const ids = state.panes.map(pane => pane.id);
  return { ids, state: bindings.createPerspectiveWorkspace(state, Object.fromEntries(ids.map((id, i) => [id, perspective(`p${i}`, i + 1)]))) };
}
test('selective depth linking delegates propagation and leaves structure, focus and unrelated panel unchanged', () => {
  const { ids, state } = envelope();
  const original = structuredClone(state);
  let result = bindings.bindPerspectiveComponent(state, ids[0], ids[1], depth);
  assert.equal(result.status, 'applied');
  result = bindings.updatePerspectiveComponent(result.state, ids[0], depth, 7);
  assert.equal(result.state.workspace.panes[1].parameters.depth, 7);
  assert.equal(result.state.perspectives[ids[1]].components['traversal.hops'], 7);
  assert.equal(result.state.perspectives[ids[1]].components.structure, 'p1');
  assert.equal(result.state.perspectives[ids[2]].components['traversal.hops'], 3);
  assert.deepEqual(result.state.perspectives[ids[1]].focus, focus);
  assert.deepEqual(state, original);
});
test('cycles terminate through existing workspace and snapshot changes retain focus and selection', () => {
  const { ids, state } = envelope();
  const selection = { id: 'focus', component: 'focus', parameter: 'selection', target: 'focus' };
  let result = bindings.updatePerspectiveComponent(state, ids[0], selection, focus);
  assert.deepEqual(result.state.perspectives[ids[1]].focus, focus);
  result = bindings.bindPerspectiveComponent(result.state, ids[0], ids[1], depth);
  result = bindings.bindPerspectiveComponent(result.state, ids[1], ids[0], depth);
  result = bindings.updatePerspectiveComponent(result.state, ids[1], depth, 5);
  assert.equal(result.state.perspectives[ids[0]].components['traversal.hops'], 5);
  const priorSelection = structuredClone(result.state.workspace.panes[0].parameters.selection);
  const run = { id: 'snapshot', component: 'time.snapshot', parameter: 'run', target: 'component' };
  result = bindings.updatePerspectiveComponent(result.state, ids[0], run, 'historical-run');
  assert.deepEqual(result.state.workspace.panes[0].parameters.selection, priorSelection);
  assert.deepEqual(result.state.perspectives[ids[0]].focus, focus);
});
test('unknown fields and suppressed component records roundtrip without a fallback resolver', () => {
  const { ids, state } = envelope();
  state.future_envelope = { preserved: ['future-v2'] };
  state.perspectives[ids[0]].components.suppressed = { status: 'excluded', origin: { id: 'stable-entry', revision: 1 }, value: { intentionally_retained_source: true } };
  const raw = bindings.serializePerspectiveWorkspace(state);
  assert.deepEqual(bindings.parsePerspectiveWorkspace(raw), state);
  assert.equal(bindings.parsePerspectiveWorkspace(raw).perspectives[ids[0]].components.suppressed.status, 'excluded');
});
test('unsupported future binding and conflicting mappings remain explicit without changing workspace', () => {
  const { ids, state } = envelope();
  const unknown = { id: 'future', component: 'hyperdimensional.rotation', parameter: 'future_rotation', target: 'component', preserved: 9 };
  let result = bindings.bindPerspectiveComponent(state, ids[0], ids[1], unknown);
  assert.equal(result.status, 'unsupported');
  assert.deepEqual(result.state.workspace, state.workspace);
  assert.deepEqual(result.state.unsupported[0].descriptor, unknown);
  result = bindings.bindPerspectiveComponent(result.state, ids[0], ids[1], depth);
  result = bindings.updatePerspectiveComponent(result.state, ids[0], { ...depth, component: 'different.meaning' }, 7);
  assert.equal(result.reason, 'parameter_mapping_conflict');
  assert.equal(bindings.updatePerspectiveComponent(state, ids[0], depth, -1).status, 'unsupported');
  const invalid = structuredClone(state);
  invalid.mappings = [depth, { ...depth, component: 'conflict' }];
  assert.throws(() => bindings.parsePerspectiveWorkspace(JSON.stringify(invalid)), /conflicting_perspective_workspace_mapping/);
});
test('bound composite depth becomes a native override and compiled query; exclusion stays authoritative', async () => {
  const pack = JSON.parse(readFileSync(new URL('../../../data/graph_perspectives/graph-perspectives.pack.json', import.meta.url), 'utf8'));
  const catalog = JSON.parse(readFileSync(new URL('../../../data/graph_perspectives/catalog.json', import.meta.url), 'utf8'));
  const [{ compilePerspective }, { nativeResolutionFromResponse }] = await Promise.all([loader.load('src/graph-perspectives/plan.ts'), loader.load('src/graph-perspectives/native-layers.ts')]);
  const key = 'graph.perspective.traversal';
  const { ids, state } = envelope();
  for (const id of ids) state.perspectives[id].components = Object.fromEntries(pack.entries.map(entry => [entry.key, structuredClone(entry.value)]));
  state.perspectives[ids[1]].components[key].relations = ['has_comment'];
  state.perspectives[ids[1]].components[key].direction = 'outgoing';
  const mapping = { id: 'depth', component: key, pointer: '/hops', parameter: 'depth', target: 'component' };
  assert.equal(bindings.bindPerspectiveComponent(state, ids[0], ids[1], mapping).reason, 'native_resolution_required');
  const nativeByPane = Object.fromEntries(await Promise.all(ids.map(async id => [id, nativeResolutionFromResponse(await invokeNativeLayers({ pack, actions: Object.entries(state.perspectives[id].components).map(([key, value]) => ({ op: 'override', key, value })) }))])));
  let result = bindings.bindPerspectiveComponent(state, ids[0], ids[1], mapping, nativeByPane);
  result = bindings.updatePerspectiveComponent(result.state, ids[0], mapping, 7, nativeByPane);
  assert.equal(result.nativeResolutionRequired, true);
  const target = result.state.perspectives[ids[1]];
  assert.deepEqual(target.components[key], { relations: ['has_comment'], direction: 'outgoing', hops: 7 });
  assert.equal(target.layerActions.at(-1).op, 'override');
  assert.equal(target.layerActions.at(-1).key, key);
  const native = await invokeNativeLayers({ pack, actions: target.layerActions });
  const plan = compilePerspective(target, nativeResolutionFromResponse(native), catalog.capabilities);
  assert.deepEqual(plan.errors, []);
  assert.equal(plan.hops, 7);
  assert.deepEqual(plan.relations, ['has_comment']);
  assert.equal(plan.direction, 'outgoing');
  const disabled = await invokeNativeLayers({ pack, actions: [{ op: 'disable', key }, ...target.layerActions] });
  const disabledPlan = compilePerspective(target, nativeResolutionFromResponse(disabled), catalog.capabilities);
  assert.ok(disabledPlan.errors.includes('traversal_hops_invalid'));
  const excluded = await invokeNativeLayers({ pack, actions: [{ op: 'exclude', key }] });
  const rejected = await invokeNativeLayers({ pack, state: excluded.layers, actions: target.layerActions });
  assert.ok(rejected.error);
  assert.equal(target.layerActions.some(action => action.op === 'reenable'), false);
});
test('composite binding uses native effective values after override and clear_override, never stale authored components', async () => {
  const pack = JSON.parse(readFileSync(new URL('../../../data/graph_perspectives/graph-perspectives.pack.json', import.meta.url), 'utf8'));
  const { nativeResolutionFromResponse } = await loader.load('src/graph-perspectives/native-layers.ts');
  const key = 'graph.perspective.traversal';
  const { ids, state } = envelope();
  for (const id of ids) state.perspectives[id].components[key] = { relations: ['stale'], direction: 'both', hops: 99 };
  state.perspectives[ids[1]].layerActions = [{ op: 'override', key, value: { relations: ['live'], direction: 'incoming', hops: 6 } }];
  const source = nativeResolutionFromResponse(await invokeNativeLayers({ pack }));
  const target = nativeResolutionFromResponse(await invokeNativeLayers({ pack, actions: state.perspectives[ids[1]].layerActions }));
  const mapping = { id: 'depth', component: key, pointer: '/hops', parameter: 'depth', target: 'component' };
  const result = bindings.bindPerspectiveComponent(state, ids[0], ids[1], mapping, { [ids[0]]: source, [ids[1]]: target });
  assert.equal(result.status, 'applied');
  assert.deepEqual(result.state.perspectives[ids[1]].components[key], { relations: ['live'], direction: 'incoming', hops: 4 });
  const cleared = nativeResolutionFromResponse(await invokeNativeLayers({ pack, actions: [...state.perspectives[ids[1]].layerActions, { op: 'clear_override', key }] }));
  const changed = bindings.updatePerspectiveComponent(result.state, ids[0], mapping, 2, { [ids[0]]: source, [ids[1]]: cleared });
  const defaultValue = pack.entries.find(entry => entry.key === key).value;
  assert.deepEqual(changed.state.perspectives[ids[1]].components[key], { ...defaultValue, hops: 2 });
  const suppressed = nativeResolutionFromResponse(await invokeNativeLayers({ pack, actions: [{ op: 'disable', key }] }));
  assert.equal(bindings.updatePerspectiveComponent(result.state, ids[0], mapping, 2, { [ids[0]]: source, [ids[1]]: suppressed }).reason, 'native_component_not_effective');
});
test('analysis export uses explicit membership, native verified anchor mapping, permissions and existing plan compiler', () => {
  const source = perspective('analysis');
  const external = { source: 'external', selector: '/not-yet-materialized' };
  const denied = { source: 'denied', selector: '/secret', canonicalId: 'secret' };
  source.analysis = { selected: [focus, external, denied], policyRef: 'policy-original' };
  const before = structuredClone(source);
  let lookupCalls = 0;
  const result = workflow.preparePerspectiveAnalysis(source, {
    id: 'analysis-plan', thesisId: 'question-1', text: '  Authored query preserved.  ',
    selectionMode: 'anchors',
    permission: { id: 'permission-1', canRead: ref => ref.source !== 'denied' },
    resolveAnchor: ref => { lookupCalls++; return ref === undefined || !ref.canonicalId ? undefined : { kind: 'entity', id: ref.canonicalId }; },
    relationHops: 0, detailResolution: 'raw', requireCounterEvidence: true, budgetWeight: 1,
  });
  assert.equal(result.status, 'partial');
  assert.equal(result.executed, false);
  assert.equal(result.selectionMode, 'anchors');
  assert.ok(result.limitations.includes('native_anchors_are_not_membership_allowlist'));
  assert.equal(lookupCalls, 2, 'denied references never reach native ID lookup');
  assert.deepEqual(result.plan.theses[0].targets, ['entity-object']);
  assert.deepEqual(result.plan.theses[0].claims, []);
  assert.equal(result.plan.theses[0].relation_hops, 0);
  assert.equal(result.plan.theses[0].text, '  Authored query preserved.  ');
  assert.deepEqual(result.unsupported.map(item => item.reason), ['native_anchor_unavailable', 'permission_denied']);
  assert.deepEqual(result.selected, source.analysis.selected);
  assert.deepEqual(result.plan.source_ref.selected, [focus, external]);
  assert.equal(result.plan.source_ref.denied_count, 1);
  assert.equal(JSON.stringify(result.plan).includes('/secret'), false, 'denied references stay in local review sidecar, not native plan metadata');
  assert.deepEqual(source, before);
});
test('empty explicit analysis selection cannot become implicit ambient retrieval', () => {
  const source = perspective('empty');
  const options = { id: 'p', thesisId: 'q', text: 'Query', selectionMode: 'anchors', permission: { id: 'p', canRead: () => true }, resolveAnchor: () => { throw new Error('not called'); }, requireCounterEvidence: true, budgetWeight: 1 };
  const result = workflow.preparePerspectiveAnalysis(source, options);
  assert.equal(result.status, 'blocked'); assert.equal(result.plan, null);
  assert.throws(() => workflow.preparePerspectiveAnalysis(source, { ...options, detailResolution: 'future-resolution' }), /unsupported_native_analysis_resolution/);
  source.analysis = { selected: [{ source: 's', selector: '/external', canonicalId: 'not-proof-of-native-id' }] };
  assert.equal(workflow.preparePerspectiveAnalysis(source, { ...options, resolveAnchor: () => undefined }).plan, null);
});
test('native nonexclusive anchors require explicit choice; exact analysis membership is blocked', () => {
  const source = perspective('scope');
  source.analysis = { selected: [focus] };
  const options = { id: 'scope', thesisId: 'question', text: 'Query', permission: { id: 'allow', canRead: () => true }, resolveAnchor: () => ({ kind: 'entity', id: 'known-native-entity' }), requireCounterEvidence: true, budgetWeight: 1 };
  assert.throws(() => workflow.preparePerspectiveAnalysis(source, options), /analysis_selection_mode_required/);
  const exact = workflow.preparePerspectiveAnalysis(source, { ...options, selectionMode: 'exact' });
  assert.equal(exact.status, 'blocked');
  assert.equal(exact.plan, null);
  assert.equal(exact.blockedReason, 'exact_membership_not_supported_by_native_plan');
  assert.deepEqual(exact.selected, source.analysis.selected);
  const anchors = workflow.preparePerspectiveAnalysis(source, { ...options, selectionMode: 'anchors' });
  assert.equal(anchors.status, 'ready');
  assert.equal(anchors.executed, false);
  assert.equal(anchors.plan.source_ref.selection_mode, 'anchors');
  assert.ok(anchors.plan.source_ref.limitations.includes('native_context_can_include_principles_preferences_dependencies_and_other_candidates'));
});
test('comparison retains unknown differences and RFC6901 escapes', () => {
  const before = perspective('same'), next = structuredClone(before);
  next.components['future/~field'] = { enabled: false };
  const diff = comparisons.comparePerspectives(before, next);
  assert.deepEqual(diff, [{ path: '/components/future~1~0field', kind: 'added', after: { enabled: false } }]);
  const reordered = { ...before, components: { future: { option: 9 }, structure: 'same', 'traversal.hops': 1 } };
  assert.deepEqual(comparisons.comparePerspectives(before, reordered), []);
});
test('visibility distinguishes exact reference, another snapshot, omissions and incomplete absence', () => {
  const result = { objects: [{ ref: focus }], omissions: [], complete: true };
  assert.equal(comparisons.explainVisibility(result, focus).state, 'visible');
  assert.equal(comparisons.explainVisibility(result, { ...focus, snapshot: 'old' }).state, 'represented');
  const other = { source: 'elsewhere', selector: '/x' };
  result.omissions = [{ ref: other, reason: 'permission' }];
  assert.equal(comparisons.explainVisibility(result, other).state, 'omitted');
  result.omissions = [{ reason: 'query_budget', count: 8 }]; result.complete = false;
  assert.equal(comparisons.explainVisibility(result, other).state, 'unresolved');
  result.complete = true;
  assert.equal(comparisons.explainVisibility(result, other).state, 'outside_selection');
});
