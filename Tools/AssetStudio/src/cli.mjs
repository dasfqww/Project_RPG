import { parseArgs } from 'node:util';
import { readFile } from 'node:fs/promises';
import { launchBlender } from './launch.mjs';
import { callBridge, readEndpoint } from './bridge-client.mjs';
import { makeRequest, StudioError } from './protocol.mjs';
import { CodexDiagnostic } from './codex-client.mjs';

const usage = `Asset Studio connection foundation (Node 24+)\n\n  node src/cli.mjs launch --blender <exe> --workspace <directory> [--input <asset.blend>]\n  node src/cli.mjs inspect --connection <connection.json>\n  node src/cli.mjs apply --connection <connection.json> --request <request.json>\n  node src/cli.mjs codex-status --codex <exe>\n\nNo model turns, API generation, or automatic retries are performed.\n`;
try {
  const { positionals, values } = parseArgs({ allowPositionals: true, options: {
    blender: { type: 'string' }, workspace: { type: 'string' }, input: { type: 'string' },
    connection: { type: 'string' }, request: { type: 'string' }, codex: { type: 'string' },
  } });
  const requireValue = (key) => { if (!values[key]) throw new StudioError('ARGUMENT_REQUIRED', `--${key} is required.`); return values[key]; };
  const command = positionals[0];
  if (!command || command === 'help') process.stdout.write(usage);
  else if (command === 'launch') {
    const result = await launchBlender(requireValue('blender'), requireValue('workspace'), { input: values.input });
    result.child.unref();
    console.log(JSON.stringify({ ready: true, connection_file: result.endpointFile, working_directory: result.sessionDir }, null, 2));
  } else if (command === 'inspect' || command === 'apply') {
    const endpoint = await readEndpoint(requireValue('connection'));
    const request = command === 'inspect' ? makeRequest('scene.inspect') : JSON.parse(await readFile(requireValue('request'), 'utf8'));
    const response = await callBridge(endpoint, request);
    console.log(JSON.stringify(response, null, 2));
    if (response.status !== 'succeeded') process.exitCode = 1;
  } else if (command === 'codex-status') {
    const client = new CodexDiagnostic(requireValue('codex'));
    try { console.log(JSON.stringify(await client.inspect(), null, 2)); }
    finally { await client.close(); }
  } else throw new StudioError('UNKNOWN_COMMAND', usage);
} catch (error) {
  console.error(JSON.stringify({ error: error.code ?? 'FAILED', message: error instanceof StudioError ? error.message : 'Operation failed; check the selected paths and file format.', effect_state: error.effectState ?? 'not_applied' }));
  process.exitCode = 1;
}
