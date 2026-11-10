import test from 'node:test';
import assert from 'node:assert/strict';
import net from 'node:net';
import { once } from 'node:events';
import { fileURLToPath } from 'node:url';
import { callBridge } from '../src/bridge-client.mjs';
import { makeRequest, validateEndpoint } from '../src/protocol.mjs';
import { CodexDiagnostic } from '../src/codex-client.mjs';

async function withServer(handler, run) {
  const sockets = new Set();
  const server = net.createServer({ allowHalfOpen: true }, socket => {
    sockets.add(socket);
    socket.on('error', () => {});
    socket.on('close', () => sockets.delete(socket));
    handler(socket);
  });
  server.listen(0, '127.0.0.1');
  await once(server, 'listening');
  try { await run({ protocol_version: 1, host: '127.0.0.1', port: server.address().port, token: 'a'.repeat(64) }); }
  finally { for (const socket of sockets) socket.destroy(); await new Promise(resolve => server.close(resolve)); }
}

test('reject remote or malformed endpoints before opening a connection', () => {
  for (const endpoint of [null, { protocol_version: 1, host: 'example.com', port: 123, token: 'a'.repeat(64) }, { protocol_version: 1, host: '127.0.0.1', port: 0, token: 'a'.repeat(64) }]) {
    assert.throws(() => validateEndpoint(endpoint), { code: 'INVALID_ENDPOINT' });
  }
});

test('edit requests require an inspected scene and retain the original operation ID', () => {
  assert.throws(() => makeRequest('object.transform'), { code: 'SCENE_REQUIRED' });
  const request = makeRequest('object.transform', { location: [1, 2, 3] }, { blender_session_id: 'session', scene_revision: 4 }, ['object'], 'same-id');
  assert.equal(request.operation_id, 'same-id');
  assert.equal(request.expected_scene_revision, 4);
  assert.throws(() => makeRequest('python.execute'), { code: 'UNSUPPORTED_OPERATION' });
});

test('accept fragmented Unicode replies and match the operation ID', async () => {
  const request = makeRequest('scene.inspect');
  await withServer(socket => {
    socket.once('data', () => {
      const wire = Buffer.from(JSON.stringify({ protocol_version: 1, operation_id: request.operation_id, status: 'succeeded', result: { name: '몬스터' } }) + '\n');
      const split = wire.indexOf(Buffer.from('몬')) + 1;
      socket.write(wire.subarray(0, split));
      setTimeout(() => socket.end(wire.subarray(split)), 5);
    });
  }, async endpoint => assert.equal((await callBridge(endpoint, request)).result.name, '몬스터'));
});

test('disconnect after sending an edit yields unknown and makes no second request', async () => {
  let connections = 0;
  await withServer(socket => { connections++; socket.once('data', () => socket.destroy()); }, async endpoint => {
    const request = makeRequest('checkpoint.save', {}, { blender_session_id: 's', scene_revision: 1 });
    await assert.rejects(callBridge(endpoint, request), { code: 'BRIDGE_DISCONNECTED', effectState: 'unknown' });
    assert.equal(connections, 1);
  });
});

test('read timeout has no scene side effect', async () => {
  await withServer(socket => socket.on('data', () => {}), async endpoint => {
    await assert.rejects(callBridge(endpoint, makeRequest('scene.inspect'), { timeoutMs: 30 }), { code: 'BRIDGE_TIMEOUT', effectState: 'not_applied' });
  });
});

test('mismatched result ID is never accepted as a successful edit', async () => {
  await withServer(socket => socket.once('data', () => socket.end(JSON.stringify({ protocol_version: 1, operation_id: 'other', status: 'succeeded' }) + '\n')), async endpoint => {
    const request = makeRequest('checkpoint.save', {}, { blender_session_id: 's', scene_revision: 1 });
    await assert.rejects(callBridge(endpoint, request), { code: 'INVALID_RESPONSE', effectState: 'unknown' });
  });
});

for (const type of ['chatgpt', 'apiKey']) {
  test(`Codex diagnostic handshake redacts credentials and reports ${type} without a model turn`, async () => {
    const fake = fileURLToPath(new URL('./fake-codex.mjs', import.meta.url));
    const client = new CodexDiagnostic(process.execPath, { args: [fake, type], env: { ...process.env, OPENAI_API_KEY: 'fake-key-must-not-reach-child' } });
    try {
      assert.throws(() => client.request('turn/start', {}), { code: 'METHOD_NOT_ALLOWED' });
      const status = await client.inspect();
      assert.equal(status.subscription_auth_detected, type === 'chatgpt');
      assert.equal(status.model_request_tested, false);
      assert.equal(status.extra_spend_policy_verified, false);
      assert.doesNotMatch(JSON.stringify(status), /private@|never-print|fake-key/);
    } finally { await client.close(); }
  });
}

test('missing Codex executable fails and closes without hanging', async () => {
  const client = new CodexDiagnostic('asset-studio-missing-executable-2039');
  try { await assert.rejects(client.inspect(), { code: 'CODEX_START_FAILED' }); }
  finally { await client.close(); }
});
