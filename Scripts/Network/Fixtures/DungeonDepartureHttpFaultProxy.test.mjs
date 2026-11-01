// Fixture boundaries only; these tests do not establish backend or UE behavior.
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import http from 'node:http';
import { randomBytes, randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import test from 'node:test';

async function fixture(t, operation, lossPoint = 'AfterCommit') {
  const sessionId = randomUUID(), characterId = randomUUID();
  const controlToken = randomBytes(32).toString('hex');
  const oldToken = randomBytes(32).toString('hex'), newToken = randomBytes(32).toString('hex');
  const base = `/api/dungeon-sessions/${sessionId}/departure`;
  const target = base + (operation === 'Cancel' ? '/cancel' : '');
  const originalReply = JSON.stringify({ state: 'Waiting', queued: operation === 'Request' });
  let revoked = false, targetCalls = 0, trace = '';
  const upstream = http.createServer((request, response) => {
    request.resume();
    request.on('end', () => {
      if (request.url === '/api/auth/logout') {
        assert.equal(request.headers.authorization === `Bearer ${oldToken}`, true, 'Logout uses only the original credential');
        revoked = true; response.writeHead(204); response.end(); return;
      }
      if (request.method === 'POST' && request.url === target) {
        targetCalls++;
        if (revoked && request.headers.authorization === `Bearer ${oldToken}`) {
          response.writeHead(401); response.end('{"error":"Unauthorized"}'); return;
        }
        response.writeHead(200); response.end(originalReply); return;
      }
      response.writeHead(200); response.end('{}');
    });
  });
  await new Promise(resolve => upstream.listen(0, '127.0.0.1', resolve));
  t.after(async () => { upstream.closeAllConnections(); await new Promise(resolve => upstream.close(resolve)); });
  const child = spawn(process.execPath, [fileURLToPath(new URL('./DungeonDepartureHttpFaultProxy.mjs', import.meta.url))], {
    windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'],
    env: { SystemRoot: process.env.SystemRoot, PROJECT_RPG_DEPARTURE_PROXY_TOKEN: controlToken,
      PROJECT_RPG_DEPARTURE_PROXY_UPSTREAM_PORT: String(upstream.address().port) },
  });
  const exited = once(child, 'exit');
  t.after(async () => { if (child.exitCode === null) child.kill(); await exited; });
  child.stdout.on('data', bytes => { trace += bytes.toString(); });
  child.stderr.on('data', bytes => { trace += bytes.toString(); });
  const ports = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Proxy readiness timeout')), 5000);
    child.once('error', error => { clearTimeout(timeout); reject(error); });
    child.once('exit', () => { clearTimeout(timeout); reject(new Error('Proxy exited before readiness')); });
    child.stdout.on('data', () => {
      const match = trace.match(/DEPARTURE_PROXY_READY ProxyPort=(\d+) ControlPort=(\d+)/);
      if (match) { clearTimeout(timeout); resolve({ proxy: Number(match[1]), control: Number(match[2]) }); }
    });
  });
  async function control(path, body) {
    return fetch(`http://127.0.0.1:${ports.control}${path}`, {
      method: body ? 'POST' : 'GET', headers: { authorization: `Bearer ${controlToken}`, 'content-type': 'application/json' },
      ...(body ? { body: JSON.stringify(body) } : {}),
    });
  }
  async function send(path, body, credential = oldToken) {
    return fetch(`http://127.0.0.1:${ports.proxy}${path}`, {
      method: body ? 'POST' : 'GET', headers: { authorization: `Bearer ${credential}`, 'content-type': 'application/json' },
      ...(body ? { body: JSON.stringify(body) } : {}),
    });
  }
  const command = { characterId, requestId: randomUUID(), expectedLobbyRevision: 2 };
  // Omitting operation must preserve the existing Cancel fixture contract.
  const arm = await control('/arm', { sessionId, characterId, ...(operation === 'Request' ? { operation } : {}),
    ...(lossPoint === 'BeforeCommit' ? { lossPoint } : {}) });
  assert.equal(arm.status, 200);
  if (operation === 'Cancel') assert.equal((await send(base, command)).status, 200);
  await assert.rejects(() => send(target, command), 'The initial transport is actually disconnected');
  return { control, send, command, target, base, oldToken, newToken, sessionId, originalReply,
    targetCalls: () => targetCalls,
    assertNoCredentials(state) {
      for (const secret of [controlToken, oldToken, newToken])
        assert.ok(!JSON.stringify(state).includes(secret) && !trace.includes(secret), 'State and logs contain no credentials');
    } };
}

for (const operation of ['Request', 'Cancel']) {
  test(`${operation}: committed loss, held reads, real upstream 401 and identical replay`, async t => {
    const f = await fixture(t, operation);
    assert.equal((await f.send(`/api/dungeon-sessions/${f.sessionId}`)).status, 503);
    assert.equal((await f.send(f.target, f.command)).status, 503);
    assert.equal(f.targetCalls(), 1, 'Held replay never reaches the upstream');
    assert.equal((await f.control('/revoke', {})).status, 200);
    assert.equal((await f.send(f.target, f.command)).status, 401);
    const replay = await f.send(f.target, f.command, f.newToken);
    assert.equal(replay.status, 200); assert.equal(await replay.text(), f.originalReply);
    assert.equal((await f.send(`/api/dungeon-sessions/${f.sessionId}`)).status, 200);
    const state = await (await f.control('/state')).json();
    assert.equal(state.operation, operation); assert.equal(state.phase, 'replayed'); assert.equal(state.failure, null);
    for (const key of ['dropped', 'blockedReads', 'blockedReplays', 'departureRequestsForwarded', 'unauthorizedForwarded', 'replayForwarded'])
      assert.equal(state[key], 1, key);
    assert.equal(state.originalReplySHA256, state.replayReplySHA256);
    assert.equal(state.changedCredentialOnReplay, true); assert.equal(state.unexpectedNewDepartures, 0);
    assert.equal(state.unexpectedCancellations, 0); assert.equal(f.targetCalls(), 3);
    f.assertNoCredentials(state);
  });
  test(`${operation}: cold-start read uses a fresh credential without enabling mutation replay`, async t => {
    const f = await fixture(t, operation);
    assert.equal((await f.control('/cold-read', {})).status, 200);
    assert.equal((await f.send(`/api/dungeon-sessions/${f.sessionId}`, null, f.newToken)).status, 200);
    assert.equal((await f.control('/revoke', {})).status, 409, 'Cold read cannot switch into mutation recovery');
    assert.equal((await f.send(f.target, f.command, f.newToken)).status, 503, 'Even original-command POST remains held');
    const state = await (await f.control('/state')).json();
    assert.equal(state.allowColdRead, true); assert.equal(state.allowRetry, false);
    assert.equal(state.coldReadsForwarded, 1); assert.equal(state.coldReadChangedCredential, true);
    assert.equal(state.replayForwarded, 0); assert.equal(state.unauthorizedForwarded, 0);
    assert.equal(f.targetCalls(), 1); assert.equal(state.failure, null);
    f.assertNoCredentials(state);
  });
  test(`${operation}: before-commit drop reaches upstream only after fresh owner read and explicit original retry`, async t => {
    const f = await fixture(t, operation, 'BeforeCommit');
    assert.equal(f.targetCalls(), 0, 'Dropped original mutation never reached the upstream');
    assert.equal((await f.control('/cold-retry', {})).status, 409);
    assert.equal((await f.control('/revoke', {})).status, 409, 'Before-commit mode cannot use committed-response recovery');
    assert.equal((await f.control('/cold-read', {})).status, 200);
    assert.equal((await f.control('/cold-retry', {})).status, 409, 'A successful fresh owner read is required first');
    assert.equal((await f.send(`/api/dungeon-sessions/${f.sessionId}`, null, f.newToken)).status, 200);
    assert.equal((await f.control('/cold-retry', {})).status, 200);
    const reply = await f.send(f.target, f.command, f.newToken);
    assert.equal(reply.status, 200); assert.equal(await reply.text(), f.originalReply);
    const state = await (await f.control('/state')).json();
    assert.equal(state.lossPoint, 'BeforeCommit'); assert.equal(state.precommitDrops, 1);
    assert.equal(state.targetRequestsForwarded, 1); assert.equal(state.targetSuccesses, 1);
    assert.equal(state.replayForwarded, 1); assert.equal(state.changedCredentialOnReplay, true);
    assert.equal(state.originalReplySHA256, null, 'No committed original response is invented');
    assert.equal(state.departureRequestsForwarded, 1); assert.equal(state.failure, null);
    assert.equal(f.targetCalls(), 1); f.assertNoCredentials(state);
  });
}

test('Request: uncertain new command or opposite intent never reaches the upstream', async t => {
  const f = await fixture(t, 'Request');
  assert.equal((await f.send(f.target, { ...f.command, requestId: randomUUID() })).status, 409);
  assert.equal((await f.send(`${f.base}/cancel`, f.command)).status, 503);
  assert.equal(f.targetCalls(), 1);
  const state = await (await f.control('/state')).json();
  assert.equal(state.unexpectedNewDepartures, 1); assert.equal(state.unexpectedCancellations, 1);
  f.assertNoCredentials(state);
});
