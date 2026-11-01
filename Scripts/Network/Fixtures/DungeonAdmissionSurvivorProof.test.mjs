import test from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import { assertSurvivorMembershipPreserved, admissionRuntimeEnvironment } from '../Run-DungeonAdmissionAbandonmentNetworkTest.mjs';

const now=Date.parse('2026-11-01T00:00:00Z');
const before={characterId:'11111111-1111-1111-1111-111111111111',characterName:'Survivor',steamId:'76561198000000001',
  connectionGeneration:'1',disconnectedAt:null,disconnectedGeneration:null,reconnectExpiresAt:null,
  leaseExpiresAt:'2026-11-01T00:01:00.1234567+00:00'};
test('unchanged active survivor lease is valid',()=>{
  assert.equal(assertSurvivorMembershipPreserved(before,structuredClone(before),now).leaseRenewed,false);
});
test('normal heartbeat may extend only the lease, retaining every other field',()=>{
  const after={...before,leaseExpiresAt:'2026-11-01T00:01:33.586849+00:00'};
  assert.equal(assertSurvivorMembershipPreserved(before,after,now).leaseRenewed,true);
  assert.equal(before.leaseExpiresAt,'2026-11-01T00:01:00.1234567+00:00');
});
for(const [name,change] of [
  ['other character',v=>{v.characterId='22222222-2222-2222-2222-222222222222';}],
  ['other account',v=>{v.steamId='76561198000000002';}],
  ['generation change',v=>{v.connectionGeneration='2';}],
  ['generation type change',v=>{v.connectionGeneration=1;}],
  ['new disconnection',v=>{v.disconnectedAt='2026-11-01T00:00:01Z';}],
  ['missing state field',v=>{delete v.disconnectedGeneration;}],
  ['microsecond regression',v=>{v.leaseExpiresAt='2026-11-01T00:01:00.1234566+00:00';}],
  ['expired lease',v=>{v.leaseExpiresAt='2026-10-31T23:59:59Z';}],
  ['missing lease',v=>{delete v.leaseExpiresAt;}],
  ['non-string lease',v=>{v.leaseExpiresAt=true;}],
  ['malformed lease',v=>{v.leaseExpiresAt='not-a-date';}],
])test(`survivor proof refuses ${name}`,()=>{
  const after=structuredClone(before);change(after);
  assert.throws(()=>assertSurvivorMembershipPreserved(before,after,now));
});
test('missing survivor and a non-finite observation clock fail closed',()=>{
  assert.throws(()=>assertSurvivorMembershipPreserved(before,null,now));
  assert.throws(()=>assertSurvivorMembershipPreserved(before,before,NaN));
});
for(const server of [false,true])test(`UE ${server?'server':'client'} receives no database/admin/proxy secrets`,()=>{
  const secrets=['PROJECT_RPG_BACKEND_ADMIN_TOKEN','POSTGRES_CONNECTION_STRING','ConnectionStrings__Postgres',
    'PROJECT_RPG_PENDING_ADMISSION_POSTGRES','PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN'];
  const base={...process.env,...Object.fromEntries(secrets.map(key=>[key,'fixture-private-value'])),PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN:'wrong-parent-scope'};
  const env=admissionRuntimeEnvironment(base,{server,serverId:'own-server',sessionId:'own-session',gameServerToken:'scoped-fixture-value'});
  for(const key of secrets)assert.equal(env[key],'');
  assert.equal(env.PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN,server?'scoped-fixture-value':'');
  assert.equal(base.PROJECT_RPG_BACKEND_ADMIN_TOKEN,'fixture-private-value','parent environment must not be mutated');
  const script=`const keys=${JSON.stringify(secrets)}; if(keys.some(k=>process.env[k]))process.exit(2); if(Boolean(process.env.PROJECT_RPG_BACKEND_GAME_SERVER_TOKEN)!==${server})process.exit(3); console.log('BOUNDARY_OK')`;
  const child=spawnSync(process.execPath,['-e',script],{env,encoding:'utf8',windowsHide:true,timeout:5000});
  assert.equal(child.status,0);assert.equal(child.stdout.trim(),'BOUNDARY_OK');assert.equal(child.stderr,'');
});
test('runtime environment requires an explicit role and exact assigned server scope',()=>{
  assert.throws(()=>admissionRuntimeEnvironment({},{}));
  assert.throws(()=>admissionRuntimeEnvironment({},{server:true,serverId:'own',sessionId:'own',gameServerToken:''}));
});
