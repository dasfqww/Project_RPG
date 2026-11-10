import { randomUUID } from 'node:crypto';

export const VERSION = 1;
export const MAX_FRAME = 1024 * 1024;
export const OPERATIONS = new Set([
  'scene.inspect', 'object.transform', 'material.apply_profile', 'checkpoint.save',
]);
export class StudioError extends Error {
  constructor(code, message, effectState = 'not_applied') {
    super(message);
    this.name = 'StudioError';
    this.code = code;
    this.effectState = effectState;
  }
}
export function makeRequest(operation, params = {}, scene = null, targetIds = [], operationId = randomUUID()) {
  if (!OPERATIONS.has(operation)) throw new StudioError('UNSUPPORTED_OPERATION', operation);
  if (operation !== 'scene.inspect' && !scene) throw new StudioError('SCENE_REQUIRED', 'Inspect the scene before editing.');
  return {
    protocol_version: VERSION,
    operation_id: operationId,
    blender_session_id: scene?.blender_session_id ?? null,
    expected_scene_revision: scene?.scene_revision ?? null,
    operation,
    target_ids: targetIds,
    params,
  };
}
export function validateEndpoint(endpoint) {
  if (endpoint?.protocol_version !== VERSION || endpoint?.host !== '127.0.0.1' ||
      !Number.isInteger(endpoint?.port) || endpoint.port < 1 || endpoint.port > 65535 ||
      typeof endpoint?.token !== 'string' || !/^[a-f0-9]{64}$/.test(endpoint.token)) {
    throw new StudioError('INVALID_ENDPOINT', 'Expected a version 1 authenticated loopback endpoint.');
  }
  return endpoint;
}
