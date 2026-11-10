import { createInterface } from 'node:readline';
const lines = createInterface({ input: process.stdin });
let initialized = false;
let notified = false;
lines.on('line', (line) => {
  const message = JSON.parse(line);
  if (message.method === 'initialize') {
    initialized = true;
    process.stdout.write(JSON.stringify({ id: message.id, result: { userAgent: 'test' } }) + '\n');
  } else if (message.method === 'initialized') notified = true;
  else if (message.method === 'account/read') {
    if (!initialized || !notified || message.params.refreshToken !== false || process.env.OPENAI_API_KEY) process.exit(9);
    const result = { id: message.id, result: { account: { type: process.argv[2] ?? 'chatgpt', planType: 'pro', email: 'private@example.invalid', accessToken: 'never-print-me' }, requiresOpenaiAuth: true } };
    const wire = JSON.stringify(result) + '\n';
    process.stdout.write(wire.slice(0, 10));
    setTimeout(() => process.stdout.write(wire.slice(10)), 5);
  } else if (message.id) process.exit(10);
});
