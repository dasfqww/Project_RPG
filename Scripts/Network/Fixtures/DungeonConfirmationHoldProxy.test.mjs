import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import fs from 'node:fs';
import path from 'node:path';
import { createHash, randomUUID } from 'node:crypto';
import { setTimeout as pause } from 'node:timers/promises';
import { startConfirmationHoldProxy } from './DungeonConfirmationHoldProxy.mjs';

const sessionId='20304050-6070-8090-a0b0-c0d0e0f00102';
const characterId='10304050-6070-8090-a0b0-c0d0e0f00102';
const steamId='76561198000000001';
const authorization=`Bearer fixture-${randomUUID()}`;
const route=`/api/dungeon-sessions/${sessionId}/members/${characterId}/admission-confirmation`;
async function waitFor(read) {
  const deadline=Date.now()+4000;
  while(Date.now()<deadline) { try { if(read()) return; } catch {} await pause(10); }
  throw new Error('Fixture observation timed out');
}
async function fixture(mode, reply, generation) {
  const observed=[];
  const upstream=http.createServer(async (request,response)=>{
    const chunks=[]; for await(const chunk of request) chunks.push(chunk);
    observed.push({url:request.url,authorization:request.headers.authorization,body:Buffer.concat(chunks).toString()});
    const result=reply?.(observed.length) ?? {status:409,body:'{"error":"dungeon_admission_not_available"}'};
    response.writeHead(result.status,{'content-type':'application/json'}).end(result.body);
  });
  await new Promise(resolve=>upstream.listen(0,'127.0.0.1',resolve));
  const root=path.resolve('Saved/Tests/ConfirmationProxy',randomUUID().replaceAll('-',''),'DungeonMembershipBackend',randomUUID().replaceAll('-',''));
  fs.mkdirSync(root,{recursive:true});
  const config={upstreamPort:upstream.address().port,root,sessionId,characterId,steamId,
    authorizationSHA256:createHash('sha256').update(authorization).digest('hex'),mode,generation};
  const proxy=await startConfirmationHoldProxy(config);
  return {proxy,config,observed,async close(){await proxy.close();upstream.closeAllConnections();await new Promise(resolve=>upstream.close(resolve));}};
}
function send(port,generation,header=authorization) {
  const body=JSON.stringify({steamId,connectionGeneration:generation});
  const request=http.request({hostname:'127.0.0.1',port,path:route,method:'POST',
    headers:{authorization:header,'content-type':'application/json','content-length':Buffer.byteLength(body)}});
  request.on('error',()=>{});
  request.end(body);
  return {request,body};
}
test('only generation two is held, and a lost socket does not lose the delayed original',async()=>{
  const f=await fixture();
  try {
    const held=send(f.proxy.port,2);
    const state=()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    await waitFor(()=>state().held===1);
    assert.equal(f.observed.length,0);
    const first=send(f.proxy.port,1);
    await new Promise(resolve=>first.request.on('response',response=>{response.resume();response.on('end',resolve);}));
    assert.equal(f.observed.length,1);
    held.request.destroy(); await pause(30);
    fs.writeFileSync(f.proxy.gate,'');
    await waitFor(()=>state().forwarded===1);
    assert.deepEqual(state().statuses,[409]);
    assert.equal(f.observed.length,2);
    assert.equal(f.observed[1].body,held.body);
    assert.equal(f.observed[1].authorization,authorization);
    const metadata=fs.readFileSync(f.proxy.evidence,'utf8');
    assert.equal(metadata.includes(authorization),false);
    assert.equal(metadata.includes(steamId),false);
    assert.equal(metadata.includes('steamId'),false);
    await pause(30);
  } finally { await f.close(); }
});
test('held-route credential mismatch cannot reach upstream',async()=>{
  const f=await fixture();
  try {
    send(f.proxy.port,2,'Bearer unrelated-fixture');
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    assert.equal(f.observed.length,0);
  } finally {await f.close();}
});
test('reusing evidence or an invalid scope is rejected',async()=>{
  const f=await fixture();
  try {
    await assert.rejects(startConfirmationHoldProxy(f.config),/reuse/);
    await assert.rejects(startConfirmationHoldProxy({...f.config,characterId:'../other'}),/scope/);
    await assert.rejects(startConfirmationHoldProxy({...f.config,mode:'anything'}),/scope/);
  } finally {await f.close();}
});
test('committed 200 never reaches UE; later actual retries are discarded without changing their bytes',async()=>{
  let disconnected=false;
  const f=await fixture('after-commit',()=>disconnected
    ? {status:409,body:'{"error":"dungeon_admission_not_available"}'}
    : {status:200,body:'{"admissionConfirmed":true}'});
  try {
    let downstreamReplies=0;
    const original=send(f.proxy.port,2);
    original.request.on('response',()=>{downstreamReplies++;});
    const state=()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    await waitFor(()=>state().committedResponses===1);
    assert.equal(state().discardedResponses,0);
    assert.equal(downstreamReplies,0);
    assert.equal(f.observed[0].body,original.body);
    disconnected=true;
    fs.writeFileSync(f.proxy.gate,'');
    await waitFor(()=>state().discardedResponses===1);
    const retry=send(f.proxy.port,2);
    retry.request.on('response',()=>{downstreamReplies++;});
    await waitFor(()=>state().discardedResponses===2);
    assert.deepEqual(state().statuses,[200,409]);
    assert.equal(state().failure,null);
    assert.equal(f.observed[1].body,original.body);
    assert.equal(f.observed[1].authorization,authorization);
    assert.equal(downstreamReplies,0);
    const metadata=fs.readFileSync(f.proxy.evidence,'utf8');
    for(const secret of [authorization,steamId,'admissionConfirmed']) assert.equal(metadata.includes(secret),false);
  } finally {await f.close();}
});
test('a malformed 200 is not committed confirmation evidence',async()=>{
  const f=await fixture('after-commit',()=>({status:200,body:'{"admissionConfirmed":false}'}));
  try {
    send(f.proxy.port,2);
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    const state=JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    assert.equal(state.committedResponses,0);
    assert.equal(state.discardedResponses,0);
  } finally {await f.close();}
});
test('postcommit fault still rejects foreign authorization before upstream',async()=>{
  const f=await fixture('after-commit');
  try {
    send(f.proxy.port,2,'Bearer unrelated-fixture');
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    assert.equal(f.observed.length,0);
  } finally {await f.close();}
});
test('committed reply replay releases original bytes once without another upstream request',async()=>{
  const body='{ "admissionConfirmed" : true }';
  const f=await fixture('after-commit-replay',()=>({status:200,body}),1);
  try {
    let replied=false;
    const original=send(f.proxy.port,1);
    const reply=new Promise(resolve=>original.request.on('response',response=>{
      replied=true; const chunks=[]; response.on('data',chunk=>chunks.push(chunk));
      response.on('end',()=>resolve({status:response.statusCode,body:Buffer.concat(chunks).toString()}));
    }));
    const state=()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    await waitFor(()=>state().committedResponses===1);
    assert.equal(state().generation,'1'); assert.equal(replied,false);
    assert.equal(f.observed.length,1); assert.equal(f.observed[0].body,original.body);
    fs.writeFileSync(f.proxy.gate,'');
    assert.deepEqual(await reply,{status:200,body});
    await waitFor(()=>state().replayedResponses===1);
    await pause(80);
    assert.equal(f.observed.length,1); assert.equal(state().replayedResponses,1);
    assert.equal(state().discardedResponses,0); assert.equal(state().failure,null);
    assert.deepEqual(state().heldReplySHA256,state().replayedReplySHA256);
    const metadata=fs.readFileSync(f.proxy.evidence,'utf8');
    for(const secret of [authorization,steamId,'admissionConfirmed']) assert.equal(metadata.includes(secret),false);
  } finally {await f.close();}
});
test('replay cannot promote a malformed success response',async()=>{
  const f=await fixture('after-commit-replay',()=>({status:200,body:'{"admissionConfirmed":"true"}'}),1);
  try {
    send(f.proxy.port,1);
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    const state=JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    assert.equal(state.committedResponses,0); assert.equal(state.replayedResponses,0);
  } finally {await f.close();}
});
test('replay mode preserves the authorization fence',async()=>{
  const f=await fixture('after-commit-replay',undefined,1);
  try {
    send(f.proxy.port,1,'Bearer unrelated-fixture');
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    assert.equal(f.observed.length,0);
  } finally {await f.close();}
});
test('replay requires a live response recipient instead of claiming delivery',async()=>{
  const f=await fixture('after-commit-replay',()=>({status:200,body:'{"admissionConfirmed":true}'}),1);
  try {
    const original=send(f.proxy.port,1);
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).committedResponses===1);
    original.request.destroy(); await pause(80);
    fs.writeFileSync(f.proxy.gate,'');
    await waitFor(()=>JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8')).failure!==null);
    const state=JSON.parse(fs.readFileSync(f.proxy.evidence,'utf8'));
    assert.equal(state.failure,'ReplayRecipientClosed'); assert.equal(state.replayedResponses,0);
    assert.equal(f.observed.length,1);
  } finally {await f.close();}
});
test('invalid generations are rejected before startup',async()=>{
  const f=await fixture();
  try {
    for(const generation of [0,-1,3,1.5,'1',NaN])
      await assert.rejects(startConfirmationHoldProxy({...f.config,generation}),/scope/);
  } finally {await f.close();}
});
