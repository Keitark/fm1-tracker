// SPDX-License-Identifier: Apache-2.0
// Synthetic host-only fixtures. No device firmware, SDK bytes or keys are used.
import { webcrypto } from 'node:crypto';
import { WIRE, encodeFrame, decodeFrame, put32, unhex } from '../protocol.mjs';
import { crc16, decodeContainerBlock, digest } from '../package.mjs';
export const BUILD = '22'.repeat(32), OLD_BUILD = '11'.repeat(32), ATTEMPT = 'a'.repeat(32), REVIEW = 'b'.repeat(32);
const fakeLoader = Uint8Array.of(70, 77, 49, 79, 84, 65, 1, 0);
export const cryptoFixture = { randomUUID: () => webcrypto.randomUUID(), subtle: { async digest(algorithm, bytes) {
  const data = new Uint8Array(bytes.buffer ?? bytes, bytes.byteOffset ?? 0, bytes.byteLength ?? bytes.length);
  if (data.length === fakeLoader.length && data.every((n, p) => n === fakeLoader[p])) return unhex(WIRE.loaderSha256).buffer;
  return webcrypto.subtle.digest(algorithm, bytes);
} } };
const put16 = (a, p, value) => { a[p] = value & 255; a[p + 1] = value >>> 8 & 255; };
export function wrapImage(image) {
  const out = new Uint8Array(image.length + 20);
  for (let n = 0; n < 20; n++) { out.set(image.subarray(n * 47, n * 47 + 47), n * 48); out[n * 48 + 47] = n < WIRE.commonIdentity.length ? WIRE.commonIdentity.charCodeAt(n) + n + 1 : 125; }
  out.set(image.subarray(940), 960); return out;
}
export async function packageFixture() {
  const image = new Uint8Array(1024 + 16384 + 32).fill(255), flash = new Uint8Array(16384).fill(90);
  const table = new Uint8Array(160), header = new Uint8Array(64);
  const entries = [{ type: 0, name: 'flash.bin', offset: 1024, data: flash }, { type: 100, name: 'ota.bin', offset: 17408, data: fakeLoader }];
  entries.forEach((entry, n) => {
    const raw = new Uint8Array(80); put16(raw, 0, entry.type); put16(raw, 2, n); put16(raw, 4, crc16(entry.data));
    put32(raw, 8, entry.offset); put32(raw, 12, entry.data.length); put32(raw, 16, Math.ceil(entry.data.length / 32) * 32);
    raw.set(new TextEncoder().encode(entry.name), 64); table.set(decodeContainerBlock(raw), n * 80); image.set(entry.data, entry.offset);
  });
  put16(header, 2, crc16(table)); put32(header, 4, image.length); put16(header, 8, 2); put16(header, 10, 4); put32(header, 12, 512); header.set(new TextEncoder().encode('AC791N'), 16); put16(header, 0, crc16(header.subarray(2)));
  image.set(decodeContainerBlock(header)); image.set(table, 64);
  const bytes = wrapImage(image);
  const manifest = { schemaVersion: 1, model: 'FM-1', family: 8, layout: 'stock-single-v1', packageBytes: bytes.length, packageSha256: await digest(bytes),
    imageBytes: image.length, imageSha256: await digest(image), targetBuildSha256: BUILD, targetIdentity: WIRE.commonIdentity,
    loaderSha256: WIRE.loaderSha256, baselineSha256: '33'.repeat(32), preservation: 'unit-baseline-app-only', hardwareQualified: false };
  return { bytes, image, manifest };
}
export function identityPacket(identity = WIRE.commonIdentity) {
  const payload = new Uint8Array(27); payload.set(new TextEncoder().encode(identity)); return encodeFrame(0x11, payload);
}
export function capabilityPacket({ state = 0, loader = 1, build = OLD_BUILD, error = 0, version = 1, family = 8, chunk = 512 } = {}) {
  const payload = new Uint8Array(47); payload.set([70, 77, 67, 49, version, family, state, loader]); put16(payload, 8, chunk); put32(payload, 10, WIRE.flashBytes); payload.set(unhex(build), 14); payload[46] = error; return encodeFrame(0x31, payload);
}
export function requestPacket(address, length, token = 7) {
  const payload = new Uint8Array(8); payload[0] = token; put32(payload, 1, address); payload[5] = length & 255; payload[6] = length >>> 8 & 255; payload[7] = length >>> 16 & 255; return encodeFrame(0x30, payload);
}
const clone = value => value === undefined ? undefined : JSON.parse(JSON.stringify(value));
export function fakeIndexedDB(options = {}) {
  const records = options.records ?? new Map(), commits = []; let writes = 0;
  const database = { objectStoreNames: { contains: name => name === 'records' }, createObjectStore() {}, close() {},
    transaction(_store, mode) {
      const snapshot = new Map([...records].map(([key, value]) => [key, clone(value)])); let timer, pending = 0, aborted = false, completed = false;
      const tx = { durability: mode === 'readwrite' ? options.durability ?? 'strict' : 'default', abort() { if (aborted || completed) return; aborted = true; clearTimeout(timer); queueMicrotask(() => tx.onabort?.()); } };
      const finish = () => {
        clearTimeout(timer); timer = setTimeout(() => {
          if (pending || aborted || completed) return;
          if (mode === 'readwrite' && (options.failWrite || ++writes === options.failWriteAt)) { tx.abort(); return; }
          completed = true;
          if (mode === 'readwrite') { records.clear(); for (const [key, value] of snapshot) records.set(key, clone(value)); commits.push(clone(snapshot.get('current'))); }
          tx.oncomplete?.();
        }, options.commitDelay ?? 0);
      };
      const request = action => { const req = {}; pending++; clearTimeout(timer); queueMicrotask(() => {
        if (aborted) return;
        try { req.result = action(); req.onsuccess?.(); } catch { req.onerror?.(); tx.abort(); }
        pending--; finish();
      }); return req; };
      tx.objectStore = () => ({ get: key => request(() => clone(snapshot.get(key))), put: value => request(() => { snapshot.set(value.key, clone(value)); return value.key; }),
        add: value => request(() => { if (snapshot.has(value.key)) throw new Error('duplicate'); snapshot.set(value.key, clone(value)); return value.key; }) }); finish(); return tx;
    } };
  return { records, commits, database, options, indexedDB: { open() { const result = {}; queueMicrotask(() => {
    if (options.failOpen) { result.onerror?.(); return; } result.result = database; result.onupgradeneeded?.(); result.onsuccess?.();
  }); return result; } } };
}
export function fakeLocks() {
  let owned = false;
  return { async request(name, options, callback) {
    if (name !== 'fm1-forge-stock-v15-device' || options.ifAvailable !== true) throw new Error('Unexpected lock contract');
    if (owned) return callback(null); owned = true;
    try { return await callback({ name }); } finally { owned = false; }
  }, get owned() { return owned; } };
}
export function fakeMidi(options = {}) {
  const calls = { permissions: 0, opens: 0, closes: 0, arms: 0, triggers: 0, sends: [], responses: [] };
  const access = new EventTarget(); Object.assign(access, { sysexEnabled: true, inputs: new Map(), outputs: new Map() });
  let mode = 'normal', sequence = [], position = 0, input;
  const emit = (target, type, extra) => { const event = new Event(type); Object.assign(event, extra); target.dispatchEvent(event); };
  const disconnect = () => { for (const p of [...access.inputs.values(), ...access.outputs.values()]) { p.state = 'disconnected'; emit(access, 'statechange', { port: p }); } };
  const replace = next => {
    disconnect(); access.inputs.clear(); access.outputs.clear(); mode = next;
    if (next === 'loader' && options.missingLoader || next === 'boot' && options.missingBoot) return;
    const name = next === 'loader' ? 'ota-FM-1' : options.name ?? 'FM-1 Forge';
    input = new EventTarget(); Object.assign(input, { id: next + '-input', name, state: 'connected', open: async () => { calls.opens++; options.onOpen?.(); return input; }, close: async () => { calls.closes++; return input; } });
    const nextRequest = () => { if (position < sequence.length) { const packet = sequence[position++]; queueMicrotask(() => emit(input, 'midimessage', { data: packet })); } };
    const output = { id: next + '-output', name, state: 'connected', open: async () => { calls.opens++; return output; }, close: async () => { calls.closes++; return output; },
      send(packet) {
        const data = Uint8Array.from(packet); calls.sends.push(data);
        if (data.length === 6 && data[1] === 0x22) {
          calls.triggers++; options.onTrigger?.(); if (options.failTrigger) throw new Error('ambiguous send');
          if (options.disconnectOnTrigger) { disconnect(); return; }
          sequence = mode === 'loader' ? options.writeRequests ?? [requestPacket(512, 512), requestPacket(0, 17), requestPacket(512, 512), requestPacket(WIRE.writeFinish, 8)] : options.checkRequests ?? [requestPacket(0, 17), requestPacket(WIRE.checkFinish, 8)];
          position = 0; nextRequest(); return;
        }
        const frame = decodeFrame(data);
        if (frame.command === 0x11) { if (!options.noIdentity) queueMicrotask(() => emit(input, 'midimessage', { data: identityPacket(mode === 'loader' ? options.loaderIdentity ?? WIRE.loaderIdentity : options.identity ?? WIRE.commonIdentity) })); }
        else if (frame.command === 0x31) queueMicrotask(() => emit(input, 'midimessage', { data: options.malformedCap ?? capabilityPacket(mode === 'boot' ? { build: options.bootBuild ?? BUILD, ...options.bootCap } : options.cap) }));
        else if (frame.command === 0x32) { calls.arms++; if (!options.noArmAck) queueMicrotask(() => emit(input, 'midimessage', { data: encodeFrame(0x32, [options.armStatus ?? 0]) })); }
        else if (frame.command === 0x30) {
          calls.responses.push(frame.payload.slice()); const address = frame.payload[1] + frame.payload[2] * 256 + frame.payload[3] * 65536 + frame.payload[4] * 16777216;
          if (address === WIRE.checkFinish) replace('loader'); else if (address === WIRE.writeFinish) replace('boot'); else nextRequest();
        }
      } };
    access.inputs.set(input.id, input); access.outputs.set(output.id, output);
  };
  replace('normal');
  const navigator = { userActivation: { isActive: true }, requestMIDIAccess: async grant => { calls.permissions++; if (!grant.sysex || options.permissionDenied) throw new Error('Permission'); return access; } };
  return { navigator, access, calls, disconnect, replace };
}
