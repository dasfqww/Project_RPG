// Bounded actual UE/Iris first-admission race. No fixture ticket consumption,
// admission completion, receipt injection, settlement, database or clock writes.
import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';
import dgram from 'node:dgram';
import { spawn, spawnSync } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { setTimeout as pause } from 'node:timers/promises';
import { pathToFileURL } from 'node:url';
import { startConfirmationHoldProxy } from './Fixtures/DungeonConfirmationHoldProxy.mjs';

const sha = value => createHash('sha256').update(value).digest('hex');
const hash = file => sha(fs.readFileSync(file));
function files(root) {
  return fs.readdirSync(root, { withFileTypes: true }).flatMap(entry => {
    const file = path.join(root, entry.name);
    assert.equal(entry.isSymbolicLink(), false, 'Backend snapshot must not contain links');
    return entry.isDirectory() ? files(file) : [{ path: file, sha256: hash(file) }];
  });
}
const fixed = list => list.every(input => fs.existsSync(input.path) && hash(input.path) === input.sha256);
const read = file => fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : '';
export function assertSurvivorMembershipPreserved(before, after, now = Date.now()) {
  assert.ok(before && after && typeof before==='object' && typeof after==='object'
    && !Array.isArray(before) && !Array.isArray(after),'Missing survivor membership');
  const {leaseExpiresAt: beforeLease,...beforeIdentity}=before;
  const {leaseExpiresAt: afterLease,...afterIdentity}=after;
  assert.deepEqual(afterIdentity,beforeIdentity,'Survivor identity, generation or disconnection state changed');
  const leaseNanoseconds=value=>{
    assert.equal(typeof value,'string','Missing survivor lease timestamp');
    const match=value.match(/^(\d{4}-\d{2}-\d{2}T\d{2}:\d{2}:\d{2})(?:\.(\d{1,7}))?(Z|[+-]\d{2}:\d{2})$/);
    assert.ok(match,'Invalid survivor lease timestamp');
    const whole=Date.parse(match[1]+match[3]);assert.ok(Number.isFinite(whole),'Invalid survivor lease instant');
    return BigInt(whole)*1000000n+BigInt((match[2] ?? '').padEnd(9,'0'));
  };
  const previous=leaseNanoseconds(beforeLease), current=leaseNanoseconds(afterLease);
  assert.ok(Number.isFinite(now) && now>=0,'Invalid observation clock');
  assert.ok(current>=previous,'Survivor lease regressed');
  assert.ok(current>BigInt(Math.trunc(now))*1000000n,'Survivor lease is expired');
  return {leaseRenewed:current>previous,leaseExpiresAtBefore:beforeLease,leaseExpiresAtAfter:afterLease};
}
export function admissionRuntimeEnvironment(base, {server,serverId,sessionId,gameServerToken}) {
  assert.equal(typeof server,'boolean','Explicit UE role required');
  const runtime={...base,PROJECT_RPG_BACKEND_ADMIN_TOKEN:'',POSTGRES_CONNECTION_STRING:'',ConnectionStrings__Postgres:'',
    PROJECT_RPG_PENDING_ADMISSION_POSTGRES:'',PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN:'',
    PROJECT_RPG_GAME_SERVER_ID:server?serverId:'',PROJECT_RPG_DUNGEON_SESSION_ID:server?sessionId:'',
    PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN:server?gameServerToken:''};
  for(const key of ['PROJECT_RPG_BACKEND_ADMIN_TOKEN','POSTGRES_CONNECTION_STRING','ConnectionStrings__Postgres',
    'PROJECT_RPG_PENDING_ADMISSION_POSTGRES','PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN'])assert.equal(runtime[key],'');
  if(server)for(const value of [serverId,sessionId,gameServerToken])assert.ok(typeof value==='string' && value.length>0,'Missing assigned server scope');
  return runtime;
}
async function waitFor(observe, label, milliseconds = 30000) {
  const end = Date.now() + milliseconds;
  while (Date.now() < end) {
    const value = await observe();
    if (value) return value;
    await pause(100);
  }
  throw new Error('Timed out: ' + label);
}
async function json(base, route, method = 'GET', authorization, body, status = 200) {
  const response = await fetch(base + '/api/' + route, { method,
    headers: { ...(authorization ? { authorization } : {}), ...(body ? { 'content-type': 'application/json' } : {}) },
    body: body ? JSON.stringify(body) : undefined, signal: AbortSignal.timeout(12000) });
  assert.equal(response.status, status, `HTTP ${method} ${route}`);
  const text = await response.text();
  return text ? JSON.parse(text) : null;
}
function launch(executable, args, options, name, root) {
  const stdout = fs.createWriteStream(path.join(root, name + '.stdout.log'));
  const stderr = fs.createWriteStream(path.join(root, name + '.stderr.log'));
  const child = spawn(executable, args, { ...options, windowsHide: true, stdio: ['ignore', 'pipe', 'pipe'] });
  const owned = { child, name, stdout, stderr, output: '', failed: false, cleanupRequested: false };
  owned.closed = new Promise(resolve => child.once('close', (code, signal) => resolve({ code, signal })));
  child.on('error', () => { owned.failed = true; });
  child.stdout.on('data', chunk => { owned.output = (owned.output + chunk.toString()).slice(-65536); });
  child.stdout.pipe(stdout); child.stderr.pipe(stderr);
  return owned;
}
function running(owned) {
  assert.ok(!owned.failed && owned.child.exitCode === null && owned.child.signalCode === null,
    'Owned process stopped: ' + owned.name);
}
async function stopAll(owned) {
  // Signal the owned server first, then all clients/backend without intervening
  // waits: cleanup is not another unexpected-disconnection test.
  const ordered = [...owned].sort((a, b) => (a.name === 'server' ? -1 : b.name === 'server' ? 1 : 0));
  for (const entry of ordered) {
    if (entry.child.exitCode === null && entry.child.signalCode === null) {
      entry.cleanupRequested = true;
      entry.child.kill();
    }
  }
  for (const entry of ordered) {
    entry.exit = await Promise.race([entry.closed, pause(10000).then(() => { throw new Error('Owned process did not stop: ' + entry.name); })]);
    for (const stream of [entry.stdout, entry.stderr]) {
      if (!stream.writableFinished) await new Promise(resolve => stream.once('finish', resolve));
    }
  }
}
async function unusedUdp(port) {
  const socket = dgram.createSocket('udp4');
  try {
    await new Promise((resolve, reject) => { socket.once('error', reject); socket.bind(port, '0.0.0.0', resolve); });
  } finally { socket.close(); }
}
function loadedModule(owned, expected) {
  // Read only this runner's exact child process. No cross-agent process control.
  const command = `$ErrorActionPreference='Stop'; $m=@((Get-Process -Id ${owned.child.pid}).Modules | Where-Object {$_.ModuleName -eq 'UnrealEditor-Project_RPG.dll'}); if($m.Count -ne 1){throw 'Module count'}; [pscustomobject]@{path=$m[0].FileName; sha256=(Get-FileHash -LiteralPath $m[0].FileName -Algorithm SHA256).Hash} | ConvertTo-Json -Compress`;
  const observed = spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-EncodedCommand', Buffer.from(command, 'utf16le').toString('base64')],
    { windowsHide: true, encoding: 'utf8', timeout: 10000 });
  assert.equal(observed.status, 0, 'Owned module observation failed');
  const module = JSON.parse(observed.stdout.replace(/^\uFEFF/, '').trim());
  assert.equal(fs.realpathSync(module.path).toLowerCase(), fs.realpathSync(expected).toLowerCase());
  assert.equal(module.sha256.toLowerCase(), hash(expected));
  return { pid: owned.child.pid, path: module.path, sha256: module.sha256.toLowerCase() };
}

async function main() {
  assert.ok([6,8,10,12].includes(process.argv.length), 'Use --backend-build <Saved snapshot> --runtime-project <Saved .uproject> [--confirmation-mode before-commit] [--storage-provider Postgres] [--run-id <fresh UUID hex>]');
  assert.equal(process.argv[2], '--backend-build'); assert.equal(process.argv[4], '--runtime-project');
  const options=new Map();
  for(let index=6;index<process.argv.length;index+=2){
    const name=process.argv[index];assert.ok(['--confirmation-mode','--storage-provider','--run-id'].includes(name) && !options.has(name),'Invalid or duplicate scenario option');
    options.set(name,process.argv[index+1]);
  }
  const mode=options.get('--confirmation-mode') ?? 'after-commit-replay';
  const storage=options.get('--storage-provider') ?? 'Memory';
  assert.ok(['before-commit','after-commit-replay'].includes(mode),'Invalid confirmation mode');
  assert.ok(['Memory','Postgres'].includes(storage),'Invalid storage provider');
  const beforeCommit=mode==='before-commit';
  assert.ok(storage!=='Postgres' || beforeCommit,'Postgres is scoped to the new before-commit scenario');
  // No arbitrary remote database is accepted by this development fixture.
  const postgres=storage==='Postgres' ? process.env.PROJECT_RPG_PENDING_ADMISSION_POSTGRES : '';
  if(storage==='Postgres')assert.ok(/^Host=127\.0\.0\.1;Port=54329;Database=project_rpg;Username=project_rpg;Password=[^;\r\n]+$/.test(postgres ?? ''),'An explicit local development database is required');
  const workspace = fs.realpathSync(process.cwd());
  assert.ok(fs.existsSync(path.join(workspace, 'Project_RPG.uproject')));
  const saved = fs.realpathSync(path.join(workspace, 'Saved')) + path.sep;
  const source = fs.realpathSync(process.argv[3]);
  const project = fs.realpathSync(process.argv[5]);
  for (const input of [source, project]) assert.ok(input.toLowerCase().startsWith(saved.toLowerCase()), 'Use local Saved artifacts');
  assert.equal(path.basename(project), 'Project_RPG.uproject');
  const module = path.join(path.dirname(project), 'Binaries/Win64/UnrealEditor-Project_RPG.dll');
  const executable = 'D:/UE_5.8/Engine/Binaries/Win64/UnrealEditor.exe';
  assert.ok(fs.existsSync(module) && fs.existsSync(executable) && fs.existsSync(path.join(source, 'ProjectRpg.Backend.dll')));
  const port = 18879;
  await unusedUdp(port);
  const id = options.get('--run-id') ?? randomUUID().replaceAll('-', '');
  assert.ok(/^[0-9a-f]{32}$/.test(id),'Invalid execution identity');
  const taskRoot = path.join(workspace, beforeCommit ? 'Saved/BuildChecks/PendingAdmissionBeforeCommit-20261101' : 'Saved/BuildChecks/PendingAdmissionIris-20261029', id);
  const proxyRoot = path.join(workspace, 'Saved/Tests/DungeonMembershipBackend', id);
  const userRoot = path.join(workspace, 'Saved/PIA', id.slice(0, 12));
  for(const fresh of [taskRoot,proxyRoot,userRoot])assert.equal(fs.existsSync(fresh),false,'Refusing to reuse an execution directory');
  fs.mkdirSync(taskRoot, { recursive: true }); fs.mkdirSync(proxyRoot); fs.mkdirSync(userRoot, { recursive: true });
  const build = path.join(taskRoot, 'backend-build');
  const sourceFiles = files(source);
  fs.cpSync(source, build, { recursive: true, errorOnExist: true });
  const scripts = [process.argv[1], 'Scripts/Network/Fixtures/DungeonConfirmationHoldProxy.mjs',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.h',
    'Source/Project_RPG/Private/Tests/RPGDungeonFlowE2EProbe.cpp',
    'Source/Project_RPG/Private/Tests/RPGDungeonAdmissionAbandonmentProbe.cpp',
    'Source/Project_RPG/Public/GameMode/RPGGameModeBase.h',
    'Source/Project_RPG/Private/GameMode/RPGGameModeBase.cpp',
    'Source/Project_RPG/Private/GameMode/RPGGameModePendingAdmission.cpp',
    'Content/Maps/bossmap.umap'].map(file => {
      const resolved=beforeCommit && /^(Source|Content)[/\\]/.test(file) ? path.resolve(path.dirname(project),file) : path.resolve(file);
      return {path:resolved,sha256:hash(resolved)};
    });
  const runtimeFiles = [module, project, ...files(path.join(path.dirname(project), 'Config')).map(file => file.path)]
    .map(file => ({ path: file, sha256: hash(file) }));
  const backendFiles = files(build);
  const sharedModule = path.join(workspace, 'Binaries/Win64/UnrealEditor-Project_RPG.dll');
  const sharedHash = beforeCommit ? null : hash(sharedModule);
  const admin = randomUUID().replaceAll('-', '');
  const env = { ...process.env, ASPNETCORE_ENVIRONMENT: 'Development', Storage__Provider: storage,
    Steam__AllowDevelopmentAuthentication: 'true', PROJECT_RPG_BACKEND_ADMIN_TOKEN: admin,
    POSTGRES_CONNECTION_STRING: postgres, ConnectionStrings__Postgres: postgres,
    PROJECT_RPG_PENDING_ADMISSION_POSTGRES: '',
    Kestrel__Endpoints__Http__Url: 'http://127.0.0.1:0', PROJECT_RPG_GAME_SERVER_ID: '',
    PROJECT_RPG_DUNGEON_SESSION_ID: '', PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN: '' };
  const processes = [];
  const backend = launch('dotnet', [path.join(build, 'ProjectRpg.Backend.dll')], { cwd: build, env }, 'backend', taskRoot);
  processes.push(backend);
  let proxy; let result; let failure;
  const gate = path.join(proxyRoot, 'never-open-victory.gate');
  const logs = [];
  try {
    const base = await waitFor(async () => {
      running(backend);
      const match = backend.output.match(/Now listening on: (http:\/\/127\.0\.0\.1:\d+)/);
      if (!match) return false;
      const health = await (await fetch(match[1] + '/health/ready', { signal: AbortSignal.timeout(2000) })).json();
      assert.equal(health.storageProvider, storage);
      return health.status === 'ready' && match[1];
    }, 'Memory host');
    const members = [];
    for (let index = 0; index < 2; index++) {
      const steamId = (76561400000000000n + BigInt('0x' + id.slice(0, 8)) * 4n + BigInt(index)).toString();
      const auth = await json(base, 'auth/steam-ticket', 'POST', undefined, { ticket: 'dev:' + steamId });
      const authorization = 'Bearer ' + auth.accessToken;
      const character = await json(base, 'characters', 'POST', authorization, { name: 'Iris' + id.slice(0, 10) + index }, 201);
      members.push({ steamId, authorization, characterId: character.characterId });
    }
    const [target, peer] = members;
    let session = await json(base, 'dungeon-sessions', 'POST', target.authorization,
      { characterId: target.characterId, dungeonId: 'Dungeon.RaidSwordMan', difficulty: 'Hard' }, 201);
    const sessionId = session.dungeonSessionId;
    const route = 'dungeon-sessions/' + sessionId;
    session = await json(base, route + '/members', 'POST', peer.authorization, { characterId: peer.characterId });
    const revision = Number(session.lobbyRevision);
    assert.ok(Number.isSafeInteger(revision) && revision > 0 && session.members.length === 2);
    await json(base, route + '/departure', 'POST', target.authorization,
      { characterId: target.characterId, requestId: randomUUID(), expectedLobbyRevision: revision });
    const serverId = 'pending-iris-' + id.slice(0, 12);
    const assignment = await json(base, route + '/activate', 'POST', 'Bearer ' + admin,
      { serverId, serverAddress: '127.0.0.1:' + port });
    const serverAuth = 'Bearer ' + assignment.gameServerAccessToken;
    proxy = await startConfirmationHoldProxy({ upstreamPort: Number(new URL(base).port), root: proxyRoot,
      sessionId, characterId: target.characterId, steamId: target.steamId,
      authorizationSHA256: sha(serverAuth), generation: 1, mode });
    const state = () => JSON.parse(read(proxy.evidence));
    const common = ['-UseIrisReplication=1', '-RPGRequireIris', '-unattended', '-nop4', '-nosplash', '-nosound',
      '-nosteam', '-NoLiveCoding', '-NoLoadingScreen', '-DisablePython', '-DDC=(EnginePak,Local)', '-nullrhi',
      '-RPGDungeonMembershipBackendE2E', '-RPGExpectedClients=2', '-RPGDungeonFlowE2E',
      '-RPGPendingAdmissionAbandonmentE2E', '-RPGAbandonCharacter=' + target.characterId,
      '-ExecCmds=t.MaxFPS 30',
      '-ini:EditorPerProjectUserSettings:[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]:bAutoStartServer=False',
      '-ini:Game:[/Script/Project_RPG.HttpWebManager]:RequestTimeoutSeconds=30',
      '-ini:Game:[/Script/RPGInventoryRuntime.RPGItemBackendSubsystem]:ApiUrl=' + base + '/api',
      '-ini:Game:[/Script/Project_RPG.RPGEconomyBackendSubsystem]:ApiUrl=' + base + '/api'];
    function unreal(name, destination, args, webBase, server = false) {
      const log = path.join(taskRoot, name + '.log'); logs.push(log);
      const runtimeEnv = admissionRuntimeEnvironment(env,{server,serverId,sessionId,gameServerToken:assignment.gameServerAccessToken});
      const entry = launch(executable, [project, destination, ...common, ...args, '-abslog=' + log,
        '-UserDir=' + path.join(userRoot, name),
        '-ini:Game:[/Script/Project_RPG.HttpWebManager]:ApiUrl=' + webBase + '/api'],
      { cwd: path.dirname(project), env: runtimeEnv }, name, taskRoot);
      entry.log = log; processes.push(entry); return entry;
    }
    const server = unreal('server', '/Game/Maps/bossmap', ['-server', '-port=' + port,
      '-RPGItemE2EServer', '-RPGItemE2EAllowUnverifiedIdentity', '-RPGFlowE2EPlayers=2', '-RPGFlowE2EGate=' + gate],
    'http://127.0.0.1:' + proxy.port, true);
    const inspect = () => {
      for (const entry of processes) running(entry);
      for (const log of logs) assert.ok(!/DUNGEON_FLOW_E2E_FAIL|Fatal error:|Assertion failed:|Ensure condition failed:/.test(read(log)), 'Native probe failure: ' + path.basename(log));
      assert.equal(state().failure, null, 'Confirmation proxy failed');
    };
    async function marker(entry, value, milliseconds = 30000) {
      await waitFor(() => { inspect(); return read(entry.log).includes(value); }, value, milliseconds);
    }
    await marker(server, 'DUNGEON_FLOW_E2E_SERVER_READY', 120000);
    // /start and both ticket consumptions are exclusively made by UE production.
    assert.equal((await json(base, route + '/membership-snapshot', 'GET', serverAuth)).state, 'InProgress');
    const clientArgs = member => ['-game', '-RPGFlowE2ESteam=' + member.steamId,
      '-RPGFlowE2ECharacter=' + member.characterId, '-RPGFlowE2ESession=' + sessionId];
    const survivor = unreal('survivor', '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase', clientArgs(peer), base);
    await marker(survivor, 'DUNGEON_FLOW_E2E_ADMITTED', 120000);
    const pendingClient = unreal('target', '/Engine/Maps/Entry?game=/Script/Engine.GameModeBase', clientArgs(target), base);
    await waitFor(() => { inspect(); return beforeCommit
      ? state().held === 1 && state().forwarded === 0 && state().committedResponses === 0
      : state().committedResponses === 1 && state().replayedResponses === 0; }, beforeCommit ? 'original confirmation held before forwarding' : 'original confirmation committed', 90000);
    await marker(server, 'DUNGEON_PENDING_ADMISSION HELD');
    await marker(pendingClient, 'DUNGEON_PENDING_ADMISSION TARGET_CONNECT');
    const loadedModules = [server, survivor, pendingClient].map(entry => loadedModule(entry, module));
    const before = await json(base, route + '/membership-snapshot', 'GET', serverAuth);
    const peerBefore = before.members.find(member => member.characterId === peer.characterId);
    assert.equal(Number(before.members.find(member => member.characterId === target.characterId).connectionGeneration), 1);
    const receipt = await json(base, route + '/members/' + target.characterId + '/abandon', 'POST', target.authorization);
    assert.equal(receipt.rewardExcluded, true);
    await marker(server, 'DUNGEON_PENDING_ADMISSION CLOSED', 15000);
    assert.equal(state().replayedResponses, 0);
    assert.equal(state().committedResponses, beforeCommit ? 0 : 1);
    fs.writeFileSync(proxy.gate, '');
    await waitFor(() => { inspect(); return beforeCommit
      ? state().forwarded === 1 && state().statuses.length === 1
      : state().replayedResponses === 1; }, beforeCommit ? 'original held request forwarded after abandonment' : 'original 200 replay');
    await marker(server, 'DUNGEON_PENDING_ADMISSION LATE_REPLY_INERT', 15000);
    assert.equal(state().forwarded, 1); assert.equal(state().held, 1);
    if(beforeCommit){
      assert.equal(state().committedResponses,0);assert.equal(state().replayedResponses,0);
      assert.equal(state().statuses[0],403);assert.equal(state().heldReplySHA256.length,0);
    }else{
      assert.deepEqual(state().heldReplySHA256, state().replayedReplySHA256);
      assert.equal(state().statuses[0],200);
    }
    assert.ok(!read(pendingClient.log).includes('DUNGEON_FLOW_E2E_ADMITTED'));
    const after = await json(base, route + '/membership-snapshot', 'GET', serverAuth);
    assert.equal(after.state, 'InProgress'); assert.equal(after.members.length, 1);
    const survivorLease=assertSurvivorMembershipPreserved(peerBefore,after.members[0]);
    assert.equal(after.abandonments.length, 1); assert.equal(after.disconnectionExpulsions.length, 0);
    assert.equal(after.abandonments[0].abandonedAt, receipt.abandonedAt);
    await json(base, route + '/reward-snapshot/' + target.characterId, 'GET', serverAuth, undefined, 403);
    await json(base, 'join-tickets', 'POST', target.authorization, { dungeonSessionId: sessionId, characterId: target.characterId }, 409);
    inspect();
    result = { sessionId, loadedModules, confirmationMode: mode, originalConfirmationStatus: beforeCommit ? 403 : 200, realUeStartAndTicketConsumption: true,
      originalRequestHeldBeforeForwarding: beforeCommit, noConfirmationCommitted: beforeCommit,
      firstGeneration: 1, pendingSocketObserved: true, receiptPolledByProduction: true,
      closedBeforeReplyRelease: true, callbackOwnerReleased: true, lateReplyDidNotPublishAuthority: true,
      survivorConnectionIdentityPawnHealthPreserved: true, immutableReceiptPreserved: true,
      survivorLease,
      administrativeAndDatabaseCredentialsExcludedFromUe:true,assignedServerTokenExcludedFromClients:true,
      rewardRejected403: true, freshTicketRejected409: true,
      proxyEvidence: proxy.evidence, proxyEvidenceSHA256: hash(proxy.evidence) };
    console.log(beforeCommit ? 'PENDING_ADMISSION_IRIS: close before original request forwarding; actual late confirmation denied; survivor preserved.'
      : 'PENDING_ADMISSION_IRIS: close before original 200; late callback inert; survivor preserved.');
  } catch (error) { failure = error.message; }
  finally {
    await stopAll(processes);
    if (proxy) await proxy.close();
  }
  const inputsUnchanged = fixed(scripts) && fixed(runtimeFiles) && fixed(backendFiles) && fixed(sourceFiles);
  if (!inputsUnchanged || (!beforeCommit && hash(sharedModule) !== sharedHash)) failure ??= 'Reserved inputs changed';
  const proof = { schemaVersion: 1, passed: !failure, failure, runId: id, scope: `Actual UE/Iris sockets + Development authentication/${storage} HTTP; isolated module; ${mode}`,
    result, scripts, runtimeFiles, backendFiles, sourceSnapshot: source, inputsUnchanged,
    sharedModuleSHA256: sharedHash, sharedModuleUnchanged: beforeCommit ? null : hash(sharedModule) === sharedHash,
    processes: processes.map(entry => ({ name: entry.name, pid: entry.child.pid, exit: entry.exit, ownedCleanupRequested: entry.cleanupRequested })),
    sharedModuleIsRuntimeInput: !beforeCommit, confirmationMode: mode,
    postgres: storage==='Postgres', steam: false, shipping: false, normalSharedModuleApplied: false,
    settlement: false, naturalCombat: false, clockOverride: false,
    closedLogs: [...logs, ...processes.flatMap(entry => [path.join(taskRoot, entry.name + '.stdout.log'), path.join(taskRoot, entry.name + '.stderr.log')])]
      .filter(file => fs.existsSync(file)).map(file => ({ path: file, sha256: hash(file) })) };
  if(beforeCommit){
    // All owned writers are closed. Preserve small evidence before removing only
    // this execution's generated backend copy, proxy root and UserDir.
    proof.cleanup={complete:false,deletedBytes:0,remaining:[],failure:null};
    const targets=[build,proxyRoot,userRoot];
    try{
      if(proxy && fs.existsSync(proxy.evidence)){
        const retained=path.join(taskRoot,'ConfirmationHold.proof.json');fs.copyFileSync(proxy.evidence,retained,fs.constants.COPYFILE_EXCL);
        if(result){result.proxyEvidence=retained;assert.equal(hash(retained),result.proxyEvidenceSHA256);}
      }
      const expected=[path.join(taskRoot,'backend-build'),path.join(workspace,'Saved/Tests/DungeonMembershipBackend',id),path.join(workspace,'Saved/PIA',id.slice(0,12))];
      for(let index=0;index<targets.length;index++){
        const target=path.resolve(targets[index]);assert.equal(target,path.resolve(expected[index]));
        assert.ok(/^[0-9a-f]{32}$/.test(id) && target.startsWith(saved),'Cleanup must remain in the owned Saved subtree');
        for(let parent=target;parent && parent!==path.dirname(parent);parent=path.dirname(parent)){
          if(fs.existsSync(parent))assert.equal(fs.lstatSync(parent).isSymbolicLink(),false,'Refuse cleanup through a reparse ancestor');
        }
        if(!fs.existsSync(target))continue;
        const entries=files(target);const bytes=entries.reduce((sum,entry)=>sum+fs.statSync(entry.path).size,0);
        fs.rmSync(target,{recursive:true,force:false});proof.cleanup.deletedBytes+=bytes;
      }
      proof.cleanup.remaining=targets.filter(target=>fs.existsSync(target));assert.equal(proof.cleanup.remaining.length,0);
      proof.cleanup.complete=true;
    }catch(error){proof.cleanup.failure=error.message;proof.cleanup.remaining=targets.filter(target=>fs.existsSync(target));failure ??='Owned temporary cleanup failed';}
    proof.snapshotHashesAreHistorical=true;proof.passed=!failure;proof.failure=failure;
  }
  fs.writeFileSync(path.join(taskRoot, 'Network.proof.json'), JSON.stringify(proof, null, 2));
  if (failure) throw new Error(failure + '; evidence: ' + taskRoot);
  console.log('PASS: ' + taskRoot);
}
if(process.argv[1] && import.meta.url===pathToFileURL(path.resolve(process.argv[1])).href)
  main().catch(error => { console.error('PENDING_ADMISSION_IRIS failed: ' + error.message); process.exitCode = 1; });
