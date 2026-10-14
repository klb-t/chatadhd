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

test('a client session declares lazily: nothing on its own, once at the first ask, again only when the payload changed or ran out', async () => {
  const calls = [];
  let clock = 1000;
  const actions = { clientKind: 'android', clientDeclare: async request => { calls.push(request); return { client_id: request.client_id, capabilities: {}, runnable_actions: [], refused: {},
    expires: new Date(clock + 60000).toISOString() }; } };
  let microphone = 'prompt';
  const changing = () => host({ permissions: { query: async ({ name }) => ({ state: name === 'microphone' ? microphone : 'granted' }) } });
  const session = new sessionModule.ClientSession({ actions, host: changing(), id: 'web-1', label: 'L', now: () => clock });
  assert.equal(session.last, null);
  assert.equal(calls.length, 0, 'creating the session sends nothing: viewing the page is not a reason to declare');
  assert.equal(await session.ensure(), 'web-1');
  assert.equal(calls.length, 1);
  assert.equal(calls[0].client.kind, 'android', 'the transport says what kind of client this is');
  assert.equal(calls[0].capabilities.captures_audio.permission, 'prompt');
  assert.equal(await session.ensure(), 'web-1');
  assert.equal(await session.ensure(), 'web-1');
  assert.equal(calls.length, 1, 'the same payload is not declared again');
  microphone = 'granted';
  await session.ensure();
  assert.equal(calls.length, 2, 'a permission that changed is declared');
  assert.equal(calls[1].capabilities.captures_audio.permission, 'granted');
  await session.ensure();
  assert.equal(calls.length, 2);
  clock += 61000;
  await session.ensure();
  assert.equal(calls.length, 3, 'the kernel forgets a declaration when its validity ends: declared again at the next ask');
  await session.ensure(true);
  assert.equal(calls.length, 4);
  // two asks at once make one declaration
  const parallel = new sessionModule.ClientSession({ actions, host: host(), id: 'web-5', now: () => clock });
  const before = calls.length;
  assert.deepEqual(await Promise.all([parallel.ensure(), parallel.ensure(), parallel.ensure()]), ['web-5', 'web-5', 'web-5']);
  assert.equal(calls.length, before + 1);
  const quiet = new sessionModule.ClientSession({ actions: undefined, host: host(), id: 'web-2' });
  assert.equal(await quiet.ensure(), '');
  // a kernel that refuses is not asked again with the same payload; one that cannot be reached is asked again at the next ask
  let refusals = 0;
  const refusing = new sessionModule.ClientSession({ actions: { clientDeclare: async () => { refusals += 1; throw new Error('Unsupported endpoint'); } }, host: host(), id: 'web-3' });
  assert.equal(await refusing.ensure(), '');
  assert.equal(await refusing.ensure(), '');
  assert.equal(refusals, 1);
  assert.equal(refusing.last, null);
  let attempts = 0;
  const offline = new sessionModule.ClientSession({ actions: { clientDeclare: async () => { attempts += 1; throw new TypeError('Failed to fetch'); } }, host: host(), id: 'web-4' });
  assert.equal(await offline.ensure(), '', 'a kernel that cannot be reached: nothing is claimed');
  await offline.ensure();
  assert.equal(attempts, 2);
  // the id is chosen once and kept for the tab
  const kept = new Map();
  const store = { getItem: key => kept.get(key) ?? null, setItem: (key, value) => kept.set(key, value) };
  const first = sessionModule.clientIdentity(store);
  assert.match(first, /^web-[0-9a-f]{12}$/);
  assert.equal(sessionModule.clientIdentity(store), first);
  assert.match(sessionModule.clientIdentity({ getItem: () => { throw new Error('blocked'); }, setItem: () => { throw new Error('blocked'); } }), /^web-/, 'blocked storage: an id for the page');
});

// ── recording ───────────────────────────────────────────────────────

// The kernel's side of an upload, as a double: it keeps what it was given and names it by a hash of the bytes (the real one is sha-256).
const uploads = [];
const upload = async (blob, name) => {
  const bytes = [...new Uint8Array(await blob.arrayBuffer())];
  uploads.push({ bytes, type: blob.type, name });
  return { hash: 'h'.repeat(60) + String(bytes.join('')).slice(0, 4).padEnd(4, '0') };
};

test('a recording starts and stops: the sound goes to the kernel as bytes and the answer names it by hash; the microphone is closed', async () => {
  const h = host();
  uploads.length = 0;
  const recording = await device.startRecording(h, upload);
  assert.deepEqual(h.opened[0], { audio: true });
  assert.equal(FakeRecorder.starts, 1);
  const outcome = await recording.stop();
  assert.equal(outcome.state, 'ok');
  assert.deepEqual(uploads, [{ bytes: [1, 2, 3, 4], type: 'audio/webm;codecs=opus', name: 'recording' }]);
  assert.deepEqual(outcome.parts, [{ kind: 'audio', mime: 'audio/webm;codecs=opus', blob_hash: 'h'.repeat(60) + '1234' }]);
  assert.equal(JSON.stringify(outcome).includes('base64'), false, 'no bytes in the answer');
  assert.deepEqual(h.opened[1].stopped, [true], 'the tracks are stopped: the microphone light goes out');
  assert.equal((await recording.stop()).state, 'failed', 'a recording ends once');
});

test('a microphone the person refused is declined, one that is missing is unavailable, a silent recording fails; abort closes it', async () => {
  const refuse = name => host({ mediaDevices: { getUserMedia: async () => { const e = new Error('Permission denied'); e.name = name; throw e; } } });
  await assert.rejects(device.startRecording(refuse('NotAllowedError'), upload), error => error.state === 'declined' && /Permission denied/.test(error.message));
  await assert.rejects(device.startRecording(refuse('NotFoundError'), upload), error => error.state === 'unavailable');
  await assert.rejects(device.startRecording(refuse('NotReadableError'), upload), error => error.state === 'failed');
  await assert.rejects(device.startRecording({}, upload), error => error.state === 'unavailable');
  class Silent extends FakeRecorder { stop() { queueMicrotask(() => this.onstop?.()); } }
  const silent = await device.startRecording(host({ MediaRecorder: Silent }), upload);
  assert.deepEqual((await silent.stop()).state, 'failed');
  const h = host();
  const dropped = await device.startRecording(h, upload);
  dropped.abort();
  assert.deepEqual(h.opened[1].stopped, [true]);
});

test('a recording the kernel could not take is a failed step with the reason, not a lost answer; the microphone is closed all the same', async () => {
  const h = host();
  const recording = await device.startRecording(h, async () => { throw new Error('HTTP 413'); });
  const outcome = await recording.stop();
  assert.deepEqual(outcome, { state: 'failed', error: { message: 'HTTP 413' } });
  assert.deepEqual(h.opened[1].stopped, [true]);
});

// ── the other steps ─────────────────────────────────────────────────

const load = async hash => new Blob([new Uint8Array(7)], { type: 'audio/mp4' });
const step = (action, over = {}) => ({ action, title: action, with: {}, inputs: [], ...over });

test('the clipboard: read brings the text, write puts the text that flowed in or was named', async () => {
  const h = host();
  assert.deepEqual(await device.runStep(h, step('device.clipboard_read'), load, upload), { state: 'ok', parts: [{ kind: 'text', text: 'from the clipboard' }] });
  assert.equal((await device.runStep(h, step('device.clipboard_write', { inputs: [{ kind: 'text', text: 'one' }, { kind: 'text', text: 'two' }] }), load, upload)).state, 'ok');
  assert.deepEqual(h.opened.at(-1), { copied: 'one\n\ntwo' });
  await device.runStep(h, step('device.clipboard_write', { with: { text: 'named' }, inputs: [{ kind: 'text', text: 'ignored' }] }), load, upload);
  assert.deepEqual(h.opened.at(-1), { copied: 'named' });
  const denied = host({ clipboard: { readText: async () => { const e = new Error('Read permission denied.'); e.name = 'NotAllowedError'; throw e; } } });
  const outcome = await device.runStep(denied, step('device.clipboard_read'), load, upload);
  assert.equal(outcome.state, 'declined');
  assert.equal((await device.runStep({}, step('device.clipboard_read'), load, upload)).state, 'unavailable');
});

test('a notification asks for the permission when the browser has not been asked, and respects a refusal', async () => {
  const h = host();
  const ok = await device.runStep(h, step('device.notify', { title: 'Loom', with: { text: 'Done' } }), load, upload);
  assert.equal(ok.state, 'ok');
  assert.deepEqual(h.opened.at(-1), { title: 'Loom', body: 'Done' });
  const refused = host({ notifications: { permission: () => 'denied', request: async () => 'denied', show: () => assert.fail('must not show') } });
  assert.equal((await device.runStep(refused, step('device.notify', { with: { text: 'x' } }), load, upload)).state, 'declined');
  const shell = [];
  const native = (state, shown = true) => ({ state: () => state, request: () => shell.push('asked'), show: (t, b) => { shell.push([t, b]); return shown; } });
  assert.equal((await device.runStep(host({ notifications: undefined, nativeNotify: native('granted') }), step('device.notify', { title: 'T', with: { text: 'B' } }), load, upload)).state, 'ok');
  assert.deepEqual(shell, [['T', 'B']]);
  shell.length = 0;
  const asked = await device.runStep(host({ notifications: undefined, nativeNotify: native('prompt') }), step('device.notify', { with: { text: 'B' } }), load, upload);
  assert.equal(asked.state, 'declined', 'the system dialog comes first; the person does it again');
  assert.deepEqual(shell, ['asked']);
  assert.equal((await device.runStep(host({ notifications: undefined, nativeNotify: native('granted', false) }), step('device.notify', { with: { text: 'B' } }), load, upload)).state, 'declined');
});

test('files: a chosen text arrives as text, a chosen recording as audio with its bytes; closing the chooser declines; a picture can come from the chooser', async () => {
  const h = host();
  const text = await device.runStep(h, step('device.pick_file'), load, upload);
  assert.deepEqual(text, { state: 'ok', parts: [{ kind: 'text', text: 'hello', name: 'note.txt' }] });
  const audio = await device.runStep(host({ pickFile: async () => new File([new Uint8Array([5, 6])], 'a.m4a', { type: 'audio/mp4' }) }), step('device.pick_file'), load, upload);
  assert.deepEqual(uploads.at(-1), { bytes: [5, 6], type: 'audio/mp4', name: 'a.m4a' });
  assert.deepEqual(audio.parts, [{ kind: 'audio', mime: 'audio/mp4', blob_hash: 'h'.repeat(60) + '5600', name: 'a.m4a' }]);
  const other = await device.runStep(host({ pickFile: async () => new File(['x'], 'a.bin', { type: '' }) }), step('device.pick_file'), load, upload);
  assert.equal(other.parts[0].kind, 'file_ref');
  assert.equal((await device.runStep(host({ pickFile: async () => null }), step('device.pick_file'), load, upload)).state, 'declined');
  const picture = await device.runStep(h, step('device.capture_image'), load, upload);
  assert.deepEqual(uploads.at(-1), { bytes: [9, 9], type: 'image/jpeg', name: '' });
  assert.deepEqual(picture.parts, [{ kind: 'image', mime: 'image/jpeg', blob_hash: 'h'.repeat(60) + '9900', name: '' }]);
  const fallback = host({ takePicture: undefined, mediaDevices: undefined });
  await device.runStep(fallback, step('device.capture_image'), load, upload);
  assert.deepEqual(fallback.opened.at(-1), { accept: 'image/*', capture: 'environment' }, 'no camera API: the chooser with capture opens the camera app');
});

test('playback takes the recording the step names (an argument or the audio that flowed in) from the kernel', async () => {
  const h = host();
  const asked = [];
  const track = async hash => { asked.push(hash); return load(hash); };
  assert.equal((await device.runStep(h, step('device.play_audio', { with: { audio: 'a'.repeat(64) } }), track, upload)).state, 'ok');
  assert.equal((await device.runStep(h, step('device.play_audio', { inputs: [{ kind: 'audio', blob_hash: 'b'.repeat(64) }] }), track, upload)).state, 'ok');
  assert.deepEqual(asked, ['a'.repeat(64), 'b'.repeat(64)]);
  assert.deepEqual(h.opened.at(-1), { played: 7, type: 'audio/mp4' });
  assert.equal((await device.runStep(h, step('device.play_audio'), track, upload)).state, 'failed', 'nothing named to play');
});

test('asking for a permission answers with its state; an action this client has no executor for is unavailable', async () => {
  const h = host();
  assert.deepEqual((await device.runStep(h, step('device.request_permission', { with: { permission: 'microphone' } }), load, upload)).data, { permission: 'microphone', state: 'granted' });
  assert.deepEqual(h.opened[1].stopped, [true], 'the stream opened for the question is closed at once');
  const refuse = host({ mediaDevices: { getUserMedia: async () => { const e = new Error('no'); e.name = 'NotAllowedError'; throw e; } } });
  assert.deepEqual((await device.runStep(refuse, step('device.request_permission', { with: { permission: 'camera' } }), load, upload)).data, { permission: 'camera', state: 'denied' });
  assert.equal((await device.runStep(h, step('device.request_permission', { with: { permission: 'notifications' } }), load, upload)).data.state, 'granted');
  assert.equal((await device.runStep(h, step('device.capture_video'), load, upload)).state, 'unavailable');
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
  const html = renderToStaticMarkup(createElement(block.default, { feature, step: spec, runId: 'ar_9', receiptId: 'arr_9', host: host(), load, upload, clientRef: 'web:web-1',
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
  const unknown = renderToStaticMarkup(createElement(block.default, { feature, step: { action: 'device.capture_video' }, runId: 'ar_9', receiptId: 'arr_9', host: host(), load, upload, clientRef: 'x',
    onAnswer: async () => {}, onCancel: () => {}, testId: 'u' }));
  assert.match(unknown, /<button[^>]*disabled=""[^>]*data-testid="u-start"/);
});

// ── the transports ──────────────────────────────────────────────────

const calls = [
  ['clientDeclare', { client_id: 'web-1', client: { kind: 'web' }, capabilities: { captures_audio: { permission: 'prompt', via: 'MediaRecorder' } } }, '/api/client/declare', 'client.declare'],
  ['clientCapabilities', { client_id: 'web-1' }, '/api/client/capabilities', 'client.capabilities'],
  ['clientResult', { run_id: 'ar_1', receipt_id: 'arr_1', state: 'ok', parts: [{ kind: 'audio', mime: 'audio/webm', blob_hash: 'a'.repeat(64) }], ref: 'web:web-1' }, '/api/actions/client_result', 'client_result'],
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

// ── bytes: uploaded and fetched, never inside the JSON (P14 6) ──────────────────────────────

const HASH = 'c'.repeat(64);

test('over HTTP a recording is a multipart upload of the file with its type, answered with the blob; a blob is fetched as the bytes of a GET', async () => {
  const original = globalThis.fetch;
  const seen = [];
  const bytes = new Uint8Array([0, 1, 2, 253, 254, 255]);
  globalThis.fetch = async (url, options = {}) => {
    seen.push({ url, options });
    if (url === '/api/client/blob/upload') return new Response(JSON.stringify({ hash: HASH, size: 6, mime: 'audio/webm' }));
    if (url === `/api/client/blob/${HASH}/raw`) return new Response(bytes, { headers: { 'Content-Type': 'audio/webm' } });
    return new Response(JSON.stringify({ error: { code: 'not_found', message: 'no blob' } }), { status: 404 });
  };
  try {
    const api = new LoomHttpApi();
    const stored = await api.actions.clientBlobUpload(new Blob([bytes], { type: 'audio/webm' }), { name: 'rec.webm' });
    assert.deepEqual(stored, { hash: HASH, size: 6, mime: 'audio/webm' });
    assert.equal(seen[0].options.method, 'POST');
    const form = seen[0].options.body;
    assert.ok(form instanceof FormData, 'multipart, not JSON');
    assert.equal(form.get('mime'), 'audio/webm');
    const file = form.get('file');
    assert.equal(file.name, 'rec.webm');
    assert.equal(file.type, 'audio/webm');
    assert.deepEqual([...new Uint8Array(await file.arrayBuffer())], [...bytes]);
    assert.equal(seen[0].options.headers['Content-Type'], undefined, 'the browser sets the boundary');
    const fetched = await api.actions.clientBlobFetch(HASH);
    assert.equal(seen[1].url, `/api/client/blob/${HASH}/raw`);
    assert.equal(seen[1].options.method, undefined, 'a GET');
    assert.equal(fetched.type, 'audio/webm');
    assert.deepEqual([...new Uint8Array(await fetched.arrayBuffer())], [...bytes]);
    await assert.rejects(api.actions.clientBlobFetch('d'.repeat(64)), error => error.status === 404);
    globalThis.fetch = async () => new Response(JSON.stringify({ error: { code: 'invalid_argument', message: 'multipart field file is required' } }), { status: 400 });
    await assert.rejects(api.actions.clientBlobUpload(new Blob(['x'])), /multipart field file is required/);
  } finally { globalThis.fetch = original; }
});

// The kernel of the native shell, as a double behind the bridge: the actions that build an upload from chunks and read a blob in chunks.
function bridgeKernel(blobBytes = new Uint8Array(0)) {
  const log = [];
  const parts = [];
  const bridge = { call: (method, json) => {
    const { action, request } = JSON.parse(json).action ? JSON.parse(json) : { action: '', request: {} };
    log.push({ action, request });
    if (action === 'client.blob_begin') return JSON.stringify({ upload_id: 'up_000000000001' });
    if (action === 'client.blob_append') {
      if (request.data_base64 === 'FAIL') return JSON.stringify({ error: { code: 'io', message: 'disk full' } });
      parts.push(Uint8Array.from(atob(request.data_base64), c => c.charCodeAt(0)));
      return JSON.stringify({ upload_id: request.upload_id, size: parts.reduce((n, p) => n + p.length, 0) });
    }
    if (action === 'client.blob_commit') return JSON.stringify(request.abort ? { upload_id: request.upload_id, aborted: true } : { hash: HASH, size: parts.reduce((n, p) => n + p.length, 0), mime: request.mime });
    if (action === 'client.blob_read') {
      const length = Math.min(request.length, blobBytes.length - request.offset);
      const chunk = blobBytes.subarray(request.offset, request.offset + length);
      return JSON.stringify({ hash: request.hash, size: blobBytes.length, mime: 'audio/mp4', offset: request.offset, length, eof: request.offset + length >= blobBytes.length, data_base64: btoa(Array.from(chunk, c => String.fromCharCode(c)).join('')) });
    }
    return JSON.stringify({});
  } };
  return { bridge, log, parts };
}

test('over the native bridge the bytes cross in bounded chunks into an upload the kernel stores by hash, and come back in chunks', async () => {
  const chunk = 196608;
  const bytes = new Uint8Array(chunk * 2 + 10).map((_, i) => i % 251);
  const kernel = bridgeKernel(bytes);
  globalThis.window = { LoomBridge: kernel.bridge };
  try {
    const api = new LoomJniApi();
    const stored = await api.actions.clientBlobUpload(new Blob([bytes], { type: 'audio/mp4' }), { name: 'a.m4a' });
    assert.deepEqual(stored, { hash: HASH, size: bytes.length, mime: 'audio/mp4' });
    assert.deepEqual(kernel.log.map(row => row.action), ['client.blob_begin', 'client.blob_append', 'client.blob_append', 'client.blob_append', 'client.blob_commit']);
    assert.deepEqual(kernel.parts.map(part => part.length), [chunk, chunk, 10], 'a bounded chunk at a time');
    assert.deepEqual(kernel.log.at(-1).request, { upload_id: 'up_000000000001', mime: 'audio/mp4' });
    assert.deepEqual([...new Uint8Array(await new Blob(kernel.parts).arrayBuffer())], [...bytes], 'the kernel got every byte in order');
    // reading back: the same bytes, chunk by chunk, until the kernel says it is the end
    kernel.log.length = 0;
    const fetched = await api.actions.clientBlobFetch(HASH);
    assert.deepEqual(kernel.log.map(row => [row.action, row.request.offset, row.request.length]),
      [['client.blob_read', 0, chunk], ['client.blob_read', chunk, chunk], ['client.blob_read', chunk * 2, chunk]]);
    assert.equal(fetched.type, 'audio/mp4');
    assert.deepEqual([...new Uint8Array(await fetched.arrayBuffer())], [...bytes]);
  } finally { delete globalThis.window; }
});

test('a chunk the kernel refuses ends the upload: it is aborted and the reason reaches the caller', async () => {
  const kernel = bridgeKernel();
  const original = kernel.bridge.call;
  kernel.bridge.call = (method, json) => { const answer = original(method, json); return JSON.parse(json).action === 'client.blob_append' ? JSON.stringify({ error: { code: 'io', message: 'disk full' } }) : answer; };
  globalThis.window = { LoomBridge: kernel.bridge };
  try {
    const api = new LoomJniApi();
    await assert.rejects(api.actions.clientBlobUpload(new Blob([new Uint8Array(5)], { type: 'audio/mp4' })), /disk full/);
    assert.deepEqual(kernel.log.map(row => row.action), ['client.blob_begin', 'client.blob_append', 'client.blob_commit']);
    assert.deepEqual(kernel.log.at(-1).request, { upload_id: 'up_000000000001', abort: true });
  } finally { delete globalThis.window; }
});
