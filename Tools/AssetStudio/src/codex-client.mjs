import { spawn } from 'node:child_process';
import { StudioError, MAX_FRAME } from './protocol.mjs';

// Diagnostic surface deliberately cannot start turns, log in/out, or buy credits.
const ALLOWED_METHODS = new Set(['initialize', 'account/read']);
export class CodexDiagnostic {
  constructor(executable, { cwd, env = process.env, timeoutMs = 15000, args = ['app-server', '--listen', 'stdio://'] } = {}) {
    this.nextId = 1;
    this.pending = new Map();
    this.buffer = '';
    this.timeoutMs = timeoutMs;
    this.closed = false;
    this.ended = false;
    const childEnv = { ...env };
    for (const name of Object.keys(childEnv)) {
      if (/^(OPENAI_API_KEY|CODEX_API_KEY|AZURE_OPENAI_API_KEY)$/i.test(name)) delete childEnv[name];
    }
    this.child = spawn(executable, args, { cwd, env: childEnv, windowsHide: true, stdio: ['pipe', 'pipe', 'pipe'] });
    this.child.stderr.on('data', () => {}); // Do not persist unfiltered server diagnostics or account details.
    this.child.stdin.on('error', () => this.failAll('CODEX_DISCONNECTED'));
    this.child.stdout.setEncoding('utf8');
    this.child.stdout.on('data', (data) => this.receive(data));
    this.child.on('error', () => this.failAll('CODEX_START_FAILED'));
    this.child.on('close', () => { this.ended = true; this.failAll('CODEX_DISCONNECTED'); });
  }
  failAll(code) {
    this.closed = true;
    for (const entry of this.pending.values()) {
      clearTimeout(entry.timer);
      entry.reject(new StudioError(code, 'Codex diagnostic connection is unavailable.'));
    }
    this.pending.clear();
  }
  receive(data) {
    if (this.closed) return;
    this.buffer += data;
    if (Buffer.byteLength(this.buffer) > MAX_FRAME) return this.failAll('CODEX_FRAME_TOO_LARGE');
    let newline;
    while ((newline = this.buffer.indexOf('\n')) >= 0) {
      const line = this.buffer.slice(0, newline);
      this.buffer = this.buffer.slice(newline + 1);
      if (!line.trim()) continue;
      let message;
      try { message = JSON.parse(line); } catch { return this.failAll('CODEX_INVALID_RESPONSE'); }
      if (message.method) {
        if (message.id !== undefined && !this.child.stdin.destroyed) {
          this.child.stdin.write(JSON.stringify({ id: message.id, error: { code: -32601, message: 'Diagnostic client does not accept server requests.' } }) + '\n');
        }
        continue;
      }
      const entry = this.pending.get(message.id);
      if (!entry) continue;
      clearTimeout(entry.timer);
      this.pending.delete(message.id);
      if (message.error) entry.reject(new StudioError('CODEX_REQUEST_FAILED', 'Codex rejected the diagnostic request.'));
      else if (!Object.hasOwn(message, 'result')) entry.reject(new StudioError('CODEX_INVALID_RESPONSE', 'Missing result.'));
      else entry.resolve(message.result);
    }
  }
  request(method, params) {
    if (!ALLOWED_METHODS.has(method)) throw new StudioError('METHOD_NOT_ALLOWED', 'Only connection and account inspection are enabled.');
    if (this.closed) throw new StudioError('CODEX_DISCONNECTED', 'Diagnostic connection closed.');
    const id = this.nextId++;
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id);
        reject(new StudioError('CODEX_TIMEOUT', 'Codex diagnostic request timed out.'));
      }, this.timeoutMs);
      this.pending.set(id, { resolve, reject, timer });
      this.child.stdin.write(JSON.stringify({ id, method, params }) + '\n');
    });
  }
  async inspect() {
    await this.request('initialize', { clientInfo: { name: 'asset_studio_diagnostic', title: 'Asset Studio', version: '0.1.0' } });
    this.child.stdin.write(JSON.stringify({ method: 'initialized', params: {} }) + '\n');
    const result = await this.request('account/read', { refreshToken: false });
    const type = result?.account?.type ?? null;
    return {
      connected: true,
      authentication: type,
      plan_type: type === 'chatgpt' ? result.account.planType ?? null : null,
      subscription_auth_detected: type === 'chatgpt',
      model_request_tested: false,
      extra_spend_policy_verified: false,
    };
  }
  async close() {
    const child = this.child;
    if (this.ended || child.exitCode !== null || child.signalCode !== null) return;
    this.failAll('CODEX_CLOSED');
    await new Promise((resolve) => {
      const timer = setTimeout(() => child.kill(), 1500);
      child.once('close', () => { clearTimeout(timer); resolve(); });
      child.stdin.end();
    });
  }
}
