// SPDX-License-Identifier: Apache-2.0
import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs/promises';
import { WIRE, pack7, unpack7, encodeFrame, decodeFrame, parseIdentity, parseCapabilities, parseRequest,
  identityQuery, armCommand, parseArmAck, imageResponse } from '../protocol.mjs';
import { validateManifest, inspectPackage, validateImage, unwrapPackage, crc16, digest, decodeContainerBlock } from '../package.mjs';
import { packageFixture, cryptoFixture, capabilityPacket, identityPacket, requestPacket, BUILD, wrapImage } from './fixtures.mjs';

test('continuous 7-bit stream matches factual vector and roundtrips partial/max payloads', () => {
  assert.deepEqual([...pack7([255, 128, 85])], [127, 1, 86, 2]);
  for (const n of [0, 1, 2, 7, 47, 481, 512, 520]) {
    const bytes = Uint8Array.from({ length: n }, (_, p) => p * 113 & 255);
    assert.deepEqual(unpack7(pack7(bytes)), bytes); assert.deepEqual(decodeFrame(encodeFrame(0x30, bytes), 0x30).payload, bytes);
  }
  assert.deepEqual([...identityQuery()], [0xf0, 0, 0x32, 0x45, 0, 0, 0, 0x40, 0x7f, 0xf7]);
});
test('noncanonical packing, checksum, missing terminator, oversized payload and wrong command reject', () => {
  assert.throws(() => unpack7([0]), { code: 'invalid_frame' }); assert.throws(() => unpack7([255]), { code: 'invalid_frame' });
  const packet = encodeFrame(0x31, [0]); packet[2] ^= 1; assert.throws(() => decodeFrame(packet), { code: 'invalid_frame' });
  assert.throws(() => decodeFrame(encodeFrame(0x30, [1]).slice(0, -1)), { code: 'invalid_frame' });
  assert.throws(() => encodeFrame(0x30, new Uint8Array(521)), { code: 'invalid_frame' });
  assert.throws(() => decodeFrame(encodeFrame(0x30, [1]), 0x31), { code: 'invalid_frame' });
});
test('identity and capability records are exact, with read-only unavailable backend supported', () => {
  assert.equal(parseIdentity(identityPacket()), WIRE.commonIdentity);
  assert.equal(parseCapabilities(capabilityPacket()).chunkBytes, 512);
  const unavailable = parseCapabilities(capabilityPacket({ loader: 0, error: 3 })); assert.equal(unavailable.installAvailable, false); assert.equal(unavailable.error, 3);
  for (const patch of [{ family: 9 }, { version: 2 }, { chunk: 1024 }, { state: 4 }, { state: 1, loader: 0, error: 3 }, { error: 2 }, { build: '0'.repeat(64) }]) {
    assert.throws(() => parseCapabilities(capabilityPacket(patch)));
  }
  assert.throws(() => parseIdentity(encodeFrame(0x11, new Uint8Array(28))), { code: 'invalid_identity' });
  const bad = new Uint8Array(27); bad.set([65, 0, 66]); assert.throws(() => parseIdentity(encodeFrame(0x11, bad)), { code: 'invalid_identity' });
});
test('ARM and file replies bind exact metadata, permit reordering and reject out-of-range/phase', () => {
  const arm = decodeFrame(armCommand(17440, BUILD, '33'.repeat(32)), 0x32); assert.equal(arm.payload.length, 70); assert.deepEqual([...arm.payload.subarray(0, 2)], [1, 8]);
  assert.deepEqual([...arm.payload.subarray(38)], Array(32).fill(0x33)); assert.throws(() => armCommand(17440, BUILD), { code: 'invalid_digest' });
  parseArmAck(encodeFrame(0x32, [0])); assert.throws(() => parseArmAck(encodeFrame(0x32, [3])), { code: 'arm_rejected' });
  const image = Uint8Array.from({ length: 1024 }, (_, p) => p & 255);
  const request = parseRequest(requestPacket(512, 17, 0xa3), 1024, WIRE.checkFinish);
  const response = decodeFrame(imageResponse(request, image), 0x30).payload;
  assert.deepEqual(response.slice(0, 8), request.header); assert.deepEqual(response.slice(8), image.slice(512, 529));
  for (const [address, length] of [[0, 0], [0, 513], [1024, 1], [1023, 2], [0xffffffff, 1]]) assert.throws(() => parseRequest(requestPacket(address, length), 1024, WIRE.checkFinish), { code: 'request_out_of_range' });
  assert.throws(() => parseRequest(requestPacket(WIRE.writeFinish, 8), 1024, WIRE.checkFinish), { code: 'wrong_terminal' });
  assert.throws(() => parseRequest(requestPacket(WIRE.checkFinish, 7), 1024, WIRE.checkFinish), { code: 'wrong_terminal' });
  const terminal = parseRequest(requestPacket(WIRE.checkFinish, 8), 1024, WIRE.checkFinish);
  assert.deepEqual([...decodeFrame(imageResponse(terminal, image)).payload.slice(8)], [115, 117, 99, 99, 101, 115, 115, 0]);
});
test('strict manifest rejects unknown fields, relaxed loader, qualification claims and invalid target', async () => {
  const { manifest } = await packageFixture(); assert.deepEqual(validateManifest(manifest), manifest);
  for (const patch of [{ force: true }, { loaderSha256: '0'.repeat(64) }, { hardwareQualified: true }, { layout: 'dual-bank' }, { targetIdentity: 'FM-1_015' }, { imageBytes: 1 }, { targetBuildSha256: 'aa' }]) assert.throws(() => validateManifest({ ...manifest, ...patch }), { code: patch.targetBuildSha256 ? 'invalid_digest' : 'invalid_manifest' });
});
test('synthetic container validates all envelopes with test-only pinned loader digest substitution', async () => {
  const fixture = await packageFixture(), before = Uint8Array.from(fixture.bytes);
  const artifact = await inspectPackage(fixture.bytes, fixture.manifest, cryptoFixture);
  assert.equal(artifact.details.flashBytes, 16384); assert.deepEqual(artifact.image, fixture.image); assert.deepEqual(fixture.bytes, before);
  await assert.rejects(inspectPackage(fixture.bytes, fixture.manifest), { code: 'invalid_loader' });
  assert.equal(crc16(new TextEncoder().encode('123456789')), 0x31c3);
});
test('hash/markers, UFW CRC, flags, overlap, full-flash/reserved and loader bounds fail independently', async () => {
  const fixture = await packageFixture();
  const changed = fixture.bytes.slice(); changed[1500] ^= 1;
  await assert.rejects(inspectPackage(changed, fixture.manifest, cryptoFixture), { code: 'invalid_package' });
  const marker = fixture.bytes.slice(); marker[47] ^= 1; assert.throws(() => unwrapPackage(marker), { code: 'invalid_package' });
  const badHeader = fixture.image.slice(); badHeader[0] ^= 1; await assert.rejects(validateImage(badHeader, cryptoFixture), { code: 'invalid_package' });
  const badTable = fixture.image.slice(); badTable[64] ^= 1; await assert.rejects(validateImage(badTable, cryptoFixture), { code: 'invalid_package' });
  const badData = fixture.image.slice(); badData[1200] ^= 1; await assert.rejects(validateImage(badData, cryptoFixture), { code: 'invalid_package' });
  for (const patch of [(entry) => { entry[8] = 0; entry[9] = 0; }, (entry) => { entry[24] = 1; }, (entry) => { entry[6] = 1; },
    (entry) => { entry[12] = 0; entry[13] = 0; entry[14] = 16; }, (entry) => { entry[0] = 1; }]) {
    const image = fixture.image.slice(), entry = decodeContainerBlock(image.subarray(64, 144)); patch(entry); image.set(decodeContainerBlock(entry), 64);
    const header = decodeContainerBlock(image.subarray(0, 64)); const tableCrc = crc16(image.subarray(64, 224)); header[2] = tableCrc & 255; header[3] = tableCrc >>> 8; const headerCrc = crc16(header.subarray(2)); header[0] = headerCrc & 255; header[1] = headerCrc >>> 8; image.set(decodeContainerBlock(header));
    const bytes = wrapImage(image), manifest = { ...fixture.manifest, packageSha256: await digest(bytes), imageSha256: await digest(image) };
    await assert.rejects(inspectPackage(bytes, manifest, cryptoFixture), { code: 'invalid_package' });
  }
});
test('production web modules have no process, Node, network firmware fetch or native helper path', async () => {
  for (const file of ['protocol', 'package', 'web-midi', 'journal', 'updater', 'index', 'app']) {
    const text = await fs.readFile(new URL('../' + file + '.mjs', import.meta.url), 'utf8');
    assert.doesNotMatch(text, /(?:from\s*|import\s*\()["']node:|child_process|localStorage|fetch\s*\(|XMLHttpRequest|WebSocket|\bforce\s*:/);
  }
});
