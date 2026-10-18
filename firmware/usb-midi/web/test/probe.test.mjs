// SPDX-License-Identifier: Apache-2.0
import test from 'node:test';
import assert from 'node:assert/strict';
import { createCommonUpdater, DEFAULT_TIMING } from '../updater.mjs';
import { DEVICE_LOCK, AttemptStore } from '../journal.mjs';
import { decodeFrame, UpdateError } from '../protocol.mjs';
import { packageFixture, cryptoFixture, fakeIndexedDB, fakeLocks, fakeMidi, ATTEMPT, REVIEW } from './fixtures.mjs';
const timing={...DEFAULT_TIMING,open:0,handshake:20,arm:20,start:0,reply:0,terminal:0,poll:2,idleCheck:10,idleWrite:10,loader:20,reboot:20,enumerate:1};
async function fixture(options={}){
  const files=await packageFixture(),storage=fakeIndexedDB(),locks=fakeLocks(),midi=fakeMidi(options.midi??{});
  const updater=createCommonUpdater({navigator:midi.navigator,indexedDB:storage.indexedDB,crypto:cryptoFixture,locks,location:{origin:'https://fm1.test'},secureContext:true,timing,beforeWrite:options.beforeWrite});
  return {...files,storage,locks,midi,updater,args:{attemptId:ATTEMPT,reviewId:REVIEW,packageBytes:files.bytes,manifest:files.manifest}};
}
test('probe performs fresh bounded identity/capability reads without another grant, ARM or trigger',async()=>{
  const options={},f=await fixture({midi:options});await f.updater.connect();const before=f.midi.calls.sends.length,permissions=f.midi.calls.permissions;
  options.cap={build:'66'.repeat(32)};await f.updater.probe();
  assert.deepEqual(f.midi.calls.sends.slice(before).map(packet=>decodeFrame(packet).command),[0x11,0x31]);
  assert.equal(f.updater.status().capabilities.buildSha256,'66'.repeat(32));assert.equal(f.updater.status().connected,true);
  assert.equal(f.midi.calls.permissions,permissions);assert.equal(f.midi.calls.arms,0);assert.equal(f.midi.calls.triggers,0);assert.equal(f.midi.calls.opens,f.midi.calls.closes);
});
test('unresponsive, removed, changed-pair and wrong-identity probes clear cached connection and never write',async()=>{
  for(const variant of ['unresponsive','removed','changed-pair','wrong-identity']){
    const options={},f=await fixture({midi:options});await f.updater.connect();
    if(variant==='unresponsive')options.noIdentity=true;
    if(variant==='removed'){f.midi.access.inputs.clear();f.midi.access.outputs.clear();}
    if(variant==='changed-pair')for(const port of f.midi.access.inputs.values())port.id='replacement';
    if(variant==='wrong-identity')options.identity='FM-1_015';
    await assert.rejects(f.updater.probe());assert.equal(f.updater.status().connected,false);assert.equal(f.updater.status().identity,null);
    assert.equal(f.midi.calls.arms,0);assert.equal(f.midi.calls.triggers,0);assert.equal(f.midi.calls.opens,f.midi.calls.closes);
  }
});
test('probe keeps gate-off firmware connected and visibly unavailable; port-close failure invalidates evidence',async()=>{
  const options={},f=await fixture({midi:options});await f.updater.connect();options.cap={loader:0,error:3};await f.updater.probe();
  assert.equal(f.updater.status().connected,true);assert.equal(f.updater.status().installAvailable,false);
  assert.equal(f.updater.status().capabilities.error,3);
  f.midi.access.inputs.values().next().value.close=async()=>{throw new Error('Close failed');};
  await assert.rejects(f.updater.probe(),{code:'close_failed'});assert.equal(f.updater.status().connected,false);assert.equal(f.midi.calls.triggers,0);
});
test('another owner of the shared lock prevents probe I/O without pretending a performed probe failed',async()=>{
  const f=await fixture();await f.updater.connect();const before=f.midi.calls.sends.length,opens=f.midi.calls.opens;
  await f.locks.request(DEVICE_LOCK,{ifAvailable:true},async()=>{await assert.rejects(f.updater.probe(),{code:'device_busy'});});
  assert.equal(f.midi.calls.sends.length,before);assert.equal(f.midi.calls.opens,opens);assert.equal(f.updater.status().connected,true);
});
test('disconnect during closing an identified port cannot resurrect stale connect or probe evidence',async()=>{
  for(const operation of ['connect','probe']){
    const f=await fixture();if(operation==='probe')await f.updater.connect();
    const input=f.midi.access.inputs.values().next().value,originalClose=input.close;
    input.close=async()=>{await originalClose();f.midi.disconnect();};
    await assert.rejects(f.updater[operation](),{code:'disconnected'});
    assert.equal(f.updater.status().connected,false);assert.equal(f.updater.status().identity,null);assert.equal(f.updater.status().installAvailable,false);
    assert.equal(f.midi.calls.arms,0);assert.equal(f.midi.calls.triggers,0);
  }
});
test('fresh peek is initialized-only, read-only and callable inside the held same-origin lock',async()=>{
  const f=await fixture();await assert.rejects(f.updater.peekSavedAttempt(),{code:'storage_unavailable'});await f.updater.initialize();
  const store=new AttemptStore({indexedDB:f.storage.indexedDB,crypto:cryptoFixture,origin:'https://fm1.test',secureContext:true});await store.initialize();
  const pair={inputId:'i',outputId:'o',inputName:'FM-1 Forge',outputName:'FM-1 Forge'},journal=await store.prepare({attemptId:ATTEMPT,reviewId:REVIEW,pair,manifest:f.manifest});
  await journal.transition('done',{state:'unknown',transferFinished:true,bootIdentityVerified:false});
  assert.equal(f.updater.status().recoveryRequired,false);const commits=f.storage.commits.length;
  await f.locks.request(DEVICE_LOCK,{ifAvailable:true},async()=>{const record=await f.updater.peekSavedAttempt();assert.equal(record.state,'unknown');assert.equal(record.attemptId,ATTEMPT);});
  assert.equal(f.storage.commits.length,commits);assert.equal(f.updater.status().recoveryRequired,false);
  f.storage.records.get('current').request.manifest.baselineSha256='77'.repeat(32);await assert.rejects(f.updater.peekSavedAttempt(),{code:'corrupt_journal'});
  f.storage.records.get('scope').sentinel=cryptoFixture.randomUUID();await assert.rejects(f.updater.peekSavedAttempt(),{code:'storage_scope_changed'});
});
test('beforeWrite awaits fresh guard under the shared lock after validation and before any journal intent/ARM/trigger',async()=>{
  let f,calls=0;f=await fixture({beforeWrite:async details=>{
    calls++;assert.equal(details.operation,'update');assert.equal(Object.isFrozen(details),true);assert.equal(f.locks.owned,true);
    assert.equal(f.storage.records.has('current'),false);assert.equal(f.midi.calls.arms,0);assert.equal(f.midi.calls.triggers,0);
    await new Promise(resolve=>setTimeout(resolve,1));throw new UpdateError('opposite_recovery_required','Opposite engine is unresolved');
  }});await f.updater.connect();
  await assert.rejects(f.updater.update({...f.args,packageBytes:new Uint8Array(f.bytes.length)}),{code:'invalid_package'});assert.equal(calls,0);
  await assert.rejects(f.updater.update(f.args),{code:'opposite_recovery_required'});assert.equal(calls,1);assert.equal(f.storage.records.has('current'),false);assert.equal(f.midi.calls.arms,0);assert.equal(f.midi.calls.triggers,0);
});
