import test, { after } from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import path from 'node:path';
import os from 'node:os';
import { mkdtemp, mkdir, writeFile, rm, lstat, readdir } from 'node:fs/promises';
import { spawn } from 'node:child_process';
import { once } from 'node:events';
import { randomBytes, createHash } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { canonicalCommand } from './PreparedPickupHttpObservation.mjs';

const id = n => `${n}${n}${n}${n}${n}${n}${n}${n}-${n}${n}${n}${n}-${n}${n}${n}${n}-${n}${n}${n}${n}-${n}${n}${n}${n}${n}${n}${n}${n}${n}${n}${n}${n}`;
const sessionId=id(1), characterId=id(2), dropId=id(3), requestId=id(4), itemId=id(5);
const serverId='prepared-fixture', authorization='Bearer fixture-server-secret';
const sha = value => createHash('sha256').update(value).digest('hex');
let deletedFixtureBytes=0, deletedFixtureRoots=0;
async function fixtureBytes(filename) {
  const info=await lstat(filename);
  if(info.isSymbolicLink())return 0;
  if(info.isFile())return info.size;
  let bytes=0;for(const name of await readdir(filename))bytes+=await fixtureBytes(path.join(filename,name));return bytes;
}
after(()=>console.log(`PREPARED_FIXTURE_CLEANUP deletedBytes=${deletedFixtureBytes} deletedRoots=${deletedFixtureRoots}`));
const command = {
  requestId, operation:'Item.Pickup', commandFingerprint:'prepared-private-fingerprint',
  actor:{type:'Character',ownerId:characterId}, affectedQuantity:1,
  mutations:[{expectedRevision:'0',newRecord:{definitionType:'RPGItemDefinition',definitionName:'GameItem.Equipment.Weapon.GreatSword',
    owner:{type:'Character',ownerId:characterId},location:{containerType:'Inventory',containerId:characterId,slotIndex:0},
    state:{instanceId:itemId,quantity:1,generationSeed:123},metadata:{creationSource:'WorldPickup'},revision:'0'}}],
};
const envelope={dropId,command};
const prefix=`/api/dungeon-sessions/${sessionId}/pickup-intents`;
async function fixture(t, {journalChange, responseChange, fault, slotConflict=false, revisionConflict=false}={}) {
  const temporaryRoot=await mkdtemp(path.join(os.tmpdir(),'rpg-prepared-observer-'));
  const journalRoot=path.join(temporaryRoot,sessionId);
  await mkdir(journalRoot);
  const journal={schemaVersion:2,serverId,sessionId,dropId,body:JSON.stringify(command)};
  if(journalChange) journalChange(journal);
  const journalFile=path.join(journalRoot,`${requestId}.json`);
  await writeFile(journalFile,JSON.stringify(journal));
  const requests=[];
  let preparations=0, commits=0, slotOccupied=slotConflict || revisionConflict;
  const preparedAt='2026-10-16T00:00:00Z', consumedAt='2026-10-16T00:00:01Z';
  const backend=http.createServer(async (request,response)=>{
    const chunks=[]; for await(const chunk of request) chunks.push(chunk);
    const bytes=Buffer.concat(chunks); requests.push({url:request.url,bytes});
    if(request.headers.authorization!==authorization) {response.writeHead(403).end();return;}
    let document, statusCode=200;
    if(request.url===prefix || request.url===`${prefix}/${requestId}`) {
      const normalized=JSON.parse(canonicalCommand(command));
      const status=request.method==='GET'?'Prepared':(preparations++===0?'Prepared':'AlreadyPrepared');
      document={status,dungeonSessionId:sessionId,serverId,dropId,requestId,characterId,itemId,
        preparedAt,consumedAt:commits?consumedAt:null,command:normalized};
    } else if(request.url===`${prefix}/${requestId}/commit` || request.url===`/api/item-transactions/${requestId}`) {
      if(slotOccupied && request.method==='POST') {
        statusCode=409;document={status:revisionConflict?'RevisionConflict':'LocationConflict',error:'pickup_commit_not_completed'};
      } else {
      const status=request.method==='POST'?(commits++===0?'Committed':'AlreadyCommitted'):'Committed';
      document={status,requestId,operation:command.operation,commandFingerprint:command.commandFingerprint,
        actor:command.actor,affectedQuantity:1,records:[{...command.mutations[0].newRecord,revision:1}]};
      }
    } else document={items:request.url.includes(`ownerId=${characterId}`)?[{...command.mutations[0].newRecord,revision:1}]:[]};
    if(responseChange) responseChange(document,request.url);
    response.writeHead(statusCode,{'content-type':'application/json'}).end(JSON.stringify(document));
  });
  await new Promise(resolve=>backend.listen(0,'127.0.0.1',resolve));
  const token=randomBytes(32).toString('hex');
  const child=spawn(process.execPath,[fileURLToPath(new URL('./ItemPickupHttpFaultProxy.mjs',import.meta.url))],{
    env:{...process.env,PROJECT_RPG_ITEM_PROXY_CONTROL_TOKEN:token,PROJECT_RPG_ITEM_PROXY_UPSTREAM_PORT:String(backend.address().port),
      PROJECT_RPG_ITEM_PROXY_LISTEN_PORT:'0',PROJECT_RPG_ITEM_PROXY_JOURNAL_ROOT:journalRoot},stdio:['ignore','pipe','pipe'],
  });
  let output='',errors=''; child.stderr.on('data',chunk=>{errors+=chunk;});
  t.after(async()=>{
    if(child.exitCode===null && child.signalCode===null){const stopped=once(child,'exit');child.kill();await stopped;}
    backend.closeAllConnections(); await new Promise(resolve=>backend.close(resolve));
    const resolved=path.resolve(temporaryRoot);
    assert.equal(path.dirname(resolved),path.resolve(os.tmpdir()));
    assert.ok(path.basename(resolved).startsWith('rpg-prepared-observer-'));
    const bytes=await fixtureBytes(resolved);
    await rm(resolved,{recursive:true,force:true});
    deletedFixtureBytes+=bytes;deletedFixtureRoots++;
    for(const secret of [token,authorization,command.commandFingerprint,JSON.stringify(command)]) {
      assert.ok(!output.includes(secret) && !errors.includes(secret),'fixture leaked sensitive content');
    }
  });
  const ports=await new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>reject(new Error('Proxy readiness timeout')),5000);
    child.once('error',error=>{clearTimeout(timer);reject(error);});
    child.once('exit',()=>{clearTimeout(timer);reject(new Error('Proxy exited'));});
    child.stdout.on('data',chunk=>{
      output+=chunk; const match=output.match(/^ITEM_PROXY_READY ControlPort=(\d+) ProxyPort=(\d+)\r?\n$/);
      if(match){clearTimeout(timer);resolve({control:match[1],proxy:match[2]});}
    });
  });
  const control=async(route,value)=>fetch(`http://127.0.0.1:${ports.control}/${route}`,{
    headers:{authorization:`Bearer ${token}`,'content-type':'application/json'},...(value?{method:'POST',body:JSON.stringify(value)}:{})});
  const arm=await control('arm',{protocol:'prepared',serverId,sessionId,characterIds:[characterId,id(6)],authorizationSHA256:sha(authorization),...(fault?{fault}:{}),...(slotConflict?{slotConflict:true}:{}),...(revisionConflict?{revisionConflict:true}:{})});
  assert.equal(arm.status,200); assert.equal((await arm.json()).phase,'observing');
  const send=(route,value,method='POST')=>fetch(`http://127.0.0.1:${ports.proxy}${route}`,{
    method,headers:{authorization,'content-type':'application/json'},...(value===undefined?{}:{body:typeof value==='string'?value:JSON.stringify(value)})});
  return {send,state:async()=>(await control('state')).json(),requests,journalFile,control,commits:()=>commits,freeSlot:()=>{slotOccupied=false;}};
}

test('observes journal v2 before original prepare, then forwards an empty consume unchanged',async t=>{
  const f=await fixture(t);
  assert.equal((await f.send(prefix,envelope)).status,200);
  assert.equal((await f.send(`${prefix}/${requestId}/commit`)).status,200);
  const s=await f.state();
  assert.equal(s.failure,null);assert.equal(s.phase,'observed');assert.equal(s.droppedAcknowledgements,0);
  assert.equal(s.prepared.origin.journalVerifiedBeforePrepare,true);
  assert.equal(s.prepared.origin.commandSHA256,sha(canonicalCommand(command)));
  assert.equal(s.prepared.preparations.length,1);assert.equal(s.prepared.consumes.length,1);
  assert.equal(s.prepared.consumes[0].bodyBytes,0);assert.equal(s.prepared.consumes[0].bodySHA256,sha(''));
  assert.equal(f.requests[0].bytes.toString(),JSON.stringify(envelope));assert.equal(f.requests[1].bytes.length,0);
  assert.equal((await f.control('release',{})).status,409);
  assert.equal((await f.control('restart-recovery',{})).status,409);
});
for(const [name,change] of [
  ['schema downgrade',j=>{j.schemaVersion=1;}],['foreign drop',j=>{j.dropId=id(6);}],
  ['foreign session',j=>{j.sessionId=id(6);}],['foreign server',j=>{j.serverId='foreign';}],
  ['changed seed behind same fingerprint',j=>{const c=JSON.parse(j.body);c.mutations[0].newRecord.state.generationSeed++;j.body=JSON.stringify(c);}],
]) test(`journal ${name} rejects before upstream write`,async t=>{
  const f=await fixture(t,{journalChange:change});await assert.rejects(f.send(prefix,envelope));
  assert.equal(f.requests.length,0);assert.ok((await f.state()).failure);
});
for(const [name,route,body] of [
  ['consume before prepare',`${prefix}/${requestId}/commit`,undefined],
  ['generic commit fallback','/api/item-transactions/commit',command],
]) test(`${name} cannot synthesize a grant`,async t=>{
  const f=await fixture(t);await assert.rejects(f.send(route,body));assert.equal(f.requests.length,0);
  assert.equal((await f.state()).failure,'PreparedUnexpectedWrite');
});
test('nonempty consume is rejected before forwarding',async t=>{
  const f=await fixture(t);await f.send(prefix,envelope);
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`,{}));assert.equal(f.requests.length,1);
});
test('prepare retry must retain exact raw bytes',async t=>{
  const f=await fixture(t);await f.send(prefix,envelope);
  await assert.rejects(f.send(prefix,JSON.stringify(envelope,null,2)));assert.equal(f.requests.length,1);
  assert.equal((await f.state()).failure,'PreparedFrozenCommandChanged');
});
test('journal disappearance blocks consume',async t=>{
  const f=await fixture(t);await f.send(prefix,envelope);await rm(f.journalFile);
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal(f.requests.length,1);
});
for(const kind of ['origin','receipt']) test(`corrupt successful ${kind} fails closed`,async t=>{
  const f=await fixture(t,{responseChange:(d,url)=>{
    if(kind==='origin' && url===prefix)d.command.mutations[0].newRecord.state.generationSeed++;
    if(kind==='receipt' && url.endsWith('/commit'))d.records[0].state={...d.records[0].state,quantity:2};
  }});
  if(kind==='receipt')await f.send(prefix,envelope);
  await assert.rejects(f.send(kind==='origin'?prefix:`${prefix}/${requestId}/commit`,kind==='origin'?envelope:undefined));
  assert.equal((await f.state()).failure,kind==='origin'?'PreparedOriginMismatch':'PreparedReceiptMismatch');
});

test('prepare acknowledgement loss retries the identical envelope and creates one item only after consume',async t=>{
  const f=await fixture(t,{fault:'prepare-once'});
  await assert.rejects(f.send(prefix,envelope));
  assert.equal(f.commits(),0);assert.equal((await f.state()).droppedAcknowledgements,1);
  assert.equal((await f.control('release',{})).status,409);
  assert.equal((await (await f.send(prefix,envelope)).json()).status,'AlreadyPrepared');
  assert.equal((await (await f.send(`${prefix}/${requestId}/commit`)).json()).status,'Committed');
  const s=await f.state();assert.equal(s.phase,'observed');assert.equal(f.commits(),1);
  assert.deepEqual(s.prepared.preparations.map(p=>p.acknowledgement),['dropped','delivered']);
  assert.deepEqual(s.prepared.preparations.map(p=>p.status),['Prepared','AlreadyPrepared']);
  assert.ok(f.requests[0].bytes.equals(f.requests[1].bytes));assert.equal(f.requests[2].bytes.length,0);
});
test('consume acknowledgement loss holds exact native retries until release, including re-prepare after the timer',async t=>{
  const f=await fixture(t,{fault:'consume-held'});
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.commits(),1);assert.equal((await f.state()).phase,'blocked');
  assert.equal((await f.control('release',{})).status,409);
  for(let i=0;i<4;i++)await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.commits(),1);assert.equal(f.requests.length,2);
  assert.equal((await f.control('restart-recovery',{})).status,409);
  assert.equal((await (await f.send(prefix,envelope)).json()).consumedAt,'2026-10-16T00:00:01Z');
  assert.equal((await f.control('release',{})).status,200);
  assert.equal((await (await f.send(`${prefix}/${requestId}/commit`)).json()).status,'AlreadyCommitted');
  const s=await f.state();assert.equal(s.phase,'recovered');assert.equal(s.droppedAcknowledgements,1);
  assert.deepEqual(s.prepared.consumes.map(c=>c.acknowledgement),['dropped','delivered']);
  assert.equal(s.committed.recordSHA256,s.delivered.recordSHA256);
  assert.ok(s.blockedRetries.every(r=>r.stage==='consume' && r.bodySHA256===sha('') && r.commandSHA256===s.prepared.origin.commandSHA256));
  assert.equal(f.requests.length,4);assert.ok(f.requests[0].bytes.equals(f.requests[2].bytes));
});
for(const [name,route,body] of [
  ['generic fallback','/api/item-transactions/commit',command],
  ['changed request',`${prefix}/${id(6)}/commit`,undefined],
  ['nonempty consume',`${prefix}/${requestId}/commit`,{}],
  ['changed frozen seed',prefix,{dropId,command:{...command,commandFingerprint:'changed'}}],
]) test(`held consume rejects ${name} before another upstream write`,async t=>{
  const f=await fixture(t,{fault:'consume-held'});await f.send(prefix,envelope);
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  await assert.rejects(f.send(route,body));assert.equal(f.requests.length,2);assert.equal(f.commits(),1);
  assert.ok((await f.state()).failure);assert.equal((await f.control('release',{})).status,409);
});
test('changed journal blocks a held exact consume retry',async t=>{
  const f=await fixture(t,{fault:'consume-held'});await f.send(prefix,envelope);
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  await writeFile(f.journalFile,'{}');await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.requests.length,2);assert.equal((await f.state()).blockedRetries.length,0);
});
test('prepare response with changed preparation time cannot complete a lost-ack retry',async t=>{
  let count=0;const f=await fixture(t,{fault:'prepare-once',responseChange:d=>{if(++count===2)d.preparedAt='2026-10-17T00:00:00Z';}});
  await assert.rejects(f.send(prefix,envelope));await assert.rejects(f.send(prefix,envelope));
  assert.equal((await f.state()).failure,'PreparedOriginMismatch');assert.equal(f.commits(),0);
});
test('malformed first receipt is never counted as a dropped durable consume acknowledgement',async t=>{
  const f=await fixture(t,{fault:'consume-held',responseChange:(d,url)=>{if(url.endsWith('/commit'))d.records[0].state={...d.records[0].state,quantity:2};}});
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal((await f.state()).droppedAcknowledgements,0);assert.equal((await f.state()).failure,'PreparedReceiptMismatch');
});
test('successful replay with changed record cannot recover a held consume',async t=>{
  let commits=0;const f=await fixture(t,{fault:'consume-held',responseChange:(d,url)=>{
    if(url.endsWith('/commit') && ++commits===2)d.records[0].state={...d.records[0].state,generationSeed:456};
  }});
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  for(let i=0;i<4;i++)await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  await f.control('release',{});await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal((await f.state()).failure,'PreparedReceiptMismatch');
});

async function restartFixture(t, options={}) {
  const f=await fixture(t,{...options,fault:'consume-held'});
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  for(let i=0;i<4;i++)await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  await f.send(prefix,envelope);
  assert.equal((await f.control('restart-recovery',{})).status,200);
  return f;
}
const inventory=owner=>`/api/items?ownerType=Character&ownerId=${owner}&includeTerminal=false&limit=500`;
test('prepared restart only forwards original consumed intent -> receipt -> latest owner inventory; no new writes',async t=>{
  const f=await restartFixture(t);
  assert.equal((await f.send(inventory(id(6)),undefined,'GET')).status,200);
  assert.equal((await f.send(`${prefix}/${requestId}`,undefined,'GET')).status,200);
  assert.equal((await f.send(`/api/item-transactions/${requestId}`,undefined,'GET')).status,200);
  await rm(f.journalFile); // Native receipt resolution removes it before the owner load.
  assert.equal((await f.send(inventory(characterId),undefined,'GET')).status,200);
  const s=await f.state();assert.equal(s.failure,null);assert.equal(s.phase,'restart-read-only');assert.equal(s.delivered,null);
  assert.equal(s.prepared.preparations.length,2);assert.equal(s.prepared.consumes.length,1);assert.equal(f.commits(),1);
  assert.deepEqual(s.restartRecovery.reads.map(r=>r.kind),['inventory','intent','receipt','inventory']);
  assert.ok(s.restartRecovery.reads.every(r=>r.acknowledgement==='delivered' && r.statusCode===200));
  assert.equal(s.restartRecovery.reads[1].consumed,true);assert.equal(s.restartRecovery.reads[2].recordSHA256,s.committed.recordSHA256);
  assert.equal((await f.control('release',{})).status,409);assert.equal((await f.control('restart-recovery',{})).status,409);
});
for(const [name,route,value,method='POST'] of [
  ['re-prepare',prefix,envelope],['empty consume',`${prefix}/${requestId}/commit`,undefined],
  ['generic fallback','/api/item-transactions/commit',command],['DELETE',`${prefix}/${requestId}`,undefined,'DELETE'],
]) test(`prepared restart rejects ${name} before upstream writes`,async t=>{
  const f=await restartFixture(t), before=f.requests.length;
  await assert.rejects(f.send(route,value,method));assert.equal(f.requests.length,before);assert.equal(f.commits(),1);
  const s=await f.state();assert.equal(s.failure,'PreparedWriteDuringRestartRecovery');assert.equal(s.restartRecovery.rejectedWrites,1);
});
for(const [name,route] of [
  ['receipt before intent',`/api/item-transactions/${requestId}`],['owner inventory before receipt',inventory(characterId)],
  ['foreign intent',`${prefix}/${id(7)}`],['foreign session',`/api/dungeon-sessions/${id(7)}/pickup-intents/${requestId}`],
  ['foreign inventory',inventory(id(7))],['intent query injection',`${prefix}/${requestId}?other=1`],
  ['inventory duplicate owner',inventory(id(6))+`&ownerId=${characterId}`],
]) test(`prepared restart rejects ${name} without forwarding`,async t=>{
  const f=await restartFixture(t), before=f.requests.length;
  await assert.rejects(f.send(route,undefined,'GET'));assert.equal(f.requests.length,before);assert.ok((await f.state()).failure);
});
for(const [name,change,receipt=false] of [
  ['unconsumed intent',d=>{d.consumedAt=null;}],['foreign drop',d=>{d.dropId=id(7);}],
  ['same fingerprint changed command',d=>{d.command.mutations[0].newRecord.state.generationSeed++;}],
  ['changed consumed timestamp',d=>{d.consumedAt='2026-10-17T00:00:01Z';}],
  ['changed full receipt',d=>{d.records[0].state={...d.records[0].state,generationSeed:999};},true],
]) test(`prepared restart refuses ${name}, no fallback write`,async t=>{
  const route=receipt?`/api/item-transactions/${requestId}`:`${prefix}/${requestId}`;
  const f=await restartFixture(t,{responseChange:(d,url)=>{if(url===route)change(d);}});
  if(receipt)await f.send(`${prefix}/${requestId}`,undefined,'GET');
  await assert.rejects(f.send(route,undefined,'GET'));assert.equal(f.commits(),1);
  assert.ok((await f.state()).failure);assert.ok(!(await f.state()).restartRecovery.reads.some(r=>r.kind===(receipt?'receipt':'intent')));
});
test('changed original journal prevents restart intent from reaching upstream',async t=>{
  const f=await restartFixture(t), before=f.requests.length;await writeFile(f.journalFile,'{}');
  await assert.rejects(f.send(`${prefix}/${requestId}`,undefined,'GET'));assert.equal(f.requests.length,before);
});
test('restart inventory cannot leak the owner record to the other member',async t=>{
  const f=await restartFixture(t,{responseChange:(d,url)=>{if(url===inventory(id(6)))d.items=[{...command.mutations[0].newRecord,revision:1}];}});
  await assert.rejects(f.send(inventory(id(6)),undefined,'GET'));assert.equal((await f.state()).failure,'PreparedRestartInventoryMismatch');
});

async function slotConflictFixture(t,options={}) {
  const f=await fixture(t,{...options,fault:'consume-held',slotConflict:true});
  assert.equal((await f.control('slot-conflict-ready',{})).status,409);
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.requests.length,1);assert.equal((await f.state()).phase,'slot-setup');
  assert.equal((await f.control('slot-conflict-resume',{})).status,409);
  assert.equal((await f.control('slot-conflict-ready',{})).status,200);
  return f;
}
test('actual slot 409s preserve journal/body; only freed original slot resumes before normal ack recovery',async t=>{
  const f=await slotConflictFixture(t);
  assert.equal((await f.send(`${prefix}/${requestId}/commit`)).status,409);
  assert.equal((await f.control('slot-conflict-resume',{})).status,409);
  assert.equal((await (await f.send(prefix,envelope)).json()).consumedAt,null);
  assert.equal((await f.send(`${prefix}/${requestId}/commit`)).status,409);
  let s=await f.state();assert.equal(s.phase,'slot-conflict-held');assert.equal(f.commits(),0);
  assert.ok(s.prepared.slotConflict.conflicts.every(c=>c.status==='LocationConflict' && c.acknowledgement==='delivered'));
  const before=f.requests.length;f.freeSlot();await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal(f.requests.length,before);
  assert.equal((await f.control('slot-conflict-resume',{})).status,200);
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  for(let i=0;i<4;i++)await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  await f.send(prefix,envelope);assert.equal((await f.control('release',{})).status,200);
  assert.equal((await (await f.send(`${prefix}/${requestId}/commit`)).json()).status,'AlreadyCommitted');
  s=await f.state();assert.equal(s.phase,'recovered');assert.equal(s.failure,null);assert.equal(s.droppedAcknowledgements,1);
  assert.equal(s.prepared.slotConflict.conflicts.length,2);assert.equal(s.prepared.consumes.length,2);assert.equal(s.prepared.slotConflict.setupBlocked.length,1);
  assert.equal(s.prepared.slotConflict.heldConsumes.length,1);assert.equal(s.committed.recordSHA256,s.delivered.recordSHA256);
  const prepares=f.requests.filter(r=>r.url===prefix);assert.ok(prepares.every(r=>r.bytes.equals(prepares[0].bytes)));
  assert.equal((await f.control('restart-recovery',{})).status,409);
});
for(const [name,route,body] of [
  ['new slot',prefix,{dropId,command:{...command,mutations:[{...command.mutations[0],newRecord:{...command.mutations[0].newRecord,location:{...command.mutations[0].newRecord.location,slotIndex:1}}}]}}],
  ['new request',prefix,{dropId,command:{...command,requestId:id(7)}}],
  ['generic fallback','/api/item-transactions/commit',command],
]) test(`known slot conflict rejects ${name} before forwarding`,async t=>{
  const f=await slotConflictFixture(t);await f.send(`${prefix}/${requestId}/commit`);
  const before=f.requests.length;await assert.rejects(f.send(route,body));assert.equal(f.requests.length,before);assert.equal(f.commits(),0);
});
test('slot conflict cannot interpret an unrelated 409 as the expected location conflict',async t=>{
  const f=await slotConflictFixture(t,{responseChange:(d,url)=>{if(url.endsWith('/commit'))d.status='RevisionConflict';}});
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal((await f.state()).failure,'PreparedInvalidSlotConflict');
});
test('slot scenario must not accept a successful grant before explicit release',async t=>{
  const f=await slotConflictFixture(t);f.freeSlot();
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal((await f.state()).failure,'PreparedSlotGrantedBeforeRelease');
});
test('known slot conflict still requires the unchanged durable journal on every consume',async t=>{
  const f=await slotConflictFixture(t);await f.send(`${prefix}/${requestId}/commit`);await writeFile(f.journalFile,'{}');
  const before=f.requests.length;await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal(f.requests.length,before);
});

async function revisionConflictFixture(t,options={}) {
  const f=await fixture(t,{...options,fault:'consume-held',revisionConflict:true});
  assert.equal((await f.control('revision-conflict-ready',{})).status,409);
  await f.send(prefix,envelope);await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.requests.length,1);assert.equal((await f.state()).phase,'revision-setup');
  assert.equal((await f.control('slot-conflict-ready',{})).status,409);
  assert.equal((await f.control('revision-conflict-ready',{})).status,200);
  return f;
}
test('revision conflict retains the frozen unconsumed origin, with no release or grant',async t=>{
  const f=await revisionConflictFixture(t);
  assert.equal((await f.send(`${prefix}/${requestId}/commit`)).status,409);
  assert.equal((await (await f.send(prefix,envelope)).json()).consumedAt,null);
  assert.equal((await f.send(`${prefix}/${requestId}/commit`)).status,409);
  const before=f.requests.length;await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal(f.requests.length,before);assert.equal(f.commits(),0);
  const s=await f.state();assert.equal(s.phase,'revision-conflict-held');assert.equal(s.failure,null);
  assert.equal(s.droppedAcknowledgements,0);assert.equal(s.prepared.consumes.length,0);
  assert.ok(s.prepared.revisionConflict.conflicts.every(c=>c.status==='RevisionConflict' && c.acknowledgement==='delivered'));
  assert.equal(s.prepared.revisionConflict.heldConsumes.length,1);
  for(const route of ['release','restart-recovery','slot-conflict-ready','slot-conflict-resume','revision-conflict-ready']) {
    assert.equal((await f.control(route,{})).status,409);
  }
});
for(const [name,change] of [
  ['location rejection',d=>{d.status='LocationConflict';}],
  ['records in rejection',d=>{d.records=[];}],
  ['wrong error',d=>{d.error='other';}],
]) test(`revision conflict refuses ${name}`,async t=>{
  const f=await revisionConflictFixture(t,{responseChange:(d,url)=>{if(url.endsWith('/commit'))change(d);}});
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal((await f.state()).failure,'PreparedInvalidRevisionConflict');assert.equal(f.commits(),0);
});
test('revision fixture cannot accept a successful grant as conflict evidence',async t=>{
  const f=await revisionConflictFixture(t);f.freeSlot();
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));
  assert.equal((await f.state()).failure,'PreparedRevisionGrantedUnexpectedly');
});
test('revision rejection still checks the original durable journal before each retry',async t=>{
  const f=await revisionConflictFixture(t);await f.send(`${prefix}/${requestId}/commit`);
  await writeFile(f.journalFile,'{}');const before=f.requests.length;
  await assert.rejects(f.send(`${prefix}/${requestId}/commit`));assert.equal(f.requests.length,before);
});
for(const [name,route,body] of [
  ['new item',prefix,{dropId,command:{...command,mutations:[{...command.mutations[0],newRecord:{...command.mutations[0].newRecord,state:{...command.mutations[0].newRecord.state,instanceId:id(7)}}}]}}],
  ['generic commit','/api/item-transactions/commit',command],
]) test(`revision conflict refuses ${name} before forwarding`,async t=>{
  const f=await revisionConflictFixture(t);await f.send(`${prefix}/${requestId}/commit`);
  const before=f.requests.length;await assert.rejects(f.send(route,body));assert.equal(f.requests.length,before);assert.equal(f.commits(),0);
});
