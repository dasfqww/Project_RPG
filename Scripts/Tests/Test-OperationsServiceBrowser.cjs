'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const pages=process.argv[2]||path.resolve(__dirname,'../../Backend/ProjectRpg.Backend/Pages/Ops');
const script=fs.readFileSync(path.join(pages,'ServiceControl.cshtml'),'utf8').match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
const key='ops.service.Test.github:1',pendingKey=key+'.intent.global',event={preventDefault(){}};
function storage(){const data={};Object.defineProperties(data,{getItem:{value:k=>data[k]??null},setItem:{value:(k,v)=>{data[k]=v;}},removeItem:{value:k=>{delete data[k];}}});return data;}
function boot(local,session,fetch){
 const nodes=Object.fromEntries(['service','service-form','send','reset','status','intent','lookup','trace','current-state'].map(id=>[id,{textContent:'',disabled:false,hidden:true}]));
 nodes.service.dataset={revision:'9007199254740993',storage:key};
 nodes['service-form'].elements=Object.fromEntries(Object.entries({mode:'Drain',notice:'maintenance notice',starts:'',ends:'',reason:'verified service incident reason'}).map(([k,value])=>[k,{value,disabled:false}]));
 nodes.lookup.elements={requestId:{value:''}};
 vm.runInNewContext(script,{document:{getElementById:id=>nodes[id],querySelector:()=>({value:'csrf'})},localStorage:local,sessionStorage:session,fetch,
  crypto:{randomUUID:()=> '11111111-1111-4111-8111-111111111111'},Date,BigInt,console});return nodes;
}
function response(data,code=200){return {ok:code<300,json:async()=>code<300?{data}:{code:'ops.authentication_required'}};}
(async()=>{
 const local=storage(),session=storage(),sent=[];let fail=true,missing=false,mismatch=false;
 async function fetch(url,options){
  if(options.method==='PUT'){
   sent.push(options.body);assert.equal(options.headers['X-Ops-CSRF'],'csrf');if(fail){fail=false;throw Error('lost response');}
   const v=JSON.parse(options.body);return response({receipt:{requestId:v.requestId,operation:'Ops.ServiceControl.Change.v1',target:{scope:'Service',id:'global'},reason:v.reason,
    committedAtUtc:'2026-11-06T12:00:00Z',result:{revision:'9007199254740994',mode:v.mode,noticeText:mismatch?'wrong':v.noticeText,noticeStartsAtUtc:null,noticeEndsAtUtc:null}}});
  }
  if(url.startsWith('/ops/api/actions/'))return {ok:false,json:async()=>({code:missing?'ops.action_not_found':'ops.authentication_required'})};
  return response({revision:'9007199254740995',mode:'Open',noticeText:'latest notice',noticeStartsAtUtc:null,noticeEndsAtUtc:null});
 }
 let dom=boot(local,session,fetch);dom['service-form'].onsubmit(event);await dom.send.onclick();assert.ok(session[pendingKey]);assert.ok(local[key+'.requests']);
 dom=boot(local,session,fetch);missing=true;await dom.lookup.onsubmit(event);assert.ok(session[pendingKey],'A missing receipt is not proof that the command never committed');
 mismatch=true;await dom.send.onclick();assert.ok(session[pendingKey],'Mismatching receipt cannot unlock another command');
 mismatch=false;await dom.send.onclick();assert.equal(sent[0],sent[1]);assert.equal(sent[1],sent[2],'Retries preserve exact command bytes and request ID');
 assert.equal(session[pendingKey],undefined);assert.equal(dom.service.dataset.revision,'9007199254740995','Historical receipt is followed by a fresh read');
 assert.equal(dom['service-form'].elements.notice.value,'latest notice');assert.equal(dom.reset.disabled,false);assert.equal(dom.trace.hidden,false);
 const blocked=boot({getItem(){return null;},setItem(){throw Error('storage blocked');}},storage(),fetch);
 blocked['service-form'].onsubmit(event);await blocked.send.onclick();assert.equal(sent.length,3,'Persistence failure refuses HTTP mutation');
 const snapshot=storage();snapshot[pendingKey]=JSON.stringify({body:sent[0]});const unauth=boot(storage(),snapshot,async()=>response(null,401));await unauth.send.onclick();assert.ok(snapshot[pendingKey],'401 retains original request');
 console.log('PASS Service page: persist before PUT, response loss/reload, exact retry/int64, 404/401/mismatched receipt preservation, fresh state and storage failure.');
})().catch(error=>{console.error(error);process.exitCode=1;});
