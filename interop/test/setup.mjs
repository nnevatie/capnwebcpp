// Minimal polyfills for capnweb to run in Node test environment

// Provide a navigator so capnweb's runtime checks don't crash.
if (typeof globalThis.navigator === 'undefined') {
  globalThis.navigator = { userAgent: 'node' };
}

// Align window/self to global for libraries that expect them.
if (typeof globalThis.window === 'undefined') {
  globalThis.window = globalThis;
}
if (typeof globalThis.self === 'undefined') {
  globalThis.self = globalThis;
}

// Polyfill Promise.withResolvers for Node versions that lack it (Node < 20/22)
if (typeof Promise.withResolvers !== 'function') {
  Promise.withResolvers = function withResolvers() {
    /** @type {(value: any) => void} */ let resolve;
    /** @type {(reason?: any) => void} */ let reject;
    const promise = new Promise((res, rej) => { resolve = res; reject = rej; });
    // @ts-ignore - attachers for consumers
    return { promise, resolve, reject };
  };
}
