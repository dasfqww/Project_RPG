import { spawn } from 'node:child_process';
import { createHash, randomUUID } from 'node:crypto';
import { mkdir, readdir, readFile, writeFile, lstat, realpath, rm } from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import path from 'node:path';
import { parseArgs } from 'node:util';
import assert from 'node:assert/strict';
import { setTimeout as delay } from 'node:timers/promises';
import { launchBlender } from '../src/launch.mjs';
import { callBridge } from '../src/bridge-client.mjs';
import { makeRequest } from '../src/protocol.mjs';
import { CodexDiagnostic } from '../src/codex-client.mjs';

const { values } = parseArgs({ options: { blender: { type: 'string' }, codex: { type: 'string' }, 'proof-root': { type: 'string' } } });
if (!values.blender || !values['proof-root']) throw new Error('--blender and --proof-root are required.');
const sourceRoot = fileURLToPath(new URL('../', import.meta.url));
const proofRoot = path.resolve(values['proof-root']);
await mkdir(proofRoot, { recursive: true });
const tempParent = path.join(await realpath(proofRoot), 'Temp');
await mkdir(tempParent, { recursive: true });
if ((await lstat(tempParent)).isSymbolicLink()) throw new Error('Temporary parent must not be a link.');
const runId = randomUUID();
const temp = path.join(tempParent, runId);
await mkdir(temp);
await writeFile(path.join(temp, 'owner.json'), JSON.stringify({ runId }));
const report = { run_id: runId, started_at: new Date().toISOString(), status: 'running', checks: {}, model_requests: 0, generation_requests: 0, cleanup: null };
let visible;
let failure;

async function sourceHashes(directory) {
  const result = {};
  for (const entry of await readdir(directory, { withFileTypes: true })) {
    if (['node_modules', '.runtime', '__pycache__'].includes(entry.name)) continue;
    const name = path.join(directory, entry.name);
    if (entry.isSymbolicLink()) throw new Error('Source link is not allowed in this verification.');
    if (entry.isDirectory()) Object.assign(result, await sourceHashes(name));
    else result[path.relative(sourceRoot, name).replaceAll('\\', '/')] = createHash('sha256').update(await readFile(name)).digest('hex');
  }
  return result;
}
async function stopOwned(child) {
  if (!child || child.exitCode !== null || child.signalCode !== null) return;
  await new Promise((resolve, reject) => {
    const timer = setTimeout(() => reject(new Error('Owned Blender process did not exit; preserve its temporary files.')), 10000);
    child.once('close', () => { clearTimeout(timer); resolve(); });
    child.kill();
  });
}
async function runProcess(executable, args) {
  return await new Promise((resolve, reject) => {
    const child = spawn(executable, args, { cwd: sourceRoot, windowsHide: true,
      env: { ...process.env, PYTHONDONTWRITEBYTECODE: '1' }, stdio: ['ignore', 'pipe', 'pipe'] });
    let output = '';
    let timedOut = false;
    const timer = setTimeout(() => { timedOut = true; child.kill(); }, 90000);
    const collect = data => { output = (output + data.toString()).slice(-100000); };
    child.stdout.on('data', collect);
    child.stderr.on('data', collect);
    child.on('error', error => { clearTimeout(timer); reject(error); });
    child.on('close', code => { clearTimeout(timer); resolve({ code, output, timedOut }); });
  });
}
async function safeCleanup() {
  const resolved = await realpath(temp);
  if (resolved !== temp || path.dirname(resolved) !== tempParent || path.basename(resolved) !== runId) throw new Error('Temporary path ownership check failed.');
  const marker = JSON.parse(await readFile(path.join(resolved, 'owner.json'), 'utf8'));
  if (marker.runId !== runId) throw new Error('Temporary ownership marker changed.');
  let bytes = 0;
  async function inspect(directory) {
    for (const entry of await readdir(directory)) {
      const name = path.join(directory, entry);
      const stat = await lstat(name);
      if (stat.isSymbolicLink()) throw new Error('Refusing cleanup through a symlink or junction.');
      const canonical = await realpath(name);
      const relative = path.relative(resolved, canonical);
      if (relative.startsWith('..') || path.isAbsolute(relative)) throw new Error('Cleanup escaped the owned directory.');
      if (stat.isDirectory()) await inspect(name); else bytes += stat.size;
    }
  }
  await inspect(resolved);
  await rm(resolved, { recursive: true, force: false });
  return { removed_bytes: bytes, exception: null };
}

try {
  report.source_hashes = await sourceHashes(sourceRoot);
  const node = await runProcess(process.execPath, ['--test', 'tests/transport.test.mjs']);
  assert.equal(node.code, 0, node.output);
  assert.equal(node.timedOut, false);
  report.checks.transport = { exit_code: node.code, output: node.output };
  console.log('Transport and account-diagnostic contract tests passed.');
  const backgroundDir = path.join(temp, 'contracts');
  await mkdir(backgroundDir);
  const blender = await runProcess(values.blender, ['--background', '--factory-startup', '--disable-autoexec', '--python-exit-code', '1', '--python', path.join(sourceRoot, 'tests/blender_contract.py'), '--', backgroundDir, path.join(sourceRoot, 'blender/asset_studio_bridge.py')]);
  assert.equal(blender.code, 0, blender.output);
  assert.equal(blender.timedOut, false);
  const resultLine = blender.output.split(/\r?\n/).find(line => line.startsWith('ASSET_STUDIO_RESULT '));
  assert.ok(resultLine, blender.output);
  report.checks.blender_contract = JSON.parse(resultLine.slice('ASSET_STUDIO_RESULT '.length));
  console.log(`Real Blender data checks passed: ${report.checks.blender_contract.passed}.`);
  visible = await launchBlender(values.blender, path.join(temp, 'visible'));
  console.log('Visible Blender instance connected over authenticated loopback.');
  const cli = await runProcess(process.execPath, ['src/cli.mjs', 'inspect', '--connection', visible.endpointFile]);
  assert.equal(cli.code, 0, cli.output);
  assert.equal(JSON.parse(cli.output).status, 'succeeded');
  report.checks.cli_inspect = true;
  let inspected = await callBridge(visible.endpoint, makeRequest('scene.inspect'));
  assert.equal(inspected.status, 'succeeded');
  const cube = inspected.result.objects.find(object => object.name === 'Cube');
  assert.ok(cube);
  const edit = makeRequest('object.transform', { location: [1, 2, 3] }, inspected.result, [cube.id]);
  const first = await callBridge(visible.endpoint, edit);
  assert.equal(first.status, 'succeeded', JSON.stringify(first));
  const replay = await callBridge(visible.endpoint, edit);
  assert.deepEqual(replay, first);
  inspected = await callBridge(visible.endpoint, makeRequest('scene.inspect'));
  assert.deepEqual(inspected.result.objects.find(object => object.id === cube.id).location, [1, 2, 3]);
  const badAuth = await callBridge({ ...visible.endpoint, token: '0'.repeat(64) }, makeRequest('scene.inspect'));
  assert.equal(badAuth.status, 'rejected');
  const checkpoint = first.result.artifacts.at(-1);
  const actualHash = createHash('sha256').update(await readFile(path.join(visible.sessionDir, checkpoint.path))).digest('hex');
  assert.equal(actualHash, checkpoint.sha256);
  report.checks.visible_blender = { connected: true, transform: true, duplicate_receipt: true, wrong_token_rejected: true, checkpoint_hash: true };
  await stopOwned(visible.child);
  visible = null;
  if (values.codex) {
    const diagnostic = new CodexDiagnostic(values.codex, { cwd: sourceRoot });
    try { report.checks.codex = await diagnostic.inspect(); }
    finally { await diagnostic.close(); }
    console.log(`Codex app-server connected; authentication=${report.checks.codex.authentication ?? 'none'}; no model request.`);
  }
  assert.deepEqual(await sourceHashes(sourceRoot), report.source_hashes, 'Sources changed during verification.');
  report.status = 'passed';
} catch (error) {
  failure = error;
  report.status = 'failed';
  report.failure = { code: error.code ?? null, message: error.message };
} finally {
  try {
    await stopOwned(visible?.child);
    report.cleanup = await safeCleanup();
  } catch (error) {
    report.cleanup = { exception: error.message, preserved_path: temp };
    report.status = 'failed';
    failure ??= error;
  }
  report.finished_at = new Date().toISOString();
  const summary = path.join(proofRoot, `${runId}.json`);
  await writeFile(summary, JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ status: report.status, proof: summary, cleanup: report.cleanup }));
}
if (failure) { console.error(failure.message); process.exitCode = 1; }
