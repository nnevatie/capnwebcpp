// Browser-compatible client demonstrating:
// - Batching + pipelining: multiple dependent calls, one round trip.
// - Non-batched sequential calls: multiple round trips.

import { newHttpBatchRpcSession } from '../../dist/index.js';

// Configuration with defaults that can be overridden via URL params
const urlParams = new URLSearchParams(window.location.search);
const RPC_URL = urlParams.get('rpc_url') || 'http://localhost:8000/rpc';
const SIMULATED_RTT_MS = Number(urlParams.get('rtt') || 120);
const SIMULATED_RTT_JITTER_MS = Number(urlParams.get('jitter') || 40);

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
const jittered = () => SIMULATED_RTT_MS + (SIMULATED_RTT_JITTER_MS ? Math.random() * SIMULATED_RTT_JITTER_MS : 0);

// Wrap fetch to count RPC POSTs and simulate network latency
const originalFetch = globalThis.fetch;
let fetchCount = 0;
globalThis.fetch = async (input, init) => {
    const method = init?.method || (input instanceof Request ? input.method : 'GET');
    const url = input instanceof Request ? input.url : String(input);
    if (url.startsWith(RPC_URL) && method === 'POST') {
        fetchCount++;
        // Simulate uplink and downlink latency for each RPC POST
        await sleep(jittered());
        const resp = await originalFetch(input, init);
        await sleep(jittered());
        return resp;
    }
    return originalFetch(input, init);
};

async function runPipelined() {
    fetchCount = 0;
    const t0 = performance.now();

    const api = newHttpBatchRpcSession(RPC_URL);
    const user = api.authenticate('cookie-123');
    const profile = api.getUserProfile(user.id);
    const notifications = api.getNotifications(user.id);

    const [u, p, n] = await Promise.all([user, profile, notifications]);

    const t1 = performance.now();
    return { u, p, n, ms: t1 - t0, posts: fetchCount };
}

async function runSequential() {
    fetchCount = 0;
    const t0 = performance.now();

    // 1) Authenticate (1 round trip)
    const api1 = newHttpBatchRpcSession(RPC_URL);
    const u = await api1.authenticate('cookie-123');

    // 2) Fetch profile (2nd round trip)
    const api2 = newHttpBatchRpcSession(RPC_URL);
    const p = await api2.getUserProfile(u.id);

    // 3) Fetch notifications (3rd round trip)
    const api3 = newHttpBatchRpcSession(RPC_URL);
    const n = await api3.getNotifications(u.id);

    const t1 = performance.now();
    return { u, p, n, ms: t1 - t0, posts: fetchCount };
}

// Helper to format results as HTML
function formatResults(title, results) {
    return `
        <div class="results">
            <h3>${title}</h3>
            <p><strong>HTTP POSTs:</strong> ${results.posts}</p>
            <p><strong>Time:</strong> ${results.ms.toFixed(2)} ms</p>
            <details>
                <summary>Data received</summary>
                <p><strong>Authenticated user:</strong> <code>${JSON.stringify(results.u)}</code></p>
                <p><strong>Profile:</strong> <code>${JSON.stringify(results.p)}</code></p>
                <p><strong>Notifications:</strong> <code>${JSON.stringify(results.n)}</code></p>
            </details>
        </div>
    `;
}

// Main function to run the demo
export async function runDemo() {
    const outputEl = document.getElementById('output');
    const statusEl = document.getElementById('status');

    try {
        statusEl.textContent = 'Running pipelined demo...';
        const pipelined = await runPipelined();

        statusEl.textContent = 'Running sequential demo...';
        const sequential = await runSequential();

        statusEl.textContent = 'Demo complete!';

        outputEl.innerHTML = `
            <div class="config">
                <h3>Configuration</h3>
                <p><strong>RPC URL:</strong> ${RPC_URL}</p>
                <p><strong>Simulated RTT (each direction):</strong> ~${SIMULATED_RTT_MS}ms ±${SIMULATED_RTT_JITTER_MS}ms</p>
            </div>

            ${formatResults('Pipelined (Batched, Single Round Trip)', pipelined)}
            ${formatResults('Sequential (Non-Batched, Multiple Round Trips)', sequential)}

            <div class="summary">
                <h3>Summary</h3>
                <p><strong>Pipelined:</strong> ${pipelined.posts} POST, ${pipelined.ms.toFixed(2)} ms</p>
                <p><strong>Sequential:</strong> ${sequential.posts} POSTs, ${sequential.ms.toFixed(2)} ms</p>
                <p><strong>Speedup:</strong> ${(sequential.ms / pipelined.ms).toFixed(2)}x faster with pipelining!</p>
            </div>
        `;
    } catch (err) {
        statusEl.textContent = 'Error occurred!';
        outputEl.innerHTML = `<div class="error">Error: ${err.message}</div>`;
        console.error(err);
    }
}

// Auto-run on load if this is the main module
if (import.meta.url === new URL(window.location.href).href) {
    window.addEventListener('DOMContentLoaded', runDemo);
}