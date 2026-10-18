// SPDX-License-Identifier: Apache-2.0
import { WIRE, UpdateError, requireValue, byteArray, armCommand, parseArmAck, triggerCommand, parseRequest, imageResponse } from './protocol.mjs';
import { inspectPackage, validateManifest } from './package.mjs';
import { DEVICE_LOCK, AttemptStore } from './journal.mjs';
import { createWebMidiBackend, selectPair, identifyLink } from './web-midi.mjs';
export const DEFAULT_TIMING = Object.freeze({ open: 300, handshake: 1500, arm: 20000, start: 2000, reply: 10,
  terminal: 3000, poll: 1000, idleCheck: 8000, idleWrite: 180000, loader: 30000, reboot: 40000, enumerate: 1000, total: 900000 });
export const realClock = Object.freeze({ now: () => performance.now(), sleep: milliseconds => new Promise(resolve => setTimeout(resolve, milliseconds)) });
const clone = value => JSON.parse(JSON.stringify(value));
function cancelled(signal) { if (signal?.aborted) throw new UpdateError('cancelled', 'The original update attempt was stopped.'); }
export async function runTransfer({ artifact, backend, journal, pair, expectedInitialBuildSha256, signal, clock = realClock, timing = DEFAULT_TIMING, onEvent = () => {} }) {
  const manifest = validateManifest(artifact.manifest), image = byteArray(artifact.image);
  requireValue(image.length === manifest.imageBytes, 'invalid_package', 'Reviewed image size changed.');
  let link = null, phase = 'prepared', possibleTrigger = false, transferFinished = false, requests = 0, lastEvent = -Infinity;
  const started = clock.now();
  const emit = (details = {}, force = false) => {
    if (force || clock.now() - lastEvent >= 100) { lastEvent = clock.now(); try { onEvent(Object.freeze({ phase, ...details })); } catch {} }
  };
  const transition = async (next, values = {}) => { await journal.transition(next, values); phase = next; emit({}, true); };
  const check = () => { cancelled(signal); requireValue(clock.now() - started <= timing.total, 'transfer_timeout', 'The bounded transfer deadline elapsed.'); };
  const close = async () => { if (link) { const closing = link; link = null; await closing.close(); } };
  const fresh = async (currentPair, common) => {
    link = await backend.open(currentPair); check();
    return identifyLink(link, { clock, timing, capabilities: common });
  };
  const waitFor = async (kind, milliseconds) => {
    const end = clock.now() + milliseconds;
    while (clock.now() < end) {
      check(); const candidate = selectPair(await backend.enumerate(), kind);
      if (candidate) {
        try {
          const found = await fresh(candidate, kind === 'normal');
          if (kind === 'loader') requireValue(found.identity === WIRE.loaderIdentity, 'wrong_loader', 'Only the exact reviewed OTA loader identity is accepted.');
          else requireValue(found.identity === manifest.targetIdentity && found.capabilities.buildSha256 === manifest.targetBuildSha256 &&
            found.capabilities.state === 0, 'boot_identity_mismatch', 'Reboot identity or firmware build digest differs from the reviewed target.');
          return { ...found, pair: candidate };
        } catch (error) {
          await close();
          if (!['endpoint_unavailable', 'identity_timeout', 'capability_timeout'].includes(error.code)) throw error;
        }
      }
      await clock.sleep(Math.min(timing.enumerate, Math.max(0, end - clock.now())));
    }
    throw new UpdateError(kind === 'loader' ? 'loader_timeout' : 'reboot_timeout', kind === 'loader' ? 'The exact FM-1 OTA loader did not appear.' : 'FM-1 did not return with the reviewed firmware identity.');
  };
  const serve = async (terminal, idle) => {
    let lastRequest = clock.now(), uniqueBytes = 0;
    const seen = new Uint8Array(image.length);
    while (true) {
      check();
      const packet = await link.read(timing.poll);
      check();
      if (packet === null) {
        requireValue(await link.connected(), 'disconnected', 'FM-1 disconnected during the original update.');
        requireValue(clock.now() - lastRequest < idle, 'request_timeout', 'FM-1 stopped requesting the reviewed image.');
        continue;
      }
      requireValue(++requests <= 100000, 'request_limit', 'The update request bound was exceeded.');
      const request = parseRequest(packet, image.length, terminal); lastRequest = clock.now();
      if (!request.terminal) await clock.sleep(timing.reply);
      check(); await link.send(imageResponse(request, image));
      if (request.terminal) {
        emit({ progress: terminal === WIRE.writeFinish ? 1 : 0, requests }, true);
        const deadline = clock.now() + timing.terminal; let duplicates = 0;
        while (clock.now() < deadline) {
          check(); let duplicatePacket;
          try { duplicatePacket = await link.read(Math.max(1, Math.min(timing.poll, deadline - clock.now()))); }
          catch (error) { if (error.code === 'disconnected') return; throw error; }
          if (duplicatePacket === null) { if (!await link.connected()) return; continue; }
          const repeated = parseRequest(duplicatePacket, image.length, terminal);
          requireValue(++duplicates <= 32 && repeated.terminal && repeated.header.every((value, p) => value === request.header[p]),
            'unexpected_terminal_request', 'Unexpected or excessive data after the phase finish acknowledgement.');
          check(); await link.send(imageResponse(repeated, image));
        }
        return;
      }
      for (let p = request.address; p < request.address + request.length; p++) if (!seen[p]) { seen[p] = 1; uniqueBytes++; }
      emit({ progress: terminal === WIRE.writeFinish ? Math.min(0.99, uniqueBytes / image.length) : 0, uniqueBytes, requests });
    }
  };
  try {
    check();
    const currentPair = selectPair(await backend.enumerate(), 'normal', pair);
    requireValue(currentPair, 'endpoint_unavailable', 'The reviewed FM-1 MIDI pair is unavailable.');
    const initial = await fresh(currentPair, true); check();
    requireValue(typeof expectedInitialBuildSha256 === 'string' && initial.capabilities.buildSha256 === expectedInitialBuildSha256,
      'review_stale', 'The current firmware build changed after connection/review.');
    requireValue(initial.capabilities.installAvailable && initial.capabilities.state === 0, 'install_unavailable', 'The current firmware has not enabled this reviewed install backend.');
    await transition('arming');
    await link.send(armCommand(image.length, manifest.targetBuildSha256, manifest.baselineSha256));
    const ack = await link.read(timing.arm); check();
    requireValue(ack !== null, 'arm_timeout', 'The firmware did not acknowledge the reviewed update.'); parseArmAck(ack);
    await transition('entering_check', { state: 'active', initialIdentity: initial.identity, initialBuildSha256: initial.capabilities.buildSha256 });
    check(); possibleTrigger = true; await link.send(triggerCommand());
    await transition('checking'); await clock.sleep(timing.start); await serve(WIRE.checkFinish, timing.idleCheck); await close();
    await transition('waiting_loader'); await waitFor('loader', timing.loader); check();
    await transition('entering_write', { state: 'active', loaderIdentity: WIRE.loaderIdentity });
    check(); await link.send(triggerCommand());
    await transition('writing'); await clock.sleep(timing.start); await serve(WIRE.writeFinish, timing.idleWrite); transferFinished = true;
    await transition('waiting_reboot', { transferFinished: true }); await close();
    const boot = await waitFor('normal', timing.reboot); await close();
    await transition('done', { state: 'succeeded', transferFinished: true, bootIdentityVerified: true, finalIdentity: boot.identity, finalBuildSha256: boot.capabilities.buildSha256 });
    return Object.freeze({ attemptId: journal.record.attemptId, packageSha256: manifest.packageSha256, identity: boot.identity,
      buildSha256: boot.capabilities.buildSha256, pair: clone(boot.pair), transferFinished: true, bootIdentityVerified: true,
      capabilities: { ...boot.capabilities }, readbackVerified: false, hardwareQualified: false });
  } catch (caught) {
    const error = caught instanceof UpdateError ? caught : new UpdateError('update_failed', 'The original firmware update could not be completed.');
    error.state = possibleTrigger ? 'unknown' : 'failed'; error.recoveryRequired = possibleTrigger; error.transferFinished = transferFinished;
    try { await journal.transition(phase === 'prepared' ? 'failed' : phase, { state: error.state, errorCode: error.code, transferFinished, bootIdentityVerified: false }); }
    catch { error.code = 'journal_failed'; error.state = 'unknown'; error.recoveryRequired = true; }
    throw error;
  } finally {
    if (link) {
      try { await close(); }
      catch {
        const error = new UpdateError('close_failed', 'The original update connection did not close cleanly.');
        error.state = possibleTrigger ? 'unknown' : 'failed'; error.recoveryRequired = possibleTrigger;
        try { await journal.transition(phase, { state: error.state, transferFinished, bootIdentityVerified: false, errorCode: error.code }); }
        catch { error.state = 'unknown'; error.recoveryRequired = true; }
        throw error;
      }
    }
  }
}
export function createCommonUpdater({ navigator = globalThis.navigator, indexedDB = globalThis.indexedDB, crypto = globalThis.crypto,
  locks = navigator?.locks, location = globalThis.location, secureContext = globalThis.isSecureContext, clock = realClock,
  timing = DEFAULT_TIMING, onState = () => {}, onEvent = () => {}, beforeWrite = async () => {} } = {}) {
  const store = new AttemptStore({ indexedDB, crypto, origin: location?.origin, secureContext });
  let backend = null, selected = null, evidence = null, busy = false, updating = false, ready = false, unresolved = false, lastError = null, disconnectEpoch = 0;
  const status = () => Object.freeze({ connected: Boolean(selected && evidence), pair: selected && clone(selected), identity: evidence?.identity ?? null,
    capabilities: evidence?.capabilities && { ...evidence.capabilities }, busy, updating, durableReady: ready,
    recoveryRequired: unresolved, installAvailable: Boolean(evidence?.capabilities.installAvailable && evidence.capabilities.state === 0 && !unresolved), lastError });
  const notify = () => { try { onState(status()); } catch {} };
  const withLock = async operation => {
    requireValue(typeof locks?.request === 'function', 'locks_unavailable', 'Web Locks are required for exclusive device access.');
    requireValue(!busy, 'device_busy', 'Another operation is active in this page.'); busy = true; notify();
    try { return await locks.request(DEVICE_LOCK, { mode: 'exclusive', ifAvailable: true }, async lock => {
      requireValue(lock, 'device_busy', 'Another FM-1 panel owns the device.'); return operation();
    }); }
    catch (error) {
      lastError = error instanceof UpdateError ? error.code : 'browser_failed';
      if (['storage_unavailable', 'storage_failed', 'storage_scope_changed', 'corrupt_journal', 'unsupported_origin'].includes(lastError)) { ready = false; unresolved = true; }
      throw error;
    }
    finally { busy = false; notify(); }
  };
  const load = async () => { await store.initialize(); const record = await store.current(); ready = true;
    unresolved = Boolean(record && ['prepared', 'active', 'unknown'].includes(record.state)); return record; };
  const initialize = () => withLock(load);
  // This reader is intentionally lock-free: an owning writer uses it while
  // holding DEVICE_LOCK to check the opposite engine without a recursive lock.
  // store.current() still checks the actual scope, sentinel and request digest.
  const peekSavedAttempt = async () => {
    requireValue(ready, 'storage_unavailable', 'Initialize durable common-update storage before reading its saved attempt.');
    return store.current();
  };
  const connect = () => withLock(async () => {
    requireValue(navigator?.userActivation?.isActive === true, 'user_gesture_required', 'Use Connect MIDI to grant device access.');
    requireValue(typeof navigator?.requestMIDIAccess === 'function', 'midi_unavailable', 'Web MIDI is unavailable in this browser host.');
    // Permission is requested in the actual Connect gesture, before storage awaits.
    const permission = navigator.requestMIDIAccess({ sysex: true }).catch(() => { throw new UpdateError('midi_denied', 'MIDI SysEx permission was denied or unavailable.'); });
    const [access] = await Promise.all([permission, load()]);
    if (backend) await backend.close(); selected = null; evidence = null;
    backend = createWebMidiBackend(access, { onDisconnect: portId => {
      disconnectEpoch++;
      if (selected && (selected.inputId === portId || selected.outputId === portId)) { selected = null; evidence = null; notify(); }
    } });
    const pair = selectPair(await backend.enumerate(), 'normal');
    requireValue(pair, 'endpoint_unavailable', 'No unique FM-1 common-update MIDI pair is available.');
    const openingEpoch = disconnectEpoch, link = await backend.open(pair);
    try {
      const fresh = await identifyLink(link, { clock, timing }); await link.close();
      const present = selectPair(await backend.enumerate(), 'normal', pair);
      requireValue(present && openingEpoch === disconnectEpoch, 'disconnected', 'FM-1 disconnected before its connection evidence was committed.');
      selected = pair; evidence = fresh; lastError = null;
    }
    catch (error) { selected = null; evidence = null; await link.close(); throw error; }
    notify(); return status();
  });
  const probe = () => withLock(async () => {
    requireValue(backend && selected && evidence, 'connect_required', 'Connect common firmware MIDI before checking its identity.');
    const expectedPair = clone(selected), probingEpoch = disconnectEpoch; let link = null;
    try {
      requireValue(ready, 'storage_unavailable', 'Durable common-update storage is not initialized.');
      const record = await store.current();
      unresolved = Boolean(record && ['prepared', 'active', 'unknown'].includes(record.state));
      const pair = selectPair(await backend.enumerate(), 'normal', expectedPair);
      requireValue(pair, 'endpoint_unavailable', 'The connected FM-1 MIDI pair is no longer available.');
      link = await backend.open(pair);
      const fresh = await identifyLink(link, { clock, timing });
      await link.close(); link = null;
      const present = selectPair(await backend.enumerate(), 'normal', pair);
      requireValue(present && probingEpoch === disconnectEpoch, 'disconnected', 'FM-1 disconnected before its refreshed evidence was committed.');
      selected = pair; evidence = fresh; lastError = null; notify(); return status();
    } catch (error) {
      selected = null; evidence = null;
      if (link) { try { await link.close(); } catch (closingError) { throw closingError; } }
      notify(); throw error;
    }
  });
  const update = ({ attemptId, reviewId, packageBytes, manifest, signal } = {}) => withLock(async () => {
    requireValue(navigator?.userActivation?.isActive === true, 'user_gesture_required', 'Confirm the reviewed firmware update yourself.');
    requireValue(backend && selected && evidence, 'connect_required', 'Connect and identify common-update firmware first.');
    requireValue(evidence.capabilities.installAvailable && evidence.capabilities.state === 0, 'install_unavailable', 'This firmware has not enabled its qualified install backend.');
    const reviewedPair = clone(selected), expectedInitialBuildSha256 = evidence.capabilities.buildSha256; cancelled(signal);
    const artifact = await inspectPackage(packageBytes, manifest, crypto); await load();
    requireValue(!unresolved, 'recovery_required', 'An unresolved original attempt blocks another update.');
    // The callback runs under DEVICE_LOCK and must inspect fresh opposite-
    // engine storage. Cached panel snapshots are insufficient after a reload.
    await beforeWrite(Object.freeze({ operation: 'update' }));
    cancelled(signal);
    const journal = await store.prepare({ attemptId, reviewId, pair: reviewedPair, manifest: artifact.manifest });
    updating = true; notify();
    try {
      const result = await runTransfer({ artifact, backend, journal, pair: reviewedPair, expectedInitialBuildSha256, signal, clock, timing, onEvent });
      selected = result.pair; evidence = { identity: result.identity, capabilities: { ...result.capabilities } };
      unresolved = false; lastError = null; return result;
    } catch (error) { selected = null; evidence = null; unresolved = Boolean(error.recoveryRequired); throw error; }
    finally { updating = false; notify(); }
  });
  const disconnect = () => withLock(async () => { if (backend) await backend.close(); backend = null; selected = null; evidence = null; notify(); });
  return Object.freeze({ initialize, connect, probe, update, disconnect, status, peekSavedAttempt,
    inspectPackage: (bytes, manifest) => inspectPackage(bytes, manifest, crypto).then(artifact => ({ manifest: artifact.manifest, details: artifact.details })),
    savedAttempt: () => withLock(load) });
}
