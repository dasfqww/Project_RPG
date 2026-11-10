import net from 'node:net';
import { readFile } from 'node:fs/promises';
import { MAX_FRAME, StudioError, validateEndpoint } from './protocol.mjs';

export async function readEndpoint(filename) {
  const content = await readFile(filename, 'utf8');
  if (Buffer.byteLength(content) > 4096) throw new StudioError('INVALID_ENDPOINT', 'Endpoint file too large.');
  return validateEndpoint(JSON.parse(content));
}

// One connection per operation. A timeout is never an instruction to retry a write.
export function callBridge(endpoint, request, { timeoutMs = 30000 } = {}) {
  validateEndpoint(endpoint);
  const wire = JSON.stringify({ token: endpoint.token, request }) + '\n';
  if (Buffer.byteLength(wire) > MAX_FRAME) throw new StudioError('FRAME_TOO_LARGE', 'Request exceeds limit.');
  return new Promise((resolve, reject) => {
    let sent = false;
    let settled = false;
    let buffer = Buffer.alloc(0);
    const socket = net.createConnection({ host: endpoint.host, port: endpoint.port });
    const finish = (error, value) => {
      if (settled) return;
      settled = true;
      clearTimeout(timer);
      socket.destroy();
      if (error) reject(error); else resolve(value);
    };
    const transportError = (code) => new StudioError(code,
      'Blender connection ended without a confirmed result. Inspect the operation receipt before retrying.',
      sent && request.operation !== 'scene.inspect' ? 'unknown' : 'not_applied');
    const timer = setTimeout(() => finish(transportError('BRIDGE_TIMEOUT')), timeoutMs);
    socket.on('connect', () => { sent = true; socket.end(wire); });
    socket.on('data', (chunk) => {
      buffer = Buffer.concat([buffer, chunk]);
      if (buffer.length > MAX_FRAME) return finish(transportError('FRAME_TOO_LARGE'));
      const end = buffer.indexOf(10);
      if (end < 0) return;
      try {
        const response = JSON.parse(buffer.subarray(0, end).toString('utf8'));
        if (response.protocol_version !== 1 || response.operation_id !== request.operation_id ||
            !['succeeded', 'rejected', 'unknown'].includes(response.status)) {
          return finish(transportError('INVALID_RESPONSE'));
        }
        finish(null, response);
      } catch { finish(transportError('INVALID_RESPONSE')); }
    });
    socket.on('error', () => finish(transportError('BRIDGE_DISCONNECTED')));
    socket.on('close', () => finish(transportError('BRIDGE_DISCONNECTED')));
  });
}
