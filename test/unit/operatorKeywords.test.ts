import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { test } from 'vitest';
import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/operatorKeywords.cpp'), 'utf8');
await Parser.init();

test('highlights operator declarations and conversions without capturing literal or macro text after edits', async () => {
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
    let source = Source;
    for (const [before, after, hasError] of [
      ['constexpr Value operator+', '[[nodiscard]] constexpr Value operator+', false],
      ['[[nodiscard]] constexpr Value operator+', 'constexpr Value operator+', false],
      ['constexpr Value operator+', 'constexpr Value +', true],
      ['constexpr Value +', 'constexpr Value operator+', false],
    ] as const) {
      const start = source.indexOf(before);
      assert.ok(start !== -1, before);
      const next = source.slice(0, start) + after + source.slice(start + before.length);
      tree.edit(
        new Edit({
          startIndex: start,
          oldEndIndex: start + before.length,
          newEndIndex: start + after.length,
          startPosition: point(source, start),
          oldEndPosition: point(source, start + before.length),
          newEndPosition: point(next, start + after.length),
        })
      );
      let incremental: Tree | undefined, fresh: Tree | undefined;
      try {
        incremental = parser.parse(next, tree)!;
        fresh = parser.parse(next)!;
        assert.equal(incremental.rootNode.hasError, hasError);
        assert.deepEqual(snapshot(incremental.rootNode), snapshot(fresh.rootNode));
        for (const query of queries) {
          const captures = (t: Tree): unknown[] => query.captures(t.rootNode).map((c) => [c.name, snapshot(c.node)]);
          assert.deepEqual(captures(incremental), captures(fresh));
        }
        if (!hasError) {
          const operators = incremental.rootNode.descendantsOfType('operator');
          assert.equal(operators.length, 9);
          const captures = queries[0]!.captures(incremental.rootNode);
          for (const operator of operators)
            assert.equal(captures.filter((c) => c.name === 'keyword' && c.node.id === operator.id).length, 1);
        }
        tree.delete();
        tree = incremental;
        incremental = undefined;
        source = next;
      } finally {
        incremental?.delete();
        fresh?.delete();
      }
    }
    assert.equal(source, Source);
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
function snapshot(node: Node): unknown {
  return [
    node.type,
    node.isNamed,
    node.isExtra,
    node.isMissing,
    node.hasError,
    node.startIndex,
    node.endIndex,
    node.startPosition,
    node.endPosition,
    node.children.map((child, i) => [node.fieldNameForChild(i), snapshot(child)]),
  ];
}
