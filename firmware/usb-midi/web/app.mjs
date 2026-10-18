// SPDX-License-Identifier: Apache-2.0
import { createCommonUpdater, validateManifest } from './index.mjs';
const el = id => document.getElementById(id);
let selectedManifest = null, packageFile = null, inspected = null, review = null, selection = 0, manifestSelection = 0, working = false, saved = null;
const updater = createCommonUpdater({ onState: paint, onEvent: event => {
  el('transfer').hidden = false; el('transfer-status').textContent = 'Transfer in progress · ' + event.phase;
  if (Number.isFinite(event.progress)) el('progress').value = event.progress; else el('progress').removeAttribute('value');
} });
const trusted = event => event?.isTrusted === true;
function message(error) { return error?.code ? error.code + ': ' + error.message : 'The requested operation could not be completed.'; }
function paint(snapshot = updater.status()) {
  const c = snapshot.capabilities;
  el('status').textContent = snapshot.connected ? snapshot.identity + ' · ' + (c?.installAvailable ? 'Install backend enabled' : 'Installation unavailable') : 'Not connected';
  el('connect').disabled = working || snapshot.busy; el('disconnect').disabled = working || snapshot.busy || !snapshot.connected;
  el('manifest').disabled = working; el('package').disabled = working;
  el('review').disabled = working || !inspected || !snapshot.connected || !snapshot.durableReady || !snapshot.installAvailable || snapshot.recoveryRequired;
  if (snapshot.recoveryRequired) el('notice').textContent = 'An unresolved original attempt is saved. Do not start another update. Keep its package and receipt for bench recovery.';
  else if (snapshot.connected && !c?.installAvailable) el('notice').textContent = 'This firmware reports its install backend unavailable. Selecting a file cannot enable it.';
}
async function work(action) { if (working) return; working = true; paint(); try { await action(); } catch (error) { el('notice').textContent = message(error); } finally { working = false; paint(); } }
async function inspect() {
  const epoch = ++selection; review = null; inspected = null; el('package-summary').textContent = 'Validating selected files…'; paint();
  if (!selectedManifest || !packageFile) { el('package-summary').textContent = 'Select both the manifest and matching package.'; return; }
  try {
    const result = await updater.inspectPackage(packageFile, selectedManifest);
    if (epoch !== selection) return; inspected = result;
    el('package-summary').textContent = [result.manifest.targetIdentity, result.manifest.packageBytes + ' package bytes',
      'Package SHA-256: ' + result.manifest.packageSha256, 'Target build: ' + result.manifest.targetBuildSha256,
      'Saved baseline: ' + result.manifest.baselineSha256, 'Physical qualification: pending'].join('\n');
  } catch (error) { if (epoch === selection) el('package-summary').textContent = message(error); }
  finally { if (epoch === selection) paint(); }
}
el('connect').onclick = event => { if (trusted(event)) void work(async () => { await updater.connect(); el('notice').textContent = 'Fresh firmware identity and capabilities checked.'; }); };
el('disconnect').onclick = event => { if (trusted(event)) void work(() => updater.disconnect()); };
el('manifest').onchange = async event => {
  if (!trusted(event) || working) return; selectedManifest = null; inspected = null; review = null; ++selection; paint();
  const epoch = ++manifestSelection;
  try {
    const file = el('manifest').files?.[0]; if (!file || file.size > 16384) throw new Error('manifest');
    const parsed = validateManifest(JSON.parse(await file.text())); if (epoch !== manifestSelection) return;
    selectedManifest = parsed; await inspect();
  } catch (error) { el('package-summary').textContent = error?.code ? message(error) : 'Select a supported manifest smaller than 16 KiB.'; }
};
el('package').onchange = event => { if (trusted(event) && !working) { packageFile = el('package').files?.[0] ?? null; void inspect(); } };
el('review').onclick = event => {
  const state = updater.status();
  if (!trusted(event) || working || !inspected || !state.installAvailable || state.recoveryRequired) return;
  review = { selection, pair: JSON.stringify(state.pair), attemptId: crypto.randomUUID().replaceAll('-', ''), reviewId: crypto.randomUUID().replaceAll('-', '') };
  el('review-summary').textContent = [el('package-summary').textContent, 'Current build: ' + state.capabilities.buildSha256,
    'MIDI input: ' + state.pair.inputName, 'MIDI output: ' + state.pair.outputName, 'Attempt: ' + review.attemptId].join('\n');
  el('confirmation').showModal();
};
const cancel = () => { review = null; el('confirmation').close(); };
el('cancel').onclick = cancel; el('confirmation').addEventListener('cancel', cancel);
el('confirm').onclick = event => {
  const state = updater.status(), approved = review;
  if (!trusted(event) || working || !approved || !inspected || approved.selection !== selection || approved.pair !== JSON.stringify(state.pair) || !state.installAvailable) return;
  review = null; el('confirmation').close();
  void work(async () => {
    el('transfer').hidden = false; el('transfer-status').textContent = 'Preparing the reviewed original attempt…'; el('progress').hidden = false; el('progress').removeAttribute('value');
    try {
      const result = await updater.update({ attemptId: approved.attemptId, reviewId: approved.reviewId, packageBytes: packageFile, manifest: selectedManifest });
      el('transfer-status').textContent = 'Transfer finished · reboot identity and build digest verified · hardware qualification pending';
      el('receipt').textContent = JSON.stringify(result, null, 2); el('notice').textContent = 'Full flash readback has not been performed.';
    } catch (error) {
      el('transfer-status').textContent = error.recoveryRequired ? 'Original transfer outcome uncertain · bench recovery required' : 'Stopped before transfer completion';
      try { saved = await updater.savedAttempt(); el('receipt').textContent = JSON.stringify(saved, null, 2); } catch {}
      throw error;
    } finally { el('progress').hidden = true; }
  });
};
void (async () => {
  try {
    saved = await updater.initialize(); el('notice').textContent = 'Durable local recovery storage is ready. Use Connect MIDI to grant access.';
    if (saved) { el('transfer').hidden = false; el('transfer-status').textContent = 'Saved original attempt · ' + saved.state; el('receipt').textContent = JSON.stringify(saved, null, 2); el('progress').hidden = true; }
  } catch (error) { el('notice').textContent = message(error); }
  finally { paint(); }
})();
