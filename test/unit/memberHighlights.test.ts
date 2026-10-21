import fs from 'node:fs';
import path from 'node:path';
import { expect, test } from 'vitest';

import { Parser, Query } from '@willbooster/web-tree-sitter';

import treeSitterJson from '../../tree-sitter.json';

import { loadCurrentWasmBuild } from './wasmBuild.js';

await Parser.init();

test('highlights member operators while retaining C++ dot contexts', async () => {
  const language = await loadCurrentWasmBuild();
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    treeSitterJson.grammars[0]!.highlights.map((file) =>
      fs.readFileSync(path.resolve(import.meta.dirname, '../..', file), 'utf8')
    ).join('\n')
  );
  const tree = parser.parse(`export module sample.items.details;
import sample.tools;
struct Item { int amount; };
int read(Item value, Item* pointer) {
  Item initialized = {.amount = 1};
  HELPER(NAME, .);
  return value.amount + pointer->amount + initialized.amount;
}
`)!;
  try {
    expect(tree.rootNode.hasError).toBe(false);
    const punctuation = query.captures(tree.rootNode).filter(({ node }) => node.text === '.' || node.text === '->');
    expect(punctuation.map(({ name }) => name)).toEqual([
      'delimiter',
      'delimiter',
      'delimiter',
      'delimiter',
      'operator',
      'operator',
      'operator',
      'operator',
    ]);
    expect(punctuation.map(({ node }) => node.text)).toEqual(['.', '.', '.', '.', '.', '.', '->', '.']);
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
