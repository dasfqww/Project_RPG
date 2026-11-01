// Scoped development observer/fault fixture. Never creates a command or retries it.
import http from 'node:http';
import path from 'node:path';
import { readFile, lstat } from 'node:fs/promises';
import { createHash } from 'node:crypto';

const hash = bytes => createHash('sha256').update(bytes).digest('hex');
function ordered(value) {
  if (Array.isArray(value)) return value.map(ordered);
  if (value && typeof value === 'object') return Object.fromEntries(Object.keys(value).sort().map(key => [key, ordered(value[key])]));
  return value;
}
export function canonicalCommand(value) {
  const command = structuredClone(value);
  for (const mutation of command.mutations ?? []) {
    // UE writes int64 revisions as strings; the Web DTO returns numbers. Pickup revisions are exactly zero.
    if (mutation.expectedRevision === '0') mutation.expectedRevision = 0;
    if (mutation.newRecord?.revision === '0') mutation.newRecord.revision = 0;
  }
  return JSON.stringify(ordered(command));
}
const canonicalRecord = value => JSON.stringify(ordered(value));

export function createPreparedObservation({ scope, upstreamPort, journalRoot, state, fail, readBody, filteredHeaders, observedTime }) {
  if (!journalRoot || !path.isAbsolute(journalRoot) || path.basename(journalRoot) !== scope.sessionId) throw new Error('InvalidJournalRoot');
  if (scope.fault !== undefined && !['prepare-once', 'consume-held'].includes(scope.fault)) throw new Error('InvalidPreparedFault');
  if (scope.slotConflict !== undefined && (scope.slotConflict !== true || scope.fault !== 'consume-held' || scope.characterIds.length !== 2)) throw new Error('InvalidSlotConflictScope');
  if (scope.revisionConflict !== undefined && (scope.revisionConflict !== true || scope.slotConflict !== undefined
    || scope.fault !== 'consume-held' || scope.characterIds.length !== 2)) throw new Error('InvalidRevisionConflictScope');
  const root = path.resolve(journalRoot);
  const report = state.prepared = { fault: scope.fault ?? null, origin: null, preparations: [], consumes: [], reads: [], rejectedWrites: 0 };
  const revisionConflict = scope.revisionConflict === true;
  const conflictKind = revisionConflict ? 'revision' : 'slot';
  const conflict = scope.slotConflict || revisionConflict
    ? report[revisionConflict ? 'revisionConflict' : 'slotConflict'] = { phase: 'setup', setupBlocked: [], heldConsumes: [], conflicts: [] } : null;
  let envelope = null, firstBytes = null, frozen = null, record = null, preparedAt = null, consumedAt = null;
  const prefix = `/api/dungeon-sessions/${scope.sessionId}/pickup-intents`;
  async function checkJournal(command, dropId) {
    const filename = path.resolve(root, `${command.requestId}.json`);
    if (path.dirname(filename) !== root) throw new Error('JournalPath');
    const info = await lstat(filename);
    if (!info.isFile() || info.isSymbolicLink() || info.size < 1 || info.size > 64 * 1024) throw new Error('JournalFile');
    const bytes = await readFile(filename);
    const journal = JSON.parse(bytes.toString('utf8'));
    if (journal.schemaVersion !== 2 || journal.serverId !== scope.serverId || journal.sessionId !== scope.sessionId
      || journal.dropId !== dropId || canonicalCommand(JSON.parse(journal.body)) !== canonicalCommand(command)) throw new Error('JournalIdentity');
    return hash(bytes);
  }
  async function forward(request, response) {
    if (state.failure) { response.destroy(); return; }
    const received = observedTime();
    const bytes = await readBody(request);
    let kind;
    let restartRead = null;
    if (state.restartRecovery) {
      if (request.method !== 'GET' || bytes.length !== 0) {
        report.rejectedWrites++; state.restartRecovery.rejectedWrites++;
        fail('PreparedWriteDuringRestartRecovery'); response.destroy(); return;
      }
      const url = new URL(request.url, 'http://127.0.0.1'), o = report.origin;
      if (request.url === `${prefix}/${o.requestId}`) {
        kind = 'intent'; restartRead = { kind, requestId: o.requestId, dropId: o.dropId, commandSHA256: o.commandSHA256 };
      } else if (request.url === `/api/item-transactions/${o.requestId}`) {
        if (!state.restartRecovery.reads.some(r => r.kind === 'intent' && r.acknowledgement === 'delivered')) {
          fail('PreparedRestartReceiptBeforeIntent'); response.destroy(); return;
        }
        kind = 'receipt'; restartRead = { kind, requestId: o.requestId, recordSHA256: hash(record) };
      } else if (url.pathname === '/api/items' && url.searchParams.get('ownerType') === 'Character'
        && scope.characterIds.includes(url.searchParams.get('ownerId')) && url.searchParams.get('includeTerminal') === 'false'
        && [...url.searchParams.keys()].every((key, index, keys) => ['ownerType', 'ownerId', 'includeTerminal', 'limit'].includes(key) && keys.indexOf(key) === index)
        && (!url.searchParams.has('limit') || url.searchParams.get('limit') === '500')) {
        kind = 'inventory'; restartRead = { kind, characterId: url.searchParams.get('ownerId') };
        if (restartRead.characterId === o.characterId && !state.restartRecovery.reads.some(r => r.kind === 'receipt' && r.acknowledgement === 'delivered')) {
          fail('PreparedRestartOwnerInventoryBeforeReceipt'); response.destroy(); return;
        }
      } else { fail('PreparedUnexpectedRestartRead'); response.destroy(); return; }
      // Receipt resolution removes the file before inventory loading. Until then it must be the original journal.
      if (kind !== 'inventory' && await checkJournal(envelope.command, envelope.dropId) !== o.journalSHA256) {
        fail('PreparedRestartJournalChanged'); response.destroy(); return;
      }
    } else if (request.method === 'POST' && request.url === prefix) {
      kind = 'prepare';
      if (bytes.length > 128 * 1024) { fail('PreparedBodyLimit'); response.destroy(); return; }
      const document = JSON.parse(bytes.toString('utf8')), c = document.command, r = c?.mutations?.[0]?.newRecord;
      const guid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
      const valid = v => typeof v === 'string' && guid.test(v) && v !== '00000000-0000-0000-0000-000000000000';
      if (!valid(document.dropId) || !valid(c?.requestId) || !valid(r?.state?.instanceId)
        || c.operation !== 'Item.Pickup' || c.actor?.type !== 'Character' || !scope.characterIds.includes(c.actor.ownerId)
        || c.affectedQuantity !== 1 || c.mutations.length !== 1 || Number(c.mutations[0].expectedRevision) !== 0
        || Number(r.revision) !== 0 || r.owner?.type !== 'Character' || r.owner.ownerId !== c.actor.ownerId
        || r.state.quantity !== 1 || r.location?.containerType !== 'Inventory' || r.location.containerId !== c.actor.ownerId
        || r.metadata?.creationSource !== 'WorldPickup') { fail('PreparedCommandShape'); response.destroy(); return; }
      if (firstBytes && !bytes.equals(firstBytes)) { fail('PreparedFrozenCommandChanged'); response.destroy(); return; }
      // Freeze synchronously before the journal read yields to another request.
      if (!envelope) { envelope = document; firstBytes = bytes; frozen = canonicalCommand(c); }
      const journalSHA256 = await checkJournal(c, document.dropId);
      if (!report.origin) {
        report.origin = { sessionId: scope.sessionId, serverId: scope.serverId, dropId: document.dropId, requestId: c.requestId,
          characterId: c.actor.ownerId, itemId: r.state.instanceId, commandSHA256: hash(frozen), prepareBodySHA256: hash(bytes),
          definition: r.definitionName, journalSHA256, journalSchemaVersion: 2, journalVerifiedBeforePrepare: true };
      } else if (report.origin.journalSHA256 !== journalSHA256) { fail('PreparedJournalChanged'); response.destroy(); return; }
    } else if (request.method === 'POST') {
      kind = 'consume';
      if (!envelope || report.preparations.length === 0 || request.url !== `${prefix}/${envelope.command.requestId}/commit`
        || bytes.length !== 0) { report.rejectedWrites++; fail('PreparedUnexpectedWrite'); response.destroy(); return; }
      if (await checkJournal(envelope.command, envelope.dropId) !== report.origin.journalSHA256) {
        fail('PreparedJournalChanged'); response.destroy(); return;
      }
      if (conflict && (conflict.phase === 'setup' || (conflict.phase === 'armed' && conflict.conflicts.length >= 2))) {
        const entry = { requestId: envelope.command.requestId, bodySHA256: hash(bytes), commandSHA256: hash(frozen),
          journalSHA256: report.origin.journalSHA256, ...observedTime() };
        if (conflict.phase === 'setup') { conflict.setupBlocked.push(entry); state.phase = `${conflictKind}-setup`; }
        else conflict.heldConsumes.push(entry);
        response.destroy(); return;
      }
      if (scope.fault === 'consume-held' && state.phase === 'blocked') {
        state.blockedRetries.push({ stage: 'consume', requestId: envelope.command.requestId, bodySHA256: hash(bytes),
          commandSHA256: hash(frozen), journalSHA256: report.origin.journalSHA256, ...observedTime() });
        response.destroy(); return;
      }
    } else if (request.method === 'GET' && bytes.length === 0) {
      const url = new URL(request.url, 'http://127.0.0.1');
      if (url.pathname !== '/api/items' || url.searchParams.get('ownerType') !== 'Character'
        || !scope.characterIds.includes(url.searchParams.get('ownerId')) || url.searchParams.get('includeTerminal') !== 'false') {
        fail('PreparedUnexpectedRead'); response.destroy(); return;
      }
      kind = 'inventory';
    } else { report.rejectedWrites++; fail('PreparedUnexpectedWrite'); response.destroy(); return; }
    const headers = filteredHeaders(request.headers);
    headers.host = `127.0.0.1:${upstreamPort}`; headers['content-length'] = bytes.length; headers.connection = 'close';
    const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort, method: request.method, path: request.url, headers });
    upstream.setTimeout(15000, () => upstream.destroy());
    upstream.on('error', () => { fail('PreparedUpstreamTransport'); response.destroy(); });
    response.on('close', () => { if (!response.writableEnded) upstream.destroy(); });
    upstream.on('response', incoming => {
      void (async () => {
        const body = await readBody(incoming);
        const document = JSON.parse(body.toString('utf8'));
        if (conflict && kind === 'consume' && incoming.statusCode === 409 && !record
          && ['armed', 'released'].includes(conflict.phase)) {
          if (document.status !== (revisionConflict ? 'RevisionConflict' : 'LocationConflict') || document.error !== 'pickup_commit_not_completed'
            || document.records !== undefined || document.command !== undefined) {
            fail(revisionConflict ? 'PreparedInvalidRevisionConflict' : 'PreparedInvalidSlotConflict'); response.destroy(); return;
          }
          const entry = { status: document.status, statusCode: 409, requestId: envelope.command.requestId, bodyBytes: bytes.length,
            bodySHA256: hash(bytes), commandSHA256: hash(frozen), journalSHA256: report.origin.journalSHA256,
            acknowledgement: 'pending', requestElapsedMilliseconds: received.elapsedMilliseconds, ...observedTime() };
          conflict.conflicts.push(entry);
          const outputHeaders = filteredHeaders(incoming.headers); outputHeaders['content-length'] = body.length;
          response.writeHead(409, outputHeaders); response.end(body, () => {
            entry.acknowledgement = 'delivered';
            if (conflict.phase === 'armed' && conflict.conflicts.length >= 2) state.phase = `${conflictKind}-conflict-held`;
          });
          return;
        }
        if (incoming.statusCode !== 200) { fail('PreparedUpstreamStatus'); response.destroy(); return; }
        if (conflict && kind === 'consume' && (revisionConflict || conflict.phase !== 'released')) {
          fail(revisionConflict ? 'PreparedRevisionGrantedUnexpectedly' : 'PreparedSlotGrantedBeforeRelease'); response.destroy(); return;
        }
        let entry;
        if (kind === 'prepare' || kind === 'intent') {
          const o = report.origin;
          const consumed = document.consumedAt !== null && Number.isFinite(Date.parse(document.consumedAt));
          if (!['Prepared', 'AlreadyPrepared'].includes(document.status) || document.dungeonSessionId !== scope.sessionId
            || document.serverId !== scope.serverId || document.dropId !== o.dropId || document.requestId !== o.requestId
            || document.characterId !== o.characterId || document.itemId !== o.itemId || canonicalCommand(document.command) !== frozen
            || !Number.isFinite(Date.parse(document.preparedAt)) || (document.consumedAt !== null && !consumed)
            || (!preparedAt && (document.status !== 'Prepared' || consumed))
            || (preparedAt && (document.status !== (kind === 'intent' ? 'Prepared' : 'AlreadyPrepared') || document.preparedAt !== preparedAt))
            || Boolean(record) !== consumed || (consumedAt !== null && document.consumedAt !== consumedAt)) { fail('PreparedOriginMismatch'); response.destroy(); return; }
          preparedAt ??= document.preparedAt;
          if (consumed) consumedAt ??= document.consumedAt;
          if (kind === 'intent') {
            if (!consumed) { fail('PreparedRestartUnconsumed'); response.destroy(); return; }
            Object.assign(restartRead, { consumed: true, preparedAt, consumedAt, journalSHA256: report.origin.journalSHA256 });
          } else {
          entry = { status: document.status, commandSHA256: hash(frozen), bodySHA256: hash(bytes), consumed,
            journalSHA256: o.journalSHA256, acknowledgement: 'pending', requestElapsedMilliseconds: received.elapsedMilliseconds, ...observedTime() };
          report.preparations.push(entry);
          if (scope.fault === 'prepare-once' && report.preparations.length === 1) {
            entry.acknowledgement = 'dropped'; state.droppedAcknowledgements++; state.phase = 'prepare-retry';
            response.destroy(); return;
          }
          }
        } else if (kind === 'consume' || kind === 'receipt') {
          const c = envelope.command, expected = structuredClone(c.mutations[0].newRecord); expected.revision = 1;
          if (!['Committed', 'AlreadyCommitted'].includes(document.status) || document.requestId !== c.requestId
            || document.operation !== c.operation || document.commandFingerprint !== c.commandFingerprint
            || canonicalRecord(document.actor) !== canonicalRecord(c.actor) || document.affectedQuantity !== 1
            || document.records?.length !== 1 || canonicalRecord(document.records[0]) !== canonicalRecord(expected)) {
            fail('PreparedReceiptMismatch'); response.destroy(); return;
          }
          const current = canonicalRecord(document.records[0]);
          if ((!record && document.status !== 'Committed') || (record && (record !== current || (kind === 'consume' && document.status !== 'AlreadyCommitted')))) {
            fail('PreparedCommitOrdering'); response.destroy(); return;
          }
          record ??= current;
          if (kind === 'consume') {
          entry = { status: document.status, requestId: c.requestId, itemId: expected.state.instanceId, bodyBytes: bytes.length,
            bodySHA256: hash(bytes), commandSHA256: hash(frozen), recordSHA256: hash(record),
            journalSHA256: report.origin.journalSHA256, acknowledgement: 'pending', ...observedTime() };
          report.consumes.push(entry);
          if (scope.fault === 'consume-held' && report.consumes.length === 1) {
            entry.acknowledgement = 'dropped';
            state.committed = { ...entry, characterId: c.actor.ownerId, definition: expected.definitionName, upstreamStatus: document.status };
            state.droppedAcknowledgements++; state.phase = 'blocked';
            response.destroy(); return;
          }
          }
        } else if (restartRead) {
          const expected = restartRead.characterId === report.origin.characterId ? [JSON.parse(record)] : [];
          if (!Array.isArray(document.items) || canonicalRecord(document.items) !== canonicalRecord(expected)) {
            fail('PreparedRestartInventoryMismatch'); response.destroy(); return;
          }
          Object.assign(restartRead, { itemCount: document.items.length, recordSHA256: expected.length ? hash(record) : null });
        } else report.reads.push({ characterId: new URL(request.url, 'http://127.0.0.1').searchParams.get('ownerId'), ...observedTime() });
        if (restartRead) {
          Object.assign(restartRead, { statusCode: incoming.statusCode, acknowledgement: 'pending',
            requestElapsedMilliseconds: received.elapsedMilliseconds, ...observedTime() });
          state.restartRecovery.reads.push(restartRead);
        }
        const outputHeaders = filteredHeaders(incoming.headers); outputHeaders['content-length'] = body.length;
        response.writeHead(incoming.statusCode, outputHeaders); response.end(body, () => {
          if (entry) entry.acknowledgement = 'delivered';
          if (restartRead) restartRead.acknowledgement = 'delivered';
          if (kind === 'consume') {
            if (scope.fault === 'consume-held') {
              state.delivered = { ...entry, upstreamStatus: document.status }; state.phase = 'recovered';
            } else state.phase = 'observed';
          }
        });
      })().catch(() => { fail('PreparedInvalidResponse'); response.destroy(); });
    });
    upstream.end(bytes);
  }
  forward.release = () => {
    if (scope.fault !== 'consume-held' || state.failure || state.phase !== 'blocked'
      || state.droppedAcknowledgements !== 1 || state.blockedRetries.length < 4) return false;
    state.phase = 'released'; return true;
  };
  forward.slotReady = () => {
    if (revisionConflict || !conflict || conflict.phase !== 'setup' || state.failure || state.phase !== 'slot-setup' || conflict.setupBlocked.length < 1) return false;
    conflict.phase = 'armed'; conflict.armed = observedTime(); state.phase = 'slot-conflict'; return true;
  };
  forward.slotResume = () => {
    if (revisionConflict || !conflict || conflict.phase !== 'armed' || state.failure || state.phase !== 'slot-conflict-held'
      || conflict.conflicts.length < 2 || conflict.conflicts.some(c => c.acknowledgement !== 'delivered')) return false;
    conflict.phase = 'released'; conflict.released = observedTime(); state.phase = 'observing'; return true;
  };
  forward.revisionReady = () => {
    if (!revisionConflict || !conflict || conflict.phase !== 'setup' || state.failure || state.phase !== 'revision-setup'
      || conflict.setupBlocked.length < 1) return false;
    conflict.phase = 'armed'; conflict.armed = observedTime(); state.phase = 'revision-conflict'; return true;
  };
  forward.restart = () => {
    if (conflict || scope.fault !== 'consume-held' || state.failure || state.phase !== 'blocked'
      || state.droppedAcknowledgements !== 1 || state.delivered || state.blockedRetries.length < 4
      || report.consumes.length !== 1 || report.preparations.length < 2 || !consumedAt
      || report.preparations.some(p => p.acknowledgement !== 'delivered')) return false;
    state.restartRecovery = { ...observedTime(), reads: [], rejectedWrites: 0,
      preparationCount: report.preparations.length, consumeCount: report.consumes.length, blockedRetryCount: state.blockedRetries.length };
    state.phase = 'restart-read-only'; return true;
  };
  return forward;
}
