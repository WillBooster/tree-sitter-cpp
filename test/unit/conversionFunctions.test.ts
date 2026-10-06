import fs from 'node:fs';
import path from 'node:path';
import assert from 'node:assert/strict';
import { test } from 'vitest';
import { Edit, Parser, Query, type Node, type Tree } from '@willbooster/web-tree-sitter';
import { loadCurrentWasmBuild } from './wasmBuild.js';

const Root = path.resolve(import.meta.dirname, '../..');
const Source = fs.readFileSync(path.join(Root, 'test/fixtures/conversionFunctions.cpp'), 'utf8');
await Parser.init();

test('preserves conversion declarators, friend declarations and configured queries after edits', async () => {
  const parser = new Parser();
  const queries: Query[] = [];
  let tree: Tree | undefined;
  try {
    const language = await loadCurrentWasmBuild();
    parser.setLanguage(language);
    queries.push(
      new Query(
        language,
        '(function_definition declarator: (qualified_identifier name: (operator_cast) @cast)) @definition (function_definition declarator: (operator_cast) @cast) @definition'
      ),
      new Query(
        language,
        ['queries/c/highlights.scm', 'queries/highlights.scm']
          .map((file) => fs.readFileSync(path.join(Root, file), 'utf8'))
          .join('\n')
      ),
      new Query(language, fs.readFileSync(path.join(Root, 'queries/tags.scm'), 'utf8'))
    );
    const minimal = parser.parse('struct A { operator int(); }; inline A::operator int() { return 42; }')!;
    try {
      assert.equal(minimal.rootNode.hasError, false);
      const definitions = queries[0]!.captures(minimal.rootNode).filter((c) => c.name === 'definition');
      assert.equal(definitions.length, 1);
      assert.equal(
        definitions[0]!.node.childForFieldName('declarator')?.childForFieldName('name')?.type,
        'operator_cast'
      );
      assert.equal(definitions[0]!.node.childForFieldName('type') ?? undefined, undefined);
    } finally {
      minimal.delete();
    }
    tree = parser.parse(Source)!;
    assert.equal(tree.rootNode.hasError, false, tree.rootNode.toString());
    const definitions = queries[0]!
      .captures(tree.rootNode)
      .filter((c) => c.name === 'definition')
      .map((c) => c.node);
    assert.equal(definitions.length, 16);
    for (const definition of definitions) {
      assert.equal(definition.childForFieldName('type') ?? undefined, undefined);
      const declarator = definition.childForFieldName('declarator')!;
      const cast = declarator.type === 'operator_cast' ? declarator : declarator.childForFieldName('name')!;
      assert.equal(cast.type, 'operator_cast');
      assert.ok(cast.childForFieldName('type'));
      assert.ok(cast.childForFieldName('declarator')?.descendantsOfType('parameter_list').length);
      assert.equal(Source.slice(declarator.startIndex, declarator.endIndex), declarator.text);
      assert.deepEqual(
        [declarator.startPosition, declarator.endPosition],
        [point(Source, declarator.startIndex), point(Source, declarator.endIndex)]
      );
    }
    const friends = tree.rootNode.descendantsOfType('friend_declaration');
    assert.equal(friends.length, 2);
    for (const friend of friends) {
      const declaration = friend.namedChildren[0]!;
      assert.equal(declaration.type, 'declaration');
      assert.equal(declaration.childForFieldName('type') ?? undefined, undefined);
      assert.equal(declaration.childForFieldName('declarator')?.childForFieldName('name')?.type, 'operator_cast');
    }
    const casts = tree.rootNode.descendantsOfType('operator_cast');
    assert.equal(casts.length, 30);
    for (const cast of casts) {
      assert.ok(
        queries[1]!
          .captures(cast)
          .some((c) => c.name.startsWith('type') && c.node.id === cast.childForFieldName('type')?.id)
      );
    }
    assert.deepEqual(
      queries[2]!
        .captures(tree.rootNode)
        .filter((c) => c.name === 'name' && c.node.parent?.type === 'function_declarator')
        .map((c) => c.node.text),
      ['ordinary', 'main']
    );
    let source = Source;
    for (const [before, after, hasError] of [
      ['inline Scalar::operator int() const', '[[nodiscard]] inline Scalar::operator int() const', false],
      ['[[nodiscard]] inline Scalar::operator int() const', 'inline Scalar::operator int() const', false],
      ['friend Pointer::operator int*() const;', 'friend Pointer::operator int*() const', true],
      ['friend Pointer::operator int*() const }', 'friend Pointer::operator int*() const; }', false],
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
    assert.equal(tree.rootNode.hasError, false);
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
