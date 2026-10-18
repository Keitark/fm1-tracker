// SPDX-License-Identifier: Apache-2.0
import { UpdateError, requireValue } from './protocol.mjs';
import { digest, validateManifest } from './package.mjs';
export const DEVICE_LOCK = 'fm1-forge-stock-v15-device';
const DB_NAME = 'fm1-common-sysex-v1', STORE = 'records';
const blocked = value => value?.state === 'active' || value?.state === 'unknown';
const id = value => typeof value === 'string' && /^[0-9a-f]{32}$/.test(value);
const jsonCopy = value => JSON.parse(JSON.stringify(value));
const encode = value => new TextEncoder().encode(JSON.stringify(value));
export function supportedOrigin(origin, secureContext) {
  if (secureContext !== true || typeof origin !== 'string') return false;
  if (/^codex-sandbox:\/\/mcp-app-[0-9a-f]{48}\.web-sandbox\.oaiusercontent\.com$/.test(origin)) return true;
  try { const url = new URL(origin); return url.origin === origin && (url.protocol === 'https:' || url.protocol === 'http:' && ['localhost', '127.0.0.1', '[::1]'].includes(url.hostname)); }
  catch { return false; }
}
const requestResult = request => new Promise((resolve, reject) => {
  request.onsuccess = () => resolve(request.result);
  request.onerror = () => reject(new UpdateError('storage_failed', 'The update journal request failed.'));
});
async function transact(database, mode, action) {
  let tx;
  try { tx = database.transaction(STORE, mode, mode === 'readwrite' ? { durability: 'strict' } : undefined); }
  catch { throw new UpdateError('storage_unavailable', 'Durable IndexedDB transactions are unavailable.'); }
  if (mode === 'readwrite' && tx.durability !== 'strict') {
    try { tx.abort(); } catch {}
    throw new UpdateError('storage_unavailable', 'Strict journal durability is required before updating.');
  }
  const done = new Promise((resolve, reject) => {
    tx.oncomplete = resolve;
    tx.onerror = tx.onabort = () => reject(new UpdateError('storage_failed', 'The update journal transaction did not commit.'));
  });
  done.catch(() => {});
  try { const value = await action(tx.objectStore(STORE)); await done; return value; }
  catch (error) { try { tx.abort(); } catch {} await done.catch(() => {}); throw error; }
}
export async function openDatabase(indexedDB) {
  requireValue(typeof indexedDB?.open === 'function', 'storage_unavailable', 'IndexedDB is required for interrupted-update recovery.');
  return new Promise((resolve, reject) => {
    let opening;
    try { opening = indexedDB.open(DB_NAME, 1); }
    catch { reject(new UpdateError('storage_unavailable', 'IndexedDB could not be opened.')); return; }
    opening.onupgradeneeded = () => { if (!opening.result.objectStoreNames.contains(STORE)) opening.result.createObjectStore(STORE, { keyPath: 'key' }); };
    opening.onerror = opening.onblocked = () => reject(new UpdateError('storage_unavailable', 'The update journal is unavailable.'));
    opening.onsuccess = () => {
      const database = opening.result;
      if (!database.objectStoreNames.contains(STORE)) { database.close(); reject(new UpdateError('corrupt_journal', 'The update journal store is missing.')); return; }
      database.onversionchange = () => database.close(); resolve(database);
    };
  });
}
function validateScope(value, origin) {
  requireValue(value?.key === 'scope' && value.schema === 1 && value.origin === origin && typeof value.sentinel === 'string' &&
    /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/.test(value.sentinel) && Object.keys(value).length === 4,
    'storage_scope_changed', 'The durable update storage scope changed.');
  return value;
}
function validateRecord(value) {
  requireValue(value?.key === 'current' && value.schema === 1 && id(value.attemptId) && id(value.request?.reviewId) &&
    value.request.attemptId === value.attemptId && /^[0-9a-f]{64}$/.test(value.requestDigest ?? '') &&
    ['prepared', 'active', 'unknown', 'failed', 'succeeded'].includes(value.state) &&
    ['prepared', 'arming', 'entering_check', 'checking', 'waiting_loader', 'entering_write', 'writing', 'waiting_reboot', 'done', 'failed'].includes(value.phase) &&
    typeof value.transferFinished === 'boolean' && typeof value.bootIdentityVerified === 'boolean' &&
    (!value.bootIdentityVerified || value.state === 'succeeded' && value.transferFinished && value.phase === 'done') &&
    (value.state !== 'succeeded' || value.bootIdentityVerified), 'corrupt_journal', 'The saved update attempt is inconsistent.');
  requireValue((value.state !== 'prepared' || ['prepared', 'arming'].includes(value.phase)) &&
    (value.state !== 'failed' || ['failed', 'prepared', 'arming', 'entering_check'].includes(value.phase)),
    'corrupt_journal', 'A post-trigger attempt cannot become a nonblocking pre-transfer record.');
  validateManifest(value.request.manifest); validateScope({ key: 'scope', schema: 1, ...value.request.scope }, value.request.scope?.origin);
  return value;
}
export class AttemptStore {
  constructor({ indexedDB = globalThis.indexedDB, crypto = globalThis.crypto, origin = globalThis.location?.origin, secureContext = globalThis.isSecureContext } = {}) {
    this.indexedDB = indexedDB; this.crypto = crypto; this.origin = origin; this.secureContext = secureContext;
    this.database = null; this.scope = null;
  }
  async initialize() {
    requireValue(supportedOrigin(this.origin, this.secureContext), 'unsupported_origin', 'A stable secure origin is required for durable recovery.');
    requireValue(typeof this.crypto?.randomUUID === 'function' && typeof this.crypto?.subtle?.digest === 'function', 'crypto_unavailable', 'WebCrypto and secure attempt identifiers are required.');
    this.database ??= await openDatabase(this.indexedDB);
    const scope = await transact(this.database, 'readwrite', async store => {
      let value = await requestResult(store.get('scope'));
      if (value === undefined) {
        requireValue(!this.scope, 'storage_scope_changed', 'The previously observed update storage disappeared.');
        value = { key: 'scope', schema: 1, origin: this.origin, sentinel: this.crypto.randomUUID() };
        await requestResult(store.add(value));
      }
      return validateScope(value, this.origin);
    });
    requireValue(!this.scope || this.scope.sentinel === scope.sentinel, 'storage_scope_changed', 'The previously observed update storage changed.');
    this.scope = jsonCopy(scope); await this.current(); return jsonCopy(scope);
  }
  async current() {
    requireValue(this.database && this.scope, 'storage_unavailable', 'Initialize durable update storage first.');
    const record = await transact(this.database, 'readonly', async store => {
      const scope = validateScope(await requestResult(store.get('scope')), this.origin);
      requireValue(scope.sentinel === this.scope.sentinel, 'storage_scope_changed', 'The update storage scope changed.');
      return requestResult(store.get('current'));
    });
    if (record === undefined) return null;
    validateRecord(record);
    requireValue(record.request.scope.origin === this.scope.origin && record.request.scope.sentinel === this.scope.sentinel &&
      await digest(encode(record.request), this.crypto) === record.requestDigest, 'corrupt_journal', 'The saved update request digest or scope is inconsistent.');
    return jsonCopy(record);
  }
  async prepare({ attemptId, reviewId, pair, manifest }) {
    requireValue(id(attemptId) && id(reviewId) && pair, 'review_required', 'A reviewed attempt and exact MIDI pair are required.');
    const prior = await this.current();
    requireValue(!blocked(prior) && prior?.state !== 'prepared', 'recovery_required', 'An unresolved original update must be inspected before another attempt.');
    const request = { attemptId, reviewId, pair: jsonCopy(pair), manifest: validateManifest(manifest), scope: { origin: this.scope.origin, sentinel: this.scope.sentinel } };
    const record = { key: 'current', schema: 1, attemptId, request, requestDigest: await digest(encode(request), this.crypto),
      state: 'prepared', phase: 'prepared', transferFinished: false, bootIdentityVerified: false };
    await transact(this.database, 'readwrite', async store => {
      const scope = validateScope(await requestResult(store.get('scope')), this.origin);
      requireValue(scope.sentinel === this.scope.sentinel, 'storage_scope_changed', 'Storage changed while preparing the update.');
      requireValue(JSON.stringify(await requestResult(store.get('current')) ?? null) === JSON.stringify(prior), 'journal_changed', 'The saved update changed during review.');
      requireValue(prior?.attemptId !== attemptId && await requestResult(store.get('receipt:' + attemptId)) === undefined, 'duplicate_attempt', 'This update attempt already has a receipt.');
      if (prior) await requestResult(store.add({ ...prior, key: 'receipt:' + prior.attemptId }));
      await requestResult(store.put(record));
    });
    return new AttemptJournal(this, record);
  }
}
export class AttemptJournal {
  constructor(store, record) { this.store = store; this.record = jsonCopy(record); }
  async transition(phase, values = {}) {
    const next = { ...this.record, ...values, phase }; validateRecord(next);
    await transact(this.store.database, 'readwrite', async store => {
      const scope = validateScope(await requestResult(store.get('scope')), this.store.origin);
      requireValue(scope.sentinel === this.record.request.scope.sentinel, 'storage_scope_changed', 'Update storage changed during transfer.');
      const current = await requestResult(store.get('current')); validateRecord(current);
      requireValue(JSON.stringify(current) === JSON.stringify(this.record), 'journal_changed', 'Another writer changed the original update attempt.');
      await requestResult(store.put(next));
    });
    this.record = jsonCopy(next);
  }
}
