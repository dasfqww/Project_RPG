// Loopback-only opt-in fixture. Never log credentials, bodies or receipt contents.
import http from 'node:http';
import { createHash, timingSafeEqual } from 'node:crypto';
import { isDeepStrictEqual } from 'node:util';
import { pathToFileURL } from 'node:url';

const hash = value => createHash('sha256').update(value).digest('hex');
const guid = value => typeof value === 'string' && /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/.test(value)
  && value !== '00000000-0000-0000-0000-000000000000';
const json = (response, code, value) => { response.writeHead(code, { 'content-type': 'application/json', 'cache-control': 'no-store' }); response.end(JSON.stringify(value)); };
async function read(stream, limit = 262144) {
  const parts = []; let size = 0;
  for await (const part of stream) { size += part.length; if (size > limit) throw new Error('BodyLimit'); parts.push(part); }
  return Buffer.concat(parts);
}
const receiptData = receipt => ({ requestId: receipt.requestId, operation: receipt.operation,
  commandFingerprint: receipt.commandFingerprint, actor: receipt.actor, affectedQuantity: receipt.affectedQuantity, records: receipt.records });

export async function startInventoryRecoveryProxy({ token, upstreamPort, log = () => {} }) {
  if (typeof token !== 'string' || token.length < 32 || !Number.isInteger(upstreamPort) || upstreamPort < 1 || upstreamPort > 65535)
    throw new Error('Invalid local recovery proxy configuration');
  const state = { phase: 'idle', failure: null, forwarded: 0, droppedAcknowledgements: 0, blocked: 0,
    replayed: 0, released: false, requestId: null, commandSHA256: null, recordReceiptSHA256: null,
    replayRecordReceiptSHA256: null, firstResponseSHA256: null, replayResponseSHA256: null,
    recoveryFailureArmed: false, injectedRecovery401: 0, observeCredentialExpiry: false,
    expiredForwarded: 0, backendUnauthorized: 0, backendUnauthorizedResponseSHA256: null, additionalOwnerReads: 0,
    heldRecoveryResponses: 0, releasedRecoveryResponses: 0 };
  let scope, firstBody, firstReceipt, heldRecovery;
  let closing = false;
  const fail = code => {
    state.failure ??= code; state.phase = 'failed';
    const pending = heldRecovery; heldRecovery = undefined;
    pending?.response.destroy();
    log(`INVENTORY_RECOVERY_PROXY_FAIL ${code}`);
  };
  const relay = (response, status, bytes) => {
    response.writeHead(status, { 'content-type': 'application/json', 'cache-control': 'no-store', 'content-length': bytes.length });
    response.end(bytes);
  };
  const authorized = request => {
    const a = Buffer.from(request.headers.authorization ?? ''), b = Buffer.from(`Bearer ${token}`);
    return a.length === b.length && timingSafeEqual(a, b);
  };
  const control = http.createServer((request, response) => {
    void (async () => {
      if (!authorized(request)) return json(response, 403, { error: 'Forbidden' });
      if (request.method === 'GET' && request.url === '/state') return json(response, 200, state);
      if (request.method === 'POST' && request.url === '/deliver-recovery') {
        if (state.failure || state.phase !== 'recovery-response-held' || !heldRecovery || heldRecovery.response.destroyed)
          return json(response, 409, { error: 'ControlState' });
        const pending = heldRecovery; heldRecovery = undefined;
        ++state.releasedRecoveryResponses; state.phase = 'dropped';
        // Release the exact buffered upstream bytes, not a recreated receipt.
        relay(pending.response, pending.status, pending.bytes);
        log('INVENTORY_RECOVERY_PROXY_RECOVERY_RELEASED');
        return json(response, 200, state);
      }
      if (request.method === 'POST' && request.url === '/fail-recovery' && state.phase === 'dropped' && !state.released &&
        !state.observeCredentialExpiry && !state.recoveryFailureArmed && state.injectedRecovery401 === 0) {
        state.recoveryFailureArmed = true; return json(response, 200, state);
      }
      if (request.method === 'POST' && request.url === '/observe-expired' && state.phase === 'dropped' && !state.released &&
        !state.observeCredentialExpiry && !state.recoveryFailureArmed && state.injectedRecovery401 === 0) {
        state.observeCredentialExpiry = true; return json(response, 200, state);
      }
      if (request.method === 'POST' && request.url === '/release' && state.phase === 'dropped' && !state.released &&
        !state.observeCredentialExpiry && !state.recoveryFailureArmed) {
        state.released = true; return json(response, 200, state);
      }
      if (request.method !== 'POST' || request.url !== '/arm' || scope) return json(response, 409, { error: 'ControlState' });
      const body = JSON.parse((await read(request, 4096)).toString('utf8'));
      if (!guid(body.owner) || !guid(body.source) || !/^[0-9a-f]{64}$/.test(body.authorizationSHA256)) return json(response, 400, { error: 'Scope' });
      const operation = body.operation ?? 'Item.Move';
      if (!['Item.Move', 'Item.Swap', 'Item.StackTransfer', 'Item.Split', 'Item.ClaimMail', 'Item.Equip', 'Item.Unequip', 'Item.Consume'].includes(operation) ||
        (operation !== 'Item.Move' && (!guid(body.other) || body.other === body.source || body.additionalReadOwner !== undefined)) ||
        (operation === 'Item.Move' && body.other !== undefined)) return json(response, 400, { error: 'OperationScope' });
      if (body.additionalReadOwner !== undefined && (!guid(body.additionalReadOwner) || body.additionalReadOwner === body.owner))
        return json(response, 400, { error: 'ReadScope' });
      if (body.holdRecoveryResponse !== undefined && (typeof body.holdRecoveryResponse !== 'boolean' ||
        (body.holdRecoveryResponse && operation !== 'Item.Consume'))) return json(response, 400, { error: 'HoldScope' });
      scope = { owner: body.owner, source: body.source, other: body.other, operation, authorizationSHA256: body.authorizationSHA256,
        additionalReadOwner: body.additionalReadOwner, holdRecoveryResponse: body.holdRecoveryResponse === true };
      state.phase = 'armed'; json(response, 200, state);
    })().catch(() => json(response, 400, { error: 'ControlBody' }));
  });
  const proxy = http.createServer((request, response) => {
    void (async () => {
      if (!scope || state.failure || hash(request.headers.authorization ?? '') !== scope.authorizationSHA256)
        return json(response, 403, { error: 'OutsideScope' });
      const url = new URL(request.url, 'http://127.0.0.1');
      const commit = request.method === 'POST' && request.url === '/api/item-transactions/commit';
      const readOwner = url.searchParams.get('ownerId');
      const ownedLoad = request.method === 'GET' && url.pathname === '/api/items' &&
        url.searchParams.get('ownerType') === 'Character' && (readOwner === scope.owner ||
          (scope.additionalReadOwner !== undefined && readOwner === scope.additionalReadOwner));
      if (!commit && !ownedLoad) return json(response, 403, { error: 'OutsideScope' });
      const body = await read(request);
      let command, expiredObservation = false;
      if (commit) {
        command = JSON.parse(body.toString('utf8'));
        const swap = scope.operation === 'Item.Swap';
        const merge = scope.operation === 'Item.StackTransfer', split = scope.operation === 'Item.Split', claim = scope.operation === 'Item.ClaimMail', pair = swap || merge || split;
        const equip = scope.operation === 'Item.Equip', equipment = equip || scope.operation === 'Item.Unequip';
        const consume = scope.operation === 'Item.Consume';
        const createdId = command.mutations?.[1]?.newRecord?.state?.instanceId;
        const matches = (mutation, id, quantity, slot, terminal = false, revision = 1, container = 'Inventory') => {
          const record = mutation?.newRecord;
          return mutation?.expectedRevision === revision && record?.revision === revision && record.state?.instanceId === id && record.state.quantity === quantity &&
            record.owner?.type === 'Character' && record.owner.ownerId === scope.owner && record.location?.slotIndex === slot &&
            (terminal ? record.location.containerType === 'Terminal' && record.location.containerId === '' &&
              record.lifecycleState === 'Consumed' && record.metadata?.isLocked === true
              : record.location.containerType === container && record.location.containerId === scope.owner);
        };
        if (!guid(command.requestId) || command.operation !== scope.operation || command.actor?.type !== 'Character' ||
          command.actor.ownerId !== scope.owner || command.affectedQuantity !== (equipment || consume ? 1 : split ? 2 : swap ? 0 : 7) || command.mutations?.length !== (pair ? 2 : 1) ||
          !matches(command.mutations[0], scope.source, equipment ? 1 : consume ? 6 : split ? 5 : merge ? 0 : 7, equipment || consume || split || claim ? 0 : merge ? -1 : swap ? 1 : 3, merge, 1, equip ? 'Equipment' : 'Inventory') ||
          (equipment && (command.mutations[0].newRecord.lifecycleState !== 'Active' || command.mutations[0].newRecord.metadata?.isLocked !== false ||
            command.mutations[0].newRecord.metadata.bindState !== 'CharacterBound' || command.mutations[0].newRecord.definitionType !== 'RPGItemDefinition' ||
            command.mutations[0].newRecord.definitionName !== 'GameItem.Equipment.Helm.Default' || command.mutations[0].newRecord.definitionVersion !== 1)) ||
          ((claim || consume) && (command.mutations[0].newRecord.lifecycleState !== 'Active' || command.mutations[0].newRecord.metadata?.isLocked !== false ||
            command.mutations[0].newRecord.definitionType !== 'RPGItemDefinition' ||
            command.mutations[0].newRecord.definitionName !== 'GameItem.Consume.Potion.Red.Large' || command.mutations[0].newRecord.definitionVersion !== 1)) ||
          (split ? !guid(createdId) || createdId === scope.source || createdId === scope.other ||
            !matches(command.mutations[1], createdId, 2, 3, false, 0)
            : pair && !matches(command.mutations[1], scope.other, merge ? 8 : 1, merge ? 1 : 0)) ||
          (merge && (!command.mutations.every(m => m.newRecord.definitionType === 'RPGItemDefinition' &&
            m.newRecord.definitionName === 'GameItem.Consume.Potion.Red.Large' && m.newRecord.definitionVersion === 1) ||
            command.mutations[1].newRecord.lifecycleState !== 'Active' || command.mutations[1].newRecord.metadata?.isLocked !== false)) ||
          !command.commandFingerprint) {
          fail('CommandScope'); return json(response, 403, { error: 'CommandScope' });
        }
        if (consume) {
          // One authored potion use in the same live effect scope only: 7 -> 6,
          // revision 1 -> 2, original slot 0. This is not a restart-effect fixture.
          const fingerprint = typeof command.commandFingerprint === 'string'
            ? /^([0-9a-fA-F]{8}(?:-[0-9a-fA-F]{4}){3}-[0-9a-fA-F]{12})\|1\|effectScope=([0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12})$/.exec(command.commandFingerprint) : null;
          if (!fingerprint || fingerprint[0] !== command.commandFingerprint || fingerprint[1].toLowerCase() !== scope.source || !guid(fingerprint[2])) {
            fail('CommandScope'); return json(response, 403, { error: 'CommandScope' });
          }
        }
        if (split) {
          const a = command.mutations[0].newRecord, b = structuredClone(command.mutations[1].newRecord);
          // Generated identity/revision/location/quantity are the only changes from the original stack.
          b.state.instanceId = a.state.instanceId; b.state.quantity = a.state.quantity;
          b.revision = a.revision; b.location.slotIndex = a.location.slotIndex;
          if (a.definitionType !== 'RPGItemDefinition' || a.definitionName !== 'GameItem.Consume.Potion.Red.Large' ||
            a.definitionVersion !== 1 || a.lifecycleState !== 'Active' || a.metadata?.isLocked !== false || !isDeepStrictEqual(a, b)) {
            fail('CommandScope'); return json(response, 403, { error: 'CommandScope' });
          }
        }
        if (firstBody && !body.equals(firstBody)) { fail('FrozenBytesChanged'); return json(response, 409, { error: 'FrozenBytesChanged' }); }
        if (firstBody && heldRecovery && state.phase === 'recovery-response-held') {
          ++state.blocked; return json(response, 409, { error: 'TestRecoveryResponseHeld' });
        }
        if (firstBody && state.recoveryFailureArmed) {
          state.recoveryFailureArmed = false; ++state.injectedRecovery401;
          log('INVENTORY_RECOVERY_PROXY_INJECTED_401');
          return json(response, 401, { error: 'TestRecoveryUnauthorized' }); // explicitly synthetic, never expires a real credential
        }
        if (firstBody && state.observeCredentialExpiry) { expiredObservation = true; ++state.expiredForwarded; }
        else if (firstBody && !state.released) { ++state.blocked; return json(response, 503, { error: 'TestRecoveryHeld' }); }
        if ((!firstBody && state.phase !== 'armed') || (firstBody && state.phase !== 'dropped')) {
          fail('ConcurrentCommit'); return json(response, 409, { error: 'ConcurrentCommit' });
        }
        if (!firstBody) { firstBody = Buffer.from(body); state.requestId = command.requestId; state.commandSHA256 = hash(body); state.phase = 'forwarding'; }
      } else if (body.length) return json(response, 400, { error: 'ReadBody' });
      if (ownedLoad && readOwner === scope.additionalReadOwner) ++state.additionalOwnerReads;
      const upstream = http.request({ hostname: '127.0.0.1', port: upstreamPort, method: request.method, path: request.url,
        headers: { authorization: request.headers.authorization, 'content-type': 'application/json', 'content-length': body.length, connection: 'close' } });
      upstream.setTimeout(15000, () => upstream.destroy());
      upstream.on('error', () => { fail('UpstreamTransport'); response.destroy(); });
      upstream.on('response', incoming => {
        void (async () => {
          const bytes = await read(incoming);
          if (expiredObservation) {
            const document = bytes.length ? JSON.parse(bytes.toString('utf8')) : {};
            if (incoming.statusCode !== 401 || ['Committed', 'AlreadyCommitted'].includes(document.status)) {
              fail('ExpectedRealBackend401'); response.destroy(); return;
            }
            ++state.backendUnauthorized; state.backendUnauthorizedResponseSHA256 = hash(bytes);
            log('INVENTORY_RECOVERY_PROXY_BACKEND_401');
            response.writeHead(401, { 'content-type': 'application/json', 'cache-control': 'no-store', 'content-length': bytes.length });
            response.end(bytes); return; // relay the actual backend response; never synthesize or alter it
          }
          if (commit) {
            const receipt = JSON.parse(bytes.toString('utf8'));
            const expected = command.mutations.map(mutation => { const record = structuredClone(mutation.newRecord); record.revision = mutation.expectedRevision + 1; return record; });
            const sorted = records => [...records].sort((a, b) => String(a.state?.instanceId).localeCompare(String(b.state?.instanceId)));
            const replay = firstReceipt != null;
            if (incoming.statusCode !== 200 || receipt.status !== (replay ? 'AlreadyCommitted' : 'Committed') ||
              receipt.requestId !== command.requestId || receipt.operation !== command.operation ||
              receipt.commandFingerprint !== command.commandFingerprint || !isDeepStrictEqual(receipt.actor, command.actor) ||
              receipt.affectedQuantity !== command.affectedQuantity || !Array.isArray(receipt.records) || receipt.records.length !== expected.length ||
              !isDeepStrictEqual(sorted(receipt.records), sorted(expected))) {
              fail('ReceiptMismatch'); response.destroy(); return;
            }
            const data = receiptData(receipt);
            if (!replay) {
              firstReceipt = data; ++state.forwarded; state.firstResponseSHA256 = hash(bytes);
              state.recordReceiptSHA256 = hash(JSON.stringify(data)); state.droppedAcknowledgements = 1; state.phase = 'dropped';
              log('INVENTORY_RECOVERY_PROXY_DROPPED'); response.destroy(); return;
            }
            if (!isDeepStrictEqual(firstReceipt, data)) { fail('ReplayReceiptMismatch'); response.destroy(); return; }
            ++state.replayed; state.replayResponseSHA256 = hash(bytes); state.replayRecordReceiptSHA256 = hash(JSON.stringify(data));
            log('INVENTORY_RECOVERY_PROXY_REPLAY_MATCHED');
            if (scope.holdRecoveryResponse && state.heldRecoveryResponses === 0) {
              heldRecovery = { response, status: incoming.statusCode, bytes };
              ++state.heldRecoveryResponses; state.phase = 'recovery-response-held';
              response.once('close', () => {
                if (!closing && heldRecovery?.response === response) fail('HeldRecoveryDisconnected');
              });
              log('INVENTORY_RECOVERY_PROXY_RECOVERY_HELD');
              return;
            }
          }
          // Do not forward hop-by-hop headers or a stale content length.
          relay(response, incoming.statusCode, bytes);
        })().catch(() => { fail('UpstreamBody'); response.destroy(); });
      });
      upstream.end(body);
    })().catch(() => { fail('InvalidRequest'); response.destroy(); });
  });
  for (const server of [proxy, control]) await new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', resolve); });
  return { proxyPort: proxy.address().port, controlPort: control.address().port,
    close: async () => {
      closing = true;
      const pending = heldRecovery; heldRecovery = undefined;
      pending?.response.destroy();
      await Promise.all([proxy, control].map(server => new Promise(resolve => { server.close(resolve); server.closeAllConnections(); })));
    } };
}

if (process.argv[1] && import.meta.url === pathToFileURL(process.argv[1]).href) {
  const fixture = await startInventoryRecoveryProxy({ token: process.env.PROJECT_RPG_INVENTORY_PROXY_CONTROL_TOKEN,
    upstreamPort: Number(process.env.PROJECT_RPG_INVENTORY_PROXY_UPSTREAM_PORT), log: line => console.log(line) });
  console.log(`INVENTORY_RECOVERY_PROXY_READY ControlPort=${fixture.controlPort} ProxyPort=${fixture.proxyPort}`);
}
