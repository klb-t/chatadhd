import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { loadModule, repoRoot } from './test-loader.mjs';
const python = String.raw`
import json, tempfile
from pathlib import Path
from jsonschema import Draft202012Validator
from loom.tools.structure import method_graph_export_v1 as producer
from loom.tools.structure.test_method_graph_export_v1 import fixture
from loom.tools.structure.agentic_graph_v1.packet import validate_packet
with tempfile.TemporaryDirectory() as folder:
    config, _ = fixture(folder, two_presets=True)
    packet = producer.export(config, root=folder)
    validate_packet(packet)
    Draft202012Validator(json.loads(Path('loom/tools/structure/agentic_graph_v1/graph_packet.schema.json').read_text())).validate(packet)
    print(json.dumps(packet))
`;
const generated = spawnSync('python3', ['-c', python], { cwd: repoRoot, encoding: 'utf8', maxBuffer: 4 * 1024 * 1024 });
assert.equal(generated.status, 0, generated.stderr);
const packet = JSON.parse(generated.stdout);
const original = JSON.stringify(packet);
const { createPacketAdapter } = await loadModule('src/graph-perspectives/adapters.ts');
const kinds = [...new Set(packet.claims.map(row => row.predicate))];
const descriptor = { id: 'existing-research-exporter', label: 'Actual offline exporter', structures: [{ id: 'research', label: 'Method/run structure', relations: kinds }] };
const adapter = createPacketAdapter(packet, descriptor);
const permission = { id: 'local-fixture', canRead: () => true };
const relation = packet.claims.find(row => row.predicate === 'produced_by');
assert.ok(relation, 'actual exporter must supply produced_by');
const reference = { source: descriptor.id, selector: relation.subject };
const object = await adapter.resolve(reference, { permission });
assert.equal(object.status, 'available');
assert.equal(object.confidence, undefined, 'native exporter structural confidence must not become calibrated presentation confidence');
const neighbors = await adapter.neighbors(reference, { structure: 'research', relations: ['produced_by'], direction: 'outgoing', limit: 100 }, { permission });
assert.ok(neighbors.relations.some(row => row.id === relation.id));
assert.equal(JSON.stringify(packet), original, 'reading real exported packet must not alter canonical records');
const demoPath = path.join(repoRoot, 'loom/data/graph_perspectives/demo.packet.json');
JSON.parse(readFileSync(demoPath, 'utf8'));
const validation = spawnSync('python3', ['-c', String.raw`
import json
from pathlib import Path
from jsonschema import Draft202012Validator
from loom.tools.structure.agentic_graph_v1.packet import validate_packet
packet = json.loads(Path('loom/data/graph_perspectives/demo.packet.json').read_text())
validate_packet(packet)
Draft202012Validator(json.loads(Path('loom/tools/structure/agentic_graph_v1/graph_packet.schema.json').read_text())).validate(packet)
print('safe demo passes existing native packet codec and JSON Schema')
`], { cwd: repoRoot, encoding: 'utf8' });
assert.equal(validation.status, 0, validation.stderr);
console.log(`PASS actual existing method exporter (${packet.entities.length} entities/${packet.claims.length} claims), generic projection, immutable packet and native fixture validation`);
