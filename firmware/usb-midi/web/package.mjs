// SPDX-License-Identifier: Apache-2.0
import { WIRE, byteArray, requireValue, read16, read32, hex, unhex } from './protocol.mjs';
export const MAX_PACKAGE_BYTES = WIRE.flashBytes + 65536;
const fields = ['schemaVersion', 'model', 'family', 'layout', 'packageSha256', 'packageBytes', 'imageSha256', 'imageBytes',
  'targetBuildSha256', 'targetIdentity', 'loaderSha256', 'baselineSha256', 'preservation', 'hardwareQualified'];
export function validateManifest(value) {
  requireValue(value && typeof value === 'object' && !Array.isArray(value) && Object.keys(value).length === fields.length && fields.every(key => Object.hasOwn(value, key)), 'invalid_manifest', 'Select a complete common update manifest with the supported fields.');
  requireValue(value.schemaVersion === 1 && value.model === 'FM-1' && value.family === 8 && value.layout === 'stock-single-v1' &&
    value.targetIdentity === WIRE.commonIdentity && value.loaderSha256 === WIRE.loaderSha256 && value.preservation === 'unit-baseline-app-only' && value.hardwareQualified === false,
    'invalid_manifest', 'Unsupported target, loader, preservation policy or qualification claim.');
  for (const key of ['packageSha256', 'imageSha256', 'targetBuildSha256', 'loaderSha256', 'baselineSha256']) unhex(value[key]);
  requireValue(value.targetBuildSha256 !== '0'.repeat(64), 'invalid_manifest', 'The target firmware must have an explicit nonzero build identity.');
  requireValue(Number.isInteger(value.packageBytes) && value.packageBytes >= 1044 && value.packageBytes <= MAX_PACKAGE_BYTES &&
    Number.isInteger(value.imageBytes) && value.imageBytes === value.packageBytes - 20, 'invalid_manifest', 'Manifest package sizes are invalid.');
  return Object.freeze(Object.fromEntries(fields.map(key => [key, value[key]])));
}
export async function digest(bytes, crypto = globalThis.crypto) {
  requireValue(typeof crypto?.subtle?.digest === 'function', 'crypto_unavailable', 'WebCrypto SHA-256 is required.');
  return hex(new Uint8Array(await crypto.subtle.digest('SHA-256', bytes)));
}
export function crc16(data) {
  let crc = 0;
  for (const octet of data) {
    crc ^= octet << 8;
    for (let bit = 0; bit < 8; bit++) crc = (crc << 1 ^ (crc & 0x8000 ? 0x1021 : 0)) & 65535;
  }
  return crc;
}
// Container whitening is a documented XOR stream, not encryption/authentication.
export function decodeContainerBlock(source) {
  const result = byteArray(source); let state = 65535;
  for (let p = 0; p < result.length; p++) { result[p] ^= state & 255; state = (state << 1 ^ (state & 0x8000 ? 0x1021 : 0)) & 65535; }
  return result;
}
const rounded = value => Math.ceil(value / 32) * 32;
function text(data, length, expected) {
  const end = data.indexOf(0);
  requireValue(data.length === length && end > 0 && data.subarray(end).every(n => n === 0) &&
    String.fromCharCode(...data.subarray(0, end)) === expected, 'invalid_package', 'Unexpected container target or entry name.');
}
export function unwrapPackage(source, targetIdentity = WIRE.commonIdentity) {
  const raw = byteArray(source);
  requireValue(raw.length >= 1044 && raw.length <= MAX_PACKAGE_BYTES && targetIdentity.length <= 20, 'invalid_package', 'Package size exceeds bounds.');
  const image = new Uint8Array(raw.length - 20);
  for (let block = 0; block < 20; block++) {
    const expected = block < targetIdentity.length ? targetIdentity.charCodeAt(block) + block + 1 & 255 : 125;
    requireValue(raw[block * 48 + 47] === expected, 'invalid_package', 'Outer package product marker mismatch.');
    image.set(raw.subarray(block * 48, block * 48 + 47), block * 47);
  }
  image.set(raw.subarray(960), 940); return image;
}
export async function validateImage(image, crypto = globalThis.crypto) {
  requireValue(image instanceof Uint8Array && image.length >= 1024 && image.length <= MAX_PACKAGE_BYTES - 20, 'invalid_package', 'Logical image size exceeds bounds.');
  const header = decodeContainerBlock(image.subarray(0, 64));
  requireValue(read16(header) === crc16(header.subarray(2)) && read32(header, 4) === image.length && read16(header, 8) === 2 &&
    read16(header, 10) === 4 && read32(header, 12) === 512, 'invalid_package', 'UFW header or header CRC is invalid.');
  text(header.subarray(16, 32), 16, 'AC791N');
  requireValue(header.subarray(32).every(n => n === 0) && image.subarray(224, 1024).every(n => n === 255), 'invalid_package', 'Unexpected UFW header flags or directory padding.');
  requireValue(read16(header, 2) === crc16(image.subarray(64, 224)), 'invalid_package', 'UFW directory CRC is invalid.');
  const records = [0, 1].map(index => {
    const record = decodeContainerBlock(image.subarray(64 + index * 80, 144 + index * 80));
    const type = read16(record), offset = read32(record, 8), size = read32(record, 12), stored = read32(record, 16);
    requireValue(type === (index === 0 ? 0 : 100) && read16(record, 2) === index && read32(record, 24) === 0 &&
      size > 0 && stored === rounded(size) && offset >= 1024 && offset <= image.length && stored <= image.length - offset,
      'invalid_package', 'UFW entry type, index or bounds are invalid.');
    requireValue(record.subarray(6, 8).every(n => n === 0) && record.subarray(20, 24).every(n => n === 0) && record.subarray(28, 64).every(n => n === 0), 'invalid_package', 'Unsupported UFW entry flags.');
    text(record.subarray(64, 80), 16, index === 0 ? 'flash.bin' : 'ota.bin');
    const data = image.subarray(offset, offset + size);
    requireValue(crc16(data) === read16(record, 4), 'invalid_package', 'UFW entry data CRC is invalid.');
    return { type, offset, size, stored, data };
  });
  const [flash, loader] = records;
  requireValue(flash.offset === 1024 && flash.size >= 0x4000 && flash.size <= 0xff000 && flash.size % 4096 === 0 &&
    loader.size < 32768 && loader.offset === flash.offset + flash.stored && loader.offset + loader.stored === image.length,
    'invalid_package', 'Image must contain an aligned application prefix and a bounded nonoverlapping loader.');
  requireValue(await digest(loader.data, crypto) === WIRE.loaderSha256, 'invalid_loader', 'The exact reviewed OTA loader is required.');
  return Object.freeze({ flashBytes: flash.size, loaderBytes: loader.size, loaderSha256: WIRE.loaderSha256 });
}
export async function inspectPackage(input, manifestInput, crypto = globalThis.crypto) {
  const manifest = validateManifest(manifestInput);
  let raw;
  if (input instanceof Uint8Array || Array.isArray(input)) raw = byteArray(input);
  else if (input instanceof ArrayBuffer) raw = new Uint8Array(input.slice(0));
  else {
    requireValue(input && typeof input.arrayBuffer === 'function' && input.size === manifest.packageBytes, 'invalid_package', 'Selected package size differs from the manifest.');
    raw = new Uint8Array(await input.arrayBuffer());
  }
  requireValue(raw.length === manifest.packageBytes && await digest(raw, crypto) === manifest.packageSha256, 'invalid_package', 'Selected package digest or size differs from the manifest.');
  const image = unwrapPackage(raw, manifest.targetIdentity);
  requireValue(image.length === manifest.imageBytes && await digest(image, crypto) === manifest.imageSha256, 'invalid_package', 'Logical image digest or size differs from the manifest.');
  const details = await validateImage(image, crypto);
  return Object.freeze({ manifest, image, details });
}
