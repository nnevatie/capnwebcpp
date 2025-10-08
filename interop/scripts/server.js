import { spawn } from 'node:child_process';
import { resolve } from 'node:path';
import net from 'node:net';

function binPath(name) {
  const exe = process.platform === 'win32' ? `${name}.exe` : name;
  return resolve(process.cwd(), '..', 'build', 'examples', name, exe);
}

function waitForPort(port, host = '127.0.0.1', timeoutMs = 10000) {
  const start = Date.now();
  return new Promise((resolveWait, reject) => {
    const tryConnect = () => {
      const socket = net.createConnection({ port, host }, () => {
        socket.end();
        resolveWait();
      });
      socket.on('error', () => {
        socket.destroy();
        if (Date.now() - start > timeoutMs) reject(new Error('timeout waiting for port'));
        else setTimeout(tryConnect, 200);
      });
    };
    tryConnect();
  });
}

export async function startServer(targetName, staticRoot = '..') {
  const bin = binPath(targetName);
  const args = [staticRoot];
  const child = spawn(bin, args, { stdio: 'inherit' });
  child.on('exit', (code) => {
    if (code !== 0) console.error(`${targetName} exited with code ${code}`);
  });
  await waitForPort(8000);
  return child;
}

export async function stopServer(child) {
  if (!child) return;
  try { child.kill('SIGTERM'); } catch {}
}

