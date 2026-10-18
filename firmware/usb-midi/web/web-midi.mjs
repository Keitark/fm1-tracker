// SPDX-License-Identifier: Apache-2.0
import { WIRE, UpdateError, requireValue, byteArray, identityQuery, capabilityQuery, parseIdentity, parseCapabilities } from './protocol.mjs';
// A late browser open/close must never close a newer transfer using the same
// MIDIPort object. A host lifecycle timeout quarantines that object for this
// page lifetime; cleanup may finish later, but does not silently authorize retry.
const quarantinedPorts = new WeakSet();
export function endpointKind(name) {
  if (typeof name !== 'string' || name.length > 128 || /[\x00-\x1f\x7f]/.test(name)) return null;
  const base = name.replace(/ \d+$/, '').toLowerCase();
  if (['fm-1', 'fm-1 midi', 'fm-1 forge', 'fm1_forge_001'].includes(base)) return 'normal';
  if (base === 'ota-fm-1') return 'loader';
  return null;
}
const canonical = name => name.replace(/ \d+$/, '').toLowerCase();
export function candidatePairs(inventory, kind) {
  requireValue(inventory && Array.isArray(inventory.inputs) && Array.isArray(inventory.outputs) && inventory.inputs.length <= 256 && inventory.outputs.length <= 256,
    'invalid_inventory', 'MIDI inventory exceeds bounds.');
  const pairs = [];
  for (const input of inventory.inputs) {
    const currentKind = endpointKind(input.name);
    if (!currentKind || kind && kind !== currentKind) continue;
    const group = canonical(input.name);
    const inputs = inventory.inputs.filter(port => endpointKind(port.name) && canonical(port.name) === group);
    const outputs = inventory.outputs.filter(port => endpointKind(port.name) && canonical(port.name) === group);
    requireValue(inputs.length === 1 && outputs.length <= 1, 'ambiguous_device', 'Several indistinguishable MIDI endpoints are present. Connect only the intended FM-1.');
    if (outputs.length === 1) {
      requireValue(typeof input.id === 'string' && typeof outputs[0].id === 'string', 'invalid_inventory', 'MIDI endpoint identifiers are invalid.');
      pairs.push(Object.freeze({ inputId: input.id, outputId: outputs[0].id, inputName: input.name, outputName: outputs[0].name, kind: currentKind }));
    }
  }
  return pairs;
}
export function selectPair(inventory, kind, expected) {
  const pairs = candidatePairs(inventory, kind);
  requireValue(pairs.length <= 1, 'ambiguous_device', 'More than one matching FM-1 MIDI pair is connected.');
  if (!pairs.length) return null;
  const selected = pairs[0];
  if (expected) requireValue(['inputId', 'outputId', 'inputName', 'outputName'].every(key => selected[key] === expected[key]), 'device_changed', 'The reviewed MIDI endpoint pair changed. Reconnect and review again.');
  return selected;
}
export function createWebMidiBackend(access, { onDisconnect = () => {}, portTimeout = 4000 } = {}) {
  requireValue(access?.sysexEnabled === true && access.inputs && access.outputs, 'sysex_denied', 'MIDI SysEx permission is required.');
  const links = new Set();
  const inventory = () => ({ inputs: [...access.inputs.values()].filter(p => p.state === 'connected').map(p => ({ id: p.id, name: p.name })),
    outputs: [...access.outputs.values()].filter(p => p.state === 'connected').map(p => ({ id: p.id, name: p.name })) });
  const changed = event => { if (event?.port?.state === 'disconnected') { try { onDisconnect(event.port.id); } catch {} } };
  const boundedPort = (port, method, lateCleanup = false) => new Promise((resolve, reject) => {
    let expired = false;
    const timer = setTimeout(() => { expired = true; quarantinedPorts.add(port); reject(new UpdateError('port_timeout', 'The MIDI port operation exceeded its deadline.')); }, portTimeout);
    Promise.resolve().then(() => port[method]()).then(value => {
      if (expired) { if (lateCleanup) void boundedPort(port, 'close').catch(() => {}); return; }
      clearTimeout(timer); resolve(value);
    }, error => { if (!expired) { clearTimeout(timer); if (method === 'close') quarantinedPorts.add(port); reject(error); } });
  });
  access.addEventListener('statechange', changed);
  return {
    enumerate: async () => inventory(),
    async open(pair) {
      const current = selectPair(inventory(), pair.kind, pair);
      requireValue(current, 'endpoint_unavailable', 'The selected FM-1 MIDI endpoints are unavailable.');
      const input = access.inputs.get(pair.inputId), output = access.outputs.get(pair.outputId);
      requireValue(!quarantinedPorts.has(input) && !quarantinedPorts.has(output), 'endpoint_unavailable', 'A previous MIDI lifecycle failure requires closing this page before reconnecting.');
      const queue = []; let closed = false, failure = null, waiter = null;
      const reject = error => { failure = error; if (waiter) { const pending = waiter; waiter = null; pending.reject(error); } };
      const message = event => {
        if (closed || failure || !event.data?.length || event.data[0] !== 0xf0) return;
        if (event.data.length > Math.ceil((WIRE.maxPayload + 7) * 8 / 7) + 2 || event.data.at(-1) !== 0xf7 || queue.length >= 128) {
          reject(new UpdateError('invalid_frame', 'The MIDI receive queue or frame exceeded bounds.')); return;
        }
        const packet = byteArray(event.data);
        if (waiter) { const pending = waiter; waiter = null; pending.resolve(packet); } else queue.push(packet);
      };
      const lost = () => { if (input.state !== 'connected' || output.state !== 'connected') reject(new UpdateError('disconnected', 'The FM-1 MIDI connection was lost.')); };
      const remove = () => { input.removeEventListener('midimessage', message); access.removeEventListener('statechange', lost); };
      input.addEventListener('midimessage', message); access.addEventListener('statechange', lost);
      try {
        await boundedPort(input, 'open', true); await boundedPort(output, 'open', true);
        requireValue(input.state === 'connected' && output.state === 'connected', 'endpoint_unavailable', 'FM-1 disconnected while opening MIDI.');
      } catch {
        remove(); await Promise.allSettled([boundedPort(input, 'close'), boundedPort(output, 'close')]);
        throw new UpdateError('endpoint_unavailable', 'The selected FM-1 MIDI pair could not be opened.');
      }
      const link = {
        async send(packet) {
          if (failure) throw failure;
          requireValue(!closed, 'disconnected', 'The MIDI connection is closed.');
          try { output.send(byteArray(packet)); }
          catch { throw new UpdateError('send_failed', 'MIDI packet delivery could not be established.'); }
        },
        async read(milliseconds) {
          if (failure) throw failure;
          if (queue.length) return queue.shift();
          if (closed) return null;
          requireValue(waiter === null, 'concurrent_read', 'Only one MIDI receive operation is allowed.');
          return new Promise((resolve, rejectPromise) => {
            const timer = setTimeout(() => { waiter = null; resolve(null); }, Math.max(1, Math.ceil(milliseconds)));
            waiter = { resolve: packet => { clearTimeout(timer); resolve(packet); }, reject: error => { clearTimeout(timer); rejectPromise(error); } };
          });
        },
        async drain() { if (failure) throw failure; queue.length = 0; },
        async connected() { return !closed && input.state === 'connected' && output.state === 'connected'; },
        async close() {
          if (closed) return; closed = true; links.delete(link); remove(); queue.length = 0;
          if (waiter) { const pending = waiter; waiter = null; pending.resolve(null); }
          const result = await Promise.allSettled([boundedPort(input, 'close'), boundedPort(output, 'close')]);
          requireValue(result.every(value => value.status === 'fulfilled'), 'close_failed', 'MIDI endpoints did not close cleanly.');
        },
      };
      links.add(link); return link;
    },
    async close() {
      access.removeEventListener('statechange', changed);
      const results = await Promise.allSettled([...links].map(link => link.close()));
      requireValue(results.every(value => value.status === 'fulfilled'), 'close_failed', 'MIDI cleanup did not finish cleanly.');
    },
  };
}
export async function identifyLink(link, { clock, timing, capabilities = true }) {
  await clock.sleep(timing.open); await link.drain(); await link.send(identityQuery());
  const identityPacket = await link.read(timing.handshake);
  requireValue(identityPacket !== null, 'identity_timeout', 'FM-1 did not answer its identity query.');
  const identity = parseIdentity(identityPacket);
  if (!capabilities) return Object.freeze({ identity, capabilities: null });
  requireValue(identity === WIRE.commonIdentity, 'wrong_model', 'The selected device is not common-update FM-1 firmware.');
  await link.drain(); await link.send(capabilityQuery());
  const packet = await link.read(timing.handshake);
  requireValue(packet !== null, 'capability_timeout', 'FM-1 did not answer its common update capability query.');
  return Object.freeze({ identity, capabilities: parseCapabilities(packet) });
}
