import path from 'node:path';

import { afterAll, beforeAll, expect, test } from 'vitest';
import { createTestHarness, type TestHarness } from 'wrangler';

// The same Worker with and without Node.js compatibility, since the package must run in both.
const configs = {
  'tree-sitter-cpp-test': 'wrangler.jsonc',
  'tree-sitter-cpp-test-no-nodejs-compat': 'wrangler.no-nodejs-compat.jsonc',
};

let server: TestHarness | undefined;

beforeAll(async () => {
  server = createTestHarness({
    workers: Object.values(configs).map((config) => ({
      configPath: path.join(import.meta.dirname, '../fixtures/workerd', config),
    })),
  });
  await server.listen();
}, 120_000);

afterAll(async () => {
  await server?.close();
});

test.each(Object.keys(configs))('parses in Cloudflare Workers with the imported Wasm modules (%s)', async (name) => {
  const response = await server!.getWorker(name).fetch('http://localhost/', { method: 'POST', body: 'class A {};' });
  expect(await response.text()).toBe(
    '(translation_unit (class_specifier name: (type_identifier) body: (field_declaration_list)))'
  );
});
