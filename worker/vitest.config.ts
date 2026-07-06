import { defineConfig } from "vitest/config";

// Plain vitest (Node environment) with dependency-injected fetch/Cache, not
// @cloudflare/vitest-pool-workers. See README.md "Why plain vitest" for the
// tradeoff -- this keeps the test suite fast and dependency-light while
// still exercising the exact same code path Cloudflare runs, because
// index.ts's handleRequest() takes fetch/Cache as injectable options rather
// than reaching for the `caches` / global fetch bindings directly.
export default defineConfig({
  test: {
    include: ["test/**/*.test.ts"],
  },
});
