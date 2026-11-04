'use strict';
// Executes the shipped page script with a minimal DOM/storage/fetch adapter.
// This tests request recovery semantics; it is not a browser rendering test.
const fs = require('fs'), path = require('path'), vm = require('vm'), assert = require('assert/strict');
const pages = process.argv[2] || path.resolve(__dirname, '../../Backend/ProjectRpg.Backend/Pages/Ops');
const source = fs.readFileSync(path.join(pages, 'Compensation.cshtml'), 'utf8').match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
const logout = fs.readFileSync(path.join(pages, '_Layout.cshtml'), 'utf8').match(/<script[^>]*>([\s\S]*?)<\/script>/)[1];
const key = 'ops.compensation.Test.github:1', character = '10000000-0000-0000-0000-000000000001';
const tokenKey = key + '.token.' + character, event = {preventDefault(){}};
const storage = () => {
  const data = {};
  Object.defineProperties(data, {
    getItem:{value:k => data[k] ?? null}, setItem:{value:(k,v) => {data[k]=v;}}, removeItem:{value:k => {delete data[k];}}
  });
  return data;
};
function boot(local, session, server) {
  const node = () => ({textContent:'', disabled:false, hidden:true, children:[], append(c){this.children.push(c);}, replaceChildren(){this.children=[];}});
  const nodes = Object.fromEntries(['compensation','preview-form','status','preview','commit','new-intent','refresh-preview','lookup-form','request-history'].map(k=>[k,node()]));
  nodes.compensation.dataset={character,storage:key};
  nodes['preview-form'].elements={caseId:{value:'20000000-0000-0000-0000-000000000001'},definitionKey:{value:'RPGItemDefinition:Ore:1'},
    reason:{value:'confirmed incident reason'},__RequestVerificationToken:{value:'csrf'}};
  nodes['preview-form'].reset=()=>{};
  nodes['lookup-form'].elements={requestId:{value:''}};
  let ids=0;
  vm.runInNewContext(source,{document:{getElementById:id=>nodes[id],createElement:node},localStorage:local,sessionStorage:session,
    crypto:{randomUUID:()=>{ids++;return '30000000-0000-0000-0000-' + String(ids).padStart(12,'0');}},
    fetch:server, console});
  return nodes;
}
(async()=>{
  const local=storage(), session=storage(), sent=[]; let previews=0, drop=true;
  const server=async(url,options)=>{
    const input=options?.body ? JSON.parse(options.body) : null;
    if(url.endsWith('/preview')) return {ok:true,json:async()=>({data:{normalizedIntent:input,previewToken:'token-'+(++previews),expiresAtUtc:'2026-11-05T00:05:00Z'}})};
    if(url.endsWith('/compensations')) {
      assert.equal(options.headers['X-Ops-CSRF'],'csrf');
      assert.equal(JSON.parse(session[tokenKey]).requestId,input.requestId,'Token and ID stored before POST');
      assert.equal(JSON.parse(local[key+'.requests'])[0].id,input.requestId,'Durable recovery ID stored before POST');
      sent.push(input);
      if(drop){drop=false;throw new Error('response lost after commit');}
      return {ok:true,json:async()=>({data:{disposition:'Replayed',receipt:{requestId:input.requestId,result:{itemId:'original-item'}}}})};
    }
    return {ok:false,json:async()=>({code:'ops.action_not_found',httpRequestId:'trace'})};
  };
  let dom=boot(local,session,server);
  await dom['preview-form'].onsubmit(event); assert.equal(sent.length,0,'Preview never submits a mutation');
  await dom.commit.onclick(); assert.equal(sent.length,1); const id=sent[0].requestId;
  await dom['preview-form'].onsubmit(event); assert.equal(previews,1,'Unknown outcome prevents a replacement intent');
  dom=boot(local,session,server);
  assert.equal(dom['lookup-form'].elements.requestId.value,id,'Reload recovers original ID');
  await dom['lookup-form'].onsubmit(event); assert.equal(sent.length,1,'404 lookup does not auto-generate or submit another request');
  await dom['refresh-preview'].onclick();
  assert.equal(JSON.parse(session[tokenKey]).requestId,id,'Refreshing expired preview preserves original ID');
  await dom.commit.onclick();
  assert.equal(sent[1].requestId,id); assert.notEqual(sent[1].previewToken,sent[0].previewToken);
  assert.equal(session[tokenKey],undefined,'Confirmed receipt clears short-lived token');
  assert.equal(JSON.parse(local[key+'.requests'])[0].id,id,'Confirmed ID retained for later recovery');
  session[tokenKey]='sensitive-preview'; let submit;
  vm.runInNewContext(logout,{document:{querySelector:()=>({addEventListener:(_,handler)=>{submit=handler;}})},sessionStorage:session});
  submit(); assert.equal(session[tokenKey],undefined); assert.ok(local[key+'.requests'],'Logout retains recovery IDs');
  const broken={getItem(){return null;},setItem(){throw new Error('storage disabled');}};
  const blocked=boot(broken,storage(),server);
  await blocked['preview-form'].onsubmit(event); await blocked.commit.onclick();
  assert.equal(sent.length,2,'Failed durable request storage prevents POST');
  assert.match(blocked.status.textContent,/storage disabled/);
  console.log('PASS Compensation page script: persist-before-send, lost response/reload, same-ID preview refresh/retry, 404 recovery, logout token disposal and storage failure refusal.');
})().catch(error=>{console.error(error);process.exitCode=1;});
