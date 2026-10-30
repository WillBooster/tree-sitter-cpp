import assert from 'node:assert/strict';
import fs from 'node:fs';
import path from 'node:path';

import { Parser, type Tree } from '@willbooster/web-tree-sitter';
import { test } from 'vitest';

import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/operatorKeywords.cpp'), 'utf8');
await Parser.init();

test('parses operator declarations and conversions without splitting literal or macro text', async () => {
  const parser = new Parser();
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    tree = parser.parse(Source)!;
    assert.equal(tree.rootNode.hasError, false);
    const operators = tree.rootNode.descendantsOfType('operator');
    assert.equal(operators.length, 9);
    assert.equal(operators.filter((node) => node.parent?.type === 'operator_cast').length, 1);
    assert.equal(operators.filter((node) => node.parent?.type === 'operator_name').length, 8);
    for (const operator of operators) {
      assert.equal(operator.isNamed, false);
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
    }
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
          assert.equal(variant.rootNode.descendantsOfType('operator').length, 9);
        }
      } finally {
        variant.delete();
      }
    }
  } finally {
    tree?.delete();
    parser.delete();
  }
});
function point(source: string, index: number): { row: number; column: number } {
  const prefix = source.slice(0, index);
  return { row: prefix.split('\n').length - 1, column: prefix.length - prefix.lastIndexOf('\n') - 1 };
}
