import { spawn } from 'node:child_process';
import { randomUUID } from 'node:crypto';
import { mkdir, readFile, realpath, lstat } from 'node:fs/promises';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { setTimeout as delay } from 'node:timers/promises';
import { StudioError } from './protocol.mjs';
import { readEndpoint } from './bridge-client.mjs';

export async function launchBlender(executable, workspace, { input, timeoutMs = 60000 } = {}) {
  const absolute = path.resolve(workspace);
  await mkdir(absolute, { recursive: true, mode: 0o700 });
  const sessions = path.join(await realpath(absolute), 'sessions');
  await mkdir(sessions, { recursive: true, mode: 0o700 });
  if ((await lstat(sessions)).isSymbolicLink()) throw new StudioError('UNSAFE_WORKSPACE', 'Sessions directory is a link.');
  const sessionDir = path.join(sessions, randomUUID());
  await mkdir(sessionDir, { mode: 0o700 });
  const bridgePath = fileURLToPath(new URL('../blender/asset_studio_bridge.py', import.meta.url));
  const args = ['--factory-startup', '--disable-autoexec'];
  if (input) {
    const source = await realpath(input);
    if (path.extname(source).toLowerCase() !== '.blend') throw new StudioError('INPUT_FORMAT', 'This first bridge accepts .blend inputs.');
    args.push(source);
  }
  args.push('--python', bridgePath);
  const child = spawn(executable, args, {
    env: { ...process.env, ASSET_STUDIO_SESSION_DIR: sessionDir, PYTHONDONTWRITEBYTECODE: '1' },
    windowsHide: false, detached: false, stdio: 'ignore',
  });
  let launchError;
  child.on('error', () => { launchError = new StudioError('BLENDER_START_FAILED', 'Could not start the selected Blender executable.'); });
  const endpointFile = path.join(sessionDir, 'connection.json');
  const deadline = Date.now() + timeoutMs;
  while (Date.now() < deadline) {
    if (launchError) throw launchError;
    if (child.exitCode !== null || child.signalCode !== null) throw new StudioError('BLENDER_EXITED', 'Blender exited before the bridge became ready.');
    try {
      const endpoint = await readEndpoint(endpointFile);
      return { child, sessionDir, endpointFile, endpoint };
    } catch (error) {
      if (error.code !== 'ENOENT') { child.kill(); throw error; }
    }
    try {
      const failure = JSON.parse(await readFile(path.join(sessionDir, 'startup-error.json'), 'utf8'));
      child.kill();
      throw new StudioError('BLENDER_START_FAILED', failure.code);
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    await delay(100);
  }
  child.kill(); // Only the process created by this call, before it was handed to a user.
  throw new StudioError('BLENDER_START_TIMEOUT', 'Blender bridge did not become ready.');
}
