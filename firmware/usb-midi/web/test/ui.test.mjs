// SPDX-License-Identifier: Apache-2.0
import test from 'node:test';
import assert from 'node:assert/strict';
import vm from 'node:vm';
import fs from 'node:fs/promises';
import { webcrypto } from 'node:crypto';
import { validateManifest } from '../package.mjs';
import { packageFixture, OLD_BUILD, BUILD, ATTEMPT } from './fixtures.mjs';
const app = await fs.readFile(new URL('../app.mjs', import.meta.url), 'utf8');
const tick = () => new Promise(resolve => setImmediate(resolve));
const user = { isTrusted: true }, synthetic = { isTrusted: false };
async function mount(options = {}) {
  const fixture = await packageFixture(), nodes = new Map(), calls = [];
  const node = id => {
    if (!nodes.has(id)) nodes.set(id, { id, files: [], hidden: false, disabled: false, textContent: '', open: false,
      showModal() { this.open = true; }, close() { this.open = false; }, removeAttribute(name) { delete this[name]; }, addEventListener() {} });
    return nodes.get(id);
  };
  let callbacks;
  const state = { connected: false, pair: null, capabilities: null, installAvailable: false, durableReady: true, busy: false, recoveryRequired: Boolean(options.saved), ...options.state };
  const fakeUpdater = {
    status: () => ({ ...state }),
    initialize: async () => { calls.push('initialize'); if (options.storageError) throw Object.assign(new Error('Storage unavailable'), { code: 'storage_unavailable' }); return options.saved ?? null; },
    savedAttempt: async () => options.saved ?? null,
    connect: async () => { calls.push('connect'); Object.assign(state, { connected: true, identity: 'FM1_FORGE_001', pair: { inputId: 'i', outputId: 'o', inputName: 'FM-1 Forge', outputName: 'FM-1 Forge' }, capabilities: { buildSha256: OLD_BUILD, installAvailable: true }, installAvailable: true, ...options.connectedState }); callbacks.onState(fakeUpdater.status()); return fakeUpdater.status(); },
    disconnect: async () => { calls.push('disconnect'); state.connected = false; callbacks.onState(fakeUpdater.status()); },
    inspectPackage: async (file, manifest) => { calls.push({ inspect: file, manifest }); return { manifest, details: { flashBytes: 16384 } }; },
    update: async args => { calls.push({ update: args }); callbacks.onEvent({ phase: 'checking' }); return { attemptId: args.attemptId, buildSha256: BUILD, identity: 'FM1_FORGE_001', transferFinished: true, bootIdentityVerified: true, readbackVerified: false, hardwareQualified: false }; },
  };
  const context = vm.createContext({ document: { getElementById: node }, window: { testDependencies: { createCommonUpdater: given => { callbacks = given; return fakeUpdater; }, validateManifest } }, crypto: webcrypto, console });
  vm.runInContext(app.replace("import { createCommonUpdater, validateManifest } from './index.mjs';", 'const {createCommonUpdater,validateManifest}=window.testDependencies;'), context); await tick();
  const selectFiles = async () => {
    node('manifest').files = [{ name: 'build.json', size: 2000, text: async () => JSON.stringify(fixture.manifest) }];
    await node('manifest').onchange(user); node('package').files = [{ name: 'build.fwsc', size: fixture.bytes.length }]; node('package').onchange(user); await tick();
  };
  return { ...fixture, node, calls, state, selectFiles };
}
test('static UI is passive at startup, ignores synthetic actions and requires separate trusted review/confirmation', async () => {
  const ui = await mount(); assert.deepEqual(ui.calls, ['initialize']); ui.node('connect').onclick(synthetic); await tick(); assert.deepEqual(ui.calls, ['initialize']);
  ui.node('connect').onclick(user); await tick(); await ui.selectFiles(); assert.equal(ui.node('review').disabled, false);
  ui.node('review').onclick(synthetic); assert.equal(ui.node('confirmation').open, false);
  ui.node('review').onclick(user); assert.equal(ui.node('confirmation').open, true); ui.node('confirm').onclick(synthetic); await tick(); assert.equal(ui.calls.filter(c => c.update).length, 0);
  ui.node('cancel').onclick(); ui.node('confirm').onclick(user); await tick(); assert.equal(ui.calls.filter(c => c.update).length, 0);
  ui.node('review').onclick(user); ui.node('confirm').onclick(user); ui.node('confirm').onclick(user); await tick(); assert.equal(ui.calls.filter(c => c.update).length, 1);
  const args = ui.calls.find(c => c.update).update; assert.equal(args.manifest.packageSha256, ui.manifest.packageSha256); assert.match(args.attemptId, /^[a-f0-9]{32}$/);
  assert.match(ui.node('transfer-status').textContent, /build digest verified.*qualification pending/); assert.match(ui.node('receipt').textContent, /"readbackVerified": false/);
});
test('changing a file or MIDI pair invalidates the previously reviewed UI action', async () => {
  const ui = await mount(); ui.node('connect').onclick(user); await tick(); await ui.selectFiles(); ui.node('review').onclick(user);
  ui.state.pair = { ...ui.state.pair, inputId: 'replacement' }; ui.node('confirm').onclick(user); await tick(); assert.equal(ui.calls.filter(c => c.update).length, 0);
  ui.node('review').onclick(user); ui.node('package').files = [{ name: 'different.fwsc', size: 100 }]; ui.node('package').onchange(user); ui.node('confirm').onclick(user); await tick(); assert.equal(ui.calls.filter(c => c.update).length, 0);
});
test('an older asynchronous manifest read cannot replace the latest selected manifest', async () => {
  const ui = await mount(); ui.node('connect').onclick(user); await tick();
  let resolveOld; ui.node('manifest').files = [{ size: 2000, text: () => new Promise(resolve => { resolveOld = resolve; }) }]; const oldRead = ui.node('manifest').onchange(user);
  const latest = { ...ui.manifest, targetBuildSha256: '55'.repeat(32) };
  ui.node('manifest').files = [{ size: 2000, text: async () => JSON.stringify(latest) }]; await ui.node('manifest').onchange(user);
  resolveOld(JSON.stringify(ui.manifest)); await oldRead;
  ui.node('package').files = [{ name: 'latest.fwsc', size: ui.bytes.length }]; ui.node('package').onchange(user); await tick();
  assert.equal(ui.calls.filter(c => c.inspect).at(-1).manifest.targetBuildSha256, latest.targetBuildSha256);
});
test('unresolved original attempt and false native install capability keep review unavailable', async () => {
  let ui = await mount({ saved: { attemptId: ATTEMPT, state: 'unknown' } }); assert.match(ui.node('notice').textContent, /unresolved original/); assert.match(ui.node('receipt').textContent, new RegExp(ATTEMPT));
  ui.node('connect').onclick(user); await tick(); await ui.selectFiles(); assert.equal(ui.node('review').disabled, true);
  ui.node('review').onclick(user); assert.equal(ui.node('confirmation').open, false);
  ui = await mount({ connectedState: { installAvailable: false, capabilities: { buildSha256: OLD_BUILD, installAvailable: false } } });
  ui.node('connect').onclick(user); await tick(); await ui.selectFiles(); assert.equal(ui.node('review').disabled, true); assert.match(ui.node('status').textContent, /Installation unavailable/);
});
