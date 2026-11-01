// Local test fixture: forward one scoped player POST, then lose its committed 200.
// Never log credentials, request bodies, ticket values, or receipt contents.
import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';

const token = process.env.PROJECT_RPG_ABANDON_PROXY_CONTROL_TOKEN;
const upstreamPort = Number(process.env.PROJECT_RPG_ABANDON_PROXY_UPSTREAM_PORT);
const transparent = process.env.PROJECT_RPG_ABANDON_PROXY_TRANSPARENT === '1';
if (!token || token.length < 32 || !Number.isInteger(upstreamPort)
  || upstreamPort < 1 || upstreamPort > 65535) throw new Error('Invalid abandonment proxy configuration.');

const digest = value => createHash('sha256').update(value).digest('hex');
const guid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
const state = { phase: 'idle', forwarded: 0, droppedAcknowledgements: 0, receiptSHA256: null,
  blockedReplays: 0, replayReleased: false, unauthorizedForwarded: 0,
  replayForwarded: 0, replayReceiptSHA256: null, failure: null };
let scope = null;

function fail(reason) {
  state.failure ??= reason;
  state.phase = 'failed';
  console.error(`ABANDON_PROXY_FAIL ${reason}`);
}
function authorize(request) {
  const actual = Buffer.from(request.headers.authorization ?? '');
  const expected = Buffer.from(`Bearer ${token}`);
  return actual.length === expected.length && timingSafeEqual(actual, expected);
}
function send(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(value));
}
async function readBody(request, maxBytes) {
  const chunks = [];
  let size = 0;
  for await (const chunk of request) {
    size += chunk.length;
    if (size > maxBytes) throw new Error('BodyLimit');
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

const control = http.createServer((request, response) => {
  void (async () => {
    if (!authorize(request)) { send(response, 403, { error: 'Forbidden' }); return; }
    if (request.method === 'GET' && request.url === '/state') { send(response, 200, state); return; }
    if (request.method === 'POST' && request.url === '/release-replay' && transparent && state.phase === 'dropped') {
      state.replayReleased = true;
      send(response, 200, state); return;
    }
    if (request.method !== 'POST' || request.url !== '/arm' || scope) {
      send(response, 409, { error: 'Invalid control state' }); return;
    }
    const body = JSON.parse((await readBody(request, 4096)).toString('utf8'));
    if (!guid.test(body.sessionId) || !guid.test(body.characterId)
      || !(transparent && body.authorizationSHA256 == null)
        && !/^[0-9a-f]{64}$/.test(body.authorizationSHA256)) {
      send(response, 400, { error: 'Invalid scope' }); return;
    }
    scope = { path: `/api/dungeon-sessions/${body.sessionId}/members/${body.characterId}/abandon`,
      sessionId: body.sessionId, characterId: body.characterId,
      authorizationSHA256: body.authorizationSHA256 };
    state.phase = 'armed';
    send(response, 200, state);
  })().catch(() => send(response, 400, { error: 'Invalid control request' }));
});

const proxy = http.createServer((request, response) => {
  void (async () => {
    if (transparent && request.url?.startsWith('/api/') &&
      (!scope || request.method !== 'POST' || request.url !== scope.path || state.phase === 'dropped')) {
      const replay = scope && request.method === 'POST' && request.url === scope.path;
      if (replay && !state.replayReleased) {
        if ((await readBody(request, 1)).length !== 0) { fail('UnexpectedReplayBody'); response.destroy(); return; }
        state.blockedReplays++;
        send(response, 503, { error: 'TestReplayHeld' });
        return;
      }
      const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort,
        method: request.method, path: request.url,
        headers: { ...request.headers, host: `127.0.0.1:${upstreamPort}` } });
      upstream.setTimeout(15000, () => upstream.destroy(new Error('UpstreamTimeout')));
      upstream.on('error', () => { fail('UpstreamTransport'); response.destroy(); });
      upstream.on('response', upstreamResponse => {
        if (!replay) {
          response.writeHead(upstreamResponse.statusCode, upstreamResponse.headers);
          upstreamResponse.pipe(response);
          return;
        }
        void (async () => {
          const body = await readBody(upstreamResponse, 16384);
          if (upstreamResponse.statusCode === 401) {
            state.unauthorizedForwarded++;
            response.writeHead(401, upstreamResponse.headers);
            response.end(body);
            return;
          }
          if (upstreamResponse.statusCode !== 200 || digest(body) !== state.receiptSHA256) {
            fail('ReplayReceiptMismatch'); response.destroy(); return;
          }
          state.replayForwarded++;
          state.replayReceiptSHA256 = digest(body);
          console.log('ABANDON_PROXY_REPLAY_MATCHED');
          response.writeHead(upstreamResponse.statusCode, upstreamResponse.headers);
          response.end(body);
        })().catch(() => { fail('ReplayTransport'); response.destroy(); });
      });
      request.pipe(upstream);
      return;
    }
    if (!scope || request.method !== 'POST' || request.url !== scope.path
      || !/^Bearer \S+$/.test(request.headers.authorization ?? '')
      || (scope.authorizationSHA256 && digest(request.headers.authorization ?? '') !== scope.authorizationSHA256)) {
      send(response, 403, { error: 'Outside fixture scope' }); return;
    }
    if (state.phase !== 'armed') { send(response, 409, { error: 'Already used' }); return; }
    if ((await readBody(request, 1)).length !== 0) {
      fail('UnexpectedBody'); response.destroy(); return;
    }
    state.phase = 'forwarding';
    state.forwarded++;
    const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort,
      method: 'POST', path: scope.path,
      headers: { authorization: request.headers.authorization, host: `127.0.0.1:${upstreamPort}`,
        'content-length': '0', connection: 'close' } });
    upstream.setTimeout(15000, () => upstream.destroy(new Error('UpstreamTimeout')));
    upstream.on('error', () => { fail('UpstreamTransport'); response.destroy(); });
    upstream.on('response', upstreamResponse => {
      void (async () => {
        const body = await readBody(upstreamResponse, 16384);
        const receipt = JSON.parse(body.toString('utf8'));
        if (upstreamResponse.statusCode !== 200 || receipt.dungeonSessionId !== scope.sessionId
          || receipt.characterId !== scope.characterId || receipt.rewardExcluded !== true
          || !['Loading', 'InProgress'].includes(receipt.sessionStateAtAbandonment)
          || !receipt.abandonedAt) {
          fail('UpstreamReceiptMismatch'); response.destroy(); return;
        }
        state.receiptSHA256 = digest(body);
        state.droppedAcknowledgements = 1;
        state.phase = 'dropped';
        console.log('ABANDON_PROXY_DROPPED');
        // Upstream returned the committed receipt. Deliver no bytes to the player.
        response.destroy();
      })().catch(() => { fail('InvalidUpstreamResponse'); response.destroy(); });
    });
    upstream.end();
  })().catch(() => { fail('InvalidRequest'); response.destroy(); });
});
for (const listener of [control, proxy]) {
  listener.on('error', () => { console.error('ABANDON_PROXY_FAIL ListenerFailure'); process.exit(1); });
}
proxy.listen(0, '127.0.0.1', () => {
  control.listen(0, '127.0.0.1', () =>
    console.log(`ABANDON_PROXY_READY ControlPort=${control.address().port} ProxyPort=${proxy.address().port}`));
});
