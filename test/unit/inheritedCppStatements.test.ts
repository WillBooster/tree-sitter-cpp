import fs from 'node:fs';

import { Edit, Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves C++ attributed substatement kinds and query ranges after an incremental edit', () => {
  const source = fs.readFileSync(new URL('../fixtures/inheritedCppStatements.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const query = new Query(
    language,
    `(attributed_statement [
    (throw_statement)
    (for_range_loop)
    (co_yield_statement)
    (co_return_statement)
  ] @body)`
  );
  const tree = parser.parse(source)!;
  const captures = (parsed: Tree): [string, string, number, number][] =>
    query.captures(parsed.rootNode).map(({ node }) => [node.type, node.text, node.startIndex, node.endIndex]);
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(captures(tree).map(([kind, text]) => [kind, text])).toEqual([
      ['throw_statement', 'throw 1;'],
      ['for_range_loop', 'for (int value : values) { (void)value; }'],
      ['throw_statement', 'throw 2;'],
      ['throw_statement', 'throw 3;'],
      ['co_yield_statement', 'co_yield 1;'],
      ['co_return_statement', 'co_return;'],
    ]);
    for (const [, text, start, end] of captures(tree)) {
      expect(start).toBe(source.indexOf(text));
      expect(end).toBe(start + text.length);
    }
    const start = source.indexOf('throw 1;') + 'throw '.length;
    const preceding = source.slice(0, start).split('\n');
    const position = { row: preceding.length - 1, column: preceding.at(-1)!.length };
    tree.edit(
      new Edit({
        startIndex: start,
        oldEndIndex: start + 1,
        newEndIndex: start + 1,
        startPosition: position,
        oldEndPosition: { ...position, column: position.column + 1 },
        newEndPosition: { ...position, column: position.column + 1 },
      })
    );
    const changed = source.slice(0, start) + '9' + source.slice(start + 1);
    const incremental = parser.parse(changed, tree)!;
    const fresh = parser.parse(changed)!;
    try {
      expect(incremental.rootNode.hasError, incremental.rootNode.toString()).toBe(false);
      expect(incremental.rootNode.toString()).toBe(fresh.rootNode.toString());
      expect(captures(incremental)).toEqual(captures(fresh));
      expect(captures(incremental)[0]?.slice(0, 2)).toEqual(['throw_statement', 'throw 9;']);
    } finally {
      fresh.delete();
      incremental.delete();
    }
  } finally {
    tree.delete();
    query.delete();
    parser.delete();
  }
});
