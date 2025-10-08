export default {
  test: {
    globals: true,
    setupFiles: [],
    hookTimeout: 30000,
    testTimeout: 30000,
    reporters: ["default"],
    // Run test files sequentially to avoid port 8000 conflicts between servers.
    fileParallelism: false,
    // Ensure a single worker/thread executes tests.
    pool: 'threads',
    minThreads: 1,
    maxThreads: 1,
  },
};

