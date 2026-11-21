import { Parser } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();
const language = await loadCurrentWasmBuild();

// Corpus files store LF line endings, so the other line endings of the directives are checked here.
test.each(['\n', '\r\n', '\r'])('parses guarded else continuations with %j line endings', (ending) => {
  const source = [
    'int f(int x) {',
    '  if (x) {',
    '    g();',
    '#if 1 /* c */',
    '  } else if (x == 0) {',
    '    h();',
    '#elif defined(B)',
    '    k();',
    '#endif',
    '#ifdef C',
    '  } else {',
    '#endif',
    '  }',
    '  return 0;',
    '}',
    '',
  ].join(ending);
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(tree.rootNode.descendantsOfType('else_continuation')).toHaveLength(2);
  } finally {
    tree.delete();
    parser.delete();
  }
});
