// Opt-in loopback fixture. Tokens and full request/response bodies stay in memory.
import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { pathToFileURL } from 'node:url';

const sha = value => createHash('sha256').update(value).digest('hex');
const guid = value => typeof value === 'string' && /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(value)
  && value !== '00000000-0000-0000-0000-000000000000';
const headers = source => Object.fromEntries(Object.entries(source).filter(([key]) =>
  !['host', 'connection', 'keep-alive', 'transfer-encoding', 'content-length'].includes(key.toLowerCase())));
const json = (response, status, value) => {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(value));
};
async function read(stream, limit = 262144) {
  const parts = []; let length = 0;
  for await (const part of stream) { length += part.length; if (length > limit) throw new Error('BodyLimit'); parts.push(part); }
  return Buffer.concat(parts);
}

export async function startCharacterConfigurationProxy({ token, upstreamPort, listenPort = 0, log = () => {} }) {
  if (typeof token !== 'string' || token.length < 32 || !Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535
    || !Number.isInteger(listenPort) || listenPort < 0 || listenPort > 65535) throw new Error('Invalid loopback fixture configuration');
  const state = { phase: 'idle', scenario: null, failure: null, firstReadObserved: false, readCredentialRevoked: false,
    readUnauthorized: 0, droppedAcknowledgements: 0, blockedRetries: 0, writeCredentialRevoked: false,
    writeUnauthorized: 0, replayed: 0, replayCredentialChanged: false, requestId: null,
    commandSHA256: null, committedResponseSHA256: null, replayResponseSHA256: null, upstreamWrites: 0,
    conflictedWrites: 0, conflictRequestId: null, conflictExpectedRevision: null, conflictReloadRevision: null,
    successfulWrites: 0, successfulRequestId: null, successfulExpectedRevision: null, successfulRevision: null,
    catalogMode: 'unavailable', catalogUnavailableReplies: 0, catalogEmptyReplies: 0, catalogSuccessfulReads: 0,
    progressionReadMode: 'pass', progressionForbiddenReads: 0, progressionMissingReads: 0,
    progressionReadRecovered: false, recoveredReadRevision: null, creationPosts: 0 };
  let scope, readAuthorization, writeAuthorization, commandBody;
  const fail = code => { state.failure ??= code; state.phase = 'failed'; log(`CONFIGURATION_PROXY_FAIL ${code}`); };
  const authorized = request => {
    const actual = Buffer.from(request.headers.authorization ?? ''), expected = Buffer.from(`Bearer ${token}`);
    return actual.length === expected.length && timingSafeEqual(actual, expected);
  };
  const upstream = (method, path, sourceHeaders, body) => new Promise((resolve, reject) => {
    const request = http.request({ hostname: '127.0.0.1', port: upstreamPort, method, path,
      headers: { ...headers(sourceHeaders), host: `127.0.0.1:${upstreamPort}`, connection: 'close', 'content-length': body.length } }, response => {
      void read(response).then(bytes => resolve({ status: response.statusCode, headers: headers(response.headers), body: bytes }), reject);
    });
    request.setTimeout(10000, () => request.destroy(new Error('UpstreamTimeout')));
    request.on('error', reject); request.end(body);
  });
  const forward = (response, result) => {
    response.writeHead(result.status, { ...result.headers, 'content-length': result.body.length }); response.end(result.body);
  };
  const control = http.createServer((request, response) => {
    void (async () => {
      if (!authorized(request)) return json(response, 403, { error: 'Forbidden' });
      if (request.method === 'GET' && request.url === '/state') return json(response, 200, state);
      if (request.method === 'POST' && request.url === '/arm' && !scope) {
        const body = JSON.parse((await read(request, 4096)).toString('utf8'));
        const scenario = body.scenario ?? 'Recovery';
        if (!guid(body.characterId) || !['Recovery', 'Conflict', 'ReadFailures'].includes(scenario)) return json(response, 400, { error: 'Scope' });
        if (scenario === 'ReadFailures' && (!guid(body.forbiddenCharacterId) || !guid(body.missingCharacterId)
          || new Set([body.characterId, body.forbiddenCharacterId, body.missingCharacterId]).size !== 3))
          return json(response, 400, { error: 'ReadScope' });
        scope = { read: `/api/characters/${body.characterId}/progression`, write: `/api/characters/${body.characterId}/skill-configuration`, scenario };
        if (scenario === 'ReadFailures') {
          scope.forbiddenRead = `/api/characters/${body.forbiddenCharacterId}/progression`;
          scope.missingRead = `/api/characters/${body.missingCharacterId}/progression`;
        }
        state.scenario = scenario;
        state.phase = 'armed'; return json(response, 200, state);
      }
      if (request.method === 'POST' && scope?.scenario === 'ReadFailures' && !state.failure) {
        await read(request, 4096);
        if (request.url === '/release-catalog' && ((state.catalogMode === 'unavailable' && state.catalogUnavailableReplies === 1)
          || (state.catalogMode === 'empty' && state.catalogEmptyReplies === 1))) state.catalogMode = 'pass';
        else if (request.url === '/empty-catalog' && state.catalogMode === 'pass' && state.catalogSuccessfulReads === 1)
          state.catalogMode = 'empty';
        else if (request.url === '/deny-read' && state.firstReadObserved && state.catalogSuccessfulReads === 2
          && state.progressionReadMode === 'pass') state.progressionReadMode = 'forbidden';
        else if (request.url === '/missing-read' && state.progressionReadMode === 'forbidden' && state.progressionForbiddenReads === 1)
          state.progressionReadMode = 'missing';
        else if (request.url === '/release-read' && state.progressionReadMode === 'missing' && state.progressionMissingReads === 1)
          state.progressionReadMode = 'released';
        else return json(response, 409, { error: 'ReadControlState' });
        return json(response, 200, state);
      }
      const revokeRead = request.url === '/revoke-read' && state.firstReadObserved && !state.readCredentialRevoked;
      const revokeWrite = request.url === '/revoke-write' && state.phase === 'dropped' && !state.writeCredentialRevoked;
      if (request.method === 'POST' && scope?.scenario === 'Recovery' && !state.failure && (revokeRead || revokeWrite)) {
        await read(request, 4096);
        const result = await upstream('POST', '/api/auth/logout', { authorization: revokeRead ? readAuthorization : writeAuthorization }, Buffer.alloc(0));
        if (result.status !== 204) { fail('RevokeRejected'); return json(response, 502, { error: 'RevokeRejected' }); }
        if (revokeRead) state.readCredentialRevoked = true; else state.writeCredentialRevoked = true;
        return json(response, 200, state);
      }
      json(response, 409, { error: 'ControlState' });
    })().catch(() => { fail('ControlTransport'); json(response, 502, { error: 'ControlFailure' }); });
  });
  const proxy = http.createServer((request, response) => {
    void (async () => {
      if (!scope || state.failure || !request.url?.startsWith('/api/')) return json(response, 403, { error: 'Scope' });
      const body = await read(request), authorization = request.headers.authorization ?? '';
      const write = request.method === 'PUT' && request.url === scope.write;
      if (scope.scenario === 'ReadFailures') {
        if (request.method === 'POST' && request.url === '/api/characters') {
          ++state.creationPosts; fail('UnexpectedCharacterCreation'); return json(response, 409, { error: 'UnexpectedCreation' });
        }
        if (request.method === 'GET' && request.url === '/api/character-creation-options') {
          if (state.catalogMode === 'unavailable') {
            ++state.catalogUnavailableReplies; return json(response, 503, { error: 'FixtureCatalogUnavailable' });
          }
          if (state.catalogMode === 'empty') { ++state.catalogEmptyReplies; return json(response, 200, []); }
          const result = await upstream(request.method, request.url, request.headers, body);
          if (result.status === 200) ++state.catalogSuccessfulReads; else fail('CatalogRecoveryRejected');
          return forward(response, result);
        }
        if (request.method === 'GET' && request.url === scope.read
          && ['forbidden', 'missing'].includes(state.progressionReadMode)) {
          const forbidden = state.progressionReadMode === 'forbidden';
          const result = await upstream('GET', forbidden ? scope.forbiddenRead : scope.missingRead, request.headers, body);
          if (result.status !== (forbidden ? 403 : 404)) fail('ExpectedBackendReadDenial');
          else if (forbidden) ++state.progressionForbiddenReads; else ++state.progressionMissingReads;
          return forward(response, result);
        }
      }
      if (request.method === 'PUT' && request.url.endsWith('/skill-configuration') && !write)
        return json(response, 403, { error: 'Scope' });
      if (write) {
        const command = JSON.parse(body.toString('utf8'));
        if (!guid(command.requestId)) { fail('RequestId'); return json(response, 409, { error: 'RequestId' }); }
        if (scope.scenario === 'ReadFailures') {
          if (!state.progressionReadRecovered || state.successfulWrites || command.expectedRevision !== state.recoveredReadRevision) {
            fail('WriteBeforeReadRecovery'); return json(response, 409, { error: 'ReadRecoveryRequired' });
          }
          ++state.upstreamWrites;
          const result = await upstream(request.method, request.url, request.headers, body);
          if (result.status !== 200) fail('ReadRecoveryWriteRejected');
          else {
            state.successfulWrites = 1; state.successfulRequestId = command.requestId;
            state.successfulExpectedRevision = command.expectedRevision;
            state.successfulRevision = JSON.parse(result.body.toString('utf8')).revision; state.phase = 'lookups-resolved';
          }
          return forward(response, result);
        }
        if (scope.scenario === 'Conflict') {
          if (!state.conflictedWrites) {
            ++state.upstreamWrites;
            const result = await upstream(request.method, request.url, request.headers, body);
            if (result.status !== 409 || JSON.parse(result.body.toString('utf8')).error !== 'stale_progression_revision')
              fail('ExpectedBackendRevisionConflict');
            else {
              state.conflictedWrites = 1; state.conflictRequestId = command.requestId;
              state.conflictExpectedRevision = command.expectedRevision;
              state.commandSHA256 = sha(body); state.phase = 'conflicted';
            }
            return forward(response, result);
          }
          if (!state.conflictReloadRevision || state.successfulWrites || command.requestId === state.conflictRequestId
            || command.expectedRevision !== state.conflictReloadRevision) {
            fail('ConflictNewCommandRequired'); return json(response, 409, { error: 'ConflictCommandState' });
          }
          ++state.upstreamWrites;
          const result = await upstream(request.method, request.url, request.headers, body);
          const record = JSON.parse(result.body.toString('utf8'));
          if (result.status !== 200 || Number(record.revision) !== Number(state.conflictReloadRevision) + 1)
            fail('ConflictResolutionRejected');
          else {
            state.successfulWrites = 1; state.successfulRequestId = command.requestId;
            state.successfulExpectedRevision = command.expectedRevision; state.successfulRevision = record.revision;
            state.phase = 'conflict-resolved';
          }
          return forward(response, result);
        }
        if (!commandBody) {
          if (state.readUnauthorized !== 1 || !authorization || authorization === readAuthorization) {
            fail('WriteBeforeReadRecovery'); return json(response, 409, { error: 'WriteState' });
          }
          commandBody = Buffer.from(body); writeAuthorization = authorization;
          state.requestId = command.requestId; state.commandSHA256 = sha(body);
        } else if (!commandBody.equals(body)) {
          fail('RetryCommandChanged'); return json(response, 409, { error: 'RetryChanged' });
        } else if (!state.writeCredentialRevoked) {
          ++state.blockedRetries; return json(response, 503, { error: 'FixtureRetryNotReleased' });
        } else if (state.replayed) {
          fail('UnexpectedWriteAfterReplay'); return json(response, 409, { error: 'UnexpectedWrite' });
        }
        ++state.upstreamWrites;
        const result = await upstream(request.method, request.url, request.headers, body);
        if (!state.droppedAcknowledgements) {
          if (result.status !== 200) { fail('FirstCommitRejected'); return forward(response, result); }
          state.committedResponseSHA256 = sha(result.body); state.droppedAcknowledgements = 1;
          state.phase = 'dropped'; response.destroy(); return;
        }
        if (authorization === writeAuthorization) {
          if (result.status !== 401) fail('RetiredWriteCredentialAccepted');
          else ++state.writeUnauthorized;
        } else {
          if (state.writeUnauthorized !== 1 || authorization === readAuthorization || result.status !== 200
            || sha(result.body) !== state.committedResponseSHA256) fail('ReplayReceiptMismatch');
          else {
            state.replayed = 1; state.replayCredentialChanged = true;
            state.replayResponseSHA256 = sha(result.body); state.phase = 'replayed';
          }
        }
        return forward(response, result);
      }
      const result = await upstream(request.method, request.url, request.headers, body);
      if (request.method === 'GET' && request.url === scope.read) {
        if (!state.firstReadObserved && result.status === 200) { readAuthorization = authorization; state.firstReadObserved = true; }
        if (state.readCredentialRevoked && authorization === readAuthorization && result.status === 401) ++state.readUnauthorized;
        if (scope.scenario === 'Conflict' && state.conflictedWrites === 1 && !state.successfulWrites && result.status === 200) {
          const record = JSON.parse(result.body.toString('utf8'));
          if (Number(record.revision) <= Number(state.conflictExpectedRevision)) fail('ConflictReloadNotNewer');
          else state.conflictReloadRevision = record.revision;
        }
        if (scope.scenario === 'ReadFailures' && state.progressionReadMode === 'released'
          && !state.successfulWrites && result.status === 200) {
          state.progressionReadRecovered = true;
          state.recoveredReadRevision = JSON.parse(result.body.toString('utf8')).revision; state.phase = 'reads-recovered';
        }
      }
      forward(response, result);
    })().catch(() => { fail('ProxyTransport'); if (!response.destroyed) json(response, 502, { error: 'ProxyFailure' }); });
  });
  const listen = (server, port) => new Promise((resolve, reject) => { server.once('error', reject); server.listen(port, '127.0.0.1', resolve); });
  try { await listen(control, 0); await listen(proxy, listenPort); }
  catch (error) { control.close(); proxy.close(); throw error; }
  return { port: proxy.address().port, controlPort: control.address().port,
    close: () => Promise.all([control, proxy].map(server => new Promise(resolve => { server.closeAllConnections(); server.close(resolve); }))) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const fixture = await startCharacterConfigurationProxy({ token: process.env.PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN,
    upstreamPort: Number(process.env.PROJECT_RPG_E2E_PROXY_UPSTREAM_PORT), listenPort: Number(process.env.PROJECT_RPG_E2E_PROXY_LISTEN_PORT ?? 0),
    log: message => process.stdout.write(message + '\n') });
  process.stdout.write(`CONFIGURATION_PROXY_READY listen=${fixture.port} control=${fixture.controlPort}\n`);
  process.on('SIGTERM', () => { void fixture.close().then(() => process.exit(0)); });
}
