import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { loadModule, repoRoot } from './test-loader.mjs';
const { createPacketAdapter, createDemandAdapter, createApplicationProfileAdapter } = await loadModule('src/graph-perspectives/adapters.ts');
const fixturePath = path.join(repoRoot, 'loom/data/graph_perspectives');
const packet = JSON.parse(readFileSync(path.join(fixturePath, 'demo.packet.json'), 'utf8'));
const catalog = JSON.parse(readFileSync(path.join(fixturePath, 'catalog.json'), 'utf8'));
const descriptor = catalog.sourceDescriptor;
const context = { permission: { id: 'local-test', canRead: () => true } };
const ref = id => structuredClone(packet.entities.find(entity => entity.id === id).attrs.perspective.ref);
const original = JSON.stringify(packet);
let count = 0;
async function check(name, fn) { await fn(); count++; console.log(`PASS ${name}`); }

await check('native packet lazy object projection preserves canonical bytes and unknown fields', async () => {
  const adapter = createPacketAdapter(packet, descriptor);
  assert.equal(adapter.metrics.materializedObjects, 0);
  const object = await adapter.resolve(ref('e_cell'), context);
  assert.equal(object.status, 'available');
  assert.equal(object.ref.canonicalId, ref('e_cell').canonicalId);
  assert.equal(object.confidence, undefined, 'parser does not invent or certify confidence');
  object.properties.raw.attrs.future_extension = { x: 7 };
  assert.equal(JSON.stringify(packet), original);
  assert.equal(adapter.metrics.materializedObjects, 1);
});

await check('same object addresses physical and logical structures without renderer state', async () => {
  const adapter = createPacketAdapter(packet, descriptor);
  const query = { relations: [], direction: 'both', limit: 50 };
  const physical = await adapter.neighbors(ref('e_cell'), { ...query, structure: 'physical' }, context);
  const logical = await adapter.neighbors(ref('e_cell'), { ...query, structure: 'logical' }, context);
  assert.ok(physical.relations.length);
  assert.ok(logical.relations.length);
  assert.notDeepEqual(physical.relations.map(edge => edge.id), logical.relations.map(edge => edge.id));
  assert.equal((await adapter.resolve(ref('e_cell'), context)).ref.canonicalId, ref('e_cell').canonicalId);
});

await check('permission denied, unloaded, unavailable and unrecognized are separate addressable states', async () => {
  const adapter = createPacketAdapter(packet, descriptor);
  const denied = await adapter.resolve(ref('e_cell'), { permission: { id: 'denied', canRead: () => false } });
  assert.equal(denied.status, 'denied'); assert.equal(denied.properties, undefined);
  assert.equal((await adapter.resolve(ref('e_unloaded'), context)).status, 'unloaded');
  assert.equal((await adapter.resolve(ref('e_unavailable'), context)).status, 'unavailable');
  assert.equal((await adapter.resolve(ref('e_unknown'), context)).status, 'unknown');
  assert.deepEqual((await adapter.resolve(ref('e_unknown'), context)).ref, ref('e_unknown'));
  assert.equal((await adapter.resolve({ source: descriptor.id, selector: '/not-present' }, context)).reason, 'selector_not_found');
  assert.equal(JSON.stringify(packet), original);
});

await check('snapshots retain one canonical identity and exact representation', async () => {
  const adapter = createPacketAdapter(packet, descriptor);
  const old = await adapter.resolve(ref('e_version_v1'), context);
  const current = await adapter.resolve(ref('e_cell'), context);
  assert.equal(old.ref.canonicalId, current.ref.canonicalId);
  assert.notEqual(old.ref.snapshot, current.ref.snapshot);
  assert.equal((await adapter.resolve({ ...old.ref, snapshot: 'missing-version' }, context)).status, 'unknown');
  const byCanonical = await adapter.resolve({ source: descriptor.id, selector: '/unused-alias', canonicalId: current.ref.canonicalId, snapshot: current.ref.snapshot, representation: current.ref.representation }, context);
  assert.equal(byCanonical.properties.address.selector, current.ref.selector);
  assert.equal(byCanonical.ref.selector, '/unused-alias');
});

await check('pagination is explicit and no canonical row is lost', async () => {
  const adapter = createPacketAdapter(packet, descriptor);
  let cursor, ids = [], expected;
  do {
    const page = await adapter.neighbors(ref('e_formula'), { structure: 'computational', relations: [], direction: 'both', limit: 1, cursor }, context);
    expected = page.total; ids.push(...page.relations.map(edge => edge.id)); cursor = page.nextCursor;
  } while (cursor);
  assert.ok(expected > 1); assert.equal(ids.length, expected); assert.equal(new Set(ids).size, expected);
  await assert.rejects(adapter.neighbors(ref('e_formula'), { structure: 'computational', relations: [], direction: 'both', limit: 0 }, context), /invalid_neighbor_limit/);
  assert.equal((await adapter.neighbors(ref('e_formula'), { structure: 'new-unsupported-structure', relations: [], direction: 'both', limit: 10 }, context)).reason, 'structure_unsupported');
});

await check('unopened source invokes supplied demand loader only after permission allows access', async () => {
  let reads = 0;
  const source = { id: 'unopened-resource', label: 'Unopened', status: 'unloaded', structures: [{ id: 'parts', label: 'Parts', relations: ['contains'] }] };
  const target = { source: source.id, selector: '/nested/package/item' };
  const adapter = createDemandAdapter(source, { async resolve(input) { reads++; return { ref: input, label: 'Located object', kind: 'future-kind', status: 'available', evidence: 'source', properties: { recognition: { scope: ['/nested/package/item'], alternatives: ['mapping-b'] } } }; } });
  assert.equal(reads, 0);
  assert.equal((await adapter.resolve(target, { permission: { id: 'deny', canRead: () => false } })).status, 'denied'); assert.equal(reads, 0);
  const object = await adapter.resolve(target, context);
  assert.equal(reads, 1); assert.deepEqual(object.ref, target); assert.deepEqual(object.properties.recognition.alternatives, ['mapping-b']);
  assert.equal((await createDemandAdapter(source).resolve(target, context)).status, 'unloaded');
  assert.equal((await createDemandAdapter({ ...source, status: 'unknown' }).resolve(target, context)).status, 'unknown');
  const failed = createDemandAdapter(source, { async resolve() { throw new Error('source_offline'); } });
  assert.equal((await failed.resolve(target, context)).status, 'unavailable');
});

await check('a third previously unseen sensor structure joins unchanged mechanism via adapter data', async () => {
  const source = { id: 'sensor-capture', label: 'Capture', structures: [{ id: 'frequency-bands', label: 'Spectral relation', relations: ['harmonic-of'] }], recognition: { status: 'partial', unknown_paths: ['/vendor-extension'] } };
  const a = { source: source.id, selector: '/channels/0/bands/4' }, b = { source: source.id, selector: '/channels/0/bands/8' };
  const adapter = createDemandAdapter(source, {
    async resolve(target) { return { ref: target, label: target.selector, kind: 'frequency-bin', status: 'available', evidence: 'computed' }; },
    async neighbors() { return { relations: [{ id: 'harmonic-1', from: a, to: b, kind: 'harmonic-of', evidence: 'inferred', basis: { interpretation: 'candidate', transform: 'fft-v2' } }], total: 1, status: 'available' }; },
  });
  assert.equal((await adapter.resolve(a, context)).kind, 'frequency-bin');
  const page = await adapter.neighbors(a, { structure: 'frequency-bands', relations: ['harmonic-of'], direction: 'outgoing', limit: 10 }, context);
  assert.equal(page.relations[0].evidence, 'inferred'); assert.equal(page.relations[0].basis.interpretation, 'candidate');
  assert.equal(adapter.descriptor.recognition.unknown_paths[0], '/vendor-extension');
  const { compilePerspective } = await loadModule('src/graph-perspectives/plan.ts');
  const { selectPerspective } = await loadModule('src/graph-perspectives/select.ts');
  const values = { structure: 'frequency-bands', resolution: 'detailed', traversal: { relations: ['harmonic-of'], direction: 'outgoing', hops: 1 }, budget: { query: 10, render: 10, page: 5 } };
  const capabilities = Object.keys(values).map(id => ({ id, label: id, target: id, status: 'supported' }));
  const perspective = { schema: 'loom.graph_perspective/1', id: 'unseen-source', components: values, focus: a };
  const plan = compilePerspective(perspective, { components: Object.entries(values).map(([id, value]) => ({ id, value, status: 'effective' })) }, capabilities);
  const selection = await selectPerspective(plan, [adapter], context.permission);
  assert.equal(selection.objects.length, 2); assert.equal(selection.relations[0].kind, 'harmonic-of');
  assert.equal(selection.objects.find(object => object.ref.selector === b.selector).evidence, 'computed');
});

await check('existing registerProfile loader and source selector access unrendered profile field', async () => {
  const sourceText = readFileSync(path.join(fixturePath, 'external-profile.json'), 'utf8');
  const adapter = createApplicationProfileAdapter({ sourceText, descriptor: { id: 'external-profile', label: 'External profile', structures: [{ id: 'field-tree', label: 'Fields', relations: ['contains'] }] }, childRelation: 'contains' });
  const target = { source: 'external-profile', selector: '/presentation/tokens/accent' };
  const selected = await adapter.resolve(target, context);
  assert.equal(selected.status, 'available');
  assert.equal(selected.properties.value, JSON.parse(sourceText).presentation.tokens.accent);
  assert.equal(selected.properties.loader, 'registerProfile');
  assert.equal((await adapter.resolve({ ...target, selector: '/__proto__/polluted' }, context)).status, 'unknown');
  assert.equal((await adapter.resolve({ ...target, selector: '/presentation/~2invalid' }, context)).status, 'unknown');
  assert.equal((await adapter.resolve({ ...target, snapshot: 'missing' }, context)).reason, 'snapshot_not_available');
  const parents = await adapter.neighbors(target, { structure: 'field-tree', relations: ['contains'], direction: 'incoming', limit: 5 }, context);
  assert.equal(parents.relations[0].from.selector, '/presentation/tokens');
});

await check('packet accepts unknown metadata, maps native evidence only by descriptor, rejects malformed rows', async () => {
  const unknown = structuredClone(packet);
  unknown.entities[0].attrs.future_vendor = { format: 'unrecognized', bytes: [8, 13] };
  delete unknown.entities[0].attrs.perspective;
  const adapter = createPacketAdapter(unknown, { ...descriptor, evidenceMap: { [unknown.entities[0].evidence_class]: 'source' } });
  const object = await adapter.resolve({ source: descriptor.id, selector: unknown.entities[0].id }, context);
  assert.equal(object.evidence, 'source'); assert.deepEqual(object.properties.raw.attrs.future_vendor, { format: 'unrecognized', bytes: [8, 13] });
  const futureStatus = structuredClone(packet);
  futureStatus.entities[0].attrs.perspective.status = 'future-availability';
  const futureAdapter = createPacketAdapter(futureStatus, descriptor);
  assert.equal((await futureAdapter.resolve(futureStatus.entities[0].attrs.perspective.ref, context)).status, 'unknown');
  const duplicateClaim = structuredClone(packet); duplicateClaim.claims.push(duplicateClaim.claims[0]);
  const invalidAdapter = createPacketAdapter(duplicateClaim, descriptor);
  await assert.rejects(invalidAdapter.resolve(ref('e_cell'), context), /duplicate_packet_claim_id/);
  await assert.rejects(invalidAdapter.resolve(ref('e_cell'), context), /duplicate_packet_claim_id/);
  assert.throws(() => createPacketAdapter({ schema: 'other' }, descriptor), /invalid_graph_packet_view/);
  assert.throws(() => createPacketAdapter({ schema: 'loom.graph_packet/1', entities: [null], claims: [], sources: [] }, descriptor), /malformed_graph_packet_rows/);
});

await check('large sparse packet hub materializes only the requested page and exposes denied filtering', async () => {
  const size = 10000;
  const entities = Array.from({ length: size + 1 }, (_, index) => ({ id: `n${index}`, canonical_key: `n${index}`, kind: 'sample', label: `n${index}`, attrs: {} }));
  const claims = Array.from({ length: size }, (_, index) => ({ id: `c${index}`, subject: 'n0', predicate: 'links', object: `n${index + 1}`, assessment: {} }));
  const adapter = createPacketAdapter({ schema: 'loom.graph_packet/1', entities, claims, sources: [] }, { id: 'star', label: 'Star', structures: [{ id: 'topology', label: 'Topology', relations: ['links'] }] });
  const start = performance.now();
  const page = await adapter.neighbors({ source: 'star', selector: 'n0' }, { structure: 'topology', relations: ['*'], direction: 'outgoing', limit: 3 }, context);
  const elapsed = performance.now() - start;
  assert.equal(page.relations.length, 3); assert.equal(page.total, size); assert.equal(page.nextCursor, '3');
  assert.equal(adapter.metrics.materializedRelations, 3); assert.equal(adapter.metrics.materializedObjects, 0);
  const guarded = await adapter.neighbors({ source: 'star', selector: 'n0' }, { structure: 'topology', relations: ['links'], direction: 'outgoing', limit: 3 }, { permission: { id: 'hide-one', canRead: target => target.selector !== 'n1' } });
  assert.equal(guarded.reason, 'permission_filtered'); assert.equal(guarded.status, 'denied'); assert.equal(guarded.total, size - 1);
  assert.ok(!guarded.relations.some(edge => edge.to.selector === 'n1'));
  console.log(`  packet-hub sample: ${size + 1} entities/${size} claims, query+lazy-index ${elapsed.toFixed(2)}ms, projected relations 3`);
});

await check('actual pinned D resource packet resolves requested source pointer with descriptor mapping', async () => {
  const producerSha = '1d3d133154f213733b7a69af863613cec2dd8ca2';
  const python = String.raw`
import importlib.util, io, json, subprocess, sys, tarfile, tempfile
from pathlib import Path
from loom.tools.structure.agentic_graph_v1.packet import validate_packet
sha = sys.argv[1]
with tempfile.TemporaryDirectory() as folder:
    root = Path(folder)
    archive = subprocess.check_output(['git', 'archive', sha, 'loom/tools/resource_graph', 'loom/data/resource_graph'])
    with tarfile.open(fileobj=io.BytesIO(archive)) as tree:
        tree.extractall(root, filter='data')
    location = root / 'loom/tools/resource_graph/__init__.py'
    spec = importlib.util.spec_from_file_location('pinned_resource_graph', location, submodule_search_locations=[str(location.parent)])
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    graph = module.ResourceGraph()
    resource = root / 'measurements.json'
    resource.write_text(json.dumps({'instrument': {'samples': [4, 8, 12], 'unrecognized_extension': {'preserve': True}}}))
    graph.attach(str(resource), logical_id='actual-d-resource', format='json')
    assert graph.metrics['opens'] == 0
    assert graph.select('actual-d-resource', '/instrument/samples/1') == 8
    packet = graph.project('actual-d-resource', '/instrument', depth=2, limit=20)
    validate_packet(packet)
    assert graph.describe('actual-d-resource')['embedded_bytes'] is False
    print(json.dumps({'packet': packet, 'metrics': graph.metrics, 'source': graph.describe('actual-d-resource')}))
`;
  const generated = spawnSync('python3', ['-c', python, producerSha], { cwd: repoRoot, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
  assert.equal(generated.status, 0, generated.stderr);
  const result = JSON.parse(generated.stdout);
  const frozen = JSON.stringify(result.packet);
  const adapter = createPacketAdapter(result.packet, {
    id: 'actual-d-resource', label: 'Actual D source',
    structures: [{ id: 'source-tree', label: 'Source tree', relations: [...new Set(result.packet.claims.map(claim => claim.predicate))] }],
    objectMapping: { selector: '/attrs/selector', canonicalId: '/attrs/selector', snapshot: '/attrs/content_sha256', status: '/attrs/status', recognition: '/attrs/recognition' },
    availabilityMap: { unsupported: 'unknown', corrupt: 'unavailable', partial: 'available' },
    evidenceMap: { derived: 'computed', observed: 'source' },
  });
  const object = await adapter.resolve({ source: 'actual-d-resource', selector: '/instrument/samples/1' }, context);
  assert.equal(object.status, 'available'); assert.equal(object.properties.raw.attrs.value, 8);
  assert.equal(object.confidence, undefined, 'D parser confidence 1 is not an E calibration claim');
  assert.equal(object.ref.canonicalId, '/instrument/samples/1');
  const parent = await adapter.neighbors(object.ref, { structure: 'source-tree', relations: [], direction: 'incoming', limit: 10 }, context);
  assert.ok(parent.relations.some(edge => edge.to.selector === '/instrument/samples/1'));
  assert.equal(result.source.embedded_bytes, false); assert.equal(result.source.policy.cache, false);
  assert.equal(JSON.stringify(result.packet), frozen);
  console.log(`  D producer ${producerSha}: ${result.packet.entities.length} entities/${result.packet.claims.length} claims, opens=${result.metrics.opens}, no native import/embedding/cache`);
});

console.log(`adapter contract: ${count}/${count} groups passed`);
