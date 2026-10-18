// SPDX-License-Identifier: Apache-2.0
import test from 'node:test';
import assert from 'node:assert/strict';
import { runTransfer, DEFAULT_TIMING } from '../updater.mjs';
import { WIRE, decodeFrame } from '../protocol.mjs';
import { packageFixture, capabilityPacket, identityPacket, requestPacket, BUILD, OLD_BUILD, ATTEMPT } from './fixtures.mjs';
function scriptedBackend(options = {}) {
  let now = 0, mode = 'normal', phase = 'idle';
  const calls = { triggers: 0, terminalReplies: 0, closes: 0, packets: [] }, clock = { now: () => now, sleep: async ms => { now += ms; } };
  const pairFor = stage => ({ inputId: stage + '-i', outputId: stage + '-o', inputName: stage === 'loader' ? 'ota-FM-1' : 'FM-1 Forge', outputName: stage === 'loader' ? 'ota-FM-1' : 'FM-1 Forge', kind: stage === 'loader' ? 'loader' : 'normal' });
  const backend = {
    enumerate: async () => ({ inputs: [pairFor(mode)].map(p => ({ id: p.inputId, name: p.inputName })), outputs: [pairFor(mode)].map(p => ({ id: p.outputId, name: p.outputName })) }),
    async open() {
      const opened = mode, queue = []; let terminalSeen = false, duplicateQueued = false, closed = false;
      return {
        async drain() { queue.length = 0; },
        async send(packet) {
          calls.packets.push(Uint8Array.from(packet));
          if (packet.length === 6 && packet[1] === 0x22) {
            calls.triggers++; phase = opened === 'loader' ? 'writing' : 'checking';
            queue.push(requestPacket(opened === 'loader' ? WIRE.writeFinish : WIRE.checkFinish, 8)); return;
          }
          const frame = decodeFrame(packet);
          if (frame.command === 0x11) queue.push(identityPacket(opened === 'loader' ? WIRE.loaderIdentity : WIRE.commonIdentity));
          if (frame.command === 0x31) queue.push(capabilityPacket({ build: opened === 'boot' ? BUILD : OLD_BUILD }));
          if (frame.command === 0x32) { const raw = new Uint8Array(1); const { encodeFrame } = await import('../protocol.mjs'); queue.push(encodeFrame(0x32, raw)); }
          if (frame.command === 0x30) {
            calls.terminalReplies++; terminalSeen = true;
            if (!duplicateQueued && options.afterTerminal) { duplicateQueued = true; queue.push(...options.afterTerminal(opened)); }
          }
        },
        async read(ms) {
          if (queue.length) return queue.shift();
          now += ms;
          if (terminalSeen && options.lostAfterTerminal) { const error = new Error('Disconnected'); error.code = 'disconnected'; throw error; }
          return null;
        },
        async connected() { return !closed; },
        async close() { if (closed) return; closed = true; calls.closes++; if (terminalSeen) mode = opened === 'loader' ? 'boot' : 'loader'; },
      };
    },
  };
  return { backend, pair: pairFor('normal'), clock, calls, get phase() { return phase; } };
}
async function run(options) {
  const fixture = await packageFixture(), script = scriptedBackend(options);
  const journal = { record: { attemptId: ATTEMPT }, transitions: [], async transition(phase, values = {}) { this.record = { ...this.record, ...values, phase }; this.transitions.push({ ...this.record }); } };
  const result = runTransfer({ artifact: { manifest: fixture.manifest, image: fixture.image }, backend: script.backend, journal, pair: script.pair,
    expectedInitialBuildSha256: OLD_BUILD, clock: script.clock, timing: { ...DEFAULT_TIMING, open: 0, start: 0, terminal: 10, poll: 2, enumerate: 1 }, onEvent: options?.onEvent });
  return { result, script, journal };
}
test('bounded finish window acknowledges exact duplicate sentinels in each phase using injected clock', async () => {
  const { result, script, journal } = await run({ afterTerminal: opened => [requestPacket(opened === 'loader' ? WIRE.writeFinish : WIRE.checkFinish, 8)] });
  assert.equal((await result).bootIdentityVerified, true); assert.equal(script.calls.triggers, 2); assert.equal(script.calls.terminalReplies, 4);
  assert.equal(journal.transitions.at(-1).state, 'succeeded'); assert.ok(script.clock.now() >= 20);
});
test('wrong-phase, changed-token, ordinary read or excessive duplicate after finish stays unknown', async () => {
  for (const afterTerminal of [() => [requestPacket(WIRE.writeFinish, 8)], () => [requestPacket(WIRE.checkFinish, 8, 9)],
    () => [requestPacket(0, 1)], () => Array.from({ length: 33 }, () => requestPacket(WIRE.checkFinish, 8))]) {
    const { result, script, journal } = await run({ afterTerminal });
    await assert.rejects(result, error => error.recoveryRequired === true); assert.equal(script.calls.triggers, 1); assert.equal(journal.transitions.at(-1).state, 'unknown');
  }
});
test('expected disconnect after acknowledged finish advances to fresh loader and exact boot checks', async () => {
  const { result, script } = await run({ lostAfterTerminal: true }); assert.equal((await result).buildSha256, BUILD); assert.equal(script.calls.triggers, 2);
});
