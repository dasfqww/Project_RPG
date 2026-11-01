import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { randomBytes, createHash } from 'node:crypto';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

const characters = ['11111111-1111-1111-1111-111111111111', '22222222-2222-2222-2222-222222222222'];
const authorization = 'Bearer test-only-server-credential';
const command = {
  requestId: '33333333-3333-3333-3333-333333333333', operation: 'Item.Pickup', commandFingerprint: 'fixture-fingerprint',
  actor: { type: 'Character', ownerId: characters[0] }, affectedQuantity: 1,
  mutations: [{ expectedRevision: 0, newRecord: { definitionName: 'GameItem.Equipment.Weapon.GreatSword',
    owner: { type: 'Character', ownerId: characters[0] }, revision: 0,
    state: { instanceId: '44444444-4444-4444-4444-444444444444', quantity: 1 } } }],
};
const body = JSON.stringify(command);
async function fixture(t, corruptReceipt = false) {
  let stored = null;
  let commits = 0;
  const backend = http.createServer(async (request, response) => {
    if (request.method === 'GET' && request.url === '/api/health') { response.end(JSON.stringify({ ready: true })); return; }
    if (request.headers.authorization !== authorization) { response.writeHead(403).end(); return; }
    if (request.method === 'GET') {
      if (request.url === `/api/item-transactions/${command.requestId}` && stored) {
        response.end(JSON.stringify({ ...command, status: 'Committed', records: [stored] }));
      } else if (request.url.startsWith('/api/items?')) {
        const owner = new URL(request.url, 'http://localhost').searchParams.get('ownerId');
        response.end(JSON.stringify({ items: stored && owner === characters[0] ? [stored] : [] }));
      } else response.writeHead(404).end();
      return;
    }
    const chunks = [];
    for await (const chunk of request) chunks.push(chunk);
    const document = JSON.parse(Buffer.concat(chunks).toString());
    if (!characters.includes(document.actor?.ownerId)) { response.writeHead(403).end(); return; }
    commits++;
    const status = stored ? 'AlreadyCommitted' : 'Committed';
    stored ??= { ...document.mutations[0].newRecord, revision: 1 };
    response.writeHead(200, { 'content-type': 'application/json' });
    response.end(JSON.stringify({ ...document, status, records: [{ ...stored,
      state: { ...stored.state, quantity: corruptReceipt ? 2 : 1 } }] }));
  });
  await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
  t.after(async () => { backend.closeAllConnections(); await new Promise(resolve => backend.close(resolve)); });
  const token = randomBytes(32).toString('hex');
  const child = spawn(process.execPath, [fileURLToPath(new URL('./ItemPickupHttpFaultProxy.mjs', import.meta.url))], {
    env: { ...process.env, PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN: token,
      PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT: String(backend.address().port), PROJECT_RPG_ITEM_PROXY_LISTEN_PORT: '0' },
    stdio: ['ignore', 'pipe', 'pipe'],
  });
  let output = '', errors = '';
  child.stderr.on('data', chunk => { errors += chunk; });
  t.after(async () => {
    if (child.exitCode === null && child.signalCode === null) { const stopped = once(child, 'exit'); child.kill(); await stopped; }
    assert.ok(!output.includes(token) && !errors.includes(token));
    assert.ok(!output.includes(authorization) && !errors.includes(authorization));
    assert.ok(!output.includes('fixture-fingerprint') && !errors.includes('fixture-fingerprint'));
  });
  const ports = await new Promise((resolve, reject) => {
    const deadline = setTimeout(() => reject(new Error('Proxy readiness timeout')), 5000);
    child.once('error', error => { clearTimeout(deadline); reject(error); });
    child.once('exit', () => { clearTimeout(deadline); reject(new Error('Proxy exited')); });
    child.stdout.on('data', chunk => {
      output += chunk;
      const match = output.match(/ITEM_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)/);
      if (match) { clearTimeout(deadline); resolve({ control: Number(match[1]), proxy: Number(match[2]) }); }
    });
  });
  const controlUrl = `http://127.0.0.1:${ports.control}`;
  const proxyUrl = `http://127.0.0.1:${ports.proxy}`;
  const headers = { authorization: `Bearer ${token}`, 'content-type': 'application/json' };
  const control = (path, value) => fetch(`${controlUrl}/${path}`, { headers, ...(value ? { method: 'POST', body: JSON.stringify(value) } : {}) });
  const state = async () => (await control('state')).json();
  const arm = () => control('arm', { characterIds: characters, authorizationSHA256: createHash('sha256').update(authorization).digest('hex') });
  const send = (serialized = body, auth = authorization) => fetch(`${proxyUrl}/api/item-transactions/commit`, {
    method: 'POST', headers: { authorization: auth, 'content-type': 'application/json' }, body: serialized,
  });
  return { controlUrl, proxyUrl, control, state, arm, send, commits: () => commits, stored: () => stored };
}
test('complete upstream commit loses its ack; blocked exact retries recover one unchanged item', { timeout: 15000 }, async t => {
  const f = await fixture(t);
  assert.equal((await fetch(`${f.controlUrl}/state`)).status, 403);
  assert.equal((await f.control('release', {})).status, 409);
  assert.equal((await f.arm()).status, 200);
  assert.equal((await f.arm()).status, 400);
  assert.equal((await f.send(body, 'Bearer wrong-token')).status, 403);
  assert.equal((await f.state()).droppedAcknowledgements, 0);
  assert.equal((await fetch(`${f.proxyUrl}/api/health`)).status, 200);
  await assert.rejects(f.send());
  assert.equal(f.commits(), 1);
  assert.equal(f.stored().revision, 1);
  const blocked = await f.state();
  assert.equal(blocked.phase, 'blocked');
  assert.equal(blocked.committed.upstreamStatus, 'Committed');
  assert.equal(blocked.committed.bodySHA256, createHash('sha256').update(body).digest('hex'));
  assert.equal((await f.control('release', {})).status, 409);
  for (let index = 0; index < 4; index++) await assert.rejects(f.send());
  assert.equal(f.commits(), 1);
  assert.equal((await f.state()).blockedRetries.length, 4);
  const retries = (await f.state()).blockedRetries;
  assert.ok(retries.every(r => Number.isFinite(r.elapsedMilliseconds)));
  assert.ok(retries.every((r, i) => i === 0 || r.elapsedMilliseconds >= retries[i - 1].elapsedMilliseconds));
  assert.equal((await f.control('release', {})).status, 200);
  const recovered = await (await f.send()).json();
  assert.equal(recovered.status, 'AlreadyCommitted');
  assert.equal(recovered.records[0].state.quantity, 1);
  assert.equal(f.commits(), 2);
  assert.equal((await f.state()).phase, 'recovered');
});
for (const [name, changes] of [
  ['request ID', { requestId: characters[1] }],
  ['operation', { operation: 'Item.Other' }],
  ['owner type', { actor: { type: 'Account', ownerId: characters[0] } }],
  ['owner outside fixture', { actor: { type: 'Character', ownerId: '99999999-9999-9999-9999-999999999999' } }],
]) {
  test(`changed ${name} fails closed without forwarding another grant`, { timeout: 15000 }, async t => {
    const f = await fixture(t);
    await f.arm();
    await assert.rejects(f.send());
    await assert.rejects(f.send(JSON.stringify({ ...command, ...changes })));
    assert.equal(f.commits(), 1);
    assert.equal((await f.state()).failure, 'FrozenCommandChanged');
    assert.equal((await f.control('release', {})).status, 409);
  });
}
test('malformed successful receipt cannot be counted as a lost durable acknowledgement', { timeout: 15000 }, async t => {
  const f = await fixture(t, true);
  await f.arm();
  await assert.rejects(f.send());
  assert.equal((await f.state()).failure, 'ReceiptMismatch');
  assert.equal((await f.state()).droppedAcknowledgements, 0);
});
test('restart recovery reads original receipt/inventories without issuing another commit', { timeout: 15000 }, async t => {
  const f = await fixture(t);
  assert.equal((await f.control('restart-recovery', {})).status, 409);
  await f.arm();
  await assert.rejects(f.send());
  for (let i = 0; i < 4; i++) await assert.rejects(f.send());
  assert.equal((await f.control('restart-recovery', {})).status, 200);
  const headers = { authorization };
  const receipt = await (await fetch(`${f.proxyUrl}/api/item-transactions/${command.requestId}`, { headers })).json();
  assert.equal(receipt.records[0].state.instanceId, command.mutations[0].newRecord.state.instanceId);
  for (const ownerId of characters) {
    const items = await (await fetch(`${f.proxyUrl}/api/items?ownerType=Character&ownerId=${ownerId}&includeTerminal=false&limit=500`, { headers })).json();
    assert.equal(items.items.length, ownerId === characters[0] ? 1 : 0);
  }
  const state = await f.state();
  assert.equal(state.phase, 'restart-read-only');
  assert.equal(state.restartRecovery.reads.length, 3);
  assert.equal(state.restartRecovery.rejectedWrites, 0);
  assert.equal(f.commits(), 1);
  assert.equal((await f.control('release', {})).status, 409);
});
for (const [name, serialized] of [['exact command', body], ['different operation', JSON.stringify({ ...command, operation: 'Item.Other' })]]) {
  test(`restart recovery rejects ${name} before forwarding any write`, { timeout: 15000 }, async t => {
    const f = await fixture(t);
    await f.arm();
    await assert.rejects(f.send());
    for (let i = 0; i < 4; i++) await assert.rejects(f.send());
    await f.control('restart-recovery', {});
    await assert.rejects(f.send(serialized));
    assert.equal((await f.state()).failure, 'WriteDuringRestartRecovery');
    assert.equal((await f.state()).restartRecovery.rejectedWrites, 1);
    assert.equal(f.commits(), 1);
  });
}
