import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { spawn } from 'node:child_process';
import { randomBytes } from 'node:crypto';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';

test('real HTTP disconnects preserve committed events for a duplicate-only retry',
  { timeout: 15000 }, async t => {
    const storedIds = new Set();
    const backend = http.createServer(async (request, response) => {
      const chunks = [];
      for await (const chunk of request) chunks.push(chunk);
      const batch = JSON.parse(Buffer.concat(chunks).toString('utf8'));
      let acceptedCount = 0;
      for (const event of batch.events) {
        if (!storedIds.has(event.eventId)) {
          storedIds.add(event.eventId);
          acceptedCount++;
        }
      }
      response.writeHead(200, { 'content-type': 'application/json' });
      response.end(JSON.stringify({
        acceptedCount, duplicateCount: batch.events.length - acceptedCount,
      }));
    });
    await new Promise(resolve => backend.listen(0, '127.0.0.1', resolve));
    t.after(async () => {
      backend.closeAllConnections();
      await new Promise(resolve => backend.close(resolve));
    });
    const token = randomBytes(32).toString('hex');
    const fixture = spawn(process.execPath,
      [fileURLToPath(new URL('./TelemetryHttpFaultProxy.mjs', import.meta.url))], {
        env: {
          ...process.env,
          PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN: token,
          PROJECT_RPG_E2E_PROXY_LISTEN_PORT: '0',
          PROJECT_RPG_E2E_PROXY_UPSTREAM_PORT: String(backend.address().port),
        },
        stdio: ['ignore', 'pipe', 'pipe'],
      });
    t.after(async () => {
      if (fixture.exitCode === null && fixture.signalCode === null) {
        const stopped = once(fixture, 'exit');
        fixture.kill();
        await stopped;
      }
    });
    const ports = await new Promise((resolve, reject) => {
      let output = '';
      const deadline = setTimeout(() => reject(new Error('Proxy startup timed out')), 5000);
      fixture.once('error', error => { clearTimeout(deadline); reject(error); });
      fixture.once('exit', () => { clearTimeout(deadline); reject(new Error('Proxy exited')); });
      fixture.stdout.on('data', chunk => {
        output += chunk.toString();
        const match = output.match(/E2E_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)/);
        if (match) {
          clearTimeout(deadline);
          resolve({ control: Number(match[1]), proxy: Number(match[2]) });
        }
      });
    });
    const controlUrl = `http://127.0.0.1:${ports.control}`;
    const headers = { authorization: `Bearer ${token}`, 'content-type': 'application/json' };
    const dungeonSessionId = 'aaaaaaaa-bbbb-cccc-dddd-eeeeeeeeeeee';
    const events = [1, 2, 3].map(value => ({ eventId: `event-${value}` }));
    const setMode = async mode => {
      const response = await fetch(`${controlUrl}/mode`, {
        method: 'POST', headers, body: JSON.stringify({ mode, dungeonSessionId }),
      });
      assert.equal(response.status, 200);
      return response.json();
    };
    const getState = async () => (await fetch(`${controlUrl}/state`, { headers })).json();
    const send = async () => (await fetch(
      `http://127.0.0.1:${ports.proxy}/api/security/events/batch`, {
        method: 'POST', headers: { 'content-type': 'application/json' },
        body: JSON.stringify({ dungeonSessionId, events }),
      })).json();

    assert.equal((await fetch(`${controlUrl}/state`)).status, 403);
    await setMode('blocked');
    await assert.rejects(send);
    assert.equal(storedIds.size, 0);
    assert.ok((await getState()).blockedTelemetryRequests >= 1);

    await setMode('drop_next_ack');
    await assert.rejects(send);
    assert.equal(storedIds.size, 3);
    const unacknowledged = await getState();
    assert.equal(unacknowledged.droppedAcknowledgements, 1);
    assert.equal(unacknowledged.mode, 'blocked');
    assert.equal(unacknowledged.committedBatch.acceptedCount, 3);
    assert.equal(unacknowledged.committedBatch.duplicateCount, 0);
    await assert.rejects(send);
    assert.equal(storedIds.size, 3);

    await setMode('pass');
    assert.deepEqual(await send(), { acceptedCount: 0, duplicateCount: 3 });
    assert.equal(storedIds.size, 3);
    const recovered = await getState();
    assert.equal(recovered.deliveredBatch.acceptedCount, 0);
    assert.equal(recovered.deliveredBatch.duplicateCount, 3);
    assert.deepEqual(recovered.deliveredBatch.eventIds, events.map(event => event.eventId));
  });
