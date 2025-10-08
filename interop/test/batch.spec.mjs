import { describe, it, expect, beforeAll, afterAll } from 'vitest';
import { newHttpBatchRpcSession } from 'capnweb';
import { startServer, stopServer } from '../scripts/server.js';

let server;

describe('HTTP batch interop with capnweb', () => {
  beforeAll(async () => {
    server = await startServer('batch-pipelining');
  });

  afterAll(async () => {
    await stopServer(server);
  });

  it('auth + get profile (separate batches)', async () => {
    {
      const api = newHttpBatchRpcSession('http://127.0.0.1:8000/rpc');
      const user = await api.authenticate('cookie-123');
      expect(user.id).toBe('u_1');
    }
    {
      const api = newHttpBatchRpcSession('http://127.0.0.1:8000/rpc');
      const profile = await api.getUserProfile('u_1');
      expect(profile).toEqual({ id: 'u_1', bio: expect.any(String) });
    }
  });
});

