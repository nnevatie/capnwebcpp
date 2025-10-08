import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { newWebSocketRpcSession } from 'capnweb';
import { startServer, stopServer } from '../scripts/server.js';

globalThis.WebSocket = (await import('ws')).default;

let server;

describe('WebSocket interop with capnweb (hello world)', () => {
  beforeAll(async () => {
    server = await startServer('helloworld');
  });

  afterAll(async () => {
    await stopServer(server);
  });

  it('calls hello over WS', async () => {
    const api = newWebSocketRpcSession('ws://127.0.0.1:8000/api');
    const res = await api.hello('World');
    expect(res).toBe('Hello, World!');
  });
});

