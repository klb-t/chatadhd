// M1-W0 (4) P6: the state of the role card (src/components/role-card-state.ts) over the REAL answer of roles.resolved - the test starts the
// native loom-server on a scratch data directory, sets a model, asks the action in both languages and checks what the card would draw:
// the table has the roles of the pack (read from ports.pack, no literal), the chain is the kernel's own sentence for every step with the
// deciding step marked, the class has its reason, the flow of every class is there with "kalibracja" where the data says, and every flow
// that reads the role's setting is listed (the lists are computed from actions.pack here and compared). Words: presentation.roles,
// both languages, a missing word shows its id.
import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, rmSync } from 'node:fs';
import { readFile } from 'node:fs/promises';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const st = await loader.load('src/components/role-card-state.ts');
const webRoot = fileURLToPath(new URL('../../', import.meta.url)), loomRoot = path.resolve(webRoot, '..');
const serverBin = process.env.LOOM_SERVER_BIN || path.join(loomRoot, 'build/dev/server/loom-server');
assert.ok(existsSync(serverBin), `Build loom-server first: ${serverBin}`);
const read = async rel => JSON.parse(await readFile(new URL(rel, import.meta.url), 'utf8'));
const view = await read('../../../data/profiles/conversation_view.pack');
const feature = view.entries.find(r => r.key === 'presentation.roles').value;
const { en, pl } = feature.locales;
const translator = words => (id, params = {}) => typeof words[id] === 'string' ? words[id].replace(/\{\{(\w+)\}\}/g, (_, k) => params[k] ?? '') : id;
const t = translator(pl);
const portsPack = (await read('../../../data/runtime/ports.pack')).defaults;
const actionsPack = (await read('../../../data/runtime/actions.pack')).defaults.actions;
const actionList = Array.isArray(actionsPack) ? actionsPack : Object.values(actionsPack);

async function freePort() {
  const probe = createServer();
  await new Promise(resolve => probe.listen(0, '127.0.0.1', resolve));
  const { port } = probe.address();
  await new Promise(resolve => probe.close(resolve));
  return port;
}
const dataDir = mkdtempSync(path.join(tmpdir(), 'loom-role-card-'));
const port = await freePort(), base = `http://127.0.0.1:${port}`;
const child = spawn(serverBin, ['--host', '127.0.0.1', '--port', String(port), '--data-dir', dataDir], { cwd: loomRoot, stdio: ['ignore', 'pipe', 'pipe'] });
let serverLog = '';
for (const output of [child.stdout, child.stderr]) output.on('data', chunk => { serverLog += chunk.toString(); });
const deadline = Date.now() + 20000;
for (;;) {
  try { if ((await fetch(`${base}/api/healthz`)).ok) break; } catch { /* startup */ }
  if (child.exitCode !== null) throw Error(`server exited: ${serverLog}`);
  if (Date.now() > deadline) throw Error(`server startup timeout: ${serverLog}`);
  await new Promise(resolve => setTimeout(resolve, 100));
}
test.after(async () => {
  child.kill('SIGTERM');
  await new Promise(resolve => { child.once('exit', resolve); setTimeout(resolve, 3000); });
  rmSync(dataDir, { recursive: true, force: true });
  await loader.close();
});
const post = async (route, body) => {
  const response = await fetch(`${base}${route}`, { method: 'POST', headers: { 'content-type': 'application/json' }, body: JSON.stringify(body) });
  assert.ok(response.ok, `${route} -> ${response.status}`);
  return response.json();
};
const roles = lang => post('/api/roles/resolved', { lang });
const byId = (doc, id) => doc.roles.find(r => r.id === id);

test('every word the card asks for exists in both languages, with the same ids, and the feature says it is present', () => {
  const ids = ['title', 'intro', 'loading', 'error', 'retry', 'table.aria', 'row.open', 'card.back', 'role.model', 'why', 'today', 'state.reason', 'state.details', 'chips.help', 'planned', 'state.planned',
    'state.partial', 'state.implemented', 'class', 'class.help', 'class.assign', 'class.edit', 'class.planned', 'chain', 'chain.help', 'chain.step', 'chain.today',
    'chain.described_not_read', 'step.explicit', 'step.explicit.plain', 'step.setting', 'step.role', 'step.relative_to', 'step.none', 'step.decides', 'step.value',
    'step.value.empty', 'relative_to.note', 'flow.by_class', 'flow.by_class.help', 'flow.current', 'flow.calibration', 'flow.does', 'flow.planned', 'flow.none',
    'flow.no_words', 'flow.status.implemented', 'calls.one', 'calls.several', 'readers', 'readers.help', 'readers.also', 'readers.none', 'readers.total', 'mode',
    'mode.modes', 'mode.local', 'mode.model', 'mode.batch', 'mode.batch_no_key', 'mode.threshold', 'cost', 'cost.price', 'cost.price_io', 'cost.output_unknown',
    'cost.unknown', 'cost.week', 'technical_name', 'setting.change', 'setting.close'];
  for (const id of ids) { assert.equal(typeof en[id], 'string', `en ${id}`); assert.equal(typeof pl[id], 'string', `pl ${id}`); }
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort(), 'en and pl have the same ids');
  assert.equal(feature.status.state, 'present');
  // a missing word shows its id, never English
  const none = st.stepName({ type: 'explicit', setting: 'semantic_model' }, { settings: [] }, [], id => id);
  assert.equal(none, 'step.explicit');
});

test('the table has the roles of the pack, in the order the kernel sends them, with the model and the class in words', async () => {
  const doc = await roles('pl');
  const rows = st.roleRows(doc, t);
  assert.equal(rows.length, Object.keys(portsPack.roles).length, 'one row per role of ports.pack');
  assert.deepEqual(rows.map(r => r.id), doc.roles.map(r => r.id));
  assert.deepEqual(new Set(rows.map(r => r.id)), new Set(Object.keys(portsPack.roles)));
  for (const row of rows) {
    assert.ok(row.label && row.label !== row.id, `${row.id} has a Polish label`);
    assert.ok(row.modelText, `${row.id} says its model or that there is none`);
    assert.ok(row.tierLabel, `${row.id} has a class label`);
    assert.ok(row.why.length > 10, `${row.id} says what it is for`);
  }
  // a planned/partial role gets the chip in words, a role the data calls anything else gets none
  const planned = rows.find(r => doc.roles.find(x => x.id === r.id).status.state === 'planned');
  assert.equal(planned.state.text, pl['state.planned']);
  // the chip names its subject ("wybór przez rolę: planowane"), never the bare word that reads as "chat is planned" next to a working chat
  for (const id of ['state.planned', 'state.partial']) for (const l of [pl, en]) assert.ok(l[id].includes(': ') && l[id].length > l.planned.length + 2, `${id} says what is ${id.slice(6)}`);
  assert.equal(st.stateChip({ status: { state: 'implemented' } }, t), null);
});

test('the card of the analysis role: chain word for word, class with its reason, the flow of every class, every flow that reads the key', async () => {
  // no model yet: the chain ends on "brak modelu" and the class says there is none
  const before = byId(await roles('pl'), 'analysis.fast');
  assert.equal(before.model, null);
  assert.equal(st.chainSteps(before, [], t).filter(s => s.decides).length, 1, 'exactly one step decides');
  assert.equal(st.chainSteps(before, [], t).find(s => s.decides).type, 'none');

  await post('/api/actions/run', { action: 'settings.set', args: { key: 'semantic_model', value: 'google/gemini-2.5-flash' } });
  const doc = await roles('pl');
  const role = byId(doc, 'analysis.fast');
  assert.equal(role.model, 'google/gemini-2.5-flash');

  // the chain: every step of the kernel, its sentence verbatim, the deciding step marked, the names from the data
  const steps = st.chainSteps(role, doc.roles, t);
  assert.equal(steps.length, role.chain.length);
  steps.forEach((step, i) => {
    assert.equal(step.meaning, role.chain[i].meaning, 'the kernel\'s sentence word for word');
    assert.equal(step.decides, role.chain[i].decides === true);
    assert.ok(step.title.startsWith(`${role.chain[i].step}. `));
  });
  assert.equal(steps.filter(s => s.decides).length, 1);
  assert.ok(steps[0].decides && steps[0].title.includes('semantic_model') && steps[0].title.includes(role.settings.find(s => s.key === 'semantic_model').label));
  assert.equal(steps[0].value, 'google/gemini-2.5-flash');
  assert.ok(steps[1].title.includes(role.label), 'the role step names the role by its label');
  assert.equal(st.chainIsRead(role), true);

  // the class with its reason (the kernel's sentence), never an empty reason
  assert.equal(role.tier.id, 'cheap');
  assert.ok(role.tier.reason.length > 10);

  // the flow of every class: the model class decides which flow runs
  const classes = st.flowsByClass(role, t);
  assert.ok(classes.length >= 2, 'more than one class has a flow');
  assert.equal(classes.filter(c => c.current).length, 1);
  assert.equal(classes.find(c => c.current).tierId, role.tier.id);
  const cheap = classes.find(c => c.tierId === 'cheap'), frontier = classes.find(c => c.tierId === 'frontier');
  assert.notEqual(cheap.flow.id, frontier.flow.id, 'a cheap and a frontier model get different flows');
  assert.notEqual(cheap.flow.title, frontier.flow.title);
  assert.equal(cheap.flow.status.id, 'calibration');
  assert.equal(cheap.flow.status.text, pl['flow.calibration'], 'kalibracja where the data says so');
  for (const c of classes) {
    assert.ok(c.flow.does.length > 0, `${c.tierId} says what the flow does`);
    assert.ok(!c.flow.title.startsWith('flow.') && !c.flow.title.includes('no words'), `${c.tierId} has Polish words`);
  }
  // the lines come in the language asked: Polish differs from the English lines the data keeps
  assert.notDeepEqual(cheap.flow.does, role.flow_by_class.find(c => c.tier.id === 'cheap').flow.does);
  assert.equal(cheap.flow.calls, pl[`calls.${role.flow_by_class.find(c => c.tier.id === 'cheap').flow.calls}`]);
  const english = st.flowsByClass(byId(await roles('en'), 'analysis.fast'), translator(en));
  assert.notEqual(english.find(c => c.tierId === 'cheap').flow.title, cheap.flow.title);

  // EVERY flow that reads the role's key: the actions of the pack that declare the role or read one of its settings
  const keys = new Set(role.settings.map(s => s.key));
  const declared = actionList.filter(a => [].concat(a.uses_role ?? []).includes('analysis.fast')).map(a => a.id).sort();
  const readingKey = actionList.filter(a => !declared.includes(a.id) && (a.reads ?? []).some(k => keys.has(k))).map(a => a.id).sort();
  const listed = st.readers(role);
  assert.deepEqual(listed.own.map(f => f.id).sort(), declared);
  assert.deepEqual(listed.also.map(f => f.id).sort(), readingKey);
  assert.equal(listed.total, declared.length + readingKey.length);
  assert.ok(listed.total >= 10, 'the semantic analysis is read by many flows');
  for (const flow of [...listed.own, ...listed.also]) assert.ok(flow.title && flow.title !== flow.id, `${flow.id} has a title in words`);
  assert.ok(listed.also.every(f => keys.has(f.key)), 'the second list says which key it reads');

  // the background mode and the cost, as the data has them
  const mode = st.modeView(role, t);
  assert.ok(mode.modes.length >= 2);
  assert.ok(mode.modes.every(m => !m.startsWith('mode.')), 'every mode has its word');
  assert.match(mode.threshold, /\d/);
  assert.equal(st.priceText(role, t), role.cost.per_million_unknown_human, 'no registry: the kernel\'s reason, never 0');
  assert.equal(st.weekText(role, t), role.cost.last_7d_unknown_human);
});

test('a role nobody reads yet says so: its chain describes, today\'s reading is shown, and it has no flow by class', async () => {
  const doc = await roles('pl');
  const chat = byId(doc, 'chat.default');
  assert.equal(st.chainIsRead(chat), false);
  const today = st.todaySteps(chat, t);
  assert.ok(today.length > 0 && today.some(s => s.decides), 'what the code reads today, with the deciding step');
  assert.deepEqual(st.flowsByClass(chat, t), []);
  assert.equal(st.stateChip(chat, t).text, pl['state.planned']);
  // a role with no chain at all (formats) still has a row and no readers
  const format = doc.roles.find(r => r.chain.length === 0);
  assert.ok(format);
  assert.equal(st.readers(format).total, 0);
  assert.equal(st.modeView(format, t), null);
});

test('the value of a step: a string as it is, nothing as a word', () => {
  assert.equal(st.valueText(null, t), pl['step.value.empty']);
  assert.equal(st.valueText('', t), pl['step.value.empty']);
  assert.equal(st.valueText('x/y', t), 'x/y');
  assert.equal(st.priceText({ cost: { per_million: { input: 3, output: null, currency: 'USD' } } }, t), pl['cost.price'].replace('{{price}}', pl['cost.price_io'].replace('{{input}}', '3').replace('{{output}}', pl['cost.output_unknown']).replace('{{currency}}', 'USD')));
});
