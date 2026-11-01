import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { createHash, randomBytes } from 'node:crypto';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

const sessionId = '11111111-1111-1111-1111-111111111111';
const characterId = '22222222-2222-2222-2222-222222222222';
const path = `/api/dungeon-sessions/${sessionId}/members/${characterId}/abandon`;
const playerAuthorization = 'Bearer test-only-player-credential';
const receipt = { dungeonSessionId: sessionId, characterId, abandonedAt: '2026-10-13T00:00:00Z',
  sessionStateAtAbandonment: 'InProgress', rewardExcluded: true };

async function fixture(t, responseValue = receipt, transparent = false) {
  let commits = 0;
  let expired = false;
  const backend = http.createServer((request, response) => {
    if (request.method === 'GET' && request.url === '/api/health') {
      response.writeHead(200, { 'content-type': 'application/json' }).end('{"ready":true}'); return;
    }
    if (request.method !== 'POST' || request.url !== path || request.headers.authorization !== playerAuthorization) {
      response.writeHead(403).end(); return;
    }
    if (expired) { response.writeHead(401, { 'content-type': 'application/json' }).end('{"error":"Unauthorized"}'); return; }
    commits++;
    response.writeHead(200, { 'content-type': 'application/json' });
    response.end(JSON.stringify(responseValue));
  });
  await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
  t.after(async () => { backend.closeAllConnections(); await new Promise(resolve => backend.close(resolve)); });
  const token = randomBytes(32).toString('hex');
  const child = spawn(process.execPath,
    [fileURLToPath(new URL('./DungeonAbandonmentHttpFaultProxy.mjs', import.meta.url))],
    { env: { ...process.env, PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN: token,
      PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT: String(backend.address().port),
      PROJECT_RPG_ABANDON_PROXY_TRANSPARENT: transparent ? '1' : '0' },
      stdio: ['ignore', 'pipe', 'pipe'] });
  let output = '';
  let errors = '';
  child.stderr.on('data', chunk => { errors += chunk; });
  t.after(async () => {
    if (child.exitCode === null && child.signalCode === null) {
      const stopped = once(child, 'exit'); child.kill(); await stopped;
    }
    for (const secret of [token, playerAuthorization, receipt.abandonedAt]) {
      assert.ok(!output.includes(secret) && !errors.includes(secret));
    }
  });
  const ports = await new Promise((resolve, reject) => {
    const timeout = setTimeout(() => reject(new Error('Proxy readiness timeout')), 5000);
    child.once('error', error => { clearTimeout(timeout); reject(error); });
    child.once('exit', () => { clearTimeout(timeout); reject(new Error('Proxy exited early')); });
    child.stdout.on('data', chunk => {
      output += chunk;
      const match = output.match(/ABANDON_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)/);
      if (match) { clearTimeout(timeout); resolve({ control: Number(match[1]), proxy: Number(match[2]) }); }
    });
  });
  const controlUrl = `http://127.0.0.1:${ports.control}`;
  const proxyUrl = `http://127.0.0.1:${ports.proxy}`;
  const controlHeaders = { authorization: `Bearer ${token}`, 'content-type': 'application/json' };
  const state = async () => (await fetch(`${controlUrl}/state`, { headers: controlHeaders })).json();
  const releaseReplay = () => fetch(`${controlUrl}/release-replay`, { method: 'POST', headers: controlHeaders });
  const arm = value => fetch(`${controlUrl}/arm`, { method: 'POST', headers: controlHeaders,
    body: JSON.stringify(value ?? { sessionId, characterId,
      authorizationSHA256: createHash('sha256').update(playerAuthorization).digest('hex') }) });
  const send = (authorization = playerAuthorization, body = undefined) =>
    fetch(`${proxyUrl}${path}`, { method: 'POST', headers: { authorization }, body });
  return { controlUrl, proxyUrl, token, arm, state, releaseReplay, send,
    setExpired: value => { expired = value; }, commits: () => commits };
}

test('loses only the committed 200, then refuses a second scoped POST', { timeout: 15000 }, async t => {
  const f = await fixture(t);
  assert.equal((await fetch(`${f.controlUrl}/state`)).status, 403);
  assert.equal((await f.send()).status, 403);
  assert.equal((await f.arm()).status, 200);
  assert.equal((await f.arm()).status, 409);
  assert.equal((await f.send('Bearer wrong')).status, 403);
  assert.equal((await fetch(`${f.proxyUrl}/api/other`, { method: 'POST' })).status, 403);
  assert.equal(f.commits(), 0);
  await assert.rejects(f.send());
  const state = await f.state();
  assert.equal(state.phase, 'dropped');
  assert.equal(state.forwarded, 1);
  assert.equal(state.droppedAcknowledgements, 1);
  assert.equal(state.receiptSHA256, createHash('sha256').update(JSON.stringify(receipt)).digest('hex'));
  assert.equal(f.commits(), 1);
  assert.equal((await f.send()).status, 409);
  assert.equal(f.commits(), 1);
});

test('a malformed upstream receipt is never called a durable lost acknowledgement', { timeout: 15000 }, async t => {
  const f = await fixture(t, { ...receipt, rewardExcluded: false });
  await f.arm();
  await assert.rejects(f.send());
  const state = await f.state();
  assert.equal(state.phase, 'failed');
  assert.equal(state.droppedAcknowledgements, 0);
  assert.equal(state.failure, 'UpstreamReceiptMismatch');
  assert.equal(f.commits(), 1);
});

test('transparent lobby traffic survives one lost receipt and forwards an identical UE retry', { timeout: 15000 }, async t => {
  const f = await fixture(t, receipt, true);
  assert.deepEqual(await (await fetch(`${f.proxyUrl}/api/health`)).json(), { ready: true });
  assert.equal((await f.arm({ sessionId, characterId, authorizationSHA256: null })).status, 200);
  await assert.rejects(f.send());
  assert.equal((await f.send()).status, 503);
  assert.equal(f.commits(), 1);
  assert.equal((await f.releaseReplay()).status, 200);
  const retry = await f.send();
  assert.equal(retry.status, 200);
  assert.deepEqual(await retry.json(), receipt);
  const state = await f.state();
  assert.equal(state.forwarded, 1);
  assert.equal(state.droppedAcknowledgements, 1);
  assert.equal(state.blockedReplays, 1);
  assert.equal(state.replayReleased, true);
  assert.equal(state.replayForwarded, 1);
  assert.equal(state.replayReceiptSHA256, state.receiptSHA256);
  assert.equal(f.commits(), 2);
});

test('an expired retry forwards real 401 without clearing the receipt, then accepts a renewed retry', { timeout: 15000 }, async t => {
  const f = await fixture(t, receipt, true);
  assert.equal((await f.arm({ sessionId, characterId, authorizationSHA256: null })).status, 200);
  await assert.rejects(f.send());
  assert.equal((await f.send()).status, 503);
  f.setExpired(true);
  assert.equal((await f.releaseReplay()).status, 200);
  assert.equal((await f.send()).status, 401);
  assert.equal(f.commits(), 1);
  f.setExpired(false);
  assert.equal((await f.send()).status, 200);
  const state = await f.state();
  assert.equal(state.unauthorizedForwarded, 1);
  assert.equal(state.replayForwarded, 1);
  assert.equal(state.replayReceiptSHA256, state.receiptSHA256);
  assert.equal(f.commits(), 2);
});
