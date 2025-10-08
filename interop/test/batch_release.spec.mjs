import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { startServer, stopServer } from '../scripts/server.js';

let server;

describe('HTTP batch release frames when passing client stubs in args', () => {
  beforeAll(async () => {
    server = await startServer('batch-pipelining');
  });

  afterAll(async () => {
    await stopServer(server);
  });

  it('emits release for client-exported stub in args', async () => {
    const push = JSON.stringify(["push", ["pipeline", 0, ["getUserProfile"], ["u_1", ["export", -1]]]]);
    const pull = JSON.stringify(["pull", 1]);
    const body = `${push}\n${pull}`;

    const resp = await fetch('http://127.0.0.1:8000/rpc', {
      method: 'POST',
      body,
    });
    const text = await resp.text();
    const lines = text === '' ? [] : text.split('\n');
    // Expect both resolve for export 1 and release for id -1
    expect(lines.length).toBeGreaterThanOrEqual(2);
    const hasResolve = lines.some(l => {
      try { const m = JSON.parse(l); return Array.isArray(m) && m[0] === 'resolve' && m[1] === 1; } catch { return false; }
    });
    const hasRelease = lines.some(l => {
      try { const m = JSON.parse(l); return Array.isArray(m) && m[0] === 'release' && m[1] === -1 && m[2] === 1; } catch { return false; }
    });
    expect(hasResolve).toBe(true);
    expect(hasRelease).toBe(true);
  });
});

