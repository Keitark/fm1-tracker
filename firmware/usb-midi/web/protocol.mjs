// SPDX-License-Identifier: Apache-2.0
// Independently authored implementation of the documented wire format.
export const WIRE = Object.freeze({ family: 8, version: 1, chunk: 512, flashBytes: 1048576,
  maxPayload: 520, commonIdentity: 'FM1_FORGE_001', loaderIdentity: 'ota-FM-1_015',
  checkFinish: 0xe0000000, writeFinish: 0xf0000000,
  loaderSha256: 'ef1f2db89b011fa53e71df363f0122de0ea896618c53fb53e8da676322004541' });
export class UpdateError extends Error {
  constructor(code, message) { super(message); this.name = 'UpdateError'; this.code = code; }
}
export function requireValue(ok, code, message) { if (!ok) throw new UpdateError(code, message); }
export function byteArray(input) {
  requireValue(input instanceof Uint8Array || Array.isArray(input), 'invalid_bytes', 'Expected bytes.');
  if (Array.isArray(input)) requireValue(input.every(n => Number.isInteger(n) && n >= 0 && n <= 255), 'invalid_bytes', 'Invalid byte value.');
  return Uint8Array.from(input);
}
export const read16 = (a, p = 0) => a[p] + a[p + 1] * 256;
export const read24 = (a, p = 0) => read16(a, p) + a[p + 2] * 65536;
export const read32 = (a, p = 0) => read24(a, p) + a[p + 3] * 16777216;
export function put32(a, p, value) { for (let n = 0; n < 4; n++) a[p + n] = value >>> (8 * n) & 255; }
export const hex = data => Array.from(data, value => value.toString(16).padStart(2, '0')).join('');
export function unhex(value) {
  requireValue(typeof value === 'string' && /^[0-9a-f]{64}$/.test(value), 'invalid_digest', 'A SHA-256 digest must contain 64 lowercase hex characters.');
  return Uint8Array.from(value.match(/../g), pair => Number.parseInt(pair, 16));
}
export function pack7(source) {
  const input = byteArray(source), result = new Uint8Array(Math.ceil(input.length * 8 / 7));
  let buffer = 0, count = 0, position = 0;
  for (const octet of input) {
    buffer += octet * 2 ** count; count += 8;
    while (count >= 7) { result[position++] = buffer & 127; buffer = Math.floor(buffer / 128); count -= 7; }
  }
  if (count) result[position] = buffer;
  return result;
}
export function unpack7(source) {
  const input = byteArray(source);
  requireValue(input.every(value => value < 128), 'invalid_frame', 'SysEx payload is not seven bit.');
  const output = new Uint8Array(Math.floor(input.length * 7 / 8));
  let buffer = 0, count = 0, position = 0;
  for (const septet of input) {
    buffer += septet * 2 ** count; count += 7;
    if (count >= 8) { output[position++] = buffer & 255; buffer = Math.floor(buffer / 256); count -= 8; }
  }
  const canonical = pack7(output);
  requireValue(buffer === 0 && canonical.length === input.length && canonical.every((value, i) => value === input[i]), 'invalid_frame', 'Noncanonical SysEx padding.');
  return output;
}
const checksum = payload => 255 - payload.reduce((sum, value) => (sum + value) & 255, 0);
export function encodeFrame(command, source = []) {
  const payload = byteArray(source);
  requireValue(Number.isInteger(command) && command >= 0 && command <= 255 && payload.length <= WIRE.maxPayload, 'invalid_frame', 'Command or payload exceeds the wire limit.');
  const raw = new Uint8Array(7 + payload.length);
  raw.set([0, 0x59, command, payload.length & 255, payload.length >>> 8 & 255, payload.length >>> 16 & 255]);
  raw.set(payload, 6); raw[raw.length - 1] = checksum(payload);
  return Uint8Array.from([0xf0, ...pack7(raw), 0xf7]);
}
export function decodeFrame(source, expectedCommand) {
  const input = byteArray(source);
  requireValue(input.length >= 10 && input.length <= Math.ceil((WIRE.maxPayload + 7) * 8 / 7) + 2 && input[0] === 0xf0 && input.at(-1) === 0xf7,
    'invalid_frame', 'SysEx frame exceeds bounds or lacks delimiters.');
  const raw = unpack7(input.subarray(1, -1));
  requireValue(raw.length >= 7 && raw[0] === 0 && raw[1] === 0x59 && read24(raw, 3) === raw.length - 7 &&
    (expectedCommand === undefined || raw[2] === expectedCommand), 'invalid_frame', 'Unexpected command or payload length.');
  const payload = raw.slice(6, -1);
  requireValue(raw.at(-1) === checksum(payload), 'invalid_frame', 'SysEx checksum mismatch.');
  return Object.freeze({ command: raw[2], payload });
}
export function parseIdentity(packet) {
  const { payload } = decodeFrame(packet, 0x11);
  requireValue(payload.length === 27, 'invalid_identity', 'Identity must have exactly 27 bytes.');
  const end = payload.indexOf(0);
  requireValue(end > 0 && payload.subarray(0, end).every(n => n >= 32 && n <= 126) && payload.subarray(end).every(n => n === 0), 'invalid_identity', 'Identity is not bounded NUL-padded ASCII.');
  return String.fromCharCode(...payload.subarray(0, end));
}
export function parseCapabilities(packet) {
  const { payload: p } = decodeFrame(packet, 0x31);
  requireValue(p.length === 47 && String.fromCharCode(...p.subarray(0, 4)) === 'FMC1' && p[4] === WIRE.version &&
    p[5] === WIRE.family && p[7] <= 1 && p[6] <= 4 && read16(p, 8) === WIRE.chunk && read32(p, 10) === WIRE.flashBytes,
    'unsupported_capabilities', 'The firmware does not expose the qualified common update contract.');
  const blocked = p[6] === 0 && p[7] === 0 && p[46] === 3;
  requireValue((p[46] === 0 || blocked) && p[6] !== 4, 'device_error', 'The firmware reports an update fault.');
  requireValue(p.subarray(14, 46).some(n => n !== 0), 'invalid_capabilities', 'Firmware build identity is missing.');
  return Object.freeze({ version: p[4], family: p[5], state: p[6], loader: p[7], chunkBytes: read16(p, 8),
    flashBytes: read32(p, 10), buildSha256: hex(p.subarray(14, 46)), error: p[46], installAvailable: p[7] === 1 && p[46] === 0 });
}
export function armCommand(imageBytes, targetBuildSha256, baselineSha256) {
  requireValue(Number.isInteger(imageBytes) && imageBytes > 0 && imageBytes <= WIRE.flashBytes + 65536, 'invalid_package', 'Logical update image size exceeds bounds.');
  const payload = new Uint8Array(70); payload.set([WIRE.version, WIRE.family]); put32(payload, 2, imageBytes); payload.set(unhex(targetBuildSha256), 6); payload.set(unhex(baselineSha256), 38);
  return encodeFrame(0x32, payload);
}
export function parseArmAck(packet) {
  const { payload } = decodeFrame(packet, 0x32);
  requireValue(payload.length === 1 && payload[0] === 0, 'arm_rejected', 'The firmware did not arm the reviewed update.');
}
export const identityQuery = () => encodeFrame(0x11);
export const capabilityQuery = () => encodeFrame(0x31);
export const triggerCommand = () => Uint8Array.of(0xf0, 0x22, 0x24, 0x35, 0x7f, 0xf7);
export function parseRequest(packet, imageBytes, finishAddress) {
  const { payload: p } = decodeFrame(packet, 0x30);
  requireValue(p.length === 8, 'invalid_request', 'An update request must contain eight bytes.');
  const address = read32(p, 1), length = read24(p, 5), terminal = address === WIRE.checkFinish || address === WIRE.writeFinish;
  if (terminal) requireValue(address === finishAddress && length === 8, 'wrong_terminal', 'Unexpected update terminal phase.');
  else requireValue(length > 0 && length <= WIRE.chunk && address < imageBytes && length <= imageBytes - address, 'request_out_of_range', 'The firmware requested bytes outside the reviewed image.');
  return Object.freeze({ token: p[0], address, length, terminal, header: p.slice() });
}
export function imageResponse(request, image) {
  const bytes = request.terminal ? Uint8Array.of(115, 117, 99, 99, 101, 115, 115, 0) : image.slice(request.address, request.address + request.length);
  requireValue(bytes.length === request.length, 'invalid_response', 'Response length differs from the exact request.');
  return encodeFrame(0x30, Uint8Array.from([...request.header, ...bytes]));
}
