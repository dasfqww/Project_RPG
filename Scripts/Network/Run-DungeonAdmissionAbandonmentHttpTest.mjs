// Real loopback HTTP / Development Memory host. No UE, fake socket, DB or clock writes.
// A captured 200 is transport evidence, never a game-authority grant after departure.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import { spawn } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { setTimeout as pause } from 'node:timers/promises';
import { startConfirmationHoldProxy } from './Fixtures/DungeonConfirmationHoldProxy.mjs';

const sha = value => createHash('sha256').update(value).digest('hex');
const fileHash = file => sha(fs.readFileSync(file));
function files(root) {
  return fs.readdirSync(root, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(root, entry.name);
    if (entry.isSymbolicLink()) throw new Error('Backend artifacts must not contain links');
    return entry.isDirectory() ? files(file) : [{ path: file, sha256: fileHash(file) }];
  });
}
function unchanged(inputs) {
  return inputs.every(input => fs.existsSync(input.path) && fileHash(input.path) === input.sha256);
}
async function waitFor(read, milliseconds = 10000) {
  const deadline = Date.now() + milliseconds;
  while (Date.now() < deadline) {
    const result = await read();
    if (result) return result;
    await pause(25);
  }
  throw new Error('Owned HTTP observation timed out');
}
async function json(base, route, method = 'GET', authorization, body, expected = 200) {
  const response = await fetch(base + '/api/' + route, {
    method, headers: { ...(authorization ? { authorization } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(12000)
  });
  assert.equal(response.status, expected, `HTTP ${method} ${route}`);
  const text = await response.text();
  return { body: text ? JSON.parse(text) : null, text, status: response.status };
}

async function scenario(base, admin, root, mode) {
  const runId = path.basename(root);
  const members = [];
  for (let index = 0; index < 2; index++) {
    const steamId = (76561400000000000n + BigInt('0x' + runId.slice(0, 8)) * 4n + BigInt(index)).toString();
    const auth = (await json(base, 'auth/steam-ticket', 'POST', undefined, { ticket: 'dev:' + steamId })).body;
    const authorization = 'Bearer ' + auth.accessToken;
    const character = (await json(base, 'characters', 'POST', authorization,
      { name: 'Http' + runId.slice(0, 10) + index }, 201)).body;
    members.push({ steamId, authorization, characterId: character.characterId });
  }
  const [target, remaining] = members;
  let session = (await json(base, 'dungeon-sessions', 'POST', target.authorization,
    { characterId: target.characterId, dungeonId: 'Dungeon.MembershipE2E', difficulty: 'Normal' }, 201)).body;
  const sessionId = session.dungeonSessionId;
  const route = 'dungeon-sessions/' + sessionId;
  session = (await json(base, route + '/members', 'POST', remaining.authorization,
    { characterId: remaining.characterId })).body;
  assert.equal(session.members.length, 2);
  const revision = Number(session.lobbyRevision);
  assert.ok(Number.isSafeInteger(revision) && revision > 0);
  await json(base, route + '/departure', 'POST', target.authorization,
    { characterId: target.characterId, requestId: randomUUID(), expectedLobbyRevision: revision });
  const serverId = 'admission-http-' + runId.slice(0, 12);
  const assignment = (await json(base, route + '/activate', 'POST', admin,
    { serverId, serverAddress: '127.0.0.1:18879' })).body;
  const server = 'Bearer ' + assignment.gameServerAccessToken;
  assert.equal((await json(base, route + '/start', 'POST', server, { serverId })).body.state, 'InProgress');
  for (const member of members) {
    const ticket = (await json(base, 'join-tickets', 'POST', member.authorization,
      { dungeonSessionId: sessionId, characterId: member.characterId })).body;
    const consumed = (await json(base, 'join-tickets/consume', 'POST', server,
      { joinTicket: ticket.joinTicket, serverId, consumptionRequestId: randomUUID() })).body;
    assert.equal(Number(consumed.connectionGeneration), 1);
  }
  await json(base, route + '/members/' + remaining.characterId + '/admission-confirmation', 'POST', server,
    { steamId: remaining.steamId, connectionGeneration: 1 });
  const proxy = await startConfirmationHoldProxy({ upstreamPort: new URL(base).port * 1, root,
    sessionId, characterId: target.characterId, steamId: target.steamId,
    authorizationSHA256: sha(server), generation: 1, mode });
  const state = () => JSON.parse(fs.readFileSync(proxy.evidence, 'utf8'));
  const confirmationRoute = route + '/members/' + target.characterId + '/admission-confirmation';
  let downstreamResolved = false;
  // Abandonment removes membership: this is the endpoint's exact 403 branch,
  // unlike the 409 used by the older still-member reconnect-generation test.
  const expectedStatus = mode === 'before-commit' ? 403 : 200;
  const delayed = json('http://127.0.0.1:' + proxy.port, confirmationRoute, 'POST', server,
    { steamId: target.steamId, connectionGeneration: 1 }, expectedStatus);
  // Attach a rejection handler now; a closed recipient must not become an unhandled rejection.
  void delayed.then(() => { downstreamResolved = true; }, () => { downstreamResolved = true; });
  try {
    await waitFor(() => {
      const current = state();
      if (current.failure) throw new Error('Scoped confirmation fault failed');
      return mode === 'before-commit' ? current.held === 1 && current.forwarded === 0
        : current.committedResponses === 1 && current.replayedResponses === 0;
    });
    assert.equal(downstreamResolved, false);
    const before = (await json(base, route + '/membership-snapshot', 'GET', server)).body;
    const original = before.members.find(member => member.characterId === target.characterId);
    assert.ok(original);
    assert.equal(Number(original.connectionGeneration), 1);
    // Membership presentation does not expose confirmedConnectionGeneration.
    // The proxy records whether the real confirmation was forwarded/accepted;
    // do not invent a public field or infer a database generation from this GET.
    const peerBefore = before.members.find(member => member.characterId === remaining.characterId);
    const receipt = (await json(base, route + '/members/' + target.characterId + '/abandon', 'POST', target.authorization)).body;
    assert.equal(receipt.rewardExcluded, true);
    const committed = (await json(base, route + '/membership-snapshot', 'GET', server)).body;
    assert.equal(committed.state, 'InProgress');
    assert.equal(committed.members.length, 1);
    assert.equal(committed.abandonments.length, 1);
    assert.equal(committed.abandonments[0].abandonedAt, receipt.abandonedAt);
    assert.deepEqual(committed.members[0], peerBefore);
    await json(base, route + '/reward-snapshot/' + target.characterId, 'GET', server, undefined, 403);
    fs.writeFileSync(proxy.gate, '');
    const reply = await delayed;
    await waitFor(() => state().forwarded === 1 && (mode === 'before-commit' || state().replayedResponses === 1));
    if (mode !== 'before-commit') {
      assert.equal(reply.body.admissionConfirmed, true);
      assert.equal(sha(reply.text), state().heldReplySHA256[0]);
      assert.deepEqual(state().heldReplySHA256, state().replayedReplySHA256);
    }
    await pause(80); // The open gate must not resend the original request/reply.
    assert.equal(state().forwarded, 1);
    assert.equal(state().held, 1);
    assert.equal(state().failure, null);
    const after = (await json(base, route + '/membership-snapshot', 'GET', server)).body;
    assert.equal(after.state, 'InProgress');
    assert.deepEqual(after.members, committed.members);
    assert.deepEqual(after.abandonments, committed.abandonments);
    assert.equal(after.disconnectionExpulsions.length, 0);
    await json(base, confirmationRoute, 'POST', server, { steamId: target.steamId, connectionGeneration: 1 }, 403);
    await json(base, 'join-tickets', 'POST', target.authorization,
      { dungeonSessionId: sessionId, characterId: target.characterId }, 409);
    const replay = (await json(base, route + '/members/' + target.characterId + '/abandon', 'POST', target.authorization)).body;
    assert.deepEqual(replay, receipt);
    return { mode, sessionId, consumedGeneration: 1, delayedStatus: reply.status,
      confirmationAtGate: mode === 'before-commit' ? 'not-forwarded' : 'upstream-accepted-200',
      originalReplyOnly: true, noProxyReconfirmation: true, immutableReceiptPreserved: true,
      remainingMemberPreserved: true, rewardSnapshotRejected403: true, newConfirmationRejected403: true,
      freshTicketRejected409: true, proxyEvidence: proxy.evidence, proxyEvidenceSHA256: fileHash(proxy.evidence) };
  } finally { await proxy.close(); await delayed.catch(() => {}); }
}

async function main() {
  const workspace = process.cwd();
  assert.ok(fs.existsSync(path.join(workspace, 'Project_RPG.uproject')), 'Run from the project workspace');
  assert.equal(process.argv[2], '--backend-build', 'Specify a completed backend build snapshot');
  assert.equal(process.argv.length, 4, 'Only --backend-build is accepted');
  const source = fs.realpathSync(process.argv[3]);
  const savedPrefix = fs.realpathSync(path.join(workspace, 'Saved')) + path.sep;
  assert.ok(source.toLowerCase().startsWith(savedPrefix.toLowerCase()), 'Use a completed local Saved artifact');
  assert.ok(fs.existsSync(path.join(source, 'ProjectRpg.Backend.dll')));
  const taskBase = path.join(workspace, 'Saved/BuildChecks/AdmissionAbandonmentHttp-20261029');
  fs.mkdirSync(taskBase, { recursive: true });
  const taskRoot = path.join(taskBase, randomUUID().replaceAll('-', ''));
  fs.mkdirSync(taskRoot); // Never overwrite another attempt's proof.
  const build = path.join(taskRoot, 'backend-build');
  const originalFiles = files(source);
  fs.cpSync(source, build, { recursive: true, errorOnExist: true });
  const scriptFiles = [process.argv[1], path.join(workspace, 'Scripts/Network/Fixtures/DungeonConfirmationHoldProxy.mjs')]
    .map(file => ({ path: path.resolve(file), sha256: fileHash(file) }));
  const copiedFiles = files(build);
  const workspaceDll = path.join(workspace, 'Binaries/Win64/UnrealEditor-Project_RPG.dll');
  const workspaceDllBefore = fileHash(workspaceDll);
  const admin = randomUUID().replaceAll('-', '');
  const environment = { ...process.env, ASPNETCORE_ENVIRONMENT: 'Development', Storage__Provider: 'Memory',
    Steam__AllowDevelopmentAuthentication: 'true', PROJECT_RPG_BACKEND_ADMIN_TOKEN: admin,
    POSTGRES_CONNECTION_STRING: '', ConnectionStrings__Postgres: '',
    Kestrel__Endpoints__Http__Url: 'http://127.0.0.1:0', PROJECT_RPG_GAME_SERVER_ID: '',
    PROJECT_RPG_DUNGEON_SESSION_ID: '', PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN: '',
    DungeonContent__Entries__1__DungeonId: 'Dungeon.MembershipE2E',
    DungeonContent__Entries__1__MapPackage: '/Game/Maps/testmap', DungeonContent__Entries__1__Difficulties__0: 'Normal' };
  const out = fs.createWriteStream(path.join(taskRoot, 'backend.stdout.log'));
  const err = fs.createWriteStream(path.join(taskRoot, 'backend.stderr.log'));
  const backend = spawn('dotnet', [path.join(build, 'ProjectRpg.Backend.dll')],
    { cwd: build, env: environment, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  const stopped = new Promise(resolve => backend.once('close', resolve));
  let output = ''; let spawnFailed = false;
  backend.on('error', () => { spawnFailed = true; });
  backend.stdout.on('data', chunk => { output = (output + chunk.toString()).slice(-65536); });
  backend.stdout.pipe(out); backend.stderr.pipe(err);
  let results; let failure;
  try {
    const base = await waitFor(async () => {
      if (spawnFailed || backend.exitCode !== null) throw new Error('Owned backend failed before readiness');
      const match = output.match(/Now listening on: (http:\/\/127\.0\.0\.1:\d+)/);
      if (!match) return false;
      const response = await fetch(match[1] + '/health/ready', { signal: AbortSignal.timeout(2000) });
      const health = await response.json();
      assert.equal(health.storageProvider, 'Memory');
      return health.status === 'ready' && match[1];
    }, 30000);
    results = [];
    for (const mode of ['before-commit', 'after-commit-replay']) {
      const root = path.join(workspace, 'Saved/Tests/DungeonMembershipBackend', randomUUID().replaceAll('-', ''));
      fs.mkdirSync(root, { recursive: true });
      results.push(await scenario(base, 'Bearer ' + admin, root, mode));
      console.log('ADMISSION_ABANDON_HTTP ' + mode + ' passed');
    }
    assert.ok(unchanged(scriptFiles) && unchanged(copiedFiles) && unchanged(originalFiles), 'Reserved input changed');
    assert.equal(fileHash(workspaceDll), workspaceDllBefore);
  } catch (error) { failure = error.message; }
  finally {
    if (backend.exitCode === null && backend.signalCode === null) backend.kill();
    await Promise.race([stopped, pause(5000, undefined, { ref: false }).then(() => { throw new Error('Owned backend failed to stop'); })]);
    if (!out.writableFinished) await new Promise(resolve => out.once('finish', resolve));
    if (!err.writableFinished) await new Promise(resolve => err.once('finish', resolve));
  }
  const proof = { schemaVersion: 1, passed: !failure, scope: 'Real Development-auth loopback HTTP with a frozen Memory backend; no UE/Iris/PG',
    failure, cases: results ?? [], backendProcessId: backend.pid, backendSnapshot: source,
    backendModuleSHA256: fileHash(path.join(build, 'ProjectRpg.Backend.dll')), fixedBackendFiles: copiedFiles,
    scriptFiles, workspaceModuleSHA256: workspaceDllBefore, workspaceModuleUnchanged: fileHash(workspaceDll) === workspaceDllBefore,
    existingBackendReplaced: false, newBuild: false, postgres: false, unreal: false, iris: false,
    normalSharedModuleApplied: false, nativePendingConnectionIntegration: false,
    rewardSettlement: false, clockOverride: false, steam: false, shipping: false,
    closedLogs: ['backend.stdout.log', 'backend.stderr.log'].map(name => ({ path: path.join(taskRoot, name), sha256: fileHash(path.join(taskRoot, name)) })) };
  fs.writeFileSync(path.join(taskRoot, 'Http.proof.json'), JSON.stringify(proof, null, 2));
  if (failure) throw new Error(failure);
  console.log('PASS: two authoritative HTTP orderings; UE network integration remains separate. ' + taskRoot);
}
main().catch(error => { console.error('ADMISSION_ABANDON_HTTP failed: ' + error.message); process.exitCode = 1; });
