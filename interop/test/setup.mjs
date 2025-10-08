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

