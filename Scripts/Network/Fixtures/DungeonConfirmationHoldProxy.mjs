// Test-only loopback fault: delay the request, or discard its committed reply.
// Credentials/body remain in memory; the gate never grants game authority.
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createHash } from 'node:crypto';
import { pathToFileURL } from 'node:url';

const digest = value => createHash('sha256').update(value).digest('hex');
const guid = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/;
export async function startConfirmationHoldProxy(config) {
  const { upstreamPort, root, sessionId, characterId, steamId, authorizationSHA256 } = config;
  const mode = config.mode ?? 'before-commit';
  const generation = config.generation ?? 2;
  if (!Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535 ||
      !path.isAbsolute(root) || !/^[0-9a-f]{32}$/.test(path.basename(root)) ||
      path.basename(path.dirname(root)) !== 'DungeonMembershipBackend' || !fs.statSync(root).isDirectory() ||
      !guid.test(sessionId) || !guid.test(characterId) || !/^\d{1,20}$/.test(steamId) ||
      !/^[0-9a-f]{64}$/.test(authorizationSHA256) ||
      ![1, 2].includes(generation) ||
      !['before-commit', 'after-commit', 'after-commit-replay'].includes(mode)) throw new Error('Invalid confirmation fixture scope');
  const evidence = path.join(root, 'confirmation-hold.proof.json');
  const gate = path.join(root, 'release-confirmation.gate');
  if (fs.existsSync(evidence) || fs.existsSync(gate)) throw new Error('Refusing to reuse a fault directory');
  const target = `/api/dungeon-sessions/${sessionId}/members/${characterId}/admission-confirmation`;
  const state = { schemaVersion: 1, mode, sessionId, characterId, generation: String(generation), held: 0,
    phase: 'ready', forwarded: 0, statuses: [], committedResponses: 0, discardedResponses: 0,
    replayedResponses: 0, heldReplySHA256: [], replayedReplySHA256: [], failure: null };
  const pending = [];
  const active = new Set();
  const save = () => fs.writeFileSync(evidence, JSON.stringify(state));
  const fail = code => { state.failure ??= code; state.phase = 'failed'; save(); console.error(`DUNGEON_CONFIRM_HOLD_FAIL ${code}`); };
  const proxy = http.createServer((request, response) => {
    void (async () => {
      if (request.url?.startsWith('/api/') !== true) { response.writeHead(404).end(); return; }
      if (request.method === 'POST' && request.url === target) {
        const chunks = []; let size = 0;
        for await (const chunk of request) {
          size += chunk.length;
          if (size > 8192) throw new Error('BodyLimit');
          chunks.push(chunk);
        }
        const body = Buffer.concat(chunks);
        const value = JSON.parse(body.toString('utf8'));
        if (value.connectionGeneration === generation) {
          if (value.steamId !== steamId || digest(request.headers.authorization ?? '') !== authorizationSHA256 ||
              state.failure || state.held >= 4) throw new Error('ScopedRequestMismatch');
          const entry = { request, response, body };
          state.held++; state.phase = 'held'; save();
          if (mode !== 'before-commit') { forward(entry, true); return; }
          // Keep the captured original even if the UE HTTP timeout closes its
          // transport: releasing models a delayed original request, not a new confirmation.
          if (fs.existsSync(gate)) forward(entry, true); else pending.push(entry);
          return;
        }
        forward({ request, response, body }, false);
        return;
      }
      forward({ request, response }, false);
    })().catch(() => { fail('InvalidScopedRequest'); response.destroy(); });
  });
  function forward({ request, response, body }, held) {
    const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort, path: request.url,
      method: request.method, headers: { ...request.headers, host: `127.0.0.1:${upstreamPort}` } });
    active.add(upstream);
    upstream.setTimeout(15000, () => upstream.destroy());
    upstream.on('error', () => { fail('UpstreamTransport'); response.destroy(); });
    upstream.on('close', () => active.delete(upstream));
    upstream.on('response', incoming => {
      if (held && mode !== 'before-commit') {
        // Read the complete upstream reply without sending even its headers to
        // UE. A 200 alone is not confirmation evidence; validate the body too.
        const chunks = []; let size = 0;
        incoming.on('error', () => { fail('UpstreamResponse'); response.destroy(); });
        incoming.on('data', chunk => {
          size += chunk.length;
          if (size > 4096) { fail('ResponseLimit'); incoming.destroy(); response.destroy(); }
          else chunks.push(chunk);
        });
        incoming.on('end', () => {
          if (state.failure) { response.destroy(); return; }
          if (incoming.statusCode === 200) {
            try {
              if (JSON.parse(Buffer.concat(chunks).toString('utf8')).admissionConfirmed !== true)
                throw new Error('InvalidConfirmation');
            } catch { fail('InvalidCommittedConfirmation'); response.destroy(); return; }
            state.committedResponses++;
          } else if (incoming.statusCode !== 409 || state.committedResponses === 0) {
            fail('UnexpectedConfirmationStatus'); response.destroy(); return;
          }
          const reply = { status: incoming.statusCode, headers: incoming.headers, body: Buffer.concat(chunks) };
          state.heldReplySHA256.push(digest(reply.body));
          state.forwarded++; state.statuses.push(incoming.statusCode);
          state.phase = 'committed-response-held'; save();
          const entry = { response, reply };
          if (fs.existsSync(gate)) releaseReply(entry); else pending.push(entry);
        });
        return;
      }
      if (held) {
        state.forwarded++; state.statuses.push(incoming.statusCode);
        state.phase = 'released'; save();
      }
      if (!response.destroyed) { response.writeHead(incoming.statusCode, incoming.headers); incoming.pipe(response); }
      else incoming.resume();
    });
    if (body) upstream.end(body); else request.pipe(upstream);
  }
  function discard(entry) {
    entry.response.destroy(); state.discardedResponses++;
    state.phase = 'response-discarded'; save();
  }
  function releaseReply(entry) {
    if (mode !== 'after-commit-replay') { discard(entry); return; }
    if (entry.response.destroyed) { fail('ReplayRecipientClosed'); return; }
    // Send only the already received original bytes. Never reissue a successful
    // confirmation after departure or synthesize a new admission grant.
    entry.response.writeHead(entry.reply.status, entry.reply.headers).end(entry.reply.body);
    state.replayedResponses++; state.replayedReplySHA256.push(digest(entry.reply.body));
    state.phase = 'response-replayed'; save();
  }
  const timer = setInterval(() => {
    if (!fs.existsSync(gate)) return;
    while (pending.length) {
      const entry = pending.shift();
      if (mode !== 'before-commit') releaseReply(entry); else forward(entry, true);
    }
  }, 25);
  timer.unref();
  await new Promise((resolve, reject) => { proxy.once('error', reject); proxy.listen(0, '127.0.0.1', resolve); });
  save();
  return { port: proxy.address().port, evidence, gate,
    async close() {
      clearInterval(timer);
      for (const entry of pending) entry.response.destroy();
      for (const request of active) request.destroy();
      proxy.closeAllConnections();
      await new Promise(resolve => proxy.close(resolve));
    } };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  try {
    const fixture = await startConfirmationHoldProxy({
      upstreamPort: Number(process.env.PROJECT_RPG_CONFIRM_HOLD_UPSTREAM),
      root: process.env.PROJECT_RPG_CONFIRM_HOLD_ROOT ?? '',
      sessionId: process.env.PROJECT_RPG_DUNGEON_SESSION_ID,
      characterId: process.env.PROJECT_RPG_CONFIRM_HOLD_CHARACTER,
      steamId: process.env.PROJECT_RPG_CONFIRM_HOLD_STEAM,
      authorizationSHA256: process.env.PROJECT_RPG_CONFIRM_HOLD_AUTH_HASH,
      mode: process.env.PROJECT_RPG_CONFIRM_HOLD_MODE,
      generation: process.env.PROJECT_RPG_CONFIRM_HOLD_GENERATION === undefined
        ? undefined : Number(process.env.PROJECT_RPG_CONFIRM_HOLD_GENERATION)
    });
    console.log(`DUNGEON_CONFIRM_HOLD_READY Port=${fixture.port}`);
  } catch { console.error('DUNGEON_CONFIRM_HOLD_FAIL InvalidConfiguration'); process.exitCode = 1; }
}
