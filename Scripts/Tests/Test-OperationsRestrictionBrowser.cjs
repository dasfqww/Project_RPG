'use strict';
const fs=require('fs'),path=require('path'),vm=require('vm'),assert=require('assert/strict');
const pages=process.argv[2]||path.resolve(__dirname,'../../Backend/ProjectRpg.Backend/Pages/Ops');
const script=fs.readFileSync(path.join(pages,'Restrictions.cshtml'),'utf8').match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
const logout=fs.readFileSync(path.join(pages,'_Layout.cshtml'),'utf8').match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
const key='ops.restrictions.Test.github:1',account='76561198012345678',pendingKey=key+'.intent.'+account;
const event={preventDefault(){}};
function storage(){const data={};Object.defineProperties(data,{getItem:{value:k=>data[k]??null},setItem:{value:(k,v)=>{data[k]=v;}},removeItem:{value:k=>{delete data[k];}}});return data;}
function boot(local,session,fetch){
 const element=()=>({textContent:'',disabled:false,children:[],replaceChildren(){this.children=[];},append(v){this.children.push(v);}});
 const nodes=Object.fromEntries(['restriction','restriction-form','send','reset','status','intent','lookup','requests','current-state'].map(id=>[id,element()]));
 nodes.restriction.dataset={account,revision:'9007199254740993',storage:key};
 nodes['restriction-form'].elements={operation:{value:'set'},expires:{value:''},reason:{value:'verified incident restriction reason'},__RequestVerificationToken:{value:'csrf'}};
 nodes['restriction-form'].querySelector=()=>element();nodes.lookup.elements={requestId:{value:''}};
 vm.runInNewContext(script,{document:{getElementById:id=>nodes[id],createElement:element},localStorage:local,sessionStorage:session,fetch,
  crypto:{randomUUID:()=> '11111111-1111-1111-1111-111111111111'},location:{pathname:'/page',assign(){}},console});return nodes;
}
(async()=>{
 const local=storage(),session=storage(),sent=[];let drop=true;
 async function fetch(url,options){
  if(options?.method==='POST'){
   const input=JSON.parse(options.body);assert.equal(input.expectedRevision,'9007199254740993','Revision remains an int64 string');
   assert.equal(options.headers['X-Ops-CSRF'],'csrf');assert.equal(JSON.parse(session[pendingKey]).requestId,input.requestId);
   assert.equal(JSON.parse(local[key+'.requests'])[0].id,input.requestId,'Recovery ID saved before POST');sent.push(input);
   if(drop){drop=false;throw new Error('response lost after commit');}
   return {ok:true,json:async()=>({data:{receipt:{requestId:input.requestId,result:{state:'Active',revision:'9007199254740994'}}}})};
  }
  if(url.startsWith('/ops/api/account-restrictions/'))return {ok:true,json:async()=>({data:{state:'Released',revision:'9007199254740995',expiresAtUtc:'2026-11-06T00:00:00Z'}})};
  return {ok:false,json:async()=>({code:'ops.action_not_found'})};
 }
 let dom=boot(local,session,fetch);dom['restriction-form'].onsubmit(event);assert.equal(sent.length,0,'Review never sends a command');
 await dom.send.onclick();assert.equal(sent.length,1);const id=sent[0].requestId;
 dom['restriction-form'].onsubmit(event);assert.equal(JSON.parse(session[pendingKey]).requestId,id,'Uncertain request cannot be replaced');
 dom=boot(local,session,fetch);assert.equal(dom.lookup.elements.requestId.value,id,'Reload recovers original ID');
 await dom.lookup.onsubmit(event);assert.equal(sent.length,1,'404 never auto-submits another ID');
 await dom.send.onclick();assert.equal(sent[1].requestId,id,'Retry uses original ID');
 assert.equal(dom.restriction.dataset.revision,'9007199254740995','Historical receipt is followed by a fresh current-state read');
 assert.match(dom['current-state'].textContent,/Released/);assert.equal(session[pendingKey],undefined);assert.ok(local[key+'.requests']);
 session[pendingKey]='pending intent';let submit;vm.runInNewContext(logout,{document:{querySelector:()=>({addEventListener:(_,fn)=>{submit=fn;}})},sessionStorage:session});
 submit();assert.equal(session[pendingKey],undefined,'Logout removes command drafts');assert.ok(local[key+'.requests'],'Logout retains recovery IDs');
 const blocked=boot({getItem(){return null;},setItem(){throw new Error('storage disabled');}},storage(),fetch);
 blocked['restriction-form'].onsubmit(event);await blocked.send.onclick();assert.equal(sent.length,2,'Failed durable storage refuses POST');
 console.log('PASS Restriction page script: persist-before-send, lost response/reload, same-ID retry, int64 revision, fresh state, 404 recovery, logout and storage refusal.');
})().catch(error=>{console.error(error);process.exitCode=1;});
