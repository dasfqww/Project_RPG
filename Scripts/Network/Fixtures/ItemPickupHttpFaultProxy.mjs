// Local development fixture. Never logs credentials, request bodies or receipts.
import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { createPreparedObservation } from './PreparedPickupHttpObservation.mjs';

const token = process.env.PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN;
const upstreamPort = Number(process.env.PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT);
const listenPort = Number(process.env.PROJECT_RPG_ITEM_PROXY_LISTEN_PORT ?? 0);
if (!token || token.length < 32 || !Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535
  || !Number.isInteger(listenPort) || listenPort < 0 || listenPort > 65535 || listenPort === upstreamPort) {
  throw new Error('Invalid local item proxy configuration.');
}
const digest = value => createHash('sha256').update(value).digest('hex');
const guid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const zero = '00000000-0000-0000-0000-000000000000';
const validId = value => typeof value === 'string' && guid.test(value) && value !== zero;
const startedAt = process.hrtime.bigint();
const observedTime = () => ({ observedAtUtc: new Date().toISOString(),
  elapsedMilliseconds: Number(process.hrtime.bigint() - startedAt) / 1e6 });
const state = { phase: 'pass', failure: null, droppedAcknowledgements: 0, blockedRetries: [], committed: null, delivered: null,
  restartRecovery: null };
let scope = null;
let firstBody = null;
let firstRecord = null;
let preparedForward = null;
const hopHeaders = new Set(['connection', 'keep-alive', 'proxy-authenticate', 'proxy-authorization', 'te', 'trailer', 'transfer-encoding', 'upgrade', 'content-length']);
async function readBody(stream) {
  const chunks = [];
  let bytes = 0;
  for await (const chunk of stream) {
    bytes += chunk.length;
    if (bytes > 2 * 1024 * 1024) throw new Error('BodyLimit');
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}
function filteredHeaders(headers) {
  const extra = new Set(String(headers.connection ?? '').toLowerCase().split(',').map(s => s.trim()));
  return Object.fromEntries(Object.entries(headers).filter(([name]) => !hopHeaders.has(name.toLowerCase()) && !extra.has(name.toLowerCase())));
}
function fail(code) { state.failure ??= code; console.error(`ITEM_PROXY_FAIL ${code}`); }
function json(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(value));
}
async function forward(request, response) {
  if (!request.url.startsWith('/api/') || request.url.startsWith('//')) { response.destroy(); return; }
  if (preparedForward && digest(request.headers.authorization ?? '') === scope.authorizationSHA256) {
    await preparedForward(request, response); return;
  }
  const body = await readBody(request);
  let restartRead = null;
  if (state.restartRecovery && digest(request.headers.authorization ?? '') === scope.authorizationSHA256) {
    if (state.failure) { response.destroy(); return; }
    if (request.method !== 'GET' || body.length !== 0) {
      state.restartRecovery.rejectedWrites++;
      fail('WriteDuringRestartRecovery'); response.destroy(); return;
    }
    const url = new URL(request.url, 'http://127.0.0.1');
    if (url.pathname === `/api/item-transactions/${state.committed.requestId}` && !url.search) {
      restartRead = { kind: 'receipt', requestId: state.committed.requestId };
    } else if (url.pathname === '/api/items' && url.searchParams.get('ownerType') === 'Character'
      && scope.characterIds.includes(url.searchParams.get('ownerId')) && url.searchParams.get('includeTerminal') === 'false') {
      restartRead = { kind: 'inventory', characterId: url.searchParams.get('ownerId') };
    } else { fail('UnexpectedRestartRead'); response.destroy(); return; }
  }
  let command = null;
  if (scope && request.method === 'POST' && request.url === '/api/item-transactions/commit'
    && digest(request.headers.authorization ?? '') === scope.authorizationSHA256) {
    // Once this fixture token has selected its sole grant, every subsequent
    // commit must be the exact frozen request, even if operation/actor changed.
    if (state.failure) { response.destroy(); return; }
    if (firstBody && !body.equals(firstBody)) { fail('FrozenCommandChanged'); response.destroy(); return; }
    const document = JSON.parse(body.toString('utf8'));
    if (document.operation === 'Item.Pickup' && document.actor?.type === 'Character'
      && scope.characterIds.includes(document.actor.ownerId)) {
      if (!validId(document.requestId) || document.affectedQuantity !== 1 || document.mutations?.length !== 1
        || !validId(document.mutations[0].newRecord?.state?.instanceId)) { fail('InvalidScopedCommand'); response.destroy(); return; }
      command = document;
    }
  }
  if (command) {
    if (state.phase === 'blocked') {
      state.blockedRetries.push({ requestId: command.requestId, bodySHA256: digest(body), ...observedTime() });
      response.destroy(); return;
    }
    if (state.phase === 'armed') {
      firstBody = body;
      state.phase = 'committing';
    } else if (state.phase !== 'released' && state.phase !== 'recovered') {
      fail('ConcurrentScopedCommit'); response.destroy(); return;
    }
  }
  const headers = filteredHeaders(request.headers);
  headers.host = `127.0.0.1:${upstreamPort}`;
  headers['content-length'] = body.length;
  headers.connection = 'close';
  const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort, path: request.url, method: request.method, headers });
  upstream.on('error', () => { if (command) fail('UpstreamTransport'); response.destroy(); });
  upstream.setTimeout(15000, () => upstream.destroy());
  response.on('close', () => { if (!response.writableEnded) upstream.destroy(); });
  upstream.on('response', upstreamResponse => {
    void (async () => {
      const responseBody = await readBody(upstreamResponse);
      let receipt = null;
      if (restartRead?.kind === 'receipt') {
        const restored = JSON.parse(responseBody.toString('utf8'));
        const original = JSON.parse(firstBody.toString('utf8'));
        if (upstreamResponse.statusCode !== 200 || !['Committed', 'AlreadyCommitted'].includes(restored.status)
          || restored.requestId !== original.requestId || restored.operation !== original.operation
          || restored.commandFingerprint !== original.commandFingerprint || restored.actor?.type !== 'Character'
          || restored.actor.ownerId !== original.actor.ownerId || restored.affectedQuantity !== 1
          || restored.records?.length !== 1 || JSON.stringify(restored.records[0]) !== firstRecord) {
          fail('RestartReceiptMismatch'); response.destroy(); return;
        }
        restartRead.recordSHA256 = digest(firstRecord);
      }
      if (command) {
        receipt = JSON.parse(responseBody.toString('utf8'));
        const record = receipt.records?.[0];
        if (upstreamResponse.statusCode !== 200 || !['Committed', 'AlreadyCommitted'].includes(receipt.status)
          || receipt.requestId !== command.requestId || receipt.operation !== command.operation
          || receipt.commandFingerprint !== command.commandFingerprint || receipt.actor?.ownerId !== command.actor.ownerId
          || receipt.affectedQuantity !== 1 || receipt.records?.length !== 1 || record?.revision !== 1
          || record.state?.quantity !== 1 || record.state.instanceId !== command.mutations[0].newRecord.state.instanceId
          || record.owner?.type !== 'Character' || record.owner.ownerId !== command.actor.ownerId) {
          fail('ReceiptMismatch'); response.destroy(); return;
        }
        if (state.phase === 'committing') {
          if (receipt.status !== 'Committed') { fail('FirstCommitWasReplay'); response.destroy(); return; }
          firstRecord = JSON.stringify(record);
          state.committed = { requestId: command.requestId, characterId: command.actor.ownerId,
            itemId: record.state.instanceId, definition: record.definitionName, bodySHA256: digest(body),
            recordSHA256: digest(firstRecord), upstreamStatus: receipt.status, ...observedTime() };
          state.droppedAcknowledgements++;
          state.phase = 'blocked';
          // Complete upstream 200 means durable commit; send no bytes to UE.
          response.destroy(); return;
        }
        if (receipt.status !== 'AlreadyCommitted' || JSON.stringify(record) !== firstRecord) {
          fail('ReplayChangedRecord'); response.destroy(); return;
        }
      }
      const responseHeaders = filteredHeaders(upstreamResponse.headers);
      responseHeaders['content-length'] = responseBody.length;
      response.writeHead(upstreamResponse.statusCode, responseHeaders);
      response.end(responseBody, () => {
        if (restartRead) state.restartRecovery.reads.push({ ...restartRead, statusCode: upstreamResponse.statusCode, ...observedTime() });
        if (receipt) {
          state.delivered = { requestId: receipt.requestId, itemId: receipt.records[0].state.instanceId,
            bodySHA256: digest(body), upstreamStatus: receipt.status, ...observedTime() };
          state.phase = 'recovered';
        }
      });
    })().catch(() => { if (command) fail('InvalidUpstreamResponse'); response.destroy(); });
  });
  upstream.end(body);
}
async function control(request, response) {
  const supplied = Buffer.from(request.headers.authorization ?? '');
  const expected = Buffer.from(`Bearer ${token}`);
  if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected)) { json(response, 403, { error: 'Forbidden' }); return; }
  if (request.method === 'GET' && request.url === '/state') { json(response, 200, state); return; }
  if (request.method === 'POST' && request.url === '/arm') {
    const document = JSON.parse((await readBody(request)).toString('utf8'));
    if (document.protocol === 'prepared') {
      if (scope || !Array.isArray(document.characterIds) || document.characterIds.length < 1 || document.characterIds.length > 4
        || !document.characterIds.every(validId) || new Set(document.characterIds).size !== document.characterIds.length
        || !validId(document.sessionId) || typeof document.serverId !== 'string' || !document.serverId.trim()
        || document.serverId.length > 128 || /[\x00-\x1f\x7f]/.test(document.serverId)
        || !/^[0-9a-f]{64}$/.test(document.authorizationSHA256)) { json(response, 400, { error: 'Invalid prepared scope' }); return; }
      const observer = createPreparedObservation({ scope: document, upstreamPort,
        journalRoot: process.env.PROJECT_RPG_ITEM_PROXY_JOURNAL_ROOT, state, fail, readBody, filteredHeaders, observedTime });
      scope = document; preparedForward = observer; state.phase = 'observing'; json(response, 200, state); return;
    }
    if (scope || !Array.isArray(document.characterIds) || document.characterIds.length !== 2
      || !document.characterIds.every(validId) || new Set(document.characterIds).size !== 2
      || !/^[0-9a-f]{64}$/.test(document.authorizationSHA256)) { json(response, 400, { error: 'Invalid fixture scope' }); return; }
    scope = document;
    state.phase = 'armed';
    json(response, 200, state); return;
  }
  if (request.method === 'POST' && request.url === '/release') {
    if (preparedForward) {
      json(response, preparedForward.release() ? 200 : 409, state); return;
    }
    if (state.failure || state.phase !== 'blocked' || state.droppedAcknowledgements !== 1 || state.blockedRetries.length < 4) {
      json(response, 409, { error: 'Unverified recovery boundary' }); return;
    }
    state.phase = 'released';
    json(response, 200, state); return;
  }
  if (request.method === 'POST' && request.url === '/slot-conflict-ready') {
    json(response, preparedForward?.slotReady() ? 200 : 409, state); return;
  }
  if (request.method === 'POST' && request.url === '/slot-conflict-resume') {
    json(response, preparedForward?.slotResume() ? 200 : 409, state); return;
  }
  if (request.method === 'POST' && request.url === '/revision-conflict-ready') {
    json(response, preparedForward?.revisionReady() ? 200 : 409, state); return;
  }
  if (request.method === 'POST' && request.url === '/restart-recovery') {
    if (preparedForward) { json(response, preparedForward.restart() ? 200 : 409, state); return; }
    if (state.failure || state.phase !== 'blocked' || state.droppedAcknowledgements !== 1 || state.blockedRetries.length < 4) {
      json(response, 409, { error: 'Unverified restart boundary' }); return;
    }
    state.restartRecovery = { ...observedTime(), reads: [], rejectedWrites: 0 };
    state.phase = 'restart-read-only';
    json(response, 200, state); return;
  }
  json(response, 404, { error: 'Not found' });
}
const proxy = http.createServer((request, response) => {
  void forward(request, response).catch(() => { fail('InvalidRequest'); response.destroy(); });
});
const controls = http.createServer((request, response) => {
  void control(request, response).catch(() => json(response, 400, { error: 'Invalid control request' }));
});
for (const server of [proxy, controls]) {
  server.on('error', () => { console.error('ITEM_PROXY_FAIL ListenerFailure'); process.exit(1); });
}
proxy.listen(listenPort, '127.0.0.1', () => {
  controls.listen(0, '127.0.0.1', () => console.log(`ITEM_PROXY_READY ControlPort=${controls.address().port} ProxyPort=${proxy.address().port}`));
});
