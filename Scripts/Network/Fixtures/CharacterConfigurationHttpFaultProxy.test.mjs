import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { startCharacterConfigurationProxy } from './CharacterConfigurationHttpFaultProxy.mjs';

const character = '11111111-1111-4111-8111-111111111111';
const other = '22222222-2222-4222-8222-222222222222';
const missing = '55555555-5555-4555-8555-555555555555';
const requestId = '33333333-3333-4333-8333-333333333333';
const command = JSON.stringify({ requestId, expectedRevision: '2', skills: [], skillSlots: [] });
const controlToken = 'configuration-test-control-token-at-least-32';
const credential = ['Bearer read-fixture', 'Bearer write-fixture', 'Bearer renewed-fixture'];
function send(port, path, method = 'GET', authorization = '', body = '') {
  return new Promise((resolve, reject) => {
    const request = http.request({ hostname: '127.0.0.1', port, path, method,
      headers: { authorization, 'content-type': 'application/json', 'content-length': Buffer.byteLength(body) } }, response => {
      const parts = []; response.on('data', part => parts.push(part));
      response.on('end', () => resolve({ code: response.statusCode, body: Buffer.concat(parts).toString('utf8') }));
      response.on('error', reject);
    });
    request.on('error', reject); request.end(body);
  });
}
async function setup(firstWriteStatus = 200, scenario = 'Recovery') {
  const valid = new Set(credential); let commits = 0, writes = 0;
  const backend = http.createServer((request, response) => {
    const parts = []; request.on('data', part => parts.push(part));
    request.on('end', () => {
      if (!valid.has(request.headers.authorization)) { response.writeHead(401); return response.end('{}'); }
      if (request.url === '/api/auth/logout') { valid.delete(request.headers.authorization); response.writeHead(204); return response.end(); }
      if (scenario === 'ReadFailures' && request.method === 'GET') {
        if (request.url === `/api/characters/${other}/progression`) { response.writeHead(403); return response.end('{}'); }
        if (request.url === `/api/characters/${missing}/progression`) { response.writeHead(404); return response.end('{}'); }
        if (request.url === '/api/character-creation-options') {
          response.writeHead(200, { 'content-type': 'application/json' });
          return response.end('[{"classId":"RPGPlayerClassDefinition:GreatSwordWarrior","displayName":"Warrior","description":"Fixture"}]');
        }
      }
      if (request.method === 'PUT') {
        ++writes;
        if (scenario === 'Conflict' && writes === 1) {
          response.writeHead(409, { 'content-type': 'application/json' });
          return response.end('{"error":"stale_progression_revision"}');
        }
        if (firstWriteStatus !== 200) { response.writeHead(firstWriteStatus); return response.end('{}'); }
        if (commits === 0) ++commits;
        response.writeHead(200, { 'content-type': 'application/json' }); return response.end(JSON.stringify({ revision: scenario === 'Conflict' ? '4' : '3', requestId }));
      }
      response.writeHead(200, { 'content-type': 'application/json' });
      response.end(JSON.stringify({ revision: scenario === 'Conflict' && writes > 0 ? (commits ? '4' : '3') : '2' }));
    });
  });
  await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
  const proxy = await startCharacterConfigurationProxy({ token: controlToken, upstreamPort: backend.address().port });
  const control = async (path, body) => {
    const response = await send(proxy.controlPort, path, body === undefined ? 'GET' : 'POST', `Bearer ${controlToken}`, body === undefined ? '' : JSON.stringify(body));
    assert.equal(response.code, 200); return JSON.parse(response.body);
  };
  await control('/arm', { characterId: character, scenario,
    ...(scenario === 'ReadFailures' ? { forbiddenCharacterId: other, missingCharacterId: missing } : {}) });
  return { proxy, control, counts: () => ({ commits, writes }),
    close: async () => { await proxy.close(); backend.closeAllConnections(); await new Promise(resolve => backend.close(resolve)); } };
}
async function readRecovery(fixture) {
  assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 200);
  await fixture.control('/revoke-read', {});
  assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 401);
}

test('committed response is dropped once, retired tokens get 401, only an identical renewed command replays', async () => {
  const fixture = await setup();
  try {
    assert.equal((await send(fixture.proxy.port, `/api/characters/${other}/skill-configuration`, 'PUT', credential[1], command)).code, 403);
    await readRecovery(fixture);
    await assert.rejects(send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], command));
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], command)).code, 503);
    await fixture.control('/revoke-write', {});
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], command)).code, 401);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[2], command)).code, 200);
    const state = await fixture.control('/state');
    assert.equal(state.failure, null); assert.equal(state.phase, 'replayed');
    assert.equal(state.droppedAcknowledgements, 1); assert.equal(state.readUnauthorized, 1); assert.equal(state.writeUnauthorized, 1);
    assert.equal(state.upstreamWrites, 3); assert.equal(state.replayed, 1); assert.equal(state.replayCredentialChanged, true);
    assert.equal(state.committedResponseSHA256, state.replayResponseSHA256);
    assert.deepEqual(fixture.counts(), { commits: 1, writes: 2 });
    assert.equal(JSON.stringify(state).includes('Bearer'), false);
  } finally { await fixture.close(); }
});

test('changed retry body cannot reach the upstream', async () => {
  const fixture = await setup();
  try {
    await readRecovery(fixture);
    await assert.rejects(send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], command));
    const changed = JSON.stringify({ ...JSON.parse(command), expectedRevision: '3' });
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], changed)).code, 409);
    const state = await fixture.control('/state');
    assert.equal(state.failure, 'RetryCommandChanged'); assert.equal(state.upstreamWrites, 1);
    assert.deepEqual(fixture.counts(), { commits: 1, writes: 1 });
  } finally { await fixture.close(); }
});

test('an upstream rejection is never counted as a lost committed acknowledgement', async () => {
  const fixture = await setup(500);
  try {
    await readRecovery(fixture);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[1], command)).code, 500);
    const state = await fixture.control('/state');
    assert.equal(state.failure, 'FirstCommitRejected'); assert.equal(state.droppedAcknowledgements, 0);
    assert.deepEqual(fixture.counts(), { commits: 0, writes: 1 });
  } finally { await fixture.close(); }
});

test('real revision denial requires a fresh read and a new command based on that revision', async () => {
  const fixture = await setup(200, 'Conflict');
  try {
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 200);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], command)).code, 409);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 200);
    const renewedId = '44444444-4444-4444-8444-444444444444';
    const renewed = JSON.stringify({ ...JSON.parse(command), requestId: renewedId, expectedRevision: '3' });
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], renewed)).code, 200);
    const state = await fixture.control('/state');
    assert.equal(state.failure, null); assert.equal(state.phase, 'conflict-resolved');
    assert.equal(state.conflictedWrites, 1); assert.equal(state.successfulWrites, 1); assert.equal(state.upstreamWrites, 2);
    assert.equal(state.conflictRequestId, requestId); assert.equal(state.successfulRequestId, renewedId);
    assert.equal(state.conflictExpectedRevision, '2'); assert.equal(state.conflictReloadRevision, '3');
    assert.equal(state.successfulExpectedRevision, '3'); assert.equal(state.successfulRevision, '4');
    assert.equal(state.droppedAcknowledgements, 0); assert.deepEqual(fixture.counts(), { commits: 1, writes: 2 });
  } finally { await fixture.close(); }
});

test('a denied command ID cannot be repurposed after reading the new revision', async () => {
  const fixture = await setup(200, 'Conflict');
  try {
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], command)).code, 409);
    await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0]);
    const reused = JSON.stringify({ ...JSON.parse(command), expectedRevision: '3' });
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], reused)).code, 409);
    const state = await fixture.control('/state');
    assert.equal(state.failure, 'ConflictNewCommandRequired'); assert.equal(state.upstreamWrites, 1);
    assert.deepEqual(fixture.counts(), { commits: 0, writes: 1 });
  } finally { await fixture.close(); }
});

async function recoverCatalog(fixture) {
  assert.equal((await send(fixture.proxy.port, '/api/character-creation-options', 'GET', credential[0])).code, 503);
  await fixture.control('/release-catalog', {});
  assert.equal((await send(fixture.proxy.port, '/api/character-creation-options', 'GET', credential[0])).code, 200);
  await fixture.control('/empty-catalog', {});
  const empty = await send(fixture.proxy.port, '/api/character-creation-options', 'GET', credential[0]);
  assert.equal(empty.code, 200); assert.deepEqual(JSON.parse(empty.body), []);
  await fixture.control('/release-catalog', {});
  assert.equal((await send(fixture.proxy.port, '/api/character-creation-options', 'GET', credential[0])).code, 200);
}

test('catalog failure/empty results and scoped backend 403/404 recover before one explicit write', async () => {
  const fixture = await setup(200, 'ReadFailures');
  try {
    await recoverCatalog(fixture);
    await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0]);
    await fixture.control('/deny-read', {});
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 403);
    await fixture.control('/missing-read', {});
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 404);
    await fixture.control('/release-read', {});
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0])).code, 200);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], command)).code, 200);
    const state = await fixture.control('/state');
    assert.equal(state.failure, null); assert.equal(state.phase, 'lookups-resolved');
    assert.equal(state.catalogUnavailableReplies, 1); assert.equal(state.catalogEmptyReplies, 1); assert.equal(state.catalogSuccessfulReads, 2);
    assert.equal(state.progressionForbiddenReads, 1); assert.equal(state.progressionMissingReads, 1); assert.equal(state.progressionReadRecovered, true);
    assert.equal(state.creationPosts, 0); assert.equal(state.upstreamWrites, 1); assert.equal(state.successfulWrites, 1);
    assert.deepEqual(fixture.counts(), { commits: 1, writes: 1 });
  } finally { await fixture.close(); }
});

test('catalog lookup failure cannot be bypassed by a creation POST', async () => {
  const fixture = await setup(200, 'ReadFailures');
  try {
    assert.equal((await send(fixture.proxy.port, '/api/characters', 'POST', credential[0], '{"name":"DoNotCreate"}')).code, 409);
    const state = await fixture.control('/state');
    assert.equal(state.failure, 'UnexpectedCharacterCreation'); assert.equal(state.creationPosts, 1);
    assert.deepEqual(fixture.counts(), { commits: 0, writes: 0 });
  } finally { await fixture.close(); }
});

test('read denial does not permit an unconfirmed configuration write', async () => {
  const fixture = await setup(200, 'ReadFailures');
  try {
    await recoverCatalog(fixture);
    await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0]);
    await fixture.control('/deny-read', {});
    await send(fixture.proxy.port, `/api/characters/${character}/progression`, 'GET', credential[0]);
    assert.equal((await send(fixture.proxy.port, `/api/characters/${character}/skill-configuration`, 'PUT', credential[0], command)).code, 409);
    const state = await fixture.control('/state');
    assert.equal(state.failure, 'WriteBeforeReadRecovery'); assert.equal(state.upstreamWrites, 0);
    assert.deepEqual(fixture.counts(), { commits: 0, writes: 0 });
  } finally { await fixture.close(); }
});
