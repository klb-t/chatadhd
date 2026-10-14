// The devices of this client (P13): what the page declares, how a step is carried out on it (MediaRecorder, clipboard, notifications,
// file chooser, playback; all mocked), the answer that goes to the kernel, the block that asks the person, and the transports.
import test from 'node:test';
import assert from 'node:assert/strict';
import { readFile } from 'node:fs/promises';
import { createElement } from 'react';
import { renderToStaticMarkup } from 'react-dom/server';
import { createModuleLoader } from '../product-perspective/loader.mjs';
const loader = await createModuleLoader();
const device = await loader.load('src/context/client-device.ts');
const sessionModule = await loader.load('src/context/client-session.ts');
const view = await loader.load('src/context/client-step.ts');
const proposal = await loader.load('src/context/action-proposal.ts');
const presentation = await loader.load('src/onboarding/presentation.mjs');
const block = await loader.load('src/components/ClientStepBlock.tsx');
const { LoomHttpApi } = await loader.load('src/api/loom-http.ts');
const { LoomJniApi } = await loader.load('src/api/loom-jni.ts');
test.after(() => loader.close());
const pack = JSON.parse(await readFile(new URL('../../../data/profiles/conversation_view.pack', import.meta.url), 'utf8'));
const generated = JSON.parse(await readFile(new URL('../../src/onboarding/generated/conversation-view.json', import.meta.url), 'utf8'));
const entry = pack.entries.find(row => row.key === 'presentation.client_step');
const en = entry.value.locales.en;
const pl = entry.value.locales.pl;
const feature = presentation.resolvePresentationFeature(presentation.resolvePresentation(), 'client_step');

// ── fakes of the browser ────────────────────────────────────────────

class FakeRecorder {
  static starts = 0;
  constructor(stream) { this.stream = stream; this.mimeType = 'audio/webm;codecs=opus'; this.ondataavailable = null; this.onstop = null; this.onerror = null; }
  start() { FakeRecorder.starts += 1; }
  stop() {
    this.ondataavailable?.({ data: new Blob([new Uint8Array([1, 2, 3, 4])], { type: this.mimeType }) });
    this.ondataavailable?.({ data: new Blob([], { type: this.mimeType }) });  // an empty chunk is not a recording
    queueMicrotask(() => this.onstop?.());
  }
}
function stream() { const stopped = []; return { stopped, getTracks: () => [{ stop: () => stopped.push(true) }] }; }
function host(over = {}) {
  const opened = [];
  return {
    opened,
    mediaDevices: { getUserMedia: async constraints => { opened.push(constraints); const s = stream(); opened.push(s); return s; } },
    MediaRecorder: FakeRecorder,
    permissions: { query: async ({ name }) => ({ state: { microphone: 'prompt', camera: 'granted', notifications: 'denied', 'clipboard-write': 'granted' }[name] ?? (() => { throw new TypeError('unknown permission'); })() }) },
    notifications: { permission: () => 'prompt', request: async () => 'granted', show: (title, body) => opened.push({ title, body }) },
    clipboard: { readText: async () => 'from the clipboard', writeText: async text => opened.push({ copied: text }) },
    play: async blob => opened.push({ played: blob.size, type: blob.type }),
    pickFile: async (accept, capture) => { opened.push({ accept, capture }); return new File(['hello'], 'note.txt', { type: 'text/plain' }); },
    takePicture: async () => new Blob([new Uint8Array([9, 9])], { type: 'image/jpeg' }),
    hasDocument: true,
    ...over,
  };
}

// ── texts ───────────────────────────────────────────────────────────

test('the generated web catalog carries the texts of a device step, in both languages, completely', () => {
  assert.deepEqual(generated.entries.find(row => row.key === 'presentation.client_step').value, entry.value);
  assert.deepEqual(Object.keys(en).sort(), Object.keys(pl).sort());
  assert.notEqual(en.title, pl.title);
  assert.ok(en.start_record && en.stop_record && en.decline && en.cancel && pl.start_record && pl.stop_record);
});

// ── what the page declares ──────────────────────────────────────────

test('capabilities follow what the browser has, with the permission state it reports', async () => {
  const found = await device.detectCapabilities(host());
  assert.deepEqual(Object.keys(found).sort(), ['captures_audio', 'captures_image', 'clipboard_read', 'clipboard_write', 'picks_files', 'plays_audio', 'requests_permission', 'shows_notifications']);
  assert.deepEqual(found.captures_audio, { permission: 'prompt', via: 'MediaRecorder' });
  assert.equal(found.captures_image.permission, 'granted');
  assert.equal(found.shows_notifications.permission, 'prompt', 'Notification.permission default: the browser will ask on use');
  const blocked = await device.detectCapabilities(host({ notifications: { permission: () => 'denied', request: async () => 'denied', show: () => {} } }));
  assert.equal(blocked.shows_notifications.permission, 'denied', 'the permission the browser reports is declared as it is');
  assert.equal(found.clipboard_read.permission, 'prompt', 'a browser that does not know the permission name (Firefox) will ask on use');
  assert.equal(found.clipboard_write.permission, 'granted');
  assert.equal(found.plays_audio.permission, 'granted');
  // what the browser lacks is not declared
  const bare = await device.detectCapabilities({ hasDocument: true, pickFile: async () => null });
  assert.deepEqual(Object.keys(bare).sort(), ['captures_image', 'picks_files'], 'a file chooser is a camera too (capture), nothing else');
  assert.equal(bare.captures_image.via, 'file chooser');
  assert.deepEqual(await device.detectCapabilities({}), {});
  const noRecorder = await device.detectCapabilities(host({ MediaRecorder: undefined }));
  assert.equal('captures_audio' in noRecorder, false);
  const shell = await device.detectCapabilities(host({ notifications: undefined, nativeNotify: { state: () => 'granted', request: () => {}, show: () => true } }));
  assert.deepEqual(shell.shows_notifications, { permission: 'granted', via: 'native shell' });
  const asking = await device.detectCapabilities(host({ notifications: undefined, nativeNotify: { state: () => 'prompt', request: () => {}, show: () => false } }));
  assert.equal(asking.shows_notifications.permission, 'prompt', 'Android 13 has not been asked yet');
  // the body of client.declare
  assert.deepEqual(await device.declaration(host(), 'web-1', 'web', 'Test browser'), {
    client_id: 'web-1', client: { kind: 'web', label: 'Test browser' }, capabilities: found });
});

test('a client session declares once, again when stale, and says nothing without kernel support', async () => {
  const calls = [];
  let clock = 1000;
  const actions = { clientKind: 'android', clientDeclare: async request => { calls.push(request); return { client_id: request.client_id, capabilities: {}, runnable_actions: [], refused: {} }; } };
  const session = new sessionModule.ClientSession({ actions, host: host(), id: 'web-1', label: 'L', now: () => clock, freshMs: 30000 });
  assert.equal(session.last, null);
  assert.equal(await session.ensure(), 'web-1');
  assert.equal(calls.length, 1);
  assert.equal(calls[0].client.kind, 'android', 'the transport says what kind of client this is');
  assert.equal(await session.ensure(), 'web-1');
  assert.equal(calls.length, 1, 'fresh: not declared again');
  clock += 31000;
  await session.ensure();
  assert.equal(calls.length, 2);
  await session.ensure(true);
  assert.equal(calls.length, 3);
  const quiet = new sessionModule.ClientSession({ actions: undefined, host: host(), id: 'web-2' });
  assert.equal(await quiet.ensure(), '');
  const broken = new sessionModule.ClientSession({ actions: { clientDeclare: async () => { throw new Error('offline'); } }, host: host(), id: 'web-3' });
  assert.equal(await broken.ensure(), '', 'a kernel that cannot be reached: nothing is claimed');
  assert.equal(broken.last, null);
  // the id is chosen once and kept for the tab
  const kept = new Map();
  const store = { getItem: key => kept.get(key) ?? null, setItem: (key, value) => kept.set(key, value) };
  const first = sessionModule.clientIdentity(store);
  assert.match(first, /^web-[0-9a-f]{12}$/);
  assert.equal(sessionModule.clientIdentity(store), first);
  assert.match(sessionModule.clientIdentity({ getItem: () => { throw new Error('blocked'); }, setItem: () => { throw new Error('blocked'); } }), /^web-/, 'blocked storage: an id for the page');
});

// ── recording ───────────────────────────────────────────────────────

test('a recording starts and stops: the sound comes back as base64 with its mime type, the microphone is closed', async () => {
  const h = host();
  const recording = await device.startRecording(h);
  assert.deepEqual(h.opened[0], { audio: true });
  assert.equal(FakeRecorder.starts, 1);
  const outcome = await recording.stop();
  assert.equal(outcome.state, 'ok');
  assert.deepEqual(outcome.parts, [{ kind: 'audio', mime: 'audio/webm;codecs=opus', data_base64: btoa(String.fromCharCode(1, 2, 3, 4)) }]);
  assert.deepEqual(h.opened[1].stopped, [true], 'the tracks are stopped: the microphone light goes out');
  assert.equal((await recording.stop()).state, 'failed', 'a recording ends once');
});

test('a microphone the person refused is declined, one that is missing is unavailable, a silent recording fails; abort closes it', async () => {
  const refuse = name => host({ mediaDevices: { getUserMedia: async () => { const e = new Error('Permission denied'); e.name = name; throw e; } } });
  await assert.rejects(device.startRecording(refuse('NotAllowedError')), error => error.state === 'declined' && /Permission denied/.test(error.message));
  await assert.rejects(device.startRecording(refuse('NotFoundError')), error => error.state === 'unavailable');
  await assert.rejects(device.startRecording(refuse('NotReadableError')), error => error.state === 'failed');
  await assert.rejects(device.startRecording({}), error => error.state === 'unavailable');
  class Silent extends FakeRecorder { stop() { queueMicrotask(() => this.onstop?.()); } }
  const silent = await device.startRecording(host({ MediaRecorder: Silent }));
  assert.deepEqual((await silent.stop()).state, 'failed');
  const h = host();
  const dropped = await device.startRecording(h);
  dropped.abort();
  assert.deepEqual(h.opened[1].stopped, [true]);
});

// ── the other steps ─────────────────────────────────────────────────

const load = async hash => new Blob([new Uint8Array(7)], { type: 'audio/mp4' });
const step = (action, over = {}) => ({ action, title: action, with: {}, inputs: [], ...over });

test('the clipboard: read brings the text, write puts the text that flowed in or was named', async () => {
  const h = host();
  assert.deepEqual(await device.runStep(h, step('device.clipboard_read'), load), { state: 'ok', parts: [{ kind: 'text', text: 'from the clipboard' }] });
  assert.equal((await device.runStep(h, step('device.clipboard_write', { inputs: [{ kind: 'text', text: 'one' }, { kind: 'text', text: 'two' }] }), load)).state, 'ok');
  assert.deepEqual(h.opened.at(-1), { copied: 'one\n\ntwo' });
  await device.runStep(h, step('device.clipboard_write', { with: { text: 'named' }, inputs: [{ kind: 'text', text: 'ignored' }] }), load);
  assert.deepEqual(h.opened.at(-1), { copied: 'named' });
  const denied = host({ clipboard: { readText: async () => { const e = new Error('Read permission denied.'); e.name = 'NotAllowedError'; throw e; } } });
  const outcome = await device.runStep(denied, step('device.clipboard_read'), load);
  assert.equal(outcome.state, 'declined');
  assert.equal((await device.runStep({}, step('device.clipboard_read'), load)).state, 'unavailable');
});

test('a notification asks for the permission when the browser has not been asked, and respects a refusal', async () => {
  const h = host();
  const ok = await device.runStep(h, step('device.notify', { title: 'Loom', with: { text: 'Done' } }), load);
  assert.equal(ok.state, 'ok');
  assert.deepEqual(h.opened.at(-1), { title: 'Loom', body: 'Done' });
  const refused = host({ notifications: { permission: () => 'denied', request: async () => 'denied', show: () => assert.fail('must not show') } });
  assert.equal((await device.runStep(refused, step('device.notify', { with: { text: 'x' } }), load)).state, 'declined');
  const shell = [];
  const native = (state, shown = true) => ({ state: () => state, request: () => shell.push('asked'), show: (t, b) => { shell.push([t, b]); return shown; } });
  assert.equal((await device.runStep(host({ notifications: undefined, nativeNotify: native('granted') }), step('device.notify', { title: 'T', with: { text: 'B' } }), load)).state, 'ok');
  assert.deepEqual(shell, [['T', 'B']]);
  shell.length = 0;
  const asked = await device.runStep(host({ notifications: undefined, nativeNotify: native('prompt') }), step('device.notify', { with: { text: 'B' } }), load);
  assert.equal(asked.state, 'declined', 'the system dialog comes first; the person does it again');
  assert.deepEqual(shell, ['asked']);
  assert.equal((await device.runStep(host({ notifications: undefined, nativeNotify: native('granted', false) }), step('device.notify', { with: { text: 'B' } }), load)).state, 'declined');
});

test('files: a chosen text arrives as text, a chosen recording as audio with its bytes; closing the chooser declines; a picture can come from the chooser', async () => {
  const h = host();
  const text = await device.runStep(h, step('device.pick_file'), load);
  assert.deepEqual(text, { state: 'ok', parts: [{ kind: 'text', text: 'hello', name: 'note.txt' }] });
  const audio = await device.runStep(host({ pickFile: async () => new File([new Uint8Array([5, 6])], 'a.m4a', { type: 'audio/mp4' }) }), step('device.pick_file'), load);
  assert.deepEqual(audio.parts, [{ kind: 'audio', mime: 'audio/mp4', data_base64: btoa(String.fromCharCode(5, 6)), name: 'a.m4a' }]);
  const other = await device.runStep(host({ pickFile: async () => new File(['x'], 'a.bin', { type: '' }) }), step('device.pick_file'), load);
  assert.equal(other.parts[0].kind, 'file_ref');
  assert.equal((await device.runStep(host({ pickFile: async () => null }), step('device.pick_file'), load)).state, 'declined');
  const picture = await device.runStep(h, step('device.capture_image'), load);
  assert.deepEqual(picture.parts, [{ kind: 'image', mime: 'image/jpeg', data_base64: btoa(String.fromCharCode(9, 9)), name: '' }]);
  const fallback = host({ takePicture: undefined, mediaDevices: undefined });
  await device.runStep(fallback, step('device.capture_image'), load);
  assert.deepEqual(fallback.opened.at(-1), { accept: 'image/*', capture: 'environment' }, 'no camera API: the chooser with capture opens the camera app');
});

test('playback takes the recording the step names (an argument or the audio that flowed in) from the kernel', async () => {
  const h = host();
  const asked = [];
  const track = async hash => { asked.push(hash); return load(hash); };
  assert.equal((await device.runStep(h, step('device.play_audio', { with: { audio: 'a'.repeat(64) } }), track)).state, 'ok');
  assert.equal((await device.runStep(h, step('device.play_audio', { inputs: [{ kind: 'audio', blob_hash: 'b'.repeat(64) }] }), track)).state, 'ok');
  assert.deepEqual(asked, ['a'.repeat(64), 'b'.repeat(64)]);
  assert.deepEqual(h.opened.at(-1), { played: 7, type: 'audio/mp4' });
  assert.equal((await device.runStep(h, step('device.play_audio'), track)).state, 'failed', 'nothing named to play');
});

test('asking for a permission answers with its state; an action this client has no executor for is unavailable', async () => {
  const h = host();
  assert.deepEqual((await device.runStep(h, step('device.request_permission', { with: { permission: 'microphone' } }), load)).data, { permission: 'microphone', state: 'granted' });
  assert.deepEqual(h.opened[1].stopped, [true], 'the stream opened for the question is closed at once');
  const refuse = host({ mediaDevices: { getUserMedia: async () => { const e = new Error('no'); e.name = 'NotAllowedError'; throw e; } } });
  assert.deepEqual((await device.runStep(refuse, step('device.request_permission', { with: { permission: 'camera' } }), load)).data, { permission: 'camera', state: 'denied' });
  assert.equal((await device.runStep(h, step('device.request_permission', { with: { permission: 'notifications' } }), load)).data.state, 'granted');
  assert.equal((await device.runStep(h, step('device.capture_video'), load)).state, 'unavailable');
  assert.equal(device.stepKind(step('device.capture_video')), 'unsupported');
});

test('the answer to the kernel carries the run, the receipt, the state and only what there is to say', () => {
  assert.deepEqual(device.resultOf('ar_1', 'arr_1', { state: 'ok', parts: [{ kind: 'text', text: 'x' }] }, 'web:web-1'),
    { run_id: 'ar_1', receipt_id: 'arr_1', state: 'ok', ref: 'web:web-1', parts: [{ kind: 'text', text: 'x' }] });
  assert.deepEqual(device.resultOf('ar_1', 'arr_1', { state: 'declined', error: { message: 'no' } }, 'web:web-1'),
    { run_id: 'ar_1', receipt_id: 'arr_1', state: 'declined', ref: 'web:web-1', error: { message: 'no' } });
});

// ── the block and the pending question ──────────────────────────────

const spec = { schema: 'loom.client_step/1', action: 'device.record_audio', capability: 'captures_audio', permission: 'microphone', title: 'Record audio',
  labels: { pl: 'Nagraj dźwięk' }, prompt: { en: 'Record with the microphone of this device; stop when you are done.', pl: 'Nagraj mikrofonem tego urządzenia; zatrzymaj, gdy skończysz.' },
  expects: ['audio'], with: {}, inputs: [] };

test('a run paused on a device step is a question of kind client, with the step; an ordinary confirmation is not', () => {
  const asked = proposal.pendingAnswer({ action_run: { run_id: 'ar_9', status: 'awaiting_client', pending: { kind: 'client_step', receipt_id: 'arr_9', client_step: spec } } });
  assert.equal(asked.kind, 'client');
  assert.equal(asked.runId, 'ar_9');
  assert.equal(asked.receiptId, 'arr_9');
  assert.deepEqual(asked.clientStep, spec);
  assert.equal(proposal.pendingAnswer({ action_run: { run_id: 'ar_2', pending: { kind: 'confirmation', receipt_id: 'arr_2' } } }).kind, 'confirmation');
  assert.equal(proposal.pendingAnswer({ action_run: { run_id: 'ar_9', status: 'cancelled', pending: null } }), null, 'a closed question is not asked again');
});

test('the block shows what the kernel asks, the permission hint, a start control and the ways out - in the language of the catalog', () => {
  const html = renderToStaticMarkup(createElement(block.default, { feature, step: spec, runId: 'ar_9', receiptId: 'arr_9', host: host(), load, clientRef: 'web:web-1',
    onAnswer: async () => {}, onCancel: () => {}, testId: 'chat-client-step' }));
  assert.match(html, /data-testid="chat-client-step"/);
  assert.match(html, /data-run-id="ar_9"/);
  assert.match(html, /data-receipt-id="arr_9"/);
  assert.match(html, /data-kind="client"/);
  assert.ok(html.includes(en.title));
  assert.ok(html.includes(spec.prompt.en));
  assert.ok(html.includes(en.start_record));
  assert.ok(html.includes(en.decline));
  assert.ok(html.includes(en.cancel));
  assert.match(html, /microphone/, 'the permission the device will ask for is named');
  assert.doesNotMatch(html, /chat-client-step-stop/, 'nothing is recording yet');
  const texts = view.clientStepTexts(feature, spec);
  assert.equal(texts.kind, 'record');
  assert.equal(texts.recording(3), en.recording.replace('{{seconds}}', '3'));
  // each kind of step has its own control
  for (const [action, id] of [['device.capture_image', 'do_capture_image'], ['device.pick_file', 'do_pick_file'], ['device.clipboard_read', 'do_clipboard_read'],
    ['device.clipboard_write', 'do_clipboard_write'], ['device.notify', 'do_notify'], ['device.play_audio', 'do_play_audio'], ['device.request_permission', 'do_request_permission']])
    assert.equal(view.clientStepTexts(feature, { action }).start, en[id], action);
  // without a catalog the machine names are shown, not nothing
  assert.equal(view.clientStepTexts(null, { action: 'device.pick_file', title: 'Pick a file' }).start, 'Pick a file');
  // an action this client does not know cannot be started, only declined
  const unknown = renderToStaticMarkup(createElement(block.default, { feature, step: { action: 'device.capture_video' }, runId: 'ar_9', receiptId: 'arr_9', host: host(), load, clientRef: 'x',
    onAnswer: async () => {}, onCancel: () => {}, testId: 'u' }));
  assert.match(unknown, /<button[^>]*disabled=""[^>]*data-testid="u-start"/);
});

// ── the transports ──────────────────────────────────────────────────

const calls = [
  ['clientDeclare', { client_id: 'web-1', client: { kind: 'web' }, capabilities: { captures_audio: { permission: 'prompt', via: 'MediaRecorder' } } }, '/api/client/declare', 'client.declare'],
  ['clientCapabilities', { client_id: 'web-1' }, '/api/client/capabilities', 'client.capabilities'],
  ['clientResult', { run_id: 'ar_1', receipt_id: 'arr_1', state: 'ok', parts: [{ kind: 'audio', mime: 'audio/webm', data_base64: 'AAAA' }], ref: 'web:web-1' }, '/api/actions/client_result', 'client_result'],
  ['clientBlob', { hash: 'a'.repeat(64) }, '/api/client/blob', 'client.blob'],
];

test('over HTTP the client calls are POSTs of their request; over the native bridge they are loom_invoke on the actions port', async () => {
  const original = globalThis.fetch;
  const seen = [];
  globalThis.fetch = async (url, options) => { seen.push({ url, options }); return new Response(JSON.stringify({ id: 'ar_1', status: 'completed', url })); };
  try {
    const api = new LoomHttpApi();
    assert.equal(api.actions.clientKind, 'web');
    for (const [name, request, path] of calls) {
      await api.actions[name](request);
      assert.equal(seen.at(-1).url, path, name);
      assert.equal(seen.at(-1).options.method, 'POST', name);
      assert.deepEqual(JSON.parse(seen.at(-1).options.body), request, name);
    }
  } finally { globalThis.fetch = original; }
  const bridged = [];
  globalThis.window = { LoomBridge: { call: (method, json) => { bridged.push({ method, args: JSON.parse(json) }); return JSON.stringify({ id: 'ar_2', status: 'completed' }); } } };
  try {
    const api = new LoomJniApi();
    assert.equal(api.actions.clientKind, 'android', 'the Android shell declares itself as an android client');
    for (const [name, request, , action] of calls) {
      await api.actions[name](request);
      assert.deepEqual(bridged.at(-1), { method: 'invoke', args: { port: 'actions', action, request } }, name);
    }
  } finally { delete globalThis.window; }
});

test('the declaration the Android shell sends has the shape the kernel reads: capabilities with permission and how', async () => {
  const sent = [];
  globalThis.window = { LoomBridge: { call: (method, json) => { sent.push(JSON.parse(json)); return JSON.stringify({ client_id: 'web-9', capabilities: {}, runnable_actions: [], refused: {} }); } } };
  try {
    const api = new LoomJniApi();
    const session = new sessionModule.ClientSession({ actions: api.actions, host: host({ notifications: undefined, nativeNotify: { state: () => 'granted', request: () => {}, show: () => true } }), id: 'web-9', label: 'Android WebView' });
    assert.equal(await session.ensure(), 'web-9');
    const request = sent.at(-1).request;
    assert.deepEqual(Object.keys(request).sort(), ['capabilities', 'client', 'client_id']);
    assert.deepEqual(request.client, { kind: 'android', label: 'Android WebView' });
    for (const [name, c] of Object.entries(request.capabilities)) {
      assert.ok(['granted', 'prompt', 'denied'].includes(c.permission), name);
      assert.equal(typeof c.via, 'string', name);
    }
    assert.deepEqual(request.capabilities.shows_notifications, { permission: 'granted', via: 'native shell' });
  } finally { delete globalThis.window; }
});
