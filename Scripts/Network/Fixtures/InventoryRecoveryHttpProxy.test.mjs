import test from 'node:test';
import assert from 'node:assert/strict';
import http from 'node:http';
import { createHash } from 'node:crypto';
import { startInventoryRecoveryProxy } from './InventoryRecoveryHttpProxy.mjs';
const owner = '11111111-2222-3333-4444-555555555555', source = '22222222-2222-3333-4444-555555555555';
const auth = 'Bearer fixture-server-only', token = 'c'.repeat(48);
const command = () => ({ requestId: '33333333-2222-3333-4444-555555555555', operation: 'Item.Move', commandFingerprint: 'fixture-move',
  actor: { type: 'Character', ownerId: owner }, affectedQuantity: 7, mutations: [{ expectedRevision: 1, newRecord: {
    revision: 1, state: { instanceId: source, quantity: 7 }, owner: { type: 'Character', ownerId: owner },
    location: { containerType: 'Inventory', containerId: owner, slotIndex: 3 } } }] });
const pairedItem = '44444444-2222-3333-4444-555555555555';
const swapCommand = () => {
  const c = command(); c.operation = 'Item.Swap'; c.commandFingerprint = 'fixture-swap'; c.affectedQuantity = 0;
  c.mutations[0].newRecord.location.slotIndex = 1;
  const second = structuredClone(c.mutations[0]); second.newRecord.state.instanceId = pairedItem;
  second.newRecord.state.quantity = 1; second.newRecord.location.slotIndex = 0; c.mutations.push(second); return c;
};
const mergeCommand = () => {
  const c = swapCommand(); c.operation = 'Item.StackTransfer'; c.commandFingerprint = 'fixture-merge'; c.affectedQuantity = 7;
  for (const m of c.mutations) Object.assign(m.newRecord, { definitionType: 'RPGItemDefinition',
    definitionName: 'GameItem.Consume.Potion.Red.Large', definitionVersion: 1, lifecycleState: 'Active', metadata: { isLocked: false } });
  const a = c.mutations[0].newRecord, b = c.mutations[1].newRecord;
  a.state.quantity = 0; a.location = { containerType: 'Terminal', containerId: '', slotIndex: -1 };
  a.lifecycleState = 'Consumed'; a.metadata.isLocked = true; b.state.quantity = 8; b.location.slotIndex = 1; return c;
};
const createdItem = '55555555-2222-3333-4444-555555555555';
const splitCommand = () => {
  const c = command(); c.operation = 'Item.Split'; c.commandFingerprint = 'fixture-split'; c.affectedQuantity = 2;
  const a = c.mutations[0].newRecord;
  a.state.quantity = 5; a.state.generationSeed = 37; a.location.slotIndex = 0;
  Object.assign(a, { definitionType: 'RPGItemDefinition', definitionName: 'GameItem.Consume.Potion.Red.Large',
    definitionVersion: 1, lifecycleState: 'Active', metadata: { isLocked: false, bindState: 'CharacterBound' } });
  const b = structuredClone(c.mutations[0]); b.expectedRevision = 0; b.newRecord.revision = 0;
  b.newRecord.state.instanceId = createdItem; b.newRecord.state.quantity = 2; b.newRecord.location.slotIndex = 3;
  c.mutations.push(b); return c;
};
const claimCommand = () => {
  const c = command(); c.operation = 'Item.ClaimMail'; c.commandFingerprint = 'fixture-mail-claim';
  const record = c.mutations[0].newRecord; record.location.slotIndex = 0;
  Object.assign(record, { definitionType: 'RPGItemDefinition', definitionName: 'GameItem.Consume.Potion.Red.Large',
    definitionVersion: 1, lifecycleState: 'Active', metadata: { isLocked: false } });
  return c;
};
const equipmentCommand = (operation = 'Item.Equip') => {
  const c = claimCommand(); c.operation = operation; c.commandFingerprint = `fixture-${operation}`; c.affectedQuantity = 1;
  const record = c.mutations[0].newRecord; record.state.quantity = 1; record.definitionName = 'GameItem.Equipment.Helm.Default';
  record.location.containerType = operation === 'Item.Equip' ? 'Equipment' : 'Inventory';
  record.metadata.bindState = 'CharacterBound'; return c;
};
const effectScope = '66666666-2222-3333-4444-555555555555';
const consumeCommand = () => {
  const c = claimCommand(); c.operation = 'Item.Consume'; c.affectedQuantity = 1;
  c.commandFingerprint = `${source.toUpperCase()}|1|effectScope=${effectScope}`;
  c.mutations[0].newRecord.state.quantity = 6; return c;
};
async function setup(t, { badReceipt = false, badRecordIndex = 0, status = 200, rejectReplays = false, emptyAuthResponse = false,
  additionalReadOwner, operation = 'Item.Move', omitRecord = false, badRevision = false, expectedArmCode = 200,
  holdRecoveryResponse, badReplayReceipt = false } = {}) {
  let calls = 0;
  const upstream = http.createServer(async (request, response) => {
    if (request.method === 'GET') {
      response.writeHead(200, { 'content-type': 'application/json' }); response.end('{"items":[]}'); return;
    }
    const chunks = []; for await (const part of request) chunks.push(part);
    const c = JSON.parse(Buffer.concat(chunks)); ++calls;
    if (rejectReplays && calls > 1) { response.writeHead(401, { 'content-type': 'application/json' }); response.end(emptyAuthResponse ? '' : '{"error":"unauthorized"}'); return; }
    const records = c.mutations.map(m => { const record = structuredClone(m.newRecord); record.revision = m.expectedRevision + 1; return record; });
    if (badReceipt || (badReplayReceipt && calls > 1)) records[badRecordIndex].location.slotIndex = 4;
    if (badRevision) ++records[badRecordIndex].revision;
    if (omitRecord) records.pop();
    response.writeHead(status, { 'content-type': 'application/json' });
    response.end(JSON.stringify({ status: calls === 1 ? 'Committed' : 'AlreadyCommitted', requestId: c.requestId,
      operation: c.operation, commandFingerprint: c.commandFingerprint, actor: c.actor, affectedQuantity: c.affectedQuantity, records }));
  });
  await new Promise(resolve => upstream.listen(0, '127.0.0.1', resolve));
  const p = await startInventoryRecoveryProxy({ token, upstreamPort: upstream.address().port });
  t.after(async () => { await p.close(); await new Promise(resolve => { upstream.close(resolve); upstream.closeAllConnections(); }); });
  const control = async (path, body) => {
    const r = await fetch(`http://127.0.0.1:${p.controlPort}/${path}`, { method: body ? 'POST' : 'GET',
      headers: { authorization: `Bearer ${token}`, 'content-type': 'application/json' }, body: body ? JSON.stringify(body) : undefined });
    return { code: r.status, body: await r.json() };
  };
  const armed = await control('arm', { owner, source, operation, other: operation !== 'Item.Move' ? pairedItem : undefined,
    authorizationSHA256: createHash('sha256').update(auth).digest('hex'), additionalReadOwner, holdRecoveryResponse });
  assert.equal(armed.code, expectedArmCode);
  const send = (body = (operation === 'Item.Consume' ? consumeCommand() : ['Item.Equip', 'Item.Unequip'].includes(operation) ? equipmentCommand(operation) : operation === 'Item.ClaimMail' ? claimCommand() : operation === 'Item.Split' ? splitCommand() : operation === 'Item.StackTransfer' ? mergeCommand() : operation === 'Item.Swap' ? swapCommand() : command()), authorization = auth, signal) => fetch(`http://127.0.0.1:${p.proxyPort}/api/item-transactions/commit`, {
    method: 'POST', headers: { authorization, 'content-type': 'application/json' }, body: JSON.stringify(body), signal });
  return { send, control, calls: () => calls, p };
}
async function waitForPhase(f, phase) {
  const deadline = Date.now() + 3000;
  while (Date.now() < deadline) {
    const state = (await f.control('state')).body;
    if (state.phase === phase) return state;
    assert.equal(state.failure, null);
    await new Promise(resolve => setTimeout(resolve, 5));
  }
  assert.fail(`Recovery proxy did not enter ${phase}`);
}
const waitForHeld = f => waitForPhase(f, 'recovery-response-held');
test('held consume recovery releases the original response once and rejects duplicate/control bypasses', { timeout: 5000 }, async t => {
  const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: true });
  assert.equal((await f.control('deliver-recovery', {})).code, 409);
  await assert.rejects(f.send()); await f.control('release', {});
  let settled = false;
  const pending = f.send().then(r => { settled = true; return r; }, error => { settled = true; throw error; });
  const held = await waitForHeld(f);
  assert.equal(settled, false); assert.equal(f.calls(), 2);
  assert.equal(held.heldRecoveryResponses, 1); assert.equal(held.releasedRecoveryResponses, 0);
  assert.equal(held.recordReceiptSHA256, held.replayRecordReceiptSHA256);
  const denied = await fetch(`http://127.0.0.1:${f.p.controlPort}/deliver-recovery`, { method: 'POST' });
  assert.equal(denied.status, 403); assert.equal(settled, false);
  const duplicate = await f.send(); assert.equal(duplicate.status, 409); assert.equal(f.calls(), 2);
  assert.equal((await f.control('deliver-recovery', {})).code, 200);
  const response = await pending; assert.equal(response.status, 200);
  const bytes = Buffer.from(await response.arrayBuffer());
  assert.equal(createHash('sha256').update(bytes).digest('hex'), held.replayResponseSHA256);
  const receipt = JSON.parse(bytes); assert.equal(receipt.status, 'AlreadyCommitted');
  assert.equal(receipt.requestId, held.requestId); assert.equal(receipt.records[0].state.quantity, 6);
  assert.equal(receipt.records[0].revision, 2); assert.equal(receipt.commandFingerprint, consumeCommand().commandFingerprint);
  assert.equal((await f.control('deliver-recovery', {})).code, 409);
  // Holding is one-shot; another exact retry retains the old compatibility behavior.
  assert.equal((await f.send()).status, 200);
  const done = (await f.control('state')).body;
  assert.equal(done.heldRecoveryResponses, 1); assert.equal(done.releasedRecoveryResponses, 1);
  assert.equal(done.failure, null);
});
test('held consume recovery is opt-in and cannot be enabled for other operations or invalid flags', async t => {
  for (const operation of ['Item.Move', 'Item.Equip', 'Item.ClaimMail']) {
    const f = await setup(t, { operation, holdRecoveryResponse: true, expectedArmCode: 400 });
    assert.equal(f.calls(), 0); assert.equal((await f.control('state')).body.phase, 'idle');
  }
  for (const flag of ['true', 1, null]) {
    const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: flag, expectedArmCode: 400 });
    assert.equal(f.calls(), 0);
  }
});
test('held consume recovery rejects changed bytes without releasing a successful receipt', { timeout: 5000 }, async t => {
  const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: true });
  await assert.rejects(f.send()); await f.control('release', {});
  const pending = f.send(); const failed = assert.rejects(pending); await waitForHeld(f);
  const changed = consumeCommand(); changed.mutations[0].newRecord.metadata.creationSource = 'changed';
  assert.equal((await f.send(changed)).status, 409); await failed;
  const state = (await f.control('state')).body;
  assert.equal(state.failure, 'FrozenBytesChanged'); assert.equal(state.releasedRecoveryResponses, 0);
  assert.equal(f.calls(), 2); assert.equal((await f.control('deliver-recovery', {})).code, 409);
});
test('held consume recovery shutdown closes its pending response instead of hanging or delivering it', { timeout: 5000 }, async t => {
  const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: true });
  await assert.rejects(f.send()); await f.control('release', {});
  const pending = f.send(); const failed = assert.rejects(pending); await waitForHeld(f);
  await f.p.close(); await failed;
  assert.equal(f.calls(), 2);
});
test('held consume recovery disconnect cannot be released as a completed delivery', { timeout: 5000 }, async t => {
  const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: true });
  await assert.rejects(f.send()); await f.control('release', {});
  const abort = new AbortController();
  const pending = f.send(undefined, auth, abort.signal); const failed = assert.rejects(pending);
  await waitForHeld(f); abort.abort(); await failed;
  const state = await waitForPhase(f, 'failed');
  assert.equal(state.failure, 'HeldRecoveryDisconnected'); assert.equal(state.releasedRecoveryResponses, 0);
  assert.equal(f.calls(), 2); assert.equal((await f.control('deliver-recovery', {})).code, 409);
});
test('held consume recovery cannot hold or release a forged upstream replay receipt', { timeout: 5000 }, async t => {
  const f = await setup(t, { operation: 'Item.Consume', holdRecoveryResponse: true, badReplayReceipt: true });
  await assert.rejects(f.send()); await f.control('release', {});
  await assert.rejects(f.send());
  const state = (await f.control('state')).body;
  assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.heldRecoveryResponses, 0);
  assert.equal(state.releasedRecoveryResponses, 0); assert.equal(f.calls(), 2);
  assert.equal((await f.control('deliver-recovery', {})).code, 409);
});
test('consume drops only the committed reply and replays one exact scoped use', async t => {
  const f = await setup(t, { operation: 'Item.Consume' });
  await assert.rejects(f.send()); assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  const before = (await f.control('state')).body;
  assert.equal(before.droppedAcknowledgements, 1); assert.equal(before.replayed, 0);
  await f.control('release', {});
  const response = await f.send(); assert.equal(response.status, 200);
  const receipt = await response.json(); assert.equal(receipt.status, 'AlreadyCommitted');
  assert.equal(receipt.operation, 'Item.Consume'); assert.equal(receipt.affectedQuantity, 1);
  assert.equal(receipt.commandFingerprint, consumeCommand().commandFingerprint);
  assert.equal(receipt.records.length, 1); assert.equal(receipt.records[0].state.instanceId, source);
  assert.equal(receipt.records[0].state.quantity, 6); assert.equal(receipt.records[0].revision, 2);
  assert.equal(receipt.records[0].location.slotIndex, 0);
  const done = (await f.control('state')).body;
  assert.equal(done.replayed, 1); assert.equal(done.requestId, before.requestId);
  assert.equal(done.commandSHA256, before.commandSHA256);
  assert.equal(done.recordReceiptSHA256, done.replayRecordReceiptSHA256);
  assert.equal(done.failure, null);
});
test('consume rejects quantity, identity, owner, revision, destination and extra mutations before upstream', async t => {
  for (const alter of [c => c.affectedQuantity = 2, c => c.mutations[0].newRecord.state.quantity = 5,
    c => c.mutations[0].newRecord.state.instanceId = pairedItem, c => c.actor.ownerId = pairedItem,
    c => c.mutations[0].newRecord.owner.ownerId = pairedItem, c => c.mutations[0].expectedRevision = 0,
    c => c.mutations[0].newRecord.revision = 2, c => c.mutations[0].newRecord.location.slotIndex = 3,
    c => c.mutations[0].newRecord.location.containerType = 'Mail', c => c.mutations[0].newRecord.metadata.isLocked = true,
    c => c.mutations[0].newRecord.definitionName = 'GameItem.Equipment.Helm.Default',
    c => c.mutations[0].newRecord.lifecycleState = 'Consumed', c => c.mutations.push(structuredClone(c.mutations[0]))]) {
    const f = await setup(t, { operation: 'Item.Consume' }); const c = consumeCommand(); alter(c);
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('consume requires the original item revision and a valid live effect scope', async t => {
  for (const fingerprint of ['fixture-consume', `${source}|1`, `${source}|2|effectScope=${effectScope}`,
    `${pairedItem}|1|effectScope=${effectScope}`, `${source}|1|effectScope=00000000-0000-0000-0000-000000000000`,
    `${source}|1|effectScope=invalid`, `${source}|1|effectScope=${effectScope}|extra`,
    `${source}|1|effectScope=${effectScope}` + String.fromCharCode(10)]) {
    const f = await setup(t, { operation: 'Item.Consume' }); const c = consumeCommand(); c.commandFingerprint = fingerprint;
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('consume cannot change execution scope, original request or item metadata on recovery', async t => {
  for (const alter of [c => c.commandFingerprint = `${source.toUpperCase()}|1|effectScope=${pairedItem}`,
    c => c.requestId = createdItem, c => c.mutations[0].newRecord.metadata.creationSource = 'changed']) {
    const f = await setup(t, { operation: 'Item.Consume' });
    await assert.rejects(f.send()); await f.control('release', {});
    const c = consumeCommand(); alter(c);
    assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
    assert.equal((await f.control('state')).body.failure, 'FrozenBytesChanged');
  }
});
test('consume cannot count malformed or non-200 success responses as a committed drop', async t => {
  for (const options of [{ omitRecord: true }, { badReceipt: true }, { badRevision: true }, { status: 201 }]) {
    const f = await setup(t, { operation: 'Item.Consume', ...options }); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
  }
});
test('consume scope does not grant another owner reads or accept another service credential', async t => {
  const invalid = await setup(t, { operation: 'Item.Consume', additionalReadOwner: pairedItem, expectedArmCode: 400 });
  assert.equal((await invalid.send()).status, 403); assert.equal(invalid.calls(), 0);
  const f = await setup(t, { operation: 'Item.Consume' });
  assert.equal((await f.send(consumeCommand(), 'Bearer wrong')).status, 403); assert.equal(f.calls(), 0);
});
for (const operation of ['Item.Equip', 'Item.Unequip']) {
  test(`${operation} preserves a single original equipment record through exact replay`, async t => {
    const f = await setup(t, { operation }); await assert.rejects(f.send()); assert.equal(f.calls(), 1);
    assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
    await f.control('release', {}); const response = await f.send(); assert.equal(response.status, 200);
    const receipt = await response.json(); assert.equal(receipt.operation, operation); assert.equal(receipt.records.length, 1);
    assert.equal(receipt.records[0].state.instanceId, source); assert.equal(receipt.records[0].state.quantity, 1);
    assert.equal(receipt.records[0].revision, 2); assert.equal(receipt.records[0].metadata.bindState, 'CharacterBound');
    assert.equal(receipt.records[0].location.containerType, operation === 'Item.Equip' ? 'Equipment' : 'Inventory');
    const state = (await f.control('state')).body; assert.equal(state.replayed, 1);
    assert.equal(state.recordReceiptSHA256, state.replayRecordReceiptSHA256);
  });
  test(`${operation} rejects changed ownership, identity, quantities, slots, binding and extra mutations`, async t => {
    for (const alter of [c => c.affectedQuantity = 2, c => c.mutations[0].newRecord.state.quantity = 2,
      c => c.mutations[0].newRecord.state.instanceId = pairedItem, c => c.mutations[0].expectedRevision = 0,
      c => c.mutations[0].newRecord.owner.ownerId = pairedItem, c => c.mutations[0].newRecord.location.slotIndex = 3,
      c => c.mutations[0].newRecord.location.containerType = 'Mail', c => c.mutations[0].newRecord.metadata.isLocked = true,
      c => c.mutations[0].newRecord.metadata.bindState = 'Unbound', c => c.mutations[0].newRecord.definitionName = 'GameItem.Consume.Potion.Red.Large',
      c => c.mutations.push(structuredClone(c.mutations[0]))]) {
      const f = await setup(t, { operation }); const c = equipmentCommand(operation); alter(c);
      assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
    }
  });
  test(`${operation} cannot change metadata in its frozen retry`, async t => {
    const f = await setup(t, { operation }); await assert.rejects(f.send()); await f.control('release', {});
    const c = equipmentCommand(operation); c.mutations[0].newRecord.metadata.creationSource = 'changed';
    assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
  });
  test(`${operation} rejects missing or forged committed records`, async t => {
    for (const options of [{ omitRecord: true }, { badReceipt: true }, { badRevision: true }]) {
      const f = await setup(t, { operation, ...options }); await assert.rejects(f.send());
      const state = (await f.control('state')).body;
      assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
    }
  });
}
test('mail claim retains its whole original attachment and first free slot through exact replay', async t => {
  const f = await setup(t, { operation: 'Item.ClaimMail' }); await assert.rejects(f.send()); assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  await f.control('release', {}); const response = await f.send(); assert.equal(response.status, 200);
  const receipt = await response.json(); assert.equal(receipt.operation, 'Item.ClaimMail');
  assert.equal(receipt.records.length, 1); assert.equal(receipt.records[0].state.instanceId, source);
  assert.equal(receipt.records[0].state.quantity, 7); assert.equal(receipt.records[0].revision, 2);
  assert.equal(receipt.records[0].location.slotIndex, 0);
  const state = (await f.control('state')).body; assert.equal(state.replayed, 1);
  assert.equal(state.recordReceiptSHA256, state.replayRecordReceiptSHA256);
});
test('mail claim rejects partial quantity, new identity, foreign ownership, slots and extra mutations before upstream', async t => {
  for (const alter of [c => c.affectedQuantity = 6, c => c.mutations[0].newRecord.state.quantity = 6,
    c => c.mutations[0].newRecord.state.instanceId = pairedItem, c => c.mutations[0].expectedRevision = 0,
    c => c.mutations[0].newRecord.owner.ownerId = pairedItem, c => c.mutations[0].newRecord.location.slotIndex = 3,
    c => c.mutations[0].newRecord.location.containerType = 'Mail', c => c.mutations[0].newRecord.metadata.isLocked = true,
    c => c.mutations.push(structuredClone(c.mutations[0]))]) {
    const f = await setup(t, { operation: 'Item.ClaimMail' }); const c = claimCommand(); alter(c);
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('mail claim cannot change metadata while recovering the frozen attachment', async t => {
  const f = await setup(t, { operation: 'Item.ClaimMail' }); await assert.rejects(f.send()); await f.control('release', {});
  const c = claimCommand(); c.mutations[0].newRecord.metadata.creationSource = 'changed';
  assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
});
test('mail claim does not accept a missing or forged committed record as a lost acknowledgement', async t => {
  for (const options of [{ omitRecord: true }, { badReceipt: true }, { badRevision: true }]) {
    const f = await setup(t, { operation: 'Item.ClaimMail', ...options }); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
  }
});
test('split replays the original generated identity with source revision two and created revision one', async t => {
  const f = await setup(t, { operation: 'Item.Split' }); await assert.rejects(f.send()); assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  await f.control('release', {}); const response = await f.send(); assert.equal(response.status, 200);
  const receipt = await response.json(); assert.equal(receipt.affectedQuantity, 2);
  assert.equal(receipt.records[0].state.quantity, 5); assert.equal(receipt.records[0].revision, 2);
  assert.equal(receipt.records[1].state.instanceId, createdItem); assert.equal(receipt.records[1].state.quantity, 2);
  assert.equal(receipt.records[1].revision, 1);
  const state = (await f.control('state')).body; assert.equal(state.replayed, 1);
  assert.equal(state.recordReceiptSHA256, state.replayRecordReceiptSHA256);
});
test('split rejects invalid generated identity, revisions, ownership, quantities, metadata and occupied slot before upstream', async t => {
  for (const alter of [c => c.mutations[1].newRecord.state.instanceId = source,
    c => c.mutations[1].newRecord.state.instanceId = pairedItem, c => c.mutations[1].newRecord.state.instanceId = '00000000-0000-0000-0000-000000000000',
    c => c.mutations[1].expectedRevision = 1, c => c.mutations[1].newRecord.revision = 1,
    c => c.mutations[1].newRecord.owner.ownerId = pairedItem, c => c.affectedQuantity = 3,
    c => c.mutations[1].newRecord.state.quantity = 3, c => c.mutations[0].newRecord.state.quantity = 4,
    c => c.mutations[1].newRecord.metadata.bindState = 'Unbound', c => c.mutations[1].newRecord.state.generationSeed = 99,
    c => c.mutations[1].newRecord.location.slotIndex = 1, c => c.mutations.pop()]) {
    const f = await setup(t, { operation: 'Item.Split' }); const c = splitCommand(); alter(c);
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('split cannot substitute another fresh ID while retrying an uncertain frozen command', async t => {
  const f = await setup(t, { operation: 'Item.Split' }); await assert.rejects(f.send()); await f.control('release', {});
  const c = splitCommand(); c.mutations[1].newRecord.state.instanceId = '66666666-2222-3333-4444-555555555555';
  assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
});
test('partial split receipt or a forged created revision cannot complete a committed drop', async t => {
  for (const options of [{ omitRecord: true }, { badReceipt: true, badRecordIndex: 1 }, { badRevision: true, badRecordIndex: 1 }]) {
    const f = await setup(t, { operation: 'Item.Split', ...options }); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
  }
});
test('full merge retains consumed source and quantity eight through one exact replay', async t => {
  const f = await setup(t, { operation: 'Item.StackTransfer' });
  await assert.rejects(f.send()); assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  await f.control('release', {}); const response = await f.send(); assert.equal(response.status, 200);
  const receipt = await response.json(); assert.equal(receipt.affectedQuantity, 7);
  assert.equal(receipt.records[0].lifecycleState, 'Consumed'); assert.equal(receipt.records[0].metadata.isLocked, true);
  assert.equal(receipt.records[1].state.quantity, 8);
  const state = (await f.control('state')).body;
  assert.equal(state.replayed, 1); assert.equal(state.recordReceiptSHA256, state.replayRecordReceiptSHA256);
});
test('merge rejects resurrection, unlocked tombstone, wrong quantity/identity/owner and missing pair before upstream', async t => {
  for (const alter of [c => c.mutations[0].newRecord.lifecycleState = 'Active',
    c => c.mutations[0].newRecord.metadata.isLocked = false, c => c.mutations[0].newRecord.location.containerId = owner,
    c => c.mutations[0].newRecord.location.containerId = '00000000-0000-0000-0000-000000000000',
    c => c.mutations[1].newRecord.state.quantity = 9, c => c.affectedQuantity = 8,
    c => c.mutations[1].newRecord.owner.ownerId = pairedItem, c => c.mutations[1].newRecord.state.instanceId = source,
    c => c.mutations[1].newRecord.definitionName = 'GameItem.Equipment.Helm.Default', c => c.mutations.pop()]) {
    const f = await setup(t, { operation: 'Item.StackTransfer' }); const c = mergeCommand(); alter(c);
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('merge replay cannot alter the consumed source metadata', async t => {
  const f = await setup(t, { operation: 'Item.StackTransfer' }); await assert.rejects(f.send()); await f.control('release', {});
  const c = mergeCommand(); c.mutations[0].newRecord.state.generationSeed = 99;
  assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
});
test('merge never counts a partial or modified source receipt as a committed drop', async t => {
  for (const options of [{ badReceipt: true }, { omitRecord: true }]) {
    const f = await setup(t, { operation: 'Item.StackTransfer', ...options }); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
  }
});
test('declared swap retains two records and zero affected quantity through exact replay', async t => {
  const f = await setup(t, { operation: 'Item.Swap' });
  await assert.rejects(f.send()); assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  await f.control('release', {}); assert.equal((await f.send()).status, 200);
  const state = (await f.control('state')).body;
  assert.equal(state.replayed, 1); assert.equal(state.recordReceiptSHA256, state.replayRecordReceiptSHA256);
});
test('swap rejects forged second owner, duplicate/missing IDs, nonzero transfer and undeclared operation', async t => {
  for (const alter of [c => c.mutations[1].newRecord.owner.ownerId = pairedItem,
    c => c.mutations[1].newRecord.state.instanceId = source, c => c.mutations.pop(),
    c => c.affectedQuantity = 7, c => c.operation = 'Item.Move']) {
    const f = await setup(t, { operation: 'Item.Swap' }); const c = swapCommand(); alter(c);
    assert.equal((await f.send(c)).status, 403); assert.equal(f.calls(), 0);
  }
});
test('a swap replay cannot change even the second record metadata', async t => {
  const f = await setup(t, { operation: 'Item.Swap' }); await assert.rejects(f.send()); await f.control('release', {});
  const c = swapCommand(); c.mutations[1].newRecord.state.generationSeed = 99;
  assert.equal((await f.send(c)).status, 409); assert.equal(f.calls(), 1);
});
test('missing or changed second receipt record never completes a swap observation', async t => {
  for (const options of [{ badReceipt: true, badRecordIndex: 1 }, { omitRecord: true }]) {
    const f = await setup(t, { operation: 'Item.Swap', ...options }); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.failure, 'ReceiptMismatch'); assert.equal(state.droppedAcknowledgements, 0);
  }
});
test('declared remaining owner may load only with the same server credential; it gains no commit permission', async t => {
  const other = '44444444-2222-3333-4444-555555555555';
  const f = await setup(t, { additionalReadOwner: other });
  const load = (id, authorization = auth) => fetch(`http://127.0.0.1:${f.p.proxyPort}/api/items?ownerType=Character&ownerId=${id}`, {
    headers: { authorization } });
  assert.equal((await load(other)).status, 200);
  assert.equal((await load(owner)).status, 200);
  assert.equal((await load(other, 'Bearer wrong')).status, 403);
  assert.equal((await load('55555555-2222-3333-4444-555555555555')).status, 403);
  assert.equal((await f.control('state')).body.additionalOwnerReads, 1);
  const changed = command(); changed.actor.ownerId = other;
  assert.equal((await f.send(changed)).status, 403);
  assert.equal(f.calls(), 0);
});
test('committed response alone is dropped; blocked retries never reach upstream; exact replay succeeds', async t => {
  const f = await setup(t);
  await assert.rejects(f.send());
  assert.equal(f.calls(), 1);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  const lost = (await f.control('state')).body;
  assert.equal(lost.droppedAcknowledgements, 1); assert.equal(lost.blocked, 1);
  await f.control('release', {});
  assert.equal((await f.send()).status, 200);
  const done = (await f.control('state')).body;
  assert.equal(done.replayed, 1); assert.equal(done.recordReceiptSHA256, done.replayRecordReceiptSHA256);
  assert.equal(done.failure, null);
});
test('outside credentials and unauthenticated control cannot forward a command', async t => {
  const f = await setup(t);
  assert.equal((await f.send(command(), 'Bearer wrong')).status, 403);
  assert.equal((await fetch(`http://127.0.0.1:${f.p.controlPort}/state`)).status, 403);
  assert.equal(f.calls(), 0);
});
test('changed frozen command is rejected before any second write', async t => {
  const f = await setup(t); await assert.rejects(f.send()); await f.control('release', {});
  const changed = command(); changed.requestId = '44444444-2222-3333-4444-555555555555';
  assert.equal((await f.send(changed)).status, 409); assert.equal(f.calls(), 1);
  assert.equal((await f.control('state')).body.failure, 'FrozenBytesChanged');
});
test('wrong destination cannot be forwarded', async t => {
  const f = await setup(t); const changed = command(); changed.mutations[0].newRecord.location.slotIndex = 4;
  assert.equal((await f.send(changed)).status, 403); assert.equal(f.calls(), 0);
});
test('mismatched record and non-200 success bodies are never counted as a committed drop', async t => {
  for (const options of [{ badReceipt: true }, { status: 500 }]) {
    const f = await setup(t, options); await assert.rejects(f.send());
    const state = (await f.control('state')).body;
    assert.equal(state.droppedAcknowledgements, 0); assert.equal(state.failure, 'ReceiptMismatch');
  }
});

test('one controlled recovery 401 preserves the original frozen request and permits later exact replay', async t => {
  const f = await setup(t);
  assert.equal((await f.control('fail-recovery', {})).code, 409);
  await assert.rejects(f.send());
  const original = (await f.control('state')).body;
  assert.equal((await f.control('fail-recovery', {})).code, 200);
  assert.equal((await f.control('release', {})).code, 409);
  assert.equal((await f.send()).status, 401); assert.equal(f.calls(), 1);
  const rejected = (await f.control('state')).body;
  assert.equal(rejected.injectedRecovery401, 1); assert.equal(rejected.requestId, original.requestId);
  assert.equal(rejected.commandSHA256, original.commandSHA256); assert.equal(rejected.recordReceiptSHA256, original.recordReceiptSHA256);
  assert.equal(rejected.failure, null);
  assert.equal((await f.send()).status, 503); assert.equal(f.calls(), 1);
  assert.equal((await f.control('fail-recovery', {})).code, 409);
  await f.control('release', {}); assert.equal((await f.send()).status, 200);
  assert.equal((await f.control('state')).body.replayed, 1);
});

test('expired observation relays an upstream 401 without injecting or unlocking a replay', async t => {
  for (const emptyAuthResponse of [false, true]) {
  const f = await setup(t, { rejectReplays: true, emptyAuthResponse });
  assert.equal((await f.control('observe-expired', {})).code, 409);
  await assert.rejects(f.send());
  const original = (await f.control('state')).body;
  assert.equal((await f.control('observe-expired', {})).code, 200);
  assert.equal((await f.control('fail-recovery', {})).code, 409);
  assert.equal((await f.control('release', {})).code, 409);
  const response = await f.send(); assert.equal(response.status, 401);
  assert.equal(await response.text(), emptyAuthResponse ? '' : '{"error":"unauthorized"}');
  const state = (await f.control('state')).body;
  assert.equal(f.calls(), 2); assert.equal(state.expiredForwarded, 1); assert.equal(state.backendUnauthorized, 1);
  assert.equal(state.injectedRecovery401, 0); assert.equal(state.replayed, 0); assert.equal(state.released, false);
  assert.equal(state.requestId, original.requestId); assert.equal(state.commandSHA256, original.commandSHA256); assert.equal(state.failure, null);
  }
});
