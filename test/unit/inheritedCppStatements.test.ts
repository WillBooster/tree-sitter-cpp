import fs from 'node:fs';

import { Parser, type Tree } from '@willbooster/web-tree-sitter';
import { expect, test } from 'vitest';

import { childrenOfType } from '../helpers/ast';
import { loadCurrentWasmBuild } from './wasmBuild';

await Parser.init();
const language = await loadCurrentWasmBuild();

test('preserves C++ attributed substatement kinds and source ranges', () => {
  const source = fs.readFileSync(new URL('../fixtures/inheritedCppStatements.cpp', import.meta.url), 'utf8');
  const parser = new Parser().setLanguage(language);
  const tree = parser.parse(source)!;
  try {
    expect(tree.rootNode.hasError, tree.rootNode.toString()).toBe(false);
    expect(attributedSubstatementCaptures(tree).map(([kind, text]) => [kind, text])).toEqual([
      ['throw_statement', 'throw 1;'],
      ['for_range_loop', 'for (int value : values) { (void)value; }'],
      ['throw_statement', 'throw 2;'],
      ['throw_statement', 'throw 3;'],
      ['co_yield_statement', 'co_yield 1;'],
      ['co_return_statement', 'co_return;'],
    ]);
    for (const [, text, start, end] of attributedSubstatementCaptures(tree)) {
      expect(start).toBe(source.indexOf(text));
      expect(end).toBe(start + text.length);
    }
    const start = source.indexOf('throw 1;') + 'throw '.length;
    const changed = parser.parse(source.slice(0, start) + '9' + source.slice(start + 1))!;
    try {
      expect(changed.rootNode.hasError, changed.rootNode.toString()).toBe(false);
      expect(attributedSubstatementCaptures(changed)[0]?.slice(0, 2)).toEqual(['throw_statement', 'throw 9;']);
    } finally {
      changed.delete();
    }
  } finally {
    tree.delete();
    parser.delete();
  }
});

function attributedSubstatementCaptures(parsed: Tree): [string, string, number, number][] {
  return parsed.rootNode
    .descendantsOfType('attributed_statement')
    .flatMap((node) =>
      childrenOfType(node, ['throw_statement', 'for_range_loop', 'co_yield_statement', 'co_return_statement'])
    )
    .map((node) => [node.type, node.text, node.startIndex, node.endIndex]);
}
