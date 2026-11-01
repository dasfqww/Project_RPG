// Local E2E fixture only. Credentials and request bodies are never logged.
import http from 'node:http';
import { timingSafeEqual } from 'node:crypto';

const controlToken = process.env.PROJECT_RPG_E2E_PROXY_CONTROL_TOKEN;
if (!controlToken || controlToken.length < 32) {
  throw new Error('A test control token of at least 32 characters is required.');
}
const listenPort = Number(process.env.PROJECT_RPG_E2E_PROXY_LISTEN_PORT ?? 3000);
const upstreamPort = Number(process.env.PROJECT_RPG_E2E_PROXY_UPSTREAM_PORT ?? 3002);
if (!Number.isInteger(listenPort) || listenPort < 0 || listenPort > 65535
  || !Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535) {
  throw new Error('Invalid local test proxy port.');
}

const maximumBodyBytes = 2 * 1024 * 1024;
const hopHeaders = new Set([
  'connection', 'keep-alive', 'proxy-authenticate', 'proxy-authorization',
  'te', 'trailer', 'transfer-encoding', 'upgrade', 'content-length',
]);
const state = {
  mode: 'pass',
  dungeonSessionId: null,
  blockedTelemetryRequests: 0,
  droppedAcknowledgements: 0,
  committedBatch: null,
  deliveredBatch: null,
};

async function readBody(stream) {
  const chunks = [];
  let length = 0;
  for await (const chunk of stream) {
    length += chunk.length;
    if (length > maximumBodyBytes) {
      throw new Error('Test proxy body limit exceeded.');
    }
    chunks.push(chunk);
  }
  return Buffer.concat(chunks);
}

function filteredHeaders(headers) {
  const connectionHeaders = new Set(
    String(headers.connection ?? '').toLowerCase().split(',').map(s => s.trim()));
  return Object.fromEntries(Object.entries(headers).filter(([name]) =>
    !hopHeaders.has(name.toLowerCase()) && !connectionHeaders.has(name.toLowerCase())));
}

async function forward(request, response) {
  const isTelemetry = request.method === 'POST'
    && request.url === '/api/security/events/batch';
  if (state.mode === 'blocked') {
    if (isTelemetry) state.blockedTelemetryRequests++;
    request.socket.destroy();
    return;
  }

  const body = await readBody(request);
  let batch = null;
  if (isTelemetry) {
    const document = JSON.parse(body.toString('utf8'));
    if (document.dungeonSessionId === state.dungeonSessionId) {
      batch = document;
    }
  }
  const headers = filteredHeaders(request.headers);
  headers.host = `127.0.0.1:${upstreamPort}`;
  headers['content-length'] = body.length;
  headers.connection = 'close';
  const upstream = http.request({
    hostname: '127.0.0.1', port: upstreamPort,
    path: request.url, method: request.method, headers,
  });
  upstream.on('error', () => response.destroy());
  upstream.setTimeout(15000, () => upstream.destroy());
  response.on('close', () => {
    if (!response.writableEnded) upstream.destroy();
  });
  upstream.on('response', upstreamResponse => {
    void (async () => {
      const responseBody = await readBody(upstreamResponse);
      let acknowledgement = null;
      if (batch && upstreamResponse.statusCode === 200) {
        const result = JSON.parse(responseBody.toString('utf8'));
        acknowledgement = {
          eventIds: batch.events.map(event => event.eventId),
          acceptedCount: result.acceptedCount,
          duplicateCount: result.duplicateCount,
        };
      }
      if (acknowledgement && state.mode === 'drop_next_ack') {
        // The upstream has committed and returned its complete 200 response.
        // Hold subsequent traffic until the harness checks the durable spool.
        state.committedBatch = acknowledgement;
        state.droppedAcknowledgements++;
        state.mode = 'blocked';
        response.destroy();
        return;
      }
      const responseHeaders = filteredHeaders(upstreamResponse.headers);
      responseHeaders['content-length'] = responseBody.length;
      response.writeHead(upstreamResponse.statusCode, responseHeaders);
      response.end(responseBody, () => {
        if (acknowledgement) state.deliveredBatch = acknowledgement;
      });
    })().catch(() => response.destroy());
  });
  upstream.end(body);
}

function authorized(request) {
  const supplied = Buffer.from(request.headers.authorization ?? '');
  const expected = Buffer.from(`Bearer ${controlToken}`);
  return supplied.length === expected.length && timingSafeEqual(supplied, expected);
}

function json(response, status, value) {
  response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store' });
  response.end(JSON.stringify(value));
}

async function control(request, response) {
  if (!authorized(request)) {
    json(response, 403, { error: 'Forbidden' });
    return;
  }
  if (request.method === 'GET' && request.url === '/state') {
    json(response, 200, state);
    return;
  }
  if (request.method === 'POST' && request.url === '/mode') {
    const document = JSON.parse((await readBody(request)).toString('utf8'));
    if (!['pass', 'blocked', 'drop_next_ack'].includes(document.mode)
      || !/^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(document.dungeonSessionId)) {
      json(response, 400, { error: 'Invalid test mode or session.' });
      return;
    }
    state.mode = document.mode;
    state.dungeonSessionId = document.dungeonSessionId;
    json(response, 200, state);
    return;
  }
  json(response, 404, { error: 'Not found' });
}

const proxy = http.createServer((request, response) => {
  void forward(request, response).catch(() => response.destroy());
});
const controlServer = http.createServer((request, response) => {
  void control(request, response).catch(() => json(response, 400, { error: 'Invalid control request' }));
});
for (const server of [proxy, controlServer]) {
  server.on('error', error => {
    console.error(`E2E_PROXY_ERROR ${error.code ?? 'ServerFailure'}`);
    process.exit(1);
  });
}
proxy.listen(listenPort, '127.0.0.1', () => {
  controlServer.listen(0, '127.0.0.1', () => {
    console.log(`E2E_PROXY_READY ControlPort=${controlServer.address().port} ProxyPort=${proxy.address().port}`);
  });
});
