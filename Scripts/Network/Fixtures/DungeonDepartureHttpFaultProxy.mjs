// Loopback-only fixture. Drop before forwarding, or lose one committed reply; hold scoped reads,
// revoke only that request's own credential via the real logout API, and compare
// the original command/reply after the client's real 401 and same-account login.
// Credentials live only in memory; never log headers, bodies or token values.
import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';

const token = process.env.PROJECT_RPG_DEPARTURE_PROXY_TOKEN;
const upstreamPort = Number(process.env.PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT);
if (!token || token.length < 32 || !Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535)
  throw new Error('Invalid departure fixture configuration.');
const guid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const hash = value => createHash('sha256').update(value).digest('hex');
const state = { phase: 'idle', failure: null, dropped: 0, blockedReads: 0, blockedReplays: 0,
  departureRequestsForwarded: 0, unexpectedNewDepartures: 0, unauthorizedForwarded: 0,
  replayForwarded: 0, credentialRevoked: false, allowRetry: false, commandSHA256: null,
  originalReplySHA256: null, replayReplySHA256: null, changedCredentialOnReplay: false,
  operation: null, unexpectedCancellations: 0, allowColdRead: false,
  coldReadsForwarded: 0, coldReadChangedCredential: false, coldReadReplySHA256: null,
  lossPoint: 'AfterCommit', precommitDrops: 0, targetRequestsForwarded: 0, targetSuccesses: 0 };
let scope = null, originalAuthorization = null, command = null;
function fail(code) { state.failure ??= code; console.error(`DEPARTURE_PROXY_FAIL ${code}`); }
function send(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(value));
}
async function readBody(request, maximum = 65536) {
  const parts = []; let size = 0;
  for await (const part of request) {
    size += part.length;
    if (size > maximum) throw new Error('BodyLimit');
    parts.push(part);
  }
  return Buffer.concat(parts);
}
function authorized(request) {
  const actual = Buffer.from(request.headers.authorization ?? '');
  const expected = Buffer.from(`Bearer ${token}`);
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}
function canonical(body) {
  const value = JSON.parse(body.toString('utf8'));
  if (Object.keys(value).sort().join(',') !== 'characterId,expectedLobbyRevision,requestId'
    || !guid.test(value.characterId) || !guid.test(value.requestId)
    || !Number.isSafeInteger(value.expectedLobbyRevision) || value.expectedLobbyRevision < 1)
    throw new Error('InvalidCommand');
  if (value.characterId !== scope.characterId) throw new Error('WrongCharacter');
  return JSON.stringify({ characterId: value.characterId, requestId: value.requestId, expectedLobbyRevision: value.expectedLobbyRevision });
}
function upstream(method, path, headers, body) {
  return new Promise((resolve, reject) => {
    const request = http.request({ hostname: '127.0.0.1', port: upstreamPort, method, path,
      headers: { ...headers, host: `127.0.0.1:${upstreamPort}`, connection: 'close' } }, response => {
      void readBody(response).then(bytes => resolve({ status: response.statusCode, headers: response.headers, body: bytes }), reject);
    });
    request.setTimeout(10000, () => request.destroy(new Error('UpstreamTimeout')));
    request.on('error', reject); request.end(body);
  });
}
function forward(response, result) { response.writeHead(result.status, result.headers); response.end(result.body); }

const control = http.createServer((request, response) => {
  void (async () => {
    if (!authorized(request)) { send(response, 403, { error: 'Forbidden' }); return; }
    if (request.method === 'GET' && request.url === '/state') { send(response, 200, state); return; }
    if (request.method === 'POST' && request.url === '/arm' && !scope) {
      const value = JSON.parse((await readBody(request, 4096)).toString('utf8'));
      const operation = value.operation ?? 'Cancel';
      const lossPoint = value.lossPoint ?? 'AfterCommit';
      if (!guid.test(value.sessionId) || !guid.test(value.characterId) || !['Request', 'Cancel'].includes(operation)
        || !['BeforeCommit', 'AfterCommit'].includes(lossPoint)) {
        send(response, 400, { error: 'InvalidScope' }); return;
      }
      scope = { sessionId: value.sessionId, characterId: value.characterId,
        base: `/api/dungeon-sessions/${value.sessionId}/departure`,
        target: `/api/dungeon-sessions/${value.sessionId}/departure${operation === 'Cancel' ? '/cancel' : ''}`,
        read: `/api/dungeon-sessions/${value.sessionId}`,
        active: `/api/characters/${value.characterId}/active-dungeon-session` };
      state.operation = operation;
      state.lossPoint = lossPoint;
      state.phase = 'armed'; send(response, 200, state); return;
    }
    if (request.method === 'POST' && request.url === '/revoke' && state.phase === 'dropped'
      && state.lossPoint === 'AfterCommit' && originalAuthorization && !state.credentialRevoked && !state.allowColdRead) {
      await readBody(request, 4096);
      const result = await upstream('POST', '/api/auth/logout', { authorization: originalAuthorization, 'content-length': '0' }, Buffer.alloc(0));
      if (result.status !== 204) { fail('CredentialRevokeRejected'); send(response, 502, { error: 'RevokeRejected' }); return; }
      state.credentialRevoked = true; state.allowRetry = true;
      send(response, 200, state); return;
    }
    if (request.method === 'POST' && request.url === '/cold-read' && state.phase === 'dropped'
      && originalAuthorization && !state.allowRetry && !state.allowColdRead) {
      await readBody(request, 4096);
      state.allowColdRead = true;
      send(response, 200, state); return;
    }
    if (request.method === 'POST' && request.url === '/cold-retry' && state.phase === 'dropped'
      && state.lossPoint === 'BeforeCommit' && state.allowColdRead && state.coldReadsForwarded > 0
      && state.coldReadChangedCredential && !state.allowRetry) {
      await readBody(request, 4096);
      state.allowRetry = true;
      send(response, 200, state); return;
    }
    send(response, 409, { error: 'InvalidControlState' });
  })().catch(() => { fail('ControlTransport'); send(response, 502, { error: 'ControlFailure' }); });
});

const proxy = http.createServer((request, response) => {
  void (async () => {
    if (!request.url?.startsWith('/api/')) { send(response, 404, { error: 'UnknownRoute' }); return; }
    const body = await readBody(request);
    const afterDrop = state.dropped === 1;
    if (scope && afterDrop && state.phase !== 'replayed' && request.method === 'GET' && [scope.read, scope.active].includes(request.url)) {
      if (!state.allowColdRead) { state.blockedReads++; send(response, 503, { error: 'FixtureReadHeld' }); return; }
      const result = await upstream(request.method, request.url, request.headers, body);
      if (result.status === 200) {
        state.coldReadsForwarded++;
        state.coldReadChangedCredential ||= request.headers.authorization !== originalAuthorization;
        state.coldReadReplySHA256 = hash(result.body);
      }
      forward(response, result); return;
    }
    const target = scope && request.method === 'POST' && request.url === scope.target;
    if (scope && request.method === 'POST' && request.url === scope.base && !target) {
      if (afterDrop) { state.unexpectedNewDepartures++; fail('UnexpectedNewDeparture'); send(response, 503, { error: 'NewCommandDenied' }); return; }
      state.departureRequestsForwarded++;
    }
    if (scope && afterDrop && state.phase !== 'replayed' && state.operation === 'Request'
      && request.method === 'POST' && request.url === `${scope.base}/cancel`) {
      state.unexpectedCancellations++; fail('UnexpectedCancellation'); send(response, 503, { error: 'NewCommandDenied' }); return;
    }
    if (!target) { forward(response, await upstream(request.method, request.url, request.headers, body)); return; }
    const currentCommand = canonical(body);
    if (!/^Bearer \S+$/.test(request.headers.authorization ?? '')) throw new Error('NoCredential');
    if (state.phase === 'armed') {
      state.phase = 'forwarding'; command = currentCommand; state.commandSHA256 = hash(command);
      originalAuthorization = request.headers.authorization;
      if (state.lossPoint === 'BeforeCommit') {
        state.precommitDrops = 1; state.dropped = 1; state.phase = 'dropped';
        console.log('DEPARTURE_PROXY_UNFORWARDED_REQUEST_DROPPED');
        response.destroy(); return;
      }
      if (state.operation === 'Request') state.departureRequestsForwarded++;
      state.targetRequestsForwarded++;
      const result = await upstream(request.method, request.url, request.headers, body);
      if (result.status !== 200) { fail('InitialDepartureRejected'); forward(response, result); return; }
      state.targetSuccesses++;
      state.originalReplySHA256 = hash(result.body); state.dropped = 1; state.phase = 'dropped';
      console.log('DEPARTURE_PROXY_COMMITTED_REPLY_DROPPED');
      response.destroy(); return;
    }
    if (currentCommand !== command) {
      if (state.operation === 'Request') state.unexpectedNewDepartures++;
      fail('CommandIdentityChanged'); send(response, 409, { error: 'CommandChanged' }); return;
    }
    if (!state.allowRetry) { state.blockedReplays++; send(response, 503, { error: 'FixtureReplayHeld' }); return; }
    if (state.lossPoint === 'BeforeCommit' && state.operation === 'Request') state.departureRequestsForwarded++;
    state.targetRequestsForwarded++;
    const result = await upstream(request.method, request.url, request.headers, body);
    if (result.status === 401) { state.unauthorizedForwarded++; forward(response, result); return; }
    if (result.status !== 200 || (state.lossPoint === 'AfterCommit' && hash(result.body) !== state.originalReplySHA256)) {
      fail('ReplayResultChanged'); send(response, 502, { error: 'ReplayMismatch' }); return;
    }
    state.targetSuccesses++;
    state.replayForwarded++; state.replayReplySHA256 = hash(result.body);
    state.changedCredentialOnReplay = request.headers.authorization !== originalAuthorization;
    state.phase = 'replayed'; originalAuthorization = null;
    console.log('DEPARTURE_PROXY_ORIGINAL_COMMAND_REPLAYED');
    forward(response, result);
  })().catch(() => { fail('ProxyTransportOrScope'); response.destroy(); });
});

await new Promise(resolve => control.listen(0, '127.0.0.1', resolve));
await new Promise(resolve => proxy.listen(0, '127.0.0.1', resolve));
console.log(`DEPARTURE_PROXY_READY ProxyPort=${proxy.address().port} ControlPort=${control.address().port}`);
const close = () => { originalAuthorization = null; proxy.close(); control.close(); setTimeout(() => process.exit(0), 300).unref(); };
process.once('SIGTERM', close); process.once('SIGINT', close);
