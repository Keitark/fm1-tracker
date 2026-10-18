// SPDX-License-Identifier: Apache-2.0
import test from 'node:test';
import assert from 'node:assert/strict';
import { createCommonUpdater, DEFAULT_TIMING, runTransfer } from '../updater.mjs';
import { WIRE, decodeFrame } from '../protocol.mjs';
import { AttemptStore, supportedOrigin, DEVICE_LOCK } from '../journal.mjs';
import { createWebMidiBackend, selectPair } from '../web-midi.mjs';
import { packageFixture, cryptoFixture, fakeIndexedDB, fakeLocks, fakeMidi, requestPacket, capabilityPacket, BUILD, OLD_BUILD, ATTEMPT, REVIEW } from './fixtures.mjs';
const timing = { ...DEFAULT_TIMING, open: 0, handshake: 20, arm: 20, start: 0, reply: 0, terminal: 0, poll: 2, idleCheck: 10, idleWrite: 10, loader: 20, reboot: 20, enumerate: 1, total: 1000 };
async function context(options = {}) {
  const fixture = await packageFixture(), storage = fakeIndexedDB(options.storage), midi = fakeMidi(options.midi), locks = options.locks ?? fakeLocks(), events = [];
  const updater = createCommonUpdater({ navigator: midi.navigator, indexedDB: storage.indexedDB, crypto: cryptoFixture, locks,
    location: { origin: options.origin ?? 'https://fm1.test' }, secureContext: options.secureContext ?? true, timing: { ...timing, ...options.timing },
    onEvent: event => events.push(event) });
  return { ...fixture, storage, midi, locks, events, updater, args: extra => ({ attemptId: ATTEMPT, reviewId: REVIEW, packageBytes: fixture.bytes, manifest: fixture.manifest, ...extra }) };
}
test('construction/storage/status are passive; permission requires explicit activation and exact common identity', async () => {
  const c = await context(); assert.equal(c.midi.calls.permissions, 0); assert.equal(c.updater.status().connected, false);
  assert.equal(await c.updater.initialize(), null); await c.updater.savedAttempt(); assert.equal(c.midi.calls.opens, 0); assert.equal(c.midi.calls.sends.length, 0);
  c.midi.navigator.userActivation.isActive = false; await assert.rejects(c.updater.connect(), { code: 'user_gesture_required' }); assert.equal(c.midi.calls.permissions, 0);
  c.midi.navigator.userActivation.isActive = true; await c.updater.connect(); assert.equal(c.updater.status().identity, WIRE.commonIdentity); assert.equal(c.midi.calls.arms, 0); assert.equal(c.midi.calls.triggers, 0);
  assert.equal(c.midi.calls.opens, c.midi.calls.closes);
});
test('default blocked native capability remains readable and cannot arm or trigger', async () => {
  const c = await context({ midi: { cap: { loader: 0, error: 3 } } }); await c.updater.connect();
  assert.equal(c.updater.status().connected, true); assert.equal(c.updater.status().installAvailable, false);
  await assert.rejects(c.updater.update(c.args()), { code: 'install_unavailable' }); assert.equal(c.midi.calls.arms, 0); assert.equal(c.midi.calls.triggers, 0);
});
test('malformed/stale/wrong identity and denied permissions never authorize trigger', async () => {
  for (const midi of [{ permissionDenied: true }, { identity: 'FM-1_015' }, { noIdentity: true }, { malformedCap: capabilityPacket({ version: 2 }) }, { cap: { state: 4 } }]) {
    const c = await context({ midi }); await assert.rejects(c.updater.connect()); assert.equal(c.midi.calls.triggers, 0); assert.equal(c.updater.status().connected, false);
  }
  const midi = {}, c = await context({ midi }); await c.updater.connect(); midi.cap = { build: '44'.repeat(32) };
  await assert.rejects(c.updater.update(c.args()), { code: 'review_stale' }); assert.equal(c.midi.calls.arms, 0); assert.equal(c.midi.calls.triggers, 0);
});
test('unavailable/nonstrict/failing durable storage and insecure origin cannot open MIDI or clear recovery state', async () => {
  for (const options of [{ storage: { failOpen: true } }, { storage: { durability: 'relaxed' } }, { storage: { failWrite: true } }, { secureContext: false }, { origin: 'null' }]) {
    const c = await context(options); await assert.rejects(c.updater.connect()); assert.equal(c.midi.calls.opens, 0); assert.equal(c.midi.calls.triggers, 0); assert.equal(c.updater.status().durableReady, false);
  }
  assert.equal(supportedOrigin('file://', true), false); assert.equal(supportedOrigin('http://example.com', true), false);
  assert.equal(supportedOrigin('http://localhost:8000', true), true); assert.equal(DEVICE_LOCK, 'fm1-forge-stock-v15-device');
});
test('successful two-phase update commits intent before triggers, serves reordered/duplicate exact immutable bytes and verifies boot build', async () => {
  const midi = {}, c = await context({ midi }); await c.updater.connect();
  const original = c.image.slice(); midi.onTrigger = () => {
    const record = c.storage.records.get('current'); assert.equal(record.state, 'active'); assert.ok(['entering_check', 'entering_write'].includes(record.phase)); c.bytes.fill(0);
  };
  // A File-like snapshot prevents the test's trigger mutation from changing the approved package read.
  const packageBytes = Uint8Array.from(c.bytes), result = await c.updater.update(c.args({ packageBytes }));
  assert.equal(result.identity, WIRE.commonIdentity); assert.equal(result.buildSha256, BUILD); assert.equal(result.readbackVerified, false); assert.equal(result.hardwareQualified, false);
  assert.equal(c.midi.calls.triggers, 2); assert.equal(c.midi.calls.arms, 1); assert.equal(c.updater.status().connected, true);
  const armed = decodeFrame(c.midi.calls.sends.find(packet => packet.length > 6 && decodeFrame(packet).command === 0x32), 0x32).payload;
  assert.equal(armed.length, 70); assert.deepEqual([...armed.subarray(38)], Array(32).fill(0x33)); assert.ok(DEFAULT_TIMING.arm >= 20000);
  const partial = c.midi.calls.responses.find(p => p.length === 25); assert.deepEqual(partial.slice(8), original.slice(0, 17));
  const repeated = c.midi.calls.responses.filter(p => p[1] === 0 && p[2] === 2); assert.equal(repeated.length, 2); assert.deepEqual(repeated[0], repeated[1]);
  const saved = await c.updater.savedAttempt(); assert.equal(saved.state, 'succeeded'); assert.equal(saved.bootIdentityVerified, true);
  await assert.rejects(c.updater.update(c.args({ packageBytes })), { code: 'duplicate_attempt' }); assert.equal(c.midi.calls.triggers, 2);
});
test('package changed, missing review IDs, cancellation and stale capabilities fail before trigger', async () => {
  for (const variant of ['package', 'review', 'cancel', 'state', 'arm']) {
    const midi = {}, c = await context({ midi }); await c.updater.connect(); const abort = new AbortController(); let args = c.args();
    if (variant === 'package') args.packageBytes = new Uint8Array(c.bytes.length);
    if (variant === 'review') args.reviewId = '';
    if (variant === 'cancel') { abort.abort(); args.signal = abort.signal; }
    if (variant === 'state') midi.cap = { state: 2 };
    if (variant === 'arm') midi.armStatus = 3;
    await assert.rejects(c.updater.update(args)); assert.equal(c.midi.calls.triggers, 0);
  }
});
test('out-of-bounds/wrong-phase/malformed requests and disconnect retain unknown original attempt across reload', async () => {
  for (const midi of [{ checkRequests: [requestPacket(0xffffffff, 1)] }, { checkRequests: [requestPacket(WIRE.writeFinish, 8)] },
    { checkRequests: [Uint8Array.of(0xf0, 1, 0xf7)] }, { disconnectOnTrigger: true }, { failTrigger: true }, { checkRequests: [] }]) {
    const c = await context({ midi }); await c.updater.connect(); await assert.rejects(c.updater.update(c.args()), error => error.recoveryRequired === true);
    assert.equal((await c.updater.savedAttempt()).state, 'unknown'); assert.equal(c.midi.calls.triggers, 1);
    const reopened = createCommonUpdater({ navigator: c.midi.navigator, indexedDB: c.storage.indexedDB, crypto: cryptoFixture, locks: c.locks, location: { origin: 'https://fm1.test' }, secureContext: true, timing });
    const saved = await reopened.initialize(); assert.equal(saved.attemptId, ATTEMPT); assert.equal(reopened.status().recoveryRequired, true); assert.equal(c.midi.calls.triggers, 1);
    await assert.rejects(reopened.update(c.args({ attemptId: 'c'.repeat(32) }))); assert.equal(c.midi.calls.triggers, 1);
  }
});
test('cancel after possible trigger is unknown and never sends a second trigger automatically', async () => {
  const abort = new AbortController(), c = await context({ midi: { onTrigger: () => abort.abort() } }); await c.updater.connect();
  await assert.rejects(c.updater.update(c.args({ signal: abort.signal })), error => error.code === 'cancelled' && error.recoveryRequired);
  assert.equal(c.midi.calls.triggers, 1); assert.equal((await c.updater.savedAttempt()).state, 'unknown');
});
test('lost loader, wrong loader, missing reboot or wrong boot build never establish success', async () => {
  for (const midi of [{ missingLoader: true }, { loaderIdentity: 'ota-FM-1_014' }, { missingBoot: true }, { bootBuild: OLD_BUILD }]) {
    const c = await context({ midi }); await c.updater.connect(); await assert.rejects(c.updater.update(c.args()), error => error.recoveryRequired === true);
    const saved = await c.updater.savedAttempt(); assert.equal(saved.state, 'unknown'); assert.equal(saved.bootIdentityVerified, false);
  }
});
test('journal failure after trigger retains authoritative active latch and cannot report success', async () => {
  const midi = {}, c = await context({ midi }); await c.updater.connect(); midi.onTrigger = () => { c.storage.options.failWrite = true; };
  await assert.rejects(c.updater.update(c.args()), error => error.code === 'journal_failed' && error.recoveryRequired === true);
  assert.equal(c.storage.records.get('current').state, 'active'); assert.equal(c.midi.calls.triggers, 1);
  c.storage.options.failWrite = false; assert.equal((await c.updater.savedAttempt()).state, 'active'); assert.equal(c.updater.status().recoveryRequired, true);
});
test('same-origin shared Web Lock prevents another connection while an original update owns it', async () => {
  const locks = fakeLocks(), c = await context({ locks }); await c.updater.connect();
  const second = await context({ locks }); let checked = false;
  c.midi.navigator.userActivation.isActive = true;
  // Hold the same exact lock without opening a physical endpoint.
  await locks.request(DEVICE_LOCK, { ifAvailable: true }, async () => {
    await assert.rejects(second.updater.connect(), { code: 'device_busy' }); checked = true;
  }); assert.equal(checked, true); assert.equal(second.midi.calls.permissions, 0);
});
test('scope replacement, request corruption and prepared crash fail closed on reopen', async () => {
  for (const variant of ['scope', 'digest', 'prepared']) {
    const c = await context(); await c.updater.connect();
    const store = new AttemptStore({ indexedDB: c.storage.indexedDB, crypto: cryptoFixture, origin: 'https://fm1.test', secureContext: true }); await store.initialize();
    await store.prepare({ attemptId: ATTEMPT, reviewId: REVIEW, pair: c.updater.status().pair, manifest: c.manifest });
    if (variant === 'scope') c.storage.records.get('scope').sentinel = cryptoFixture.randomUUID();
    if (variant === 'digest') c.storage.records.get('current').request.manifest.baselineSha256 = '44'.repeat(32);
    if (variant !== 'prepared') await assert.rejects(store.current()); else { assert.equal((await c.updater.savedAttempt()).state, 'prepared'); assert.equal(c.updater.status().recoveryRequired, true); }
  }
});
test('inconsistent prepared/failed post-trigger records cannot clear recovery; unknown terminal records remain latched', async () => {
  for (const [state, phase] of [['failed', 'writing'], ['failed', 'waiting_loader'], ['failed', 'done'], ['prepared', 'entering_check'], ['prepared', 'writing']]) {
    const c = await context(); await c.updater.connect();
    const store = new AttemptStore({ indexedDB: c.storage.indexedDB, crypto: cryptoFixture, origin: 'https://fm1.test', secureContext: true }); await store.initialize();
    await store.prepare({ attemptId: ATTEMPT, reviewId: REVIEW, pair: c.updater.status().pair, manifest: c.manifest });
    Object.assign(c.storage.records.get('current'), { state, phase });
    await assert.rejects(store.current(), { code: 'corrupt_journal' });
    await assert.rejects(c.updater.savedAttempt(), { code: 'corrupt_journal' });
    assert.equal(c.updater.status().recoveryRequired, true); assert.equal(c.updater.status().durableReady, false); assert.equal(c.midi.calls.triggers, 0);
  }
  const c = await context(); await c.updater.connect();
  const store = new AttemptStore({ indexedDB: c.storage.indexedDB, crypto: cryptoFixture, origin: 'https://fm1.test', secureContext: true }); await store.initialize();
  const journal = await store.prepare({ attemptId: ATTEMPT, reviewId: REVIEW, pair: c.updater.status().pair, manifest: c.manifest });
  await journal.transition('entering_check', { state: 'failed', errorCode: 'cancelled' });
  assert.equal((await store.current()).state, 'failed');
  await journal.transition('done', { state: 'unknown', transferFinished: true, bootIdentityVerified: false, errorCode: 'close_failed' });
  const saved = await c.updater.savedAttempt(); assert.equal(saved.state, 'unknown'); assert.equal(saved.phase, 'done'); assert.equal(c.updater.status().recoveryRequired, true);
});
test('transport ignores non-SysEx, bounds frame queues and cleans up hung/late port operations', async () => {
  let midi = fakeMidi(), backend = createWebMidiBackend(midi.access, { portTimeout: 5 }), pair = selectPair(await backend.enumerate(), 'normal');
  let link = await backend.open(pair); const input = midi.access.inputs.get(pair.inputId);
  const event = new Event('midimessage'); event.data = Uint8Array.of(0x90, 60, 127); input.dispatchEvent(event); assert.equal(await link.read(1), null);
  const bad = new Event('midimessage'); bad.data = new Uint8Array(700); bad.data[0] = 0xf0; bad.data[699] = 0xf7; input.dispatchEvent(bad); await assert.rejects(link.read(1), { code: 'invalid_frame' }); await link.close(); await backend.close();
  midi = fakeMidi(); backend = createWebMidiBackend(midi.access, { portTimeout: 5 }); pair = selectPair(await backend.enumerate(), 'normal');
  let release, closed = 0; const port = midi.access.inputs.get(pair.inputId); port.open = () => new Promise(resolve => { release = resolve; }); port.close = async () => { closed++; };
  await assert.rejects(backend.open(pair), { code: 'endpoint_unavailable' }); const before = closed;
  const replacementBackend = createWebMidiBackend(midi.access, { portTimeout: 5 });
  await assert.rejects(replacementBackend.open(pair), { code: 'endpoint_unavailable' });
  release(port); await new Promise(resolve => setTimeout(resolve, 1)); assert.ok(closed > before); await backend.close(); await replacementBackend.close();
  midi = fakeMidi(); backend = createWebMidiBackend(midi.access, { portTimeout: 5 }); pair = selectPair(await backend.enumerate(), 'normal'); link = await backend.open(pair);
  midi.access.inputs.get(pair.inputId).close = () => new Promise(() => {}); await assert.rejects(link.close(), { code: 'close_failed' }); await backend.close();
});
test('generic/ambiguous endpoints never auto-select', async () => {
  const c = await context({ midi: { name: 'USB Composite Device' } }); await assert.rejects(c.updater.connect(), { code: 'endpoint_unavailable' }); assert.equal(c.midi.calls.opens, 0);
  const inventory = { inputs: [{ id: 'i1', name: 'FM-1' }, { id: 'i2', name: 'FM-1 2' }], outputs: [{ id: 'o', name: 'FM-1' }] };
  assert.throws(() => selectPair(inventory, 'normal'), { code: 'ambiguous_device' });
});
