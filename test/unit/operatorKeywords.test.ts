import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import { Parser, Query, type Tree } from '@willbooster/web-tree-sitter';
import { test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/operatorKeywords.cpp'), 'utf8');
await Parser.init();

test('highlights operator declarations and conversions without capturing literal or macro text', async () => {
  const parser = new Parser();
  const queries: Query[] = [];
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    queries.push(
      new Query(
        language,
        ['queries/c/highlights.scm', 'queries/highlights.scm']
          .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
          .join('\n')
      ),
      new Query(language, fs.readFileSync(path.join(Root, 'queries/tags.scm'), 'utf8'))
    );
    tree = parser.parse(Source)!;
    assert.equal(tree.rootNode.hasError, false);
    const operators = tree.rootNode.descendantsOfType('operator');
    assert.equal(operators.length, 9);
    assert.equal(operators.filter((node) => node.parent?.type === 'operator_cast').length, 1);
    assert.equal(operators.filter((node) => node.parent?.type === 'operator_name').length, 8);
    const captures = queries[0]!.captures(tree.rootNode);
    for (const operator of operators) {
      assert.equal(operator.isNamed, false);
      assert.equal(captures.filter((c) => c.name === 'keyword' && c.node.id === operator.id).length, 1);
      assert.equal(Source.slice(operator.startIndex, operator.endIndex), 'operator');
      assert.deepEqual(
        [operator.startPosition, operator.endPosition],
        [point(Source, operator.startIndex), point(Source, operator.endIndex)]
      );
    }
    const protectedNodes = tree.rootNode.descendantsOfType(['preproc_arg', 'string_literal', 'comment']);
    assert.equal(protectedNodes.length, 3);
    for (const node of protectedNodes) {
      assert.ok(node.text.includes('operator'));
      assert.ok(
        !captures.some(
          (c) => c.name === 'keyword' && c.node.startIndex >= node.startIndex && c.node.endIndex <= node.endIndex
        )
      );
    }
    assert.equal(
      queries[1]!.captures(tree.rootNode).filter((c) => c.name === 'name' && c.node.text === 'main').length,
      1
    );
    for (const [after, hasError] of [
      ['[[nodiscard]] constexpr Value operator+', false],
      ['constexpr Value +', true],
    ] as const) {
      const before = 'constexpr Value operator+';
      const start = Source.indexOf(before);
      const variant = parser.parse(Source.slice(0, start) + after + Source.slice(start + before.length))!;
      try {
        assert.equal(variant.rootNode.hasError, hasError);
        if (!hasError) {
          const operators = variant.rootNode.descendantsOfType('operator');
          assert.equal(operators.length, 9);
          const captures = queries[0]!.captures(variant.rootNode);
          for (const operator of operators)
            assert.equal(captures.filter((c) => c.name === 'keyword' && c.node.id === operator.id).length, 1);
        }
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree?.delete();
    for (const query of queries) query.delete();
    parser.delete();
  }
});
function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}
